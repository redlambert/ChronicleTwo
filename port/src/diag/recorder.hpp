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

#include <format>
#include <string>
#include <string_view>
#include <utility>

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
 * Advances the frame watch by one main-loop tick. Area names the place the player is in, such as
 * "town m01" or "dungeon 0 d01b01"; a change of area writes the finished area's summary.
 */
void Tick(int loop_no, std::string_view area, bool mark_held);

} // namespace dc2::diag
