#pragma once

#include <string>
#include <vector>

/**
 * Reads and decodes the town menu's heaviest textures and its character model on a worker shortly after the town
 * starts, so opening the menu finds them ready. The frame trace showed opening the menu in Palm Brinks spending 85,
 * 60 and 25 ms on texture documents (frametex, allitem, edmenu, item) and about 90 ms reading and building the
 * menu's character model, all on the game thread. Dungeons start it too, and it also warms the item pickup
 * sparkle model. Game thread only.
 *
 * The models are extra model files the worker reads too; dungeons pass the other characters' models so the first
 * swap of a session does not read them on the game thread.
 */
namespace menu_prefetch {

void Start(std::vector<std::string> models = {});

} // namespace menu_prefetch
