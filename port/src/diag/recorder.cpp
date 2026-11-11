#include "diag/recorder.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <span>
#include <string>
#include <vector>

#include "gfx/gfx.hpp"
#include "gfx/profile.hpp"
#include "log/log.hpp"
#include "platform/config.hpp"
#include "present.hpp"

namespace dc2::diag {
namespace {

using Clock = std::chrono::steady_clock;

double Ms(Clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
}

// ---------------------------------------------------------------------------------------------------------------
// Flight recorder
// ---------------------------------------------------------------------------------------------------------------

/**
 * One remembered line.
 */
struct Entry {
    Clock::time_point     time{};
    std::array<char, 12>  category{};
    std::array<char, 180> text{};
};

constexpr std::size_t kEntries = 400;

std::mutex                    g_recorder_mutex;
std::array<Entry, kEntries>   g_entries{};
std::size_t                   g_next = 0;  // slot the next line goes to
std::size_t                   g_count = 0; // lines held, up to kEntries

void CopyText(std::span<char> out, std::string_view text) {
    const std::size_t length = std::min(text.size(), out.size() - 1);
    std::memcpy(out.data(), text.data(), length);
    out[length] = '\0';
}

// ---------------------------------------------------------------------------------------------------------------
// Frame watch
// ---------------------------------------------------------------------------------------------------------------

constexpr double kBudget30 = 36.0;   // a tick at 30 per second, with a little slack
constexpr double kBudget20 = 55.0;   // slower than 20 per second
constexpr double kBudget15 = 72.0;   // slower than 15 per second
constexpr double kStall = 1000.0;    // loads, pauses and sleep: counted apart from play
constexpr double kHitchLog = 250.0;  // single frames this long also go straight to the log

/**
 * Totals for one built-in timing point over an area.
 */
struct ZoneTotal {
    double game_ms = 0.0;  // on the main (game) thread
    double other_ms = 0.0; // on other threads, mostly the render thread
};

/**
 * Graphics-chip time for one renderer pass over an area.
 */
struct PassTotal {
    double   ms = 0.0;
    uint64_t segments = 0;
};

/**
 * Everything gathered while the player stays in one area.
 */
struct AreaStats {
    std::string       name;
    Clock::time_point start{};
    uint64_t          frames = 0;  // play frames (stalls excluded)
    double            total_ms = 0.0;
    double            worst_ms = 0.0;
    uint64_t          over30 = 0;  // slower than 30 per second
    uint64_t          over20 = 0;
    uint64_t          over15 = 0;
    uint64_t          stalls = 0;  // frames over kStall (loads, pauses, sleep)
    double            stall_ms = 0.0;
    double            worst_stall_ms = 0.0;
    // Graphics chip, from the renderer's timestamps.
    gfx::FrameTimings::Span       tick_gpu;    // the frame drawn for each game tick
    gfx::FrameTimings::Span       blend_gpu;   // in-between (interpolated) display frames
    gfx::FrameTimings::Span       tick_cpu;    // render thread's CPU work for tick frames
    gfx::FrameTimings::Span       blend_cpu;   // render thread's CPU work for display frames
    gfx::FrameTimings::Span       fence_wait;  // render thread waiting for the graphics chip
    gfx::FrameTimings::Span       blend_prep;  // render thread preparing in-between frames (matching, interpolation)
    std::array<uint64_t, 4>       intervals{}; // display present gaps: on time, 1 late, 2 late, worse
    std::map<std::string, PassTotal> passes;
    std::array<ZoneTotal, gfx::profile::kMaxSites> zones{};
    // Game-thread parts of each play frame (FrameParts), and how the display kept up.
    gfx::FrameTimings::Span       logic;
    gfx::FrameTimings::Span       lease;
    gfx::FrameTimings::Span       locked;
    gfx::FrameTimings::Span       draws;
    uint64_t                      game_late = 0;     // game frames that took more ticks than asked
    uint64_t                      display_short = 0; // game frames with fewer presents than ticks
    std::vector<std::pair<double, std::string>> worst; // slowest busy frames, with their detail
};

/**
 * One 5-second slice of play, for the timeline lines.
 */
struct WindowStats {
    Clock::time_point start{};
    uint64_t          frames = 0;
    uint64_t          ticks = 0;
    uint64_t          presents = 0;
    std::array<uint64_t, 4> intervals{};
    uint64_t          game_late = 0;
    uint64_t          display_short = 0;
    double            busy_max = 0.0;
    double            lease_total = 0.0;
    double            lease_max = 0.0;
    double            logic_total = 0.0;
    double            logic_max = 0.0;
    double            draws_total = 0.0;
    uint64_t          flare_frames = 0;           // game frames that drew the sun's lens flare
    gfx::FrameTimings::Span blend_cpu;            // render thread per in-between frame
    gfx::FrameTimings::Span tick_cpu;             // render thread per tick frame
    gfx::FrameTimings::Span tick_gpu;             // graphics chip per tick frame
    gfx::FrameTimings::Span blend_gpu;            // graphics chip per in-between frame
    gfx::FrameTimings::Span fence_wait;           // render thread waiting on the chip
    gfx::FrameTimings::Span acquire;              // render thread getting a screen image
    gfx::FrameTimings::Span queue_present;        // render thread handing an image to the screen
    uint64_t          flare_off_frames = 0;       // game frames that would have drawn the flare with it switched off
    std::array<double, gfx::profile::kMaxSites> other{}; // render-thread time per timing point
};

constexpr std::size_t kWorstFrames = 6;

AreaStats                                       g_area;
WindowStats                                     g_window;
FrameParts                                      g_parts;
bool                                            g_parts_fresh = false;
bool                                            g_flare_this_frame = false;
bool                                            g_flare_enabled = true;
bool                                            g_pacing_recovery = true;
PresentStats                                    g_window_present_start{};
Clock::time_point                               g_last_area_report{};
uint64_t                                        g_presented_last = 0;
std::array<double, gfx::profile::kMaxSites>     g_zone_other_frame{}; // this tick's other-thread time per point
bool                                            g_started = false;
Clock::time_point                               g_last_tick{};
std::array<uint64_t, gfx::profile::kMaxSites>   g_zone_game_last{};
std::array<uint64_t, gfx::profile::kMaxSites>   g_zone_other_last{};
std::array<double, gfx::profile::kMaxSites>     g_zone_frame{}; // this tick's game-thread time per point
Clock::time_point                               g_last_gpu_take{};
Clock::time_point                               g_mark_since{};
bool                                            g_mark_done = false;

void AddSpan(gfx::FrameTimings::Span &into, const gfx::FrameTimings::Span &from) {
    into.count += from.count;
    into.total_ms += from.total_ms;
    into.max_ms = std::max(into.max_ms, from.max_ms);
}

/**
 * Moves the renderer's timings gathered since the last take into the current area.
 */
void TakeGpu() {
    const gfx::FrameTimings timings = gfx::TakeFrameTimings();
    AddSpan(g_area.tick_gpu, timings.canonical_gpu);
    AddSpan(g_area.blend_gpu, timings.display_gpu);
    AddSpan(g_area.tick_cpu, timings.canonical_cpu);
    AddSpan(g_area.blend_cpu, timings.display_cpu);
    AddSpan(g_area.fence_wait, timings.fence_wait);
    AddSpan(g_area.blend_prep, timings.display_prepare_cpu);
    AddSpan(g_window.blend_cpu, timings.display_cpu);
    AddSpan(g_window.tick_cpu, timings.canonical_cpu);
    AddSpan(g_window.tick_gpu, timings.canonical_gpu);
    AddSpan(g_window.blend_gpu, timings.display_gpu);
    AddSpan(g_window.fence_wait, timings.fence_wait);
    AddSpan(g_window.acquire, timings.acquire);
    AddSpan(g_window.queue_present, timings.queue_present);
    for (std::size_t i = 0; i < 4; ++i) {
        g_area.intervals[i] += timings.interval_buckets[i];
        g_window.intervals[i] += timings.interval_buckets[i];
    }
    for (const auto &scope : timings.scopes) {
        auto &pass = g_area.passes[scope.name];
        pass.ms += scope.span.total_ms;
        pass.segments += scope.span.count;
    }
}

/**
 * Reads how much time each built-in timing point added since the last tick.
 */
void SampleZones() {
    const int count = std::min(gfx::profile::site_count.load(std::memory_order_relaxed), gfx::profile::kMaxSites);
    for (int index = 0; index < count; ++index) {
        const auto  slot = static_cast<std::size_t>(index);
        const auto &site = gfx::profile::sites[slot];
        const uint64_t game = site.game_ns.load(std::memory_order_relaxed);
        const uint64_t other = site.other_ns.load(std::memory_order_relaxed);
        const double   game_ms = static_cast<double>(game - g_zone_game_last[slot]) / 1.0e6;
        const double   other_ms = static_cast<double>(other - g_zone_other_last[slot]) / 1.0e6;
        g_zone_game_last[slot] = game;
        g_zone_other_last[slot] = other;
        g_zone_frame[slot] = game_ms;
        g_zone_other_frame[slot] = other_ms;
        g_area.zones[slot].game_ms += game_ms;
        g_area.zones[slot].other_ms += other_ms;
    }
}

const char *ZoneName(std::size_t slot) {
    const char *name = gfx::profile::sites[slot].name;
    return name != nullptr ? name : "?";
}

/**
 * Names the timing points that took longest in one tick, for a slow-frame line.
 */
std::string TopZonesOf(const std::array<double, gfx::profile::kMaxSites> &frame, int how_many, double min_ms,
                       bool tenths) {
    const int count = std::min(gfx::profile::site_count.load(std::memory_order_relaxed), gfx::profile::kMaxSites);
    std::vector<std::size_t> order;
    for (int index = 0; index < count; ++index) {
        if (frame[static_cast<std::size_t>(index)] >= min_ms) {
            order.push_back(static_cast<std::size_t>(index));
        }
    }
    std::ranges::sort(order, [&](std::size_t a, std::size_t b) { return frame[a] > frame[b]; });
    std::string out;
    for (std::size_t i = 0; i < order.size() && i < static_cast<std::size_t>(how_many); ++i) {
        out += out.empty() ? "" : ", ";
        out += ZoneName(order[i]);
        out += tenths ? std::format(" {:.1f}", frame[order[i]]) : std::format(" {:.0f} ms", frame[order[i]]);
    }
    return out.empty() ? std::format("none over {:g} ms", min_ms) : out;
}

std::string TopFrameZones(int how_many) {
    return TopZonesOf(g_zone_frame, how_many, 1.0, false);
}

/**
 * Keeps a slow frame's detail if it is among the area's slowest, slowest first.
 */
void KeepWorst(double busy_ms, std::string detail) {
    auto &worst = g_area.worst;
    if (worst.size() == kWorstFrames && busy_ms <= worst.back().first) {
        return;
    }
    worst.emplace_back(busy_ms, std::move(detail));
    std::ranges::sort(worst, [](const auto &a, const auto &b) { return a.first > b.first; });
    if (worst.size() > kWorstFrames) {
        worst.pop_back();
    }
}

/**
 * Summed time of the timing point with this name, or 0 when it has not run.
 */
double SiteTotal(const std::array<double, gfx::profile::kMaxSites> &totals, std::string_view name) {
    const int count = std::min(gfx::profile::site_count.load(std::memory_order_relaxed), gfx::profile::kMaxSites);
    double    total = 0.0;
    for (int index = 0; index < count; ++index) {
        if (name == ZoneName(static_cast<std::size_t>(index))) {
            total += totals[static_cast<std::size_t>(index)];
        }
    }
    return total;
}

/**
 * The window's busiest render-thread timing points, as milliseconds per game frame.
 */
std::string WindowRenderZones(double frames) {
    std::array<double, gfx::profile::kMaxSites> per_frame{};
    for (std::size_t i = 0; i < per_frame.size(); ++i) {
        per_frame[i] = g_window.other[i] / frames;
    }
    return TopZonesOf(per_frame, 5, 0.5, true);
}

/**
 * Writes one timeline line for the 5 seconds just ended, then starts the next slice.
 */
void LogWindow(Clock::time_point now) {
    if (g_window.frames >= 30) {
        const double frames = static_cast<double>(g_window.frames);
        const double seconds = Ms(now - g_window.start) / 1000.0;
        CT_LOG_INFO(Main,
                    "diag window '{}' +{:.0f} s: {:.1f} game fps, {:.1f} display fps; late game frames {}, short "
                    "display frames {}; presents on time/1/2/3+ late {}/{}/{}/{}; game thread per frame: logic {:.1f} "
                    "(max {:.1f}), lease wait {:.1f} (max {:.1f}), busiest frame {:.1f} ms; {:.0f} draws; sun flare in "
                    "{} of {} frames; render thread per tick frame {:.1f} (max {:.1f}), per in-between frame {:.1f} "
                    "(max {:.1f}); render thread ms per game frame: {}",
                    g_area.name, Ms(now - g_area.start) / 1000.0, frames / seconds,
                    static_cast<double>(g_window.presents) / seconds, g_window.game_late, g_window.display_short,
                    g_window.intervals[0], g_window.intervals[1], g_window.intervals[2], g_window.intervals[3],
                    g_window.logic_total / frames, g_window.logic_max, g_window.lease_total / frames,
                    g_window.lease_max, g_window.busy_max, g_window.draws_total / frames, g_window.flare_frames,
                    g_window.frames, g_window.tick_cpu.Average(), g_window.tick_cpu.max_ms, g_window.blend_cpu.Average(),
                    g_window.blend_cpu.max_ms, WindowRenderZones(frames));
    }
    if (g_window.frames >= 30) {
        const double frames = static_cast<double>(g_window.frames);
        // The renderer's present step also waits for the screen's refresh (Switch vsync gate) before it hands the
        // image over; it has no timing point of its own, so it is the step's time less what is timed inside it.
        const double present_step = SiteTotal(g_window.other, "Kazarov PresentImage") / frames;
        const double queue_present = g_window.queue_present.total_ms / frames;
        const double acquire = g_window.acquire.total_ms / frames;
        const PresentStats stats = PresentGetStats();
        CT_LOG_INFO(Main,
                    "diag window detail '{}': sun flare {}{}; chip per tick frame {:.1f} (max {:.1f}), per in-between "
                    "frame {:.1f} (max {:.1f}); render thread waiting on the chip {:.1f} ms per game frame (max {:.1f}); "
                    "present step {:.1f} ms per game frame = handing over {:.1f} + rest {:.1f} (mostly waiting for the "
                    "screen's refresh, with the device held); getting a screen image {:.1f} ms per game frame; pacing "
                    "recovery {}: end-of-frame images a new tick had to present first {} (waited {:.1f} ms in all), "
                    "skipped {}",
                    g_area.name, g_flare_enabled ? "on" : "off",
                    g_window.flare_off_frames > 0
                        ? std::format(" (switched off in {} frames that would have drawn it)", g_window.flare_off_frames)
                        : std::string(),
                    g_window.tick_gpu.Average(), g_window.tick_gpu.max_ms, g_window.blend_gpu.Average(),
                    g_window.blend_gpu.max_ms, g_window.fence_wait.total_ms / frames, g_window.fence_wait.max_ms,
                    present_step, queue_present, std::max(present_step - queue_present, 0.0), acquire,
                    g_pacing_recovery ? "on" : "off", stats.forced_endpoints - g_window_present_start.forced_endpoints,
                    stats.forced_endpoint_wait_ms - g_window_present_start.forced_endpoint_wait_ms,
                    stats.skipped_endpoints - g_window_present_start.skipped_endpoints);
    }
    g_window = WindowStats{};
    g_window.start = now;
    g_window_present_start = PresentGetStats();
}

/**
 * Lists the timing points with the most time per frame over the area, for the summary.
 */
std::string TopAreaZones(bool game_thread, int how_many) {
    if (g_area.frames == 0) {
        return "none";
    }
    const int count = std::min(gfx::profile::site_count.load(std::memory_order_relaxed), gfx::profile::kMaxSites);
    std::vector<std::size_t> order;
    for (int index = 0; index < count; ++index) {
        const auto &zone = g_area.zones[static_cast<std::size_t>(index)];
        if ((game_thread ? zone.game_ms : zone.other_ms) > 0.0) {
            order.push_back(static_cast<std::size_t>(index));
        }
    }
    const auto value = [&](std::size_t slot) {
        return game_thread ? g_area.zones[slot].game_ms : g_area.zones[slot].other_ms;
    };
    std::ranges::sort(order, [&](std::size_t a, std::size_t b) { return value(a) > value(b); });
    std::string out;
    const double frames = static_cast<double>(g_area.frames);
    for (std::size_t i = 0; i < order.size() && i < static_cast<std::size_t>(how_many); ++i) {
        out += std::format("{}{} {:.1f}", out.empty() ? "" : ", ", ZoneName(order[i]), value(order[i]) / frames);
    }
    return out.empty() ? std::string("none") : out;
}

/**
 * Lists the renderer passes with the most graphics-chip time per drawn frame.
 */
std::string TopPasses(int how_many) {
    const uint64_t drawn = g_area.tick_gpu.count + g_area.blend_gpu.count;
    if (drawn == 0 || g_area.passes.empty()) {
        return "none";
    }
    std::vector<std::pair<std::string, PassTotal>> order(g_area.passes.begin(), g_area.passes.end());
    std::ranges::sort(order, [](const auto &a, const auto &b) { return a.second.ms > b.second.ms; });
    std::string out;
    for (std::size_t i = 0; i < order.size() && i < static_cast<std::size_t>(how_many); ++i) {
        out += std::format("{}{} {:.1f}", out.empty() ? "" : ", ", order[i].first,
                           order[i].second.ms / static_cast<double>(drawn));
    }
    return out;
}

double Percent(uint64_t part, uint64_t whole) {
    return whole == 0 ? 0.0 : 100.0 * static_cast<double>(part) / static_cast<double>(whole);
}

/**
 * Writes the current area's summary. is_final is false for a summary written mid-area (a mark).
 */
void LogArea(bool is_final) {
    TakeGpu();
    const double seconds = Ms(Clock::now() - g_area.start) / 1000.0;
    if (g_area.frames < 30 && g_area.stalls == 0) {
        return;
    }
    const double average = g_area.frames == 0 ? 0.0 : g_area.total_ms / static_cast<double>(g_area.frames);
    CT_LOG_INFO(Main,
                "diag area {}: '{}' {:.0f} s, {} frames, average {:.1f} ms, worst {:.0f} ms; slower than 30 fps "
                "{:.1f}%, than 20 fps {:.1f}%, than 15 fps {:.1f}%; {} stalls over 1 s (worst {:.1f} s)",
                is_final ? "summary" : "so far", g_area.name, seconds, g_area.frames,
                average, g_area.worst_ms, Percent(g_area.over30, g_area.frames), Percent(g_area.over20, g_area.frames),
                Percent(g_area.over15, g_area.frames), g_area.stalls, g_area.worst_stall_ms / 1000.0);
    CT_LOG_INFO(Main,
                "diag area gpu '{}': tick frames {} at {:.1f} ms average ({:.1f} worst), in-between frames {} at {:.1f} "
                "ms ({:.1f} worst); render thread {:.1f} ms per tick frame, {:.1f} per in-between frame, waiting on the "
                "chip {:.1f} ms average ({:.1f} worst)",
                g_area.name, g_area.tick_gpu.count, g_area.tick_gpu.Average(), g_area.tick_gpu.max_ms,
                g_area.blend_gpu.count, g_area.blend_gpu.Average(), g_area.blend_gpu.max_ms, g_area.tick_cpu.Average(),
                g_area.blend_cpu.Average(), g_area.fence_wait.Average(), g_area.fence_wait.max_ms);
    CT_LOG_INFO(Main, "diag area time '{}' (ms per frame): game thread: {}; other threads: {}; chip passes: {}",
                g_area.name, TopAreaZones(true, 6), TopAreaZones(false, 4), TopPasses(5));
    CT_LOG_INFO(Main,
                "diag area parts '{}' (ms per game frame, average/worst): logic {:.1f}/{:.1f}, lease wait {:.1f}/{:.1f}, "
                "under lease {:.1f}/{:.1f}; draws {:.0f}/{:.0f}; late game frames {} ({:.1f}%), short display frames {} "
                "({:.1f}%); render thread in-between frame {:.1f}/{:.1f}, of which prep {:.1f}/{:.1f}, tick frame "
                "{:.1f}/{:.1f}; presents on time/1/2/3+ late {}/{}/{}/{}",
                g_area.name, g_area.logic.Average(), g_area.logic.max_ms, g_area.lease.Average(), g_area.lease.max_ms,
                g_area.locked.Average(), g_area.locked.max_ms, g_area.draws.Average(), g_area.draws.max_ms,
                g_area.game_late, Percent(g_area.game_late, g_area.frames), g_area.display_short,
                Percent(g_area.display_short, g_area.frames), g_area.blend_cpu.Average(), g_area.blend_cpu.max_ms,
                g_area.blend_prep.Average(), g_area.blend_prep.max_ms, g_area.tick_cpu.Average(), g_area.tick_cpu.max_ms,
                g_area.intervals[0], g_area.intervals[1], g_area.intervals[2], g_area.intervals[3]);
    for (std::size_t i = 0; i < g_area.worst.size(); ++i) {
        CT_LOG_INFO(Main, "diag area worst '{}' #{}: {}", g_area.name, i + 1, g_area.worst[i].second);
    }
}

void StartArea(std::string_view name, Clock::time_point now) {
    g_area = AreaStats{};
    g_area.name = std::string(name);
    g_area.start = now;
    g_window = WindowStats{};
    g_window.start = now;
    g_last_area_report = now;
}

/**
 * Turns on the timing that the frame watch reads. Runs once, on the main thread.
 */
void StartWatch(Clock::time_point now) {
    g_started = true;
    g_last_tick = now;
    g_last_gpu_take = now;
    // Graphics-chip time per renderer pass; read once, when the renderer first opens its timestamps.
#ifdef _WIN32
    if (std::getenv("KZ_GPU_SCOPES") == nullptr) {
        _putenv_s("KZ_GPU_SCOPES", "pass");
    }
#else
    setenv("KZ_GPU_SCOPES", "pass", 0);
#endif
    gfx::SetFrameTimingEnabled(true);
    gfx::profile::game_thread = true;
    gfx::profile::sampling.store(true, std::memory_order_relaxed);
    const int count = std::min(gfx::profile::site_count.load(std::memory_order_relaxed), gfx::profile::kMaxSites);
    for (int index = 0; index < count; ++index) {
        const auto slot = static_cast<std::size_t>(index);
        g_zone_game_last[slot] = gfx::profile::sites[slot].game_ns.load(std::memory_order_relaxed);
        g_zone_other_last[slot] = gfx::profile::sites[slot].other_ns.load(std::memory_order_relaxed);
    }
    g_presented_last = PresentGetStats().presented;
    CT_LOG_INFO(Main, "diag: on (flight recorder, per-area frame and graphics-chip timing; hold Minus 1.5 s to mark); "
                      "frame parts v6: lease wait, logic, in-between frame cost, 5 s timeline with render thread, chip and present "
                      "wait, sun flare and pacing recovery settings, area report every 30 s, worst frames named, town sub-zones");
}

} // namespace

void Note(std::string_view category, std::string_view text) {
    std::scoped_lock lock(g_recorder_mutex);
    Entry           &entry = g_entries[g_next];
    entry.time = Clock::now();
    CopyText(entry.category, category);
    CopyText(entry.text, text);
    g_next = (g_next + 1) % kEntries;
    g_count = std::min(g_count + 1, kEntries);
}

void Dump(std::string_view reason) {
    std::vector<Entry> copy;
    {
        std::scoped_lock lock(g_recorder_mutex);
        copy.reserve(g_count);
        const std::size_t first = (g_next + kEntries - g_count) % kEntries;
        for (std::size_t i = 0; i < g_count; ++i) {
            copy.push_back(g_entries[(first + i) % kEntries]);
        }
    }
    const Clock::time_point now = Clock::now();
    CT_LOG_INFO(Main, "diag recorder ({}): last {} lines, oldest first, seconds before now", reason, copy.size());
    for (const Entry &entry : copy) {
        CT_LOG_INFO(Main, "diag  -{:.2f} {}: {}", Ms(now - entry.time) / 1000.0,
                    std::string_view(entry.category.data()), std::string_view(entry.text.data()));
    }
    if (g_started) {
        LogArea(false);
    }
    CT_LOG_INFO(Main, "diag recorder ({}): end", reason);
    LogFlush();
}

void NoteLensFlare() {
    g_flare_this_frame = true;
    if (!g_flare_enabled) {
        ++g_window.flare_off_frames;
    }
}

bool LensFlareEnabled() {
    return g_flare_enabled;
}

void FollowFlareSetting() {
    const bool enabled = ConfigGet().lens_flare;
    if (enabled != g_flare_enabled) {
        g_flare_enabled = enabled;
        CT_LOG_INFO(Main, "diag: sun flare setting {} (Display settings)", enabled ? "on" : "off");
        Notef("flare", "setting {}", enabled ? "on" : "off");
    }
    const bool recovery = ConfigGet().pacing_recovery;
    PresentSetPacingRecovery(recovery);
    if (recovery != g_pacing_recovery) {
        g_pacing_recovery = recovery;
        CT_LOG_INFO(Main, "diag: pacing recovery setting {} (Display settings)", recovery ? "on" : "off");
        Notef("pacing", "recovery {}", recovery ? "on" : "off");
    }
}

void NoteFrameParts(const FrameParts &parts) {
    g_parts = parts;
    g_parts_fresh = true;
}

void Tick(int loop_no, std::string_view area, bool mark_held) {
    const Clock::time_point now = Clock::now();
    if (!g_started) {
        StartWatch(now);
        StartArea(area, now);
        Notef("area", "loop {}, {}", loop_no, area);
        return;
    }

    const double frame_ms = Ms(now - g_last_tick);
    g_last_tick = now;
    SampleZones();
    // Read every tick, so presents made while the game sat in a pause or menu never land on one play frame.
    const uint64_t presented = PresentGetStats().presented;
    const uint64_t presents = presented - g_presented_last;
    g_presented_last = presented;
    const bool flare = g_flare_this_frame;
    g_flare_this_frame = false;
    FollowFlareSetting();

    if (frame_ms >= kStall) {
        ++g_area.stalls;
        g_area.stall_ms += frame_ms;
        g_area.worst_stall_ms = std::max(g_area.worst_stall_ms, frame_ms);
        const std::string zones = TopFrameZones(5);
        Notef("stall", "{:.2f} s without a frame in '{}' (load, pause or sleep): {}", frame_ms / 1000.0, g_area.name,
              zones);
        // Loads are worth a line of their own; pauses and sleep (much longer) are not.
        if (frame_ms < 30000.0) {
            CT_LOG_INFO(Main, "diag stall: {:.2f} s without a frame in '{}': {}", frame_ms / 1000.0, g_area.name,
                        zones);
        }
    } else {
        if (g_parts_fresh) {
            const FrameParts &p = g_parts;
            const double      busy = p.logic_ms + p.lease_ms + p.locked_ms;
            const double      draws = static_cast<double>(PresentGetStats().last_draws);
            const bool        late = p.frames > 0 && p.ticks > p.frames;
            const bool        short_display = p.ticks > 0 && presents < static_cast<uint64_t>(p.ticks);
            g_area.logic.Add(p.logic_ms);
            g_area.lease.Add(p.lease_ms);
            g_area.locked.Add(p.locked_ms);
            g_area.draws.Add(draws);
            g_area.game_late += late ? 1u : 0u;
            g_area.display_short += short_display ? 1u : 0u;
            ++g_window.frames;
            g_window.ticks += static_cast<uint64_t>(std::max(p.ticks, 0));
            g_window.presents += presents;
            g_window.game_late += late ? 1u : 0u;
            g_window.display_short += short_display ? 1u : 0u;
            g_window.busy_max = std::max(g_window.busy_max, busy);
            g_window.lease_total += p.lease_ms;
            g_window.lease_max = std::max(g_window.lease_max, p.lease_ms);
            g_window.logic_total += p.logic_ms;
            g_window.logic_max = std::max(g_window.logic_max, p.logic_ms);
            g_window.draws_total += draws;
            g_window.flare_frames += flare ? 1u : 0u;
            for (std::size_t i = 0; i < g_window.other.size(); ++i) {
                g_window.other[i] += g_zone_other_frame[i];
            }
            if (late || short_display || busy > 25.0) {
                KeepWorst(busy, std::format("+{:.1f} s, {:.1f} ms busy = logic {:.1f} + lease wait {:.1f} + under lease "
                                            "{:.1f}; {} ticks for {}, {} presents, {:.0f} draws; game thread: {}; "
                                            "render thread: {}",
                                            Ms(now - g_area.start) / 1000.0, busy, p.logic_ms, p.lease_ms, p.locked_ms,
                                            p.ticks, p.frames, presents, draws, TopZonesOf(g_zone_frame, 5, 0.5, true),
                                            TopZonesOf(g_zone_other_frame, 5, 0.5, true)));
            }
            g_parts_fresh = false;
        }
        ++g_area.frames;
        g_area.total_ms += frame_ms;
        g_area.worst_ms = std::max(g_area.worst_ms, frame_ms);
        g_area.over30 += frame_ms > kBudget30 ? 1u : 0u;
        g_area.over20 += frame_ms > kBudget20 ? 1u : 0u;
        g_area.over15 += frame_ms > kBudget15 ? 1u : 0u;
        if (frame_ms > kBudget15) {
            const std::string zones = TopFrameZones(4);
            Notef("slow", "{:.0f} ms frame in '{}': {}", frame_ms, g_area.name, zones);
            if (frame_ms > kHitchLog) {
                CT_LOG_INFO_LIMIT(std::chrono::milliseconds{250}, Main, "diag hitch: {:.0f} ms frame in '{}': {}",
                                  frame_ms, g_area.name, zones);
            }
        }
    }

    // The renderer's timings are summed until taken; take them every few seconds so the totals stay small.
    if (now - g_last_gpu_take >= std::chrono::seconds(5)) {
        g_last_gpu_take = now;
        TakeGpu();
        LogWindow(now);
    }

    // The area's full breakdown every 30 s as well, so a session that ends in an area still reports it.
    if (now - g_last_area_report >= std::chrono::seconds(30) && area == g_area.name) {
        g_last_area_report = now;
        LogArea(false);
    }

    if (area != g_area.name) {
        LogArea(true);
        Notef("area", "loop {}, {} (from {})", loop_no, area, g_area.name);
        StartArea(area, now);
    }

    // Hold Minus for 1.5 seconds: write everything now, while the problem is still on screen.
    if (mark_held) {
        if (g_mark_since == Clock::time_point{}) {
            g_mark_since = now;
        } else if (!g_mark_done && now - g_mark_since >= std::chrono::milliseconds(1500)) {
            g_mark_done = true;
            Note("mark", "player marked this moment");
            Dump("marked by the player");
        }
    } else {
        g_mark_since = {};
        g_mark_done = false;
    }
}

} // namespace dc2::diag
