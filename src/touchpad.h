/* On-screen touch gamepad [motog patch 0002]: the overlay buttons a phone
 * player presses instead of a physical gamepad. See touchpad.c. */
#ifndef GBADS_TOUCHPAD_H
#define GBADS_TOUCHPAD_H

#include "config.h"
#include "ui.h"

#include <stdbool.h>

/* Once per game-loop iteration, right after ui_read_buttons(): reads the
 * physical buttons already in btnNow (for auto-hide), folds every finger
 * resting on the pad into btnNow, and works out whether the pad shows.
 * menuOpen disarms the pad entirely: no injection, no drawing, no claims,
 * so menu taps and menu button presses stay the menu's own. */
void touchpad_poll(const Config* cfg, bool menuOpen, int screen);

/* Once per frame, after the game (and the quick bar) is drawn, before
 * ui_present(): draws the pad over `screen` while it should be visible.
 * Never runs while the menu is open (the menu presents its own frame). */
void touchpad_draw(int screen);

/* UiPointerFilter for ui_set_pointer_filter(): claims pointer events that
 * land on the pad while it is up, so a tap on a pad button presses the
 * button instead of opening the menu. Self-disarming: it only claims
 * while the game loop is actively polling (150 ms staleness window), so
 * the library and menus are never affected. */
bool touchpad_pointer_filter(const Pointer* p);

#endif
