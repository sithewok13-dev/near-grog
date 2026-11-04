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

// Near-Grog's tray for the MacVenture games (after engines/grim/neargrog_tray.cpp)

#include "common/system.h"
#include "gui/dialog.h"
#include "gui/gui-manager.h"
#include "gui/widget.h"

#include "macventure/macventure.h"
#include "common/config-manager.h"
#include "macventure/gui.h"
#include "macventure/modernui.h"
#include "macventure/neargrog_hints.h"
#include "gui/message.h"
#include "macventure/neargrog_tray.h"

#ifdef IPHONE_IOS7
// The app's build, from its Info.plist (backends/platform/ios7/ios7_misc.mm)
Common::String iOS7_getBuildVersion();
#else
#include "base/version.h"
#endif

namespace MacVenture {

namespace {

enum {
	kCmdResume = 'resm',
	kCmdLayout = 'layo',
	kCmdLook = 'look',
	kCmdOpacity = 'opac',
	kCmdScummVM = 'smvm',
	kCmdCheats = 'chea',
	kCmdCheatAll = 'chal',
	kCmdCheatFirst = 0x100, // one per cheat, in kCheatKeys' order
	kCmdUndoMove = 'undo',
	kCmdHint = 'hint',
	kCmdBack = 'back'
};

class TrayDialog : public GUI::Dialog {
public:
	// A page of big buttons: the tray itself, or its Cheats page
	TrayDialog(const char *title, const Common::String *labels, const uint32 *cmds, int n) : GUI::Dialog(0, 0, 0, 0),
			_opened(g_system->getMillis()) {

		// Sized from the screen and the theme's font, so the buttons are big
		// enough for a finger on a phone. Two columns when one won't fit.
		const int sw = g_system->getOverlayWidth(), sh = g_system->getOverlayHeight();
		const int fh = g_gui.getFontHeight();
		const int small = g_gui.getFontHeight(GUI::ThemeEngine::kFontStyleTooltip);
		const int bh = MAX(fh * 2, MIN(sw, sh) / 8);
		const int gap = bh / 4;
		const int margin = fh;
		int textW = 0;
		for (int i = 0; i < n; i++)
			textW = MAX(textW, g_gui.getStringWidth(labels[i]));
		int bw = MAX(textW + fh * 3, MIN(sw, sh) / 2);
		const int titleH = fh * 2;
		const int versionH = small * 2;
		int cols = 1;
		if (margin * 2 + titleH + n * (bh + gap) + versionH > sh - 20)
			cols = 2;
		const int rows = (n + cols - 1) / cols;
		bw = MIN(bw, (sw - 40 - margin * 2 - gap * (cols - 1)) / cols);
		_w = margin * 2 + cols * bw + (cols - 1) * gap;
		_h = margin * 2 + titleH + rows * (bh + gap) + versionH;
		_x = (sw - _w) / 2;
		_y = MAX<int>(10, (sh - _h) / 2);

		new GUI::StaticTextWidget(this, margin, margin, _w - margin * 2, fh + 4,
			Common::U32String(title), Graphics::kTextAlignCenter);
		for (int i = 0; i < n; i++) {
			const int c = i % cols, r = i / cols;
			new GUI::ButtonWidget(this, margin + c * (bw + gap), margin + titleH + r * (bh + gap), bw, bh,
				Common::U32String(labels[i]), Common::U32String(), cmds[i]);
		}
		// The house style's version readout: which build this is
#ifdef IPHONE_IOS7
		Common::String version = iOS7_getBuildVersion();
#else
		Common::String version = Common::String::format("v%s", gScummVMVersion);
#endif
		GUI::StaticTextWidget *label = new GUI::StaticTextWidget(this, margin, margin + titleH + rows * (bh + gap), _w - margin * 2, small + 4,
			Common::U32String(version), Graphics::kTextAlignCenter, Common::U32String(), GUI::ThemeEngine::kFontStyleTooltip);
		label->setEnabled(false);
	}

