#include "diag/frame_trace.hpp"

#include <chrono>
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

Writer &TheWriter() {
    static Writer writer;
    return writer;
}

} // namespace

bool Enabled() {
    return TheWriter().Enabled();
}

void Present(const PresentRecord &record) {
    Writer &writer = TheWriter();
    if (!writer.Enabled()) {
        return;
    }
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
    writer.Add(std::format("G,{},{},{:.2f},{:.2f},{:.2f},{:.2f},{:.2f},{},{},{}\n", writer.Microseconds(), record.area,
                           record.frame_ms, record.logic_ms, record.lease_ms, record.locked_ms, record.wait_ms,
                           record.ticks, record.frames, record.draws));
}

void Note(std::string_view text) {
    Writer &writer = TheWriter();
    if (!writer.Enabled()) {
        return;
    }
    writer.Add(std::format("N,{},{}\n", writer.Microseconds(), text));
}

} // namespace frame_trace
