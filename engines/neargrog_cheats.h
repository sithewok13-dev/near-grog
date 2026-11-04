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

#ifndef ENGINES_NEARGROG_CHEATS_H
#define ENGINES_NEARGROG_CHEATS_H

#include "common/array.h"
#include "common/config-manager.h"
#include "common/system.h"
#include "gui/dialog.h"
#include "gui/gui-manager.h"
#include "gui/widget.h"

/**
 * Near-Grog's cheats for the emulated games (the NES MacVentures, C64
 * Uninvited), as MacVenture has them - the same switches, kept per game:
 * Unlimited moves (any counter that goes down by one each move is held),
 * Undo death (the game as it was before the fatal move), Instant text
 * (pages turn by themselves) and Undo any move (the tray's "Undo last
 * move"). The machines' own save states do the undoing.
 */
namespace NearGrog {

enum { kCheatCount = 4 };
static const char *const kCheatKeys[kCheatCount] = {
	"neargrog_cheat_moves", "neargrog_cheat_undo", "neargrog_cheat_text", "neargrog_cheat_undomove"
};
static const char *const kCheatNames[kCheatCount] = {
	"Unlimited moves", "Undo death", "Instant text", "Undo any move"
};

inline bool cheatOn(const char *key) {
	return ConfMan.hasKey(key) && ConfMan.getBool(key);
}
inline bool anyCheatOn() {
	for (int i = 0; i < kCheatCount; i++)
		if (cheatOn(kCheatKeys[i]))
			return true;
	return false;
}

/** A page of the tray: a title and big buttons (a tap as it opens is ignored). */
class TrayPage : public GUI::Dialog {
public:
	TrayPage(const Common::String &title, const Common::Array<Common::String> &labels, const Common::Array<uint32> &cmds) :
			GUI::Dialog(0, 0, 0, 0), _opened(g_system->getMillis()) {
		const int n = labels.size();
		const int sw = g_system->getOverlayWidth(), sh = g_system->getOverlayHeight();
		const int fh = g_gui.getFontHeight();
		const int bh = MAX(fh * 2, MIN(sw, sh) / 9), gap = bh / 5, margin = fh;
		const int bw = MIN(MAX(MIN(sw, sh) / 2, fh * 12), sw - 40);
		const int cols = (sh < sw && n > 5) ? 2 : 1; // landscape: two columns
		const int rows = (n + cols - 1) / cols;
		_w = margin * (cols + 1) + bw * cols;
		_h = margin * 2 + fh * 2 + rows * (bh + gap);
		_x = (sw - _w) / 2;
		_y = MAX<int>(10, (sh - _h) / 2);
		new GUI::StaticTextWidget(this, margin, margin, _w - 2 * margin, fh + 4, Common::U32String(title), Graphics::kTextAlignCenter);
		for (int i = 0; i < n; i++) {
			const int c = i / rows, r = i % rows;
			new GUI::ButtonWidget(this, margin + c * (bw + margin), margin + fh * 2 + r * (bh + gap), bw, bh, Common::U32String(labels[i]), Common::U32String(), cmds[i]);
		}
	}
	void handleCommand(GUI::CommandSender *sender, uint32 cmd, uint32 data) override {
		if (g_system->getMillis() - _opened < 350) // a tap from the page before
			return;
		setResult(cmd);
		close();
	}

private:
	uint32 _opened;
};

/** The Cheats page: All on / off, each switch, Back. Returns once Back is chosen. */
inline void cheatsPage() {
	enum { kCmdAll = 'call', kCmdBack = 'cbak', kCmdFirst = 'ch00' };
	while (true) {
		bool all = true;
		for (int i = 0; i < kCheatCount; i++)
			all &= cheatOn(kCheatKeys[i]);
		Common::Array<Common::String> labels;
		Common::Array<uint32> cmds;
		labels.push_back(all ? "All off" : "All on");
		cmds.push_back(kCmdAll);
		for (int i = 0; i < kCheatCount; i++) {
			labels.push_back(Common::String::format("%s: %s", kCheatNames[i], cheatOn(kCheatKeys[i]) ? "On" : "Off"));
			cmds.push_back(kCmdFirst + i);
		}
		labels.push_back("Back");
		cmds.push_back(kCmdBack);
		uint32 cmd;
		{
			TrayPage page("Cheats", labels, cmds);
			cmd = page.runModal();
		}
		if (cmd == (uint32)kCmdAll) {
			for (int i = 0; i < kCheatCount; i++)
				ConfMan.setBool(kCheatKeys[i], !all);
		} else if (cmd >= (uint32)kCmdFirst && cmd < (uint32)(kCmdFirst + kCheatCount)) {
			const char *key = kCheatKeys[cmd - kCmdFirst];
			ConfMan.setBool(key, !cheatOn(key));
		} else {
			return;
		}
		ConfMan.flushToDisk();
	}
}

/** A save state from before a move, and how much text had been read then. */
struct Snapshot {
	Common::Array<byte> state;
	uint text;
};

/** The last moves' snapshots (for Undo any move and Undo death). */
class UndoStack {
public:
	void push(const Common::Array<byte> &state, uint text) {
		Snapshot s;
		s.state = state;
		s.text = text;
		_s.push_back(s);
		while (_s.size() > 20)
			_s.remove_at(0);
	}
	bool empty() const { return _s.empty(); }
	Snapshot pop() {
		Snapshot s = _s.back();
		_s.pop_back();
		return s;
	}
	void clear() { _s.clear(); }

private:
	Common::Array<Snapshot> _s;
};

/**
 * Unlimited moves: the machine's memory at each touch on the game. A byte
 * whose last three changes were each one step the same way (up or down),
 * all within five touches, is a move counter (a torch burning, a timer);
 * from then on each such step is put back. Anything else that changes it
 * lets it go (an item count, which moves now and then, never qualifies).
 */
class Countdowns {
public:
	/** At a touch: the memory now; out, the bytes to put back (index, value). */
	void move(const Common::Array<byte> &mem, Common::Array<uint32> &where, Common::Array<byte> &value) {
		_n++;
		if (_prev.size() != mem.size()) {
			_prev = mem;
			_dir.resize(mem.size());
			_steps.resize(mem.size() * 3);
			for (uint i = 0; i < mem.size(); i++)
				_dir[i] = 0;
			for (uint i = 0; i < _steps.size(); i++)
				_steps[i] = 0;
			return;
		}
		for (uint i = 0; i < mem.size(); i++) {
			if (mem[i] == _prev[i])
				continue;
			const int d = (int8)(mem[i] - _prev[i]);
			uint32 *st = &_steps[i * 3];
			if ((d == 1 || d == -1) && (_dir[i] == 0 || _dir[i] == d)) {
				_dir[i] = d;
				st[0] = st[1];
				st[1] = st[2];
				st[2] = _n;
			} else {
				_dir[i] = (d == 1 || d == -1) ? d : 0;
				st[0] = st[1] = 0;
				st[2] = (d == 1 || d == -1) ? _n : 0;
			}
			if (st[0] && _n - st[0] <= 5) {
				// a counter: put back
				where.push_back(i);
				value.push_back(_prev[i]);
				continue; // (_prev keeps the held value)
			}
			_prev[i] = mem[i];
		}
	}
	void reset() {
		_prev.clear();
	}

private:
	Common::Array<byte> _prev;
	Common::Array<int8> _dir;
	Common::Array<uint32> _steps;
	uint32 _n = 0;
};

} // End of namespace NearGrog

#endif
