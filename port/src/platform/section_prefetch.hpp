#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "platform/texture_catalog.hpp"

/**
 *
 * Reads and decodes a streamed map section's models and textures on a worker thread before the game thread builds
 * them, so the frame that builds the section finds them ready instead of reading the SD card. Palm Brinks streams
 * its sections while the player walks; this removes the read from those frames.
 *
 * The game thread collects what to read (Start), keeps the section's build step waiting while the worker runs
 * (ShouldWait, with a time limit), and logs the results (Report). The worker never logs: on Switch a thread that
 * logged crashed as it ended (see the Make page prefetch in menumain.cpp).
 *
 */
namespace section_prefetch {

/**
 *
 * What one section will read: its glTF models (map parts and villagers) and its texture documents' pictures.
 *
 */
struct Job {
    std::string                         label;       /**< Section name for the log. */
    std::vector<std::filesystem::path>  models;      /**< glTF paths, relative to the data root. */
    std::vector<texture_catalog::Entry> textures;    /**< Texture pictures to decode ahead. */
    int                                 villagers{}; /**< How many of models are villagers, for the log. */
};

/**
 *
 * Adds a villager model to the next job; the editloop queues a section's villagers as it starts loading.
 *
 */
void QueueModel(const std::filesystem::path &gltf);

/**
 *
 * Starts reading the job (plus any queued models) on the worker; returns false, leaving the game to read as before,
 * when a previous job is still running or no thread could be made.
 *
 */
bool Start(Job job);

/**
 *
 * True while the build step should wait for the worker: it is running and has not passed the time limit. Records
 * the time the game spent waiting.
 *
 */
bool ShouldWait();

/**
 *
 * Logs a finished job once, on the game thread; call every frame.
 *
 */
void Report();

} // namespace section_prefetch
