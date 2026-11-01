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

#ifndef GRIM_NEARGROG_TRAY_H
#define GRIM_NEARGROG_TRAY_H

#include "common/array.h"

namespace Grim {

/**
 * Near-Grog's tray for Grim Fandango and Escape from Monkey Island: opens
 * from the in-game menu button with chapter select (the games' own
 * developer jump scripts), a free camera, a photo button and cheats. Not
 * part of ScummVM.
 */
class NearGrogTray {
public:
	/** Shows the tray (the game is paused meanwhile) and does what is picked. */
	static void open();
	/** Called with every frame just before it is shown: takes a pending photo. */
	static void beforeFlip();
	/**
	 * A touch at (x, y) in screen pixels on a list of text: the games' menus
	 * (a tap picks the item: arrow keys, then Enter) or Monkey Island's lists
	 * at the bottom (a tap highlights the line, a tap on the highlighted line
	 * picks it: Page Up / Page Down, Enter). Returns true if the tap was used.
	 */
	static bool tap(int x, int y);
	/** A tap anywhere else: skips the line being spoken; a double tap (or a tap in a movie) skips the cutscene. */
	static bool skip(bool movie);
	/** Tells the iOS touch overlay whether the game is being played. */
	static void reportTouchMode(bool playing);
	/** Near-Grog's touch controls (true) or ScummVM's own, for this game. */
	static bool nearGrogControls();

private:
	static int _photoFrames;
	static Common::Array<int> _keys; // keycodes to press, one a frame
	static bool _keyDown;
	static uint32 _lastTap;
	static void pressKeys();
	static bool _objectNames;
	static bool _roomName;
	static bool _noWalls;
	static void chapters();
	static void cheats();
	static void freeCamera();
	static void savePhoto();
};

} // End of namespace Grim

#endif
