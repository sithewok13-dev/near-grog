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
 * Near-Grog: the MacVenture cheats, switches on the tray's Cheats page (per
 * game).
 *
 * Unlimited moves: these games count some things down one per move -
 * Shadowgate's torches, for one - in the game's own variables. With cheats
 * on, any value that has gone down by exactly one on two moves in a row is
 * a countdown, and is put back each move, so it never runs out.
 *
 * Undo death: the game is kept in memory as it was before each move; when a
 * move kills you, it is put back instead of the "You Died" box.
 *
 * Instant text: every "click to continue" is answered at once.
 *
 * Show hotspots: frames round what can be clicked (modernui.cpp).
 *
 * Undo any move: the last 20 moves are kept; the tray offers Undo last move.
 */

#include "common/config-manager.h"
#include "common/memstream.h"
#include "common/savefile.h"
#include "common/system.h"
#include "common/translation.h"

#include "macventure/macventure.h"
#include "macventure/world.h"
#include "macventure/gui.h"

namespace MacVenture {

bool MacVentureEngine::cheatOn(const char *key) {
	return ConfMan.hasKey(key) && ConfMan.getBool(key);
}

void MacVentureEngine::cheatBeforeCommand() {
	if (!cheatOn("neargrog_cheat_undo") && !cheatOn("neargrog_cheat_undomove")) {
		_cheatUndo.clear();
		return;
	}
	// The save-file wrapper owns (and deletes) the memory stream
	Common::MemoryWriteStreamDynamic *mem = new Common::MemoryWriteStreamDynamic(DisposeAfterUse::YES);
	Common::OutSaveFile out(mem);
	_world->saveGameInto(&out);
	out.finalize();
	_cheatUndo.push_back(Common::Array<byte>(mem->getData(), mem->size()));
	if (_cheatUndo.size() > 20) // the last 20 moves
		_cheatUndo.remove_at(0);
}

void MacVentureEngine::cheatAfterCommand() {
	Common::Array<uint16> now;
	_world->cheatValues(now);
	if (cheatOn("neargrog_cheat_moves") && _cheatPrev.size() == now.size()) {
		_cheatStreak.resize(now.size());
		for (uint i = 0; i < now.size(); i++) {
			if (_cheatPrev[i] > 0 && now[i] == _cheatPrev[i] - 1) {
				if (_cheatStreak[i] < 255)
					_cheatStreak[i]++;
				if (_cheatStreak[i] >= 2) {
					_world->cheatSetValue(i, _cheatPrev[i]);
					now[i] = _cheatPrev[i];
				}
			} else if (now[i] != _cheatPrev[i]) {
				_cheatStreak[i] = 0;
			}
		}
	} else {
		_cheatStreak.clear();
		_cheatStreak.resize(now.size());
	}
	_cheatPrev = now;
}

bool MacVentureEngine::cheatOnDeath() {
	if (!cheatOn("neargrog_cheat_undo") || _cheatUndo.empty())
		return false;
	_cheatUndoPending = true;
	return true;
}

void MacVentureEngine::cheatUndoIfPending() {
	if (!_cheatUndoPending)
		return;
	_cheatUndoPending = false;
	if (cheatUndoMove())
		g_system->displayMessageOnOSD(Common::U32String("You died - undone (cheat)"));
}

// Puts the game back as it was before the last move (Undo death, and the
// tray's Undo last move)
bool MacVentureEngine::cheatUndoMove() {
	if (_cheatUndo.empty())
		return false;
	Common::Array<byte> state = _cheatUndo.back();
	_cheatUndo.pop_back();
	Common::MemoryReadStream in(state.data(), state.size());
	_world->loadGameFrom(&in);
	reset();
	setInitialFlags(kGameStatePlaying);
	_world->setObjAttr(_world->getObjAttr(1, kAttrParentObject), kAttrContainerOpen, true);
	_halted = false;
	_paused = false;
	_cheatPrev.clear();
	return true;
}

} // End of namespace MacVenture
