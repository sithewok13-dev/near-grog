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
	 * A touch at (x, y) in screen pixels. In Escape from Monkey Island, a tap
	 * on one of the dialogue choices picks it (the game itself only takes the
	 * arrow keys and Enter). Returns true if the tap was used.
	 */
	static bool tap(int x, int y);

private:
	static int _photoFrames;
	static Common::Array<int> _keys; // keycodes to press, one a frame
	static bool _keyDown;
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
