#pragma once

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
void        MgTextureSetDrawVariant(const mgCTexture *source, mgCTexture *variant);
mgCTexture *MgTextureDrawVariant(mgCTexture *source);