	void handleCommand(GUI::CommandSender *sender, uint32 cmd, uint32 data) override {
		// The tray and its Cheats page reopen as soon as a switch is
		// flipped: a tap arriving as the page appears is the last one's
		if (g_system->getMillis() - _opened < 350)
			return;
		setResult(cmd);
		close();
	}

private:
	uint32 _opened;
};

} // End of anonymous namespace

// The Hints cheat: a nudge for the room you are in
void NearGrogTray::showHint(MacVentureEngine *vm) {
	const Common::String room = vm->getGui()->findWindowData(kMainGameWindow).title;
	const char *hint = findHint(ConfMan.get("gameid"), room);
	Common::String text = hint ? Common::String::format("%s\n\n%s", room.c_str(), hint)
		: Common::String::format("No hint for %s yet.", room.c_str());
	PauseToken pause = vm->pauseEngine();
	GUI::MessageDialog dialog{Common::U32String(text)};
	dialog.runModal();
}

// The Cheats page: each switch flips and the page comes back showing it
// (Hints, neargrog_cheat_hints, joins the list once rooms have hints written)
static const char *const kCheatKeys[] = { "neargrog_cheat_moves", "neargrog_cheat_undo", "neargrog_cheat_text",
	"neargrog_cheat_hotspots", "neargrog_cheat_undomove" };
static const char *const kCheatNames[] = { "Unlimited moves", "Undo death", "Instant text",
	"Show hotspots", "Undo any move" };
static const int kCheatCount = ARRAYSIZE(kCheatKeys);

static void openCheats(MacVentureEngine *vm) {
	while (true) {
		uint32 cmd;
		bool all = true;
		for (int i = 0; i < kCheatCount; i++)
			all = all && MacVentureEngine::cheatOn(kCheatKeys[i]);
		{
			PauseToken pause = vm->pauseEngine();
			Common::String labels[kCheatCount + 2];
			uint32 cmds[kCheatCount + 2];
			labels[0] = all ? "All off" : "All on";
			cmds[0] = kCmdCheatAll;
			for (int i = 0; i < kCheatCount; i++) {
				labels[i + 1] = Common::String::format("%s: %s", kCheatNames[i], MacVentureEngine::cheatOn(kCheatKeys[i]) ? "On" : "Off");
				cmds[i + 1] = kCmdCheatFirst + i;
			}
			labels[kCheatCount + 1] = "Back";
			cmds[kCheatCount + 1] = kCmdBack;
			TrayDialog page("Cheats", labels, cmds, kCheatCount + 2);
			cmd = page.runModal();
		}
		if (cmd == kCmdCheatAll) {
			for (int i = 0; i < kCheatCount; i++)
				ConfMan.setBool(kCheatKeys[i], !all);
		} else if (cmd >= (uint32)kCmdCheatFirst && cmd < (uint32)(kCmdCheatFirst + kCheatCount)) {
			const char *key = kCheatKeys[cmd - kCmdCheatFirst];
			ConfMan.setBool(key, !MacVentureEngine::cheatOn(key));
		} else {
			return;
		}
		ConfMan.flushToDisk();
		// Shown at once (the hotspots come and go with the switch)
		vm->getGui()->markRedraw();
		vm->getGui()->draw();
		g_system->updateScreen();
	}
}

void NearGrogTray::open(MacVentureEngine *vm) {
	while (true) {
		uint32 cmd;
		{
			PauseToken pause = vm->pauseEngine();
			Common::Array<Common::String> labels;
			Common::Array<uint32> cmds;
			// The cheats' own buttons first, while they are on
			if (MacVentureEngine::cheatOn("neargrog_cheat_undomove") && vm->cheatCanUndo()) {
				labels.push_back("Undo last move");
				cmds.push_back(kCmdUndoMove);
			}
			if (MacVentureEngine::cheatOn("neargrog_cheat_hints")) {
				labels.push_back("Hint for this room");
				cmds.push_back(kCmdHint);
			}
			labels.push_back(Common::String::format("Layout: %s", ModernUI::layoutName(ModernUI::configLayout())));
			cmds.push_back(kCmdLayout);
			labels.push_back(Common::String::format("Look: %s", ModernUI::lookName(ModernUI::configLook())));
			cmds.push_back(kCmdLook);
			labels.push_back(Common::String::format("Button opacity: %d%%", ModernUI::configOpacity()));
			cmds.push_back(kCmdOpacity);
			labels.push_back("Cheats");
			cmds.push_back(kCmdCheats);
			labels.push_back("ScummVM menu");
			cmds.push_back(kCmdScummVM);
			labels.push_back("Resume");
			cmds.push_back(kCmdResume);
			TrayDialog tray("Near-Grog", labels.data(), cmds.data(), labels.size());
			cmd = tray.runModal();
		}

		ModernLayout layout = ModernUI::configLayout();
		ModernLook look = ModernUI::configLook();
		int opacity = ModernUI::configOpacity();
		switch (cmd) {
		case kCmdLayout:
			{
				// C2, E1, E2, Original, round again
				static const ModernLayout kOrder[] = { kLayoutC2, kLayoutE, kLayoutE2, kLayoutOriginal };
				int k = 0;
				while (k < 4 && kOrder[k] != layout)
					k++;
				layout = kOrder[(k + 1) % 4];
				// (the Windows release has no Original: the engine's own
				// screen is the Mac one; its Windows 3.1 look is ours)
				if (layout == kLayoutOriginal && vm->isWindowsVersion())
					layout = kOrder[0];
			}
			break;
		case kCmdLook:
			look = look == kLookWin31 ? kLookDark : kLookWin31;
			break;
		case kCmdOpacity:
			opacity = opacity <= 25 ? 100 : opacity - 25;
			break;
		case kCmdScummVM:
			vm->Engine::openMainMenuDialog();
			return;
		case kCmdCheats:
			openCheats(vm);
			continue; // back to the tray
		case kCmdUndoMove:
			if (vm->cheatUndoMove())
				g_system->displayMessageOnOSD(Common::U32String("Move undone"));
			return;
		case kCmdHint:
			showHint(vm);
			return;
		default:
			return;
		}
		// Each change shows at once; the tray comes back over the new layout
		ModernUI::setConfig(layout, look, opacity);
		vm->applyLayout();
		vm->getGui()->draw();
		g_system->updateScreen();
	}
}

} // End of namespace MacVenture
