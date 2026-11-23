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
 */
namespace frame_trace {

bool Enabled();

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

} // namespace frame_trace
