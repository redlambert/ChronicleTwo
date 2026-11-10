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
    std::map<std::string, PassTotal> passes;
    std::array<ZoneTotal, gfx::profile::kMaxSites> zones{};
};

AreaStats                                       g_area;
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
std::string TopFrameZones(int how_many) {
    const int count = std::min(gfx::profile::site_count.load(std::memory_order_relaxed), gfx::profile::kMaxSites);
    std::vector<std::size_t> order;
    for (int index = 0; index < count; ++index) {
        if (g_zone_frame[static_cast<std::size_t>(index)] >= 1.0) {
            order.push_back(static_cast<std::size_t>(index));
        }
    }
    std::ranges::sort(order, [](std::size_t a, std::size_t b) { return g_zone_frame[a] > g_zone_frame[b]; });
    std::string out;
    for (std::size_t i = 0; i < order.size() && i < static_cast<std::size_t>(how_many); ++i) {
        out += std::format("{}{} {:.0f} ms", out.empty() ? "" : ", ", ZoneName(order[i]), g_zone_frame[order[i]]);
    }
    return out.empty() ? std::string("no timed point over 1 ms") : out;
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
}

void StartArea(std::string_view name, Clock::time_point now) {
    g_area = AreaStats{};
    g_area.name = std::string(name);
    g_area.start = now;
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
    CT_LOG_INFO(Main, "diag: on (flight recorder, per-area frame and graphics-chip timing; hold Minus 1.5 s to mark)");
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
