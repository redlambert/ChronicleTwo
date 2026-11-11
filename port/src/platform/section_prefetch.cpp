#include "platform/section_prefetch.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include "diag/recorder.hpp"
#include "log/log.hpp"
#include "platform/datapacks.hpp"
#include "platform/model_assets.hpp"

namespace section_prefetch {
namespace {

using Clock = std::chrono::steady_clock;

// The longest the section's build step waits for the worker. Past it the game builds the section itself, reading
// whatever the worker has not finished, exactly as before this existed; the worker keeps going safely beside it.
constexpr double kWaitLimitMs = 2500.0;

/**
 *
 * What the worker reports back; it only stores numbers, the game thread logs them.
 *
 */
struct Result {
    std::atomic<bool> done{false};
    std::atomic<int>  models_ready{0};
    std::atomic<int>  models_failed{0};
    std::atomic<int>  worker_us{0};
};

std::atomic<bool>                    running{false};
std::shared_ptr<Result>              result;
std::vector<std::filesystem::path>   queued_models;
std::string                          label;
int                                  model_count = 0;
int                                  villager_count = 0;
std::size_t                          texture_count = 0;
Clock::time_point                    started;
double                               waited_ms = 0.0;
bool                                 gave_up = false;
bool                                 logged_failure = false;
bool                                 waiting = false;
Clock::time_point                    wait_start;
// Declared last so it is destroyed, and the worker joined, before the state the worker writes.
std::jthread                         worker;

double Ms(Clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
}

} // namespace

void QueueModel(const std::filesystem::path &gltf) {
    queued_models.push_back(gltf);
}

bool Start(Job job) {
    if (running.load()) {
        CT_LOG_INFO(Town, "section prefetch: {} not read ahead, the previous section's read is still running", job.label);
        dc2::diag::Notef("section", "prefetch skipped for {} (previous still running)", job.label);
        queued_models.clear();
        return false;
    }
    // The previous worker has finished (setting done was its last step); collect it before starting another.
    if (worker.joinable()) {
        worker.join();
    }
    job.villagers += static_cast<int>(queued_models.size());
    for (auto &model : queued_models) {
        job.models.push_back(std::move(model));
    }
    queued_models.clear();

    // A model listed twice is read once.
    std::unordered_set<std::string> seen;
    std::erase_if(job.models, [&seen](const std::filesystem::path &path) { return !seen.insert(path.generic_string()).second; });

    if (job.models.empty() && job.textures.empty()) {
        return false;
    }

    label = job.label;
    model_count = static_cast<int>(job.models.size());
    villager_count = job.villagers;
    texture_count = job.textures.size();
    waited_ms = 0.0;
    gave_up = false;
    waiting = false;
    started = Clock::now();
    result = std::make_shared<Result>();
    running = true;

    auto shared_job = std::make_shared<Job>(std::move(job));
    try {
        worker = std::jthread([shared_job, report = result] {
            const auto begin = Clock::now();
            try {
                std::vector<const texture_catalog::Entry *> pointers;
                pointers.reserve(shared_job->textures.size());
                for (const auto &entry : shared_job->textures) {
                    pointers.push_back(&entry);
                }
                if (!pointers.empty()) {
                    datapacks::PrefetchEntries(pointers);
                }
            } catch (...) {
                // Not decoded ahead; the game decodes them when it enters the textures, as before.
            }
            for (const auto &model : shared_job->models) {
                if (model_assets::Warm(model)) {
                    ++report->models_ready;
                } else {
                    ++report->models_failed;
                }
            }
            report->worker_us = static_cast<int>(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - begin).count());
            report->done = true;
            running = false;
        });
    } catch (const std::exception &error) {
        running = false;
        result.reset();
        if (!logged_failure) {
            logged_failure = true;
            CT_LOG_WARN(Town, "section prefetch: no worker thread ({}); sections load as before", error.what());
        }
        return false;
    }
    CT_LOG_INFO(Town, "section prefetch: reading {} ahead: {} models ({} villagers), {} texture pictures",
                label, model_count, villager_count, texture_count);
    dc2::diag::Notef("section", "prefetch {} started: {} models, {} textures", label, model_count, texture_count);
    return true;
}

bool ShouldWait() {
    const auto now = Clock::now();
    const bool wait = running.load() && !gave_up;
    if (wait && Ms(now - started) > kWaitLimitMs) {
        gave_up = true;
        CT_LOG_WARN(Town, "section prefetch: {} still reading after {:.0f} ms; building the section without waiting further",
                    label, Ms(now - started));
        dc2::diag::Notef("section", "prefetch {} passed the wait limit", label);
    } else if (wait) {
        if (!waiting) {
            waiting = true;
            wait_start = now;
        }
        return true;
    }
    if (waiting) {
        waiting = false;
        waited_ms += Ms(now - wait_start);
    }
    return false;
}

void Report() {
    // Waits until the build step has stopped waiting too, so the log has the full wait.
    if (!result || !result->done.load() || waiting) {
        return;
    }
    const auto finished = std::move(result);
    const int  failed = finished->models_failed.load();
    CT_LOG_INFO(Town,
                "section prefetch: {} read ahead in {:.0f} ms: {} of {} models ready ({} failed), {} texture pictures; "
                "the section waited {:.0f} ms for it{}",
                label, finished->worker_us.load() / 1000.0, finished->models_ready.load(), model_count, failed,
                texture_count, waited_ms, gave_up ? " (passed the wait limit)" : "");
    dc2::diag::Notef("section", "prefetch {} done in {:.0f} ms ({} failed, waited {:.0f} ms)", label,
                     finished->worker_us.load() / 1000.0, failed, waited_ms);
}

} // namespace section_prefetch
