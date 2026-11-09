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
