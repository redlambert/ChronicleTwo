#pragma once

/**
 * Draws the timed banner naming the character selected by quick change.
 */
void PortDrawQuickChangeBanner();
/**
 * Starts the banner naming chara, sliding in from the left edge as a new emission.
 */
void PortShowQuickChangeBanner(int chara);
/**
 * Discards the quick-change banner before entering another gameplay mode.
 */
void PortCancelQuickChangeBanner();
/**
 * Plays the quick-change effect on the new character: a fade from white and a splash of stars.
 */
void PortStepQuickChangeEffect();
/**
 * Reports whether a new quick change must wait for the last one's effect to end.
 */
bool PortQuickChangeCoolingDown();
/**
 * The quick change waiting on a glow-up (still glowing or finished), or -1. Feed it back as the request each
 * tick, so the swap fires through the same checks a button press would.
 */
int PortQuickChangeWindUpHeld();
/**
 * Glows the current character up to white for a few ticks before a quick change, so the loading pause lands
 * on a white character rather than reading as a hitch. Returns -1 while glowing, the held request on the tick
 * the glow finishes, and request unchanged when no glow-up applies. A glow-up that is not polled on some tick
 * (an event, a menu or a hit took over) is dropped, and its glow fades back on its own.
 */
int PortQuickChangeWindUp(int request, bool eligible);

/**
 * Completes a quick change the frame just opened, in that same frame, so the game never stops for its menu.
 */
void PortFinishQuickChange();

/**
 * The landing sound sets for the Game page's swap sound rows: 1 the dungeon's shared battle sounds, 2 the menu and
 * system sounds, 3 the new character's own sounds. How many sounds a set holds where it is loaded now for that kind
 * of swap (SwapGroup), 0 where it is not loaded; a character's own sounds are loaded only while playing as it.
 */
int QuickChangeSoundCount(int set, int group);
/**
 * Whether one sound of a landing sound set is loaded and playable for that kind of swap.
 */
bool QuickChangeSoundPlayable(int set, int group, int number);
/**
 * Plays one sound of a landing sound set as the Game page's preview, first stopping the preview before it, which
 * can loop; set 0 only stops it.
 */
void QuickChangeSoundPreview(int set, int group, int number);

/**
 * Loads the effects the swap rows picked that the dungeon's own set leaves out, while the floor loads, from the
 * dungeon's effect pack (eff_pack) or dungeon/eff_script.
 */
void QuickChangeLoadSwapEffects(unsigned int *eff_pack);
/**
 * Whether a kSwapEffects entry can play on this floor now: true away from the dungeon and for Off.
 */
bool QuickChangeSwapEffectLoaded(int choice);
