#include "diag/frame_trace.hpp"

#include <algorithm>
#include <chrono>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "gfx/gfx.hpp"
#include "log/log.hpp"

namespace frame_trace {
namespace {

using Clock = std::chrono::steady_clock;

long long NowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}

// The trace's own cost, in nanoseconds and bytes, since the last TakeSelfCost (game_total_ns never resets).
std::atomic<std::uint64_t> game_ns{0};
std::atomic<std::uint64_t> game_total_ns{0};
std::atomic<std::uint64_t> other_ns{0};
std::atomic<std::uint64_t> writer_ns{0};
std::atomic<std::uint64_t> line_count{0};
std::atomic<std::uint64_t> byte_count{0};

/**
 * Buffers lines from any thread; a writer thread drains them with the screen's shown images once a second, so no
 * game or render frame waits on the card.
 */
class Writer {
public:
    Writer() {
        const char *setting = std::getenv("DC2_FRAME_TRACE");
        if (setting == nullptr || std::string_view(setting) != "1") {
            return;
        }
        std::filesystem::path path = LogFilePath();
        if (path.empty()) {
            return;
        }
        path.replace_extension(".frames.csv");
        file_ = std::fopen(path.string().c_str(), "wb");
        if (file_ == nullptr) {
            CT_LOG_WARN(Main, "frame trace: cannot open {}", path.string());
            return;
        }
        start_ = Clock::now();
        std::fputs("# frame trace v1; fields are described in port/src/diag/frame_trace.hpp\n", file_);
        std::fputs(std::format("# start screen_ns={}\n", gfx::ScreenClockNs()).c_str(), file_);
        gfx::SetScreenFrameTracking(true);
        enabled_ = true;
        thread_ = std::thread([this] { Run(); });
        CT_LOG_INFO(Main, "frame trace: writing {}", path.string());
    }

    ~Writer() {
        if (!enabled_) {
            return;
        }
        {
            const std::scoped_lock lock(mutex_);
            stop_ = true;
        }
        wake_.notify_all();
        thread_.join();
        std::fclose(file_);
    }

    bool Enabled() const { return enabled_; }

    long long Microseconds() const {
        return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start_).count();
    }

    void Add(std::string line) {
        line_count.fetch_add(1, std::memory_order_relaxed);
        byte_count.fetch_add(line.size(), std::memory_order_relaxed);
        const std::scoped_lock lock(mutex_);
        if (pending_.size() < (1u << 22)) {
            pending_ += line;
        }
    }

private:
    void Run() {
        std::string                         lines;
        std::vector<gfx::ShownScreenFrame> shown;
        std::unique_lock                    lock(mutex_);
        for (;;) {
            wake_.wait_for(lock, std::chrono::seconds(1), [this] { return stop_; });
            lines.swap(pending_);
            const bool stopping = stop_;
            lock.unlock();
            const long long drain_start = NowNs();
            gfx::TakeShownScreenFrames(shown);
            for (const auto &frame : shown) {
                lines += std::format("S,{},{},{}\n", frame.id, frame.queued_ns, frame.shown_ns);
            }
            shown.clear();
            if (!lines.empty()) {
                std::fwrite(lines.data(), 1, lines.size(), file_);
                std::fflush(file_);
                lines.clear();
            }
            writer_ns.fetch_add(static_cast<std::uint64_t>(NowNs() - drain_start), std::memory_order_relaxed);
            if (stopping) {
                return;
            }
            lock.lock();
        }
    }

    std::FILE              *file_ = nullptr;
    bool                    enabled_ = false;
    Clock::time_point       start_{};
    std::mutex              mutex_;
    std::condition_variable wake_;
    std::string             pending_;
    bool                    stop_ = false;
    std::thread             thread_;
};

// The game thread, as the first Game call found it, so a Slow line says which thread was slow.
std::atomic<std::thread::id> game_thread{};

Writer &TheWriter() {
    static Writer writer;
    return writer;
}

/**
 * Adds the time from its creation to its end to the trace's own cost, on the game thread's count or the others'.
 */
class CostScope {
public:
    CostScope() : start_(NowNs()) {}
    ~CostScope() {
        const auto spent = static_cast<std::uint64_t>(NowNs() - start_);
        if (std::this_thread::get_id() == game_thread.load(std::memory_order_relaxed)) {
            game_ns.fetch_add(spent, std::memory_order_relaxed);
            game_total_ns.fetch_add(spent, std::memory_order_relaxed);
        } else {
            other_ns.fetch_add(spent, std::memory_order_relaxed);
        }
    }
    CostScope(const CostScope &) = delete;
    CostScope &operator=(const CostScope &) = delete;

private:
    long long start_;
};

} // namespace

