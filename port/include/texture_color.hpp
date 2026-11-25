#pragma once

#include <cstdint>
#include <vector>

class mgCTexture;
enum class MgTextureColorMode {
    IconHighlight,
    Grey,
    Sepia,
    PartyLocked
};
mgCTexture *MgTextureCreateColorVariant(const mgCTexture *source, char *name, MgTextureColorMode mode);

/**
 * A hue change for part of a texture: turns the hue of every pixel in the top rows and scales its saturation,
 * keeping its brightness.
 */
struct MgTextureHueShift {
    float hue_turns = 0.0f;    /**< Hue rotation in whole turns (0.5 turns orange into blue). */
    float saturation = 1.0f;   /**< Saturation multiplier, clamped to full saturation. */
    float top_fraction = 1.0f; /**< Share of the texture's height, from the top, that changes. */
};

/**
 * Enters a copy of a 32-bit texture into the source's block with part of it hue shifted, or returns NULL.
 */
mgCTexture *MgTextureCreateHueVariant(const mgCTexture *source, char *name, const MgTextureHueShift &shift);

/**
 * Enters a copy of a 32-bit texture into the source's block that shows another picture instead (8-bit RGBA, width
 * by height, scaled to the texture), or returns NULL.
 */
mgCTexture *MgTextureCreateImageVariant(const mgCTexture *source, char *name, const std::vector<uint8_t> &rgba,
                                        uint32_t width, uint32_t height);
void        MgTextureSetDrawVariant(const mgCTexture *source, mgCTexture *variant);
mgCTexture *MgTextureDrawVariant(mgCTexture *source);
