#pragma once

/**
 * Reads and decodes the town menu's heaviest textures and its character model on a worker shortly after the town
 * starts, so opening the menu finds them ready. The frame trace showed opening the menu in Palm Brinks spending 85,
 * 60 and 25 ms on texture documents (frametex, allitem, edmenu, item) and about 90 ms reading and building the
 * menu's character model, all on the game thread. Dungeons start it too, and it also warms the item pickup
 * sparkle model. Game thread only.
 */
namespace menu_prefetch {

void Start();

} // namespace menu_prefetch