SelfCost TakeSelfCost() {
    SelfCost cost;
    cost.game_ms = static_cast<double>(game_ns.exchange(0, std::memory_order_relaxed)) / 1.0e6;
    cost.other_ms = static_cast<double>(other_ns.exchange(0, std::memory_order_relaxed)) / 1.0e6;
    cost.writer_ms = static_cast<double>(writer_ns.exchange(0, std::memory_order_relaxed)) / 1.0e6;
    cost.lines = line_count.exchange(0, std::memory_order_relaxed);
    cost.bytes = byte_count.exchange(0, std::memory_order_relaxed);
    return cost;
}

double GameCostMs() {
    return static_cast<double>(game_total_ns.load(std::memory_order_relaxed)) / 1.0e6;
}

bool Enabled() {
    return TheWriter().Enabled();
}

void Present(const PresentRecord &record) {
    Writer &writer = TheWriter();
    if (!writer.Enabled()) {
        return;
    }
    const CostScope cost;
    const gfx::RecentGpuWork work = gfx::GetRecentGpuWork();
    writer.Add(std::format("P,{},{},{},{:.3f},{},{},{},{:.2f},{:.2f},{},{:.2f}\n", writer.Microseconds(), record.kind,
                           record.serial, record.alpha, record.previous_ok ? 1 : 0, gfx::LastQueuedScreenFrame(),
                           gfx::ScreenClockNs(), work.canonical_ms, work.replay_ms, record.hold, record.cpu_ms));
}

void Game(const GameRecord &record) {
    Writer &writer = TheWriter();
    if (!writer.Enabled()) {
        return;
    }
    if (game_thread.load(std::memory_order_relaxed) == std::thread::id{}) {
        game_thread.store(std::this_thread::get_id(), std::memory_order_relaxed);
    }
    const CostScope cost;
    writer.Add(std::format("G,{},{},{:.2f},{:.2f},{:.2f},{:.2f},{:.2f},{},{},{}\n", writer.Microseconds(), record.area,
                           record.frame_ms, record.logic_ms, record.lease_ms, record.locked_ms, record.wait_ms,
                           record.ticks, record.frames, record.draws));
}

void Note(std::string_view text) {
    Writer &writer = TheWriter();
    if (!writer.Enabled()) {
        return;
    }
    const CostScope cost;
    writer.Add(std::format("N,{},{}\n", writer.Microseconds(), text));
}

void LongFrame(std::string_view area, double frame_ms, std::string_view game_zones, std::string_view other_zones) {
    Writer &writer = TheWriter();
    if (!writer.Enabled()) {
        return;
    }
    const CostScope cost;
    std::string game(game_zones);
    std::string other(other_zones);
    std::ranges::replace(game, ',', ';');
    std::ranges::replace(other, ',', ';');
    writer.Add(std::format("Z,{},{},{:.1f},{},|,{}\n", writer.Microseconds(), area, frame_ms, game, other));
}

void Slow(char kind, std::string_view what, double ms) {
    Writer &writer = TheWriter();
    if (!writer.Enabled()) {
        return;
    }
    const CostScope cost;
    std::string text(what);
    std::ranges::replace(text, ',', ';');
    std::ranges::replace(text, '\n', ' ');
    const char thread = std::this_thread::get_id() == game_thread.load(std::memory_order_relaxed) ? 'g' : 'w';
    writer.Add(std::format("L,{},{},{},{:.2f},{}\n", writer.Microseconds(), kind, thread, ms, text));
}

SlowScope::SlowScope(char kind, std::string_view what, double min_ms) : kind_(kind), what_(what), min_ms_(min_ms) {
    if (Enabled()) {
        start_ns_ = NowNs();
    }
}

SlowScope::~SlowScope() {
    if (start_ns_ < 0) {
        return;
    }
    const double ms = static_cast<double>(NowNs() - start_ns_) / 1.0e6;
    if (ms >= min_ms_) {
        Slow(kind_, what_, ms);
    }
}

Laps::Laps(const char *name, double min_ms) : name_(name), min_ms_(min_ms) {
    if (Enabled()) {
        start_ns_ = NowNs();
        last_ns_ = start_ns_;
    }
}

void Laps::Mark(const char *label) {
    if (start_ns_ < 0 || count_ >= kMaxLaps) {
        return;
    }
    const long long now = NowNs();
    labels_[count_] = label;
    ms_[count_] = static_cast<double>(now - last_ns_) / 1.0e6;
    ++count_;
    last_ns_ = now;
}

Laps::~Laps() {
    if (start_ns_ < 0) {
        return;
    }
    Mark("(end)");
    const double total = static_cast<double>(last_ns_ - start_ns_) / 1.0e6;
    if (total < min_ms_) {
        return;
    }
    std::string text = std::string(name_) + ":";
    for (int i = 0; i < count_; ++i) {
        if (ms_[i] >= 0.5) {
            text += std::format(" {} {:.1f};", labels_[i], ms_[i]);
        }
    }
    Slow('U', text, total);
}

} // namespace frame_trace
