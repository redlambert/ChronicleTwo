#pragma once

#include <array>
#include <string_view>

/**
 * The game effects a quick change can play around the character, for the Game page's swap effect rows. Every one is
 * in the dungeon's always-loaded effect set (dungeon/articles/eff01), so it plays on any floor, and none of them
 * hits, spawns anything or plays a sound of its own.
 */
struct SwapEffectChoice {
    std::string_view key;   /**< Settings file value. */
    std::string_view label; /**< Name shown on the Game page. */
};

constexpr std::array<SwapEffectChoice, 8> kSwapEffects{{
    {"off", "Off"},
    {"max_release", "Max charge release"},
    {"monica_release", "Monica charge release"},
    {"monica_magic", "Monica magic charge"},
    {"ridepod_flash", "Ridepod muzzle flash"},
    {"dust", "Dust cloud"},
    {"guard", "Guard sparkle"},
    {"heal", "Recovery sparkle"},
}};

/**
 * The three kinds of quick change that each get their own effect and landing sound.
 */
enum SwapGroup {
    SWAP_GROUP_HUMAN,   /**< Into Max or Monica, from anyone. */
    SWAP_GROUP_RIDEPOD, /**< Max into the Ridepod. */
    SWAP_GROUP_MONSTER, /**< Monica into her monster form. */
    SWAP_GROUP_COUNT
};

constexpr int kSwapEffectMaxRelease = 1;
constexpr int kSwapEffectMonicaMagic = 3;
constexpr int kSwapEffectRidepodFlash = 4;
