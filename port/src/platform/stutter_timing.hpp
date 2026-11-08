#pragma once

#include <chrono>

// Temporary stutter diagnostics. File reads and character loads add their time here, and the main loop
// reports any slow frame with these totals, then clears them for the next frame.
namespace dc2::stutter_timing {

inline double frame_file_ms = 0.0;
inline int    frame_files = 0;
inline double frame_character_ms = 0.0;
inline int    frame_characters = 0;

// Milliseconds since the given start point.
inline double ElapsedMs(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

inline void ResetFrame() {
    frame_file_ms = 0.0;
    frame_files = 0;
    frame_character_ms = 0.0;
    frame_characters = 0;
}

} // namespace dc2::stutter_timing
