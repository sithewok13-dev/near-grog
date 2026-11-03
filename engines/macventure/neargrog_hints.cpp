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


/*
 * Near-Grog: the Hints cheat - a nudge for the room you are in, per game,
 * written for Near-Grog (not from the games). Rooms are matched by the title
 * the game gives them.
 */

#include "common/str.h"
#include "macventure/neargrog_hints.h"

namespace MacVenture {

namespace {

struct Hint {
	const char *game; // gameid
	const char *room; // the room's title, as the game shows it
	const char *text;
};

const Hint kHints[] = {
#include "macventure/neargrog_hints_data.h"
	{ nullptr, nullptr, nullptr }
};

} // End of anonymous namespace

const char *findHint(const Common::String &game, const Common::String &room) {
	for (int i = 0; kHints[i].game; i++)
		if (game.equalsIgnoreCase(kHints[i].game) && room.equalsIgnoreCase(kHints[i].room))
			return kHints[i].text;
	return nullptr;
}

} // End of namespace MacVenture
