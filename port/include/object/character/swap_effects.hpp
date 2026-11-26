#pragma once

#include <array>
#include <string_view>

/**
 * The game effects a quick change can play around the character, for the Game page's swap effect rows: every row of
 * the game's effect table whose script never hits, never starts a hit and never plays a sound. The dungeon's own set
 * (dungeon/articles/eff01) holds some of them; the others are loaded with the floor when a swap row picks them, so the
 * first swap never waits for them.
 */
struct SwapEffectChoice {
    std::string_view key;         /**< Settings file value. */
    std::string_view label;       /**< Name shown on the Game page. */
    std::string_view script;      /**< The effect table row's script name, empty for Off. */
    bool             body_middle; /**< Placed at the middle of the body instead of the feet. */
    bool             at_press;    /**< Starts at the press instead of as the new character appears. */
    bool             in_pack;     /**< Its files are in the dungeon's own set rather than dungeon/eff_script. */
};

constexpr std::array<SwapEffectChoice, 73> kSwapEffects{{
    {"off", "Off", "", false, false, false},
    {"max_release", "Max charge release", "c01tame", false, true, true},
    {"monica_release", "Monica charge release", "c02tame", false, true, true},
    {"monica_magic", "Monica magic charge", "c02_magic_tame", true, false, true},
    {"ridepod_flash", "Ridepod muzzle flash", "gun_flash", true, false, true},
    {"dust", "Dust cloud", "pother", false, false, true},
    {"guard", "Guard sparkle", "guard_eff_a", true, false, true},
    {"heal", "Recovery sparkle", "n_heal", false, false, true},
    {"beam", "Butterfly beam", "beam", true, false, false},
    {"n_heal_sub", "Recovery sparkle, small", "n_heal_sub", false, false, false},
    {"polm_smoke", "Ridepod missile smoke", "polm_smoke", false, false, false},
    {"spore", "Pollen cloud", "spore", true, false, false},
    {"spore_sub", "Pollen cloud, small", "spore_sub", true, false, false},
    {"union", "Butterfly merge", "union", true, false, false},
    {"g_shield", "Boss shield", "g_shield", true, false, false},
    {"g_magic_sub", "Boss magic, part", "g_magic_sub", true, false, false},
    {"foot_pother", "Footstep dust", "foot_pother", false, false, true},
    {"bullet", "Gun bullet", "bullet", true, false, true},
    {"e110pol1", "Spinning sparkle", "e110pol1", true, false, false},
    {"smoke_tore", "Tore's smoke", "smoke_tore", false, false, false},
    {"foot_hamon", "Footstep ripple", "foot_hamon", false, false, true},
    {"foot_splatter", "Footstep splash", "foot_splatter", false, false, true},
    {"foot_grass", "Footstep grass", "foot_grass", false, false, true},
    {"m_fball_f", "Fireball cast", "m_fball_f", true, false, false},
    {"robo_failure", "Ridepod breakdown", "robo_failure", true, false, false},
    {"buggy_pother", "Buggy dust", "buggy_pother", false, false, false},
    {"hinoko", "Embers", "hinoko", false, false, false},
    {"potapota", "Drips", "potapota", false, false, false},
    {"p00_23a_msibuki", "Splash spray", "p00_23a_msibuki", false, false, false},
    {"mguil_kemuri", "Smoke puff", "mguil_kemuri", false, false, false},
    {"m_fballf_f", "Fire shot cast", "m_fballf_f", true, false, false},
    {"m_cball_f", "Ice shot cast", "m_cball_f", true, false, false},
    {"m_sball_f", "Thunder shot cast", "m_sball_f", true, false, false},
    {"m_wball_f", "Wind shot cast", "m_wball_f", true, false, false},
    {"m_hball_f", "Holy shot cast", "m_hball_f", true, false, false},
    {"m_mball_f", "Water shot cast", "m_mball_f", true, false, false},
    {"m_curse_f", "Curse shot cast", "m_curse_f", true, false, false},
    {"tarub", "Barrel shot", "tarub", true, false, false},
    {"m_swave_f", "Shock wave cast", "m_swave_f", false, false, false},
    {"buggy_shell_case", "Buggy shell casing", "buggy_shell_case", false, false, false},
    {"b06a_warp", "Warp", "b06a_warp", false, false, false},
    {"b06a_warp2", "Warp 2", "b06a_warp2", false, false, false},
    {"m_spin", "Spin", "m_spin", false, false, false},
    {"pother2", "Dust cloud 2", "pother2", false, false, false},
    {"pother_e107", "Dust cloud 3", "pother_e107", false, false, false},
    {"atora", "Atla glow", "atora", true, false, false},
    {"b10a_swave_f", "Boss shock wave cast", "b10a_swave_f", false, false, false},
    {"b10a_combo", "Boss combo", "b10a_combo", true, false, false},
    {"b10a_handfire", "Hand flame", "b10a_handfire", true, false, false},
    {"m_hankemuri", "Smoke", "m_hankemuri", false, false, false},
    {"b11a_hikari_blue", "Blue light", "b11a_hikari_blue", true, false, false},
    {"b11a_hikari_red", "Red light", "b11a_hikari_red", true, false, false},
    {"b11a_beam_f", "Beam cast", "b11a_beam_f", true, false, false},
    {"e209_magic", "Monster magic", "e209_magic", true, false, false},
    {"m_mahi_f", "Paralysis shot cast", "m_mahi_f", true, false, false},
    {"m_heal_f", "Monster heal cast", "m_heal_f", true, false, false},
    {"m_heal", "Monster heal", "m_heal", true, false, false},
    {"e640_beam", "Moon flower beam", "e640_beam", true, false, false},
    {"e616_beam_a", "Moon flower beam A", "e616_beam_a", true, false, false},
    {"e616_beam_b", "Moon flower beam B", "e616_beam_b", true, false, false},
    {"kemuri_train", "Train smoke", "kemuri_train", false, false, false},
    {"m_kanden", "Electric shock", "m_kanden", true, false, false},
    {"e808_mp_jet", "Jet", "e808_mp_jet", true, false, false},
    {"ggun_damage", "Warship damage", "ggun_damage", true, false, false},
    {"e219ex_fire_f", "Dragon fire cast", "e219ex_fire_f", true, false, false},
    {"e516_gun_s", "Machine gun", "e516_gun_s", true, false, false},
    {"e516_gun_tyaku", "Machine gun impact", "e516_gun_tyaku", true, false, false},
    {"mj_beam_f2", "Demon beam cast", "mj_beam_f2", true, false, false},
    {"e356_hinoko", "Embers 2", "e356_hinoko", false, false, false},
    {"e732_snow", "Light rain", "e732_snow", false, false, false},
    {"e612_pother", "Dust cloud 4", "e612_pother", false, false, false},
    {"m_smoke", "Smoke 2", "m_smoke", false, false, false},
    {"g_magic_sub2", "Boss magic, part 2", "g_magic_sub2", true, false, false},
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
