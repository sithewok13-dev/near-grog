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

#ifndef MACVENTURE_NEARGROG_TRAY_H
#define MACVENTURE_NEARGROG_TRAY_H

namespace MacVenture {

class MacVentureEngine;

/**
 * Near-Grog's tray for the MacVenture games: the layout, its look and the
 * opacity of its corner buttons, ScummVM's menu. Opens from the in-game
 * menu button (and a long press on MENU). Not part of ScummVM.
 */
class NearGrogTray {
public:
	/** Shows the tray (the game is paused meanwhile) and does what is picked. */
	static void open(MacVentureEngine *vm);
};

} // End of namespace MacVenture

#endif
