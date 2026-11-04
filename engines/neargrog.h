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

#ifndef ENGINES_NEARGROG_H
#define ENGINES_NEARGROG_H

#include "common/config-manager.h"
#include "common/system.h"

/**
 * Near-Grog's phone layouts (MacVenture, the NES games, C64 Uninvited)
 * draw on the whole screen - not only the safe area - and keep clear of the
 * Dynamic Island, the corners and the home bar themselves. Their canvas is
 * 646 pixels on the short side and the screen's own shape on the long one,
 * so it fills the screen exactly.
 */
namespace NearGrog {

/** Asks the backend (iOS) to draw the game on the whole screen. */
inline void wholeScreen(bool on) {
	ConfMan.setBool("neargrog_full_screen", on, Common::ConfigManager::kTransientDomain);
}

inline void canvasSize(bool portrait, int &w, int &h) {
	int ow = g_system->getOverlayWidth(), oh = g_system->getOverlayHeight();
	const Common::String forced = ConfMan.get("neargrog_orientation"); // (tests)
	if (!forced.empty() || ow <= 0 || oh <= 0) {
		ow = 2868;
		oh = 1320;
	}
	const int lo = MIN(ow, oh), hi = MAX(ow, oh);
	const int longSide = CLIP<int>(646 * hi / lo, 1100, 1500);
	w = portrait ? 646 : longSide;
	h = portrait ? longSide : 646;
}

/** Kept clear: the island's side in landscape (either side), its top in portrait, the home bar. */
enum {
	kLandscapeSide = 72,
	kPortraitTop = 78,
	kHomeBar = 18
};

} // End of namespace NearGrog

#endif
