#pragma once

// Always-on diagnostics for testing the port.
//
// Flight recorder: Note() keeps a short line about something that happened (an area change, an event, a stack
// clear, a monster spawn, a slow frame, ...) in memory only. Nothing reaches the log until Dump() writes the last
// few hundred lines, which happens when the player holds Minus for 1.5 seconds ("mark"), and before a safety stop.
//
// Frame watch: Tick() runs once per main-loop tick. It times every tick, samples the built-in timing points of the
// game and renderer and the graphics chip's own time, and writes one summary line per area when the area changes.
//
// Note() is safe from any thread and never logs. Dump() and Tick() run on the main thread only.

#include <chrono>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include "profile.hpp"

namespace dc2::diag {

/**
 * Keeps one line in the flight recorder. Category is a short word such as "event" or "stack".
 */
void Note(std::string_view category, std::string_view text);

/**
 * Formats and keeps one line in the flight recorder.
 */
template <class... Args>
void Notef(std::string_view category, std::format_string<Args...> format, Args &&...args) {
    Note(category, std::format(format, std::forward<Args>(args)...));
}

/**
 * Writes the flight recorder and the current area's frame summary so far to the log, then flushes it.
 */
void Dump(std::string_view reason);

/**
 * Where one game frame's time went on the main thread, measured in mgEndFrame.
 */
struct FrameParts {
    double logic_ms = 0.0;  // game update, from the end of the last wait to mgEndFrame
    double lease_ms = 0.0;  // waiting for the render lease (the render thread finishing its frame)
    double locked_ms = 0.0; // device work under the lease: uploads, recording close, readbacks, publish
    double wait_ms = 0.0;   // idle wait for the next tick boundary
    int    ticks = 0;       // 60 Hz ticks the frame took, wait included
    int    frames = 0;      // ticks the game asked for (2 at 30 game frames a second)
};

/**
 * Marks that this game frame drew the sun's lens flare. Main thread only.
 */
void NoteLensFlare();

/**
 * Whether the sun's lens flare should draw: the Display settings' Sun flare row (video.lens_flare).
 */
bool LensFlareEnabled();

/**
 * Hands the frame watch the parts of the game frame that just ended. Main thread only.
 */
void NoteFrameParts(const FrameParts &parts);

/**
 * Adds one part of a game frame's draw recording: its time and the draws it submitted. Main thread only.
 * Name must be a string literal: the frame watch keeps the pointer.
 */
void NoteDrawPart(const char *name, double ms, uint64_t draws);

/**
 * Times one part of the draw recording and counts the draws it submits, for the frame watch's draw parts.
 */
class DrawPart {
public:
    explicit DrawPart(const char *name);
    ~DrawPart();
    DrawPart(const DrawPart &) = delete;
    DrawPart &operator=(const DrawPart &) = delete;

private:
    const char                           *name_;
    std::chrono::steady_clock::time_point start_;
    uint64_t                              draws_;
};

/**
 * Advances the frame watch by one main-loop tick. Area names the place the player is in, such as
 * "town m01" or "dungeon 0 d01b01"; a change of area writes the finished area's summary.
 */
void Tick(int loop_no, std::string_view area, bool mark_held);

} // namespace dc2::diag

// A timing point that also feeds the frame watch's draw parts (time and draws per part, per area and per 5 s).
#define CT_DRAW_PART(name) \
    CT_PROFILE_ZONE(name); \
    ::dc2::diag::DrawPart ct_draw_part(name)
