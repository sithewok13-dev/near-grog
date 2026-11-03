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
#include "macventure/modernui.h"
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
	kCmdScummVM = 'smvm'
};

class TrayDialog : public GUI::Dialog {
public:
	TrayDialog() : GUI::Dialog(0, 0, 0, 0) {
		const Common::String labels[] = {
			Common::String::format("Layout: %s", ModernUI::layoutName(ModernUI::configLayout())),
			Common::String::format("Look: %s", ModernUI::lookName(ModernUI::configLook())),
			Common::String::format("Button opacity: %d%%", ModernUI::configOpacity()),
			"ScummVM menu",
			"Resume"
		};
		const uint32 cmds[] = { kCmdLayout, kCmdLook, kCmdOpacity, kCmdScummVM, kCmdResume };
		const int n = ARRAYSIZE(labels);

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
			Common::U32String("Near-Grog"), Graphics::kTextAlignCenter);
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
		setResult(cmd);
		close();
	}
};

} // End of anonymous namespace

void NearGrogTray::open(MacVentureEngine *vm) {
	while (true) {
		uint32 cmd;
		{
			PauseToken pause = vm->pauseEngine();
			TrayDialog tray;
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
