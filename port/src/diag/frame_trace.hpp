#pragma once

#include <cstdint>
#include <string_view>

/**
 * Frame trace: one line per image sent to the screen, per image the screen showed and per game frame, written to a
 * .frames.csv file beside the session log. On with DC2_FRAME_TRACE=1 (args.txt: --env DC2_FRAME_TRACE=1).
 *
 * Lines, by their first field:
 *   P,t_us,kind,serial,alpha,previous_ok,screen_id,screen_ns,canonical_gpu_ms,replay_gpu_ms,hold,cpu_ms
 *     an image sent to the screen. kind: I in-between, E end-of-frame, H in-between held (its end-of-frame image
 *     again), R replayed at alpha, C a tick shown once. previous_ok: for I, whether it blends from the end-of-frame
 *     image shown before it. hold: b chip budget, l late game frames, - none
 *   S,screen_id,queued_ns,shown_ns
 *     an image the screen showed, from its own records; screen_ns in P is on the same clock
 *   G,t_us,area,frame_ms,logic_ms,lease_ms,locked_ms,wait_ms,ticks,frames,draws
 *     a game frame that just ended
 *   N,t_us,text
 *     an event
 *   Z,t_us,area,frame_ms,game zones,|,other-thread zones
 *     a long game frame, every timing point over 0.3 ms ("name ms" pairs separated by ';')
 *   L,t_us,kind,thread,ms,what
 *     a slow single operation (see Slow); thread g the game thread, w any other
 */
namespace frame_trace {

bool Enabled();

/**
 * What the trace itself cost since the last TakeSelfCost: time spent turning records into lines (on the game thread
 * and on others), the writer thread's time draining them to the card, and how much it wrote.
 */
struct SelfCost {
    double        game_ms = 0.0;   // line building on the game thread
    double        other_ms = 0.0;  // line building on other threads (the render thread's P lines, workers' L lines)
    double        writer_ms = 0.0; // the writer thread: screen records, file writes and flushes
    std::uint64_t lines = 0;
    std::uint64_t bytes = 0;
};

// Returns the trace's cost since the last call and starts counting again. Any thread.
SelfCost TakeSelfCost();

// Line building time on the game thread so far, in ms, for subtracting the trace from a timed section that writes it.
double GameCostMs();

struct PresentRecord {
    char          kind = '?';
    std::uint64_t serial = 0;
    float         alpha = 1.0f;
    bool          previous_ok = true;
    char          hold = '-';
    double        cpu_ms = 0.0;
};

// Render thread, right after a present was made.
void Present(const PresentRecord &record);

struct GameRecord {
    std::string_view area;
    double           frame_ms = 0.0;
    double           logic_ms = 0.0;
    double           lease_ms = 0.0;
    double           locked_ms = 0.0;
    double           wait_ms = 0.0;
    int              ticks = 0;
    int              frames = 0;
    std::uint64_t    draws = 0;
};

// Game thread, once per game frame.
void Game(const GameRecord &record);

void Note(std::string_view text);

// A long game frame: every timing point on the game thread and the busiest on other threads, as "name ms, ...".
void LongFrame(std::string_view area, double frame_ms, std::string_view game_zones, std::string_view other_zones);

// A slow single operation, with what it worked on. kind: T texture document, M model build, A archive read,
// F game file load, V villager step, U town update part, S one script run (module name), C a script run of 8 ms or
// more broken down by native command ("name ms xcalls;" pairs, then the script's own code), X other.
void Slow(char kind, std::string_view what, double ms);

// Times a scope and writes a Slow line when it runs at least min_ms. Cheap when the trace is off.
class SlowScope {
public:
    SlowScope(char kind, std::string_view what, double min_ms = 3.0);
    ~SlowScope();
    SlowScope(const SlowScope &) = delete;
    SlowScope &operator=(const SlowScope &) = delete;

private:
    char             kind_;
    std::string_view what_;
    double           min_ms_;
    long long        start_ns_ = -1;
};

/**
 * Splits one long function into laps for the trace: each Mark ends the lap since the last mark under its label, and
 * when the whole run took at least min_ms the laps go out as a U line (Slow kind U). Labels must be string literals.
 * Cheap when the trace is off.
 */
class Laps {
public:
    explicit Laps(const char *name, double min_ms = 40.0);
    ~Laps();
    Laps(const Laps &) = delete;
    Laps &operator=(const Laps &) = delete;

    void Mark(const char *label);

private:
    static constexpr int kMaxLaps = 32;
    const char          *name_;
    double               min_ms_;
    long long            start_ns_ = -1;
    long long            last_ns_ = 0;
    int                  count_ = 0;
    const char          *labels_[kMaxLaps] = {};
    double               ms_[kMaxLaps] = {};
};

} // namespace frame_trace
