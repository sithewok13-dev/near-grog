/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef BACKENDS_PLATFORM_IOS7_IOS7_NEARGROG_TOUCH_H
#define BACKENDS_PLATFORM_IOS7_IOS7_NEARGROG_TOUCH_H

/*
 * Near-Grog's touch controls for Grim Fandango and Escape from Monkey Island,
 * after the owner's house style (jk-files docs/HOUSE_STYLE.md, section 3),
 * ported from the Serious Sam overlay's layout and timings:
 *
 *  - a move stick that appears under the left thumb (left 42% of the
 *    screen, radius 60 pt, 30% dead zone), walking the way it points on
 *    screen (the games' camera-relative mode); pushed right out (90%), any
 *    way, runs (the knob lights blue); no run marker
 *  - bottom right: USE in FIRE's place (42 pt), LOOK / GET / BAG on the arc
 *    around it (no RUN button: the stick runs)
 *  - top left, Monkey Island only: NEXT and PREV object (a tap is a step)
 *  - top right: QUICK SAVE and QUICK LOAD (0.3 s holds with a ring), MENU
 *    (a tap: the game's menu; held 0.45 s: the Near-Grog tray)
 *  - untouched buttons at 65%, a touched one at 100%; a touch within 6 pt
 *    of a button counts, the nearest wins
 *
 * Anywhere else a touch goes to the game as before (a tap is a click, which
 * the game uses to pick listed objects and dialogue and to skip lines). The
 * overlay only shows while the game is being played: not in its menus,
 * movies or ScummVM's dialogs.
 */

enum {
	kNearGrogTouchHidden = 0,
	kNearGrogTouchGrim = 1,
	kNearGrogTouchMonkey = 2
};

/** From the engine: what to show (kNearGrogTouch*). Any thread. */
void iOS7_setNearGrogTouchMode(int mode);
#ifdef __OBJC__
#import <CoreGraphics/CoreGraphics.h>
/**
 * The touch map, while menu taps are being worked out: a red ring where a
 * finger lands on the game view (points), fading in a second. Main thread.
 */
void iOS7_showTouchDot(CGPoint p);
#endif

/** From the backend: whether ScummVM's GUI is over the game. Any thread. */
void iOS7_setNearGrogGuiShown(bool shown);

#endif
