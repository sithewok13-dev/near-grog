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

#include "common/savefile.h"
#include "common/system.h"
#include "common/translation.h"
#include "graphics/surface.h"
#include "gui/chooser.h"
#include "gui/dialog.h"
#include "gui/message.h"
#include "gui/widget.h"
#include "image/png.h"
#include "engines/grim/bitmap.h"
#include "engines/grim/gfx_base.h"
#include "engines/grim/grim.h"
#include "engines/grim/neargrog_tray.h"

namespace Grim {

int NearGrogTray::_photoFrames = 0;
bool NearGrogTray::_objectNames = false;
bool NearGrogTray::_roomName = false;

namespace {

enum {
	kCmdResume = 'resm',
	kCmdChapters = 'chap',
	kCmdCamera = 'fcam',
	kCmdPhoto = 'phot',
	kCmdCheats = 'chea',
	kCmdScummVM = 'smvm'
};

struct Chapter {
	const char *jump;
	const char *name;
};

// Escape from Monkey Island's developer jump points (_jumpscripts.lua).
// Each one sets up the inventory and story so far.
const Chapter kMonkeyChapters[] = {
	{ "100", "Act 1: Melee Island, the start" },
	{ "113", "Melee: after the drunk falls (Scumm Bar)" },
	{ "115", "Melee: the navigator" },
	{ "120", "Melee: the gubernatorial symbol" },
	{ "130", "Melee: the catapult" },
	{ "132", "Melee: the catapult, part two" },
	{ "135", "Melee: inside the mansion" },
	{ "140", "Melee: a crew and a cushy contract" },
	{ "149", "Melee: ready to sail" },
	{ "150", "Lucre Island: arrival" },
	{ "155", "Lucre: the bank robbery" },
	{ "159", "Lucre: Pegnose's place" },
	{ "160", "Lucre: the duck, grease and sawdust" },
	{ "165", "Lucre: the evidence" },
	{ "190", "Lucre: clearing your name" },
	{ "199", "End of Act 1" },
	{ "200", "Act 2: the start" },
	{ "249", "Act 2: before Jambalaya" },
	{ "250", "Jambalaya Island: arrival" },
	{ "251", "Jambalaya: Marco and the diving" },
	{ "270", "Jambalaya: Planet Threepwood" },
	{ "275", "Jambalaya: the grogoccino" },
	{ "290", "Jambalaya: the monkey mug" },
	{ "300", "Act 3: Monkey Island" },
	{ "350", "Monkey Island: the north side" },
	{ "390", "Monkey Island: late in the act" },
	{ "395", "The endgame" }
};

class TrayDialog : public GUI::Dialog {
public:
	TrayDialog(bool monkey) : GUI::Dialog(0, 0, 0, 0) {
		const int w = MIN<int>(g_system->getOverlayWidth() - 40, 320);
		const int bh = 28, gap = 8;
		const char *const labels[] = { "Resume", "Chapter select", "Free camera", "Take a photo", "Cheats", "ScummVM menu" };
		const uint32 cmds[] = { kCmdResume, kCmdChapters, kCmdCamera, kCmdPhoto, kCmdCheats, kCmdScummVM };
		const int n = ARRAYSIZE(labels);
		const int h = 50 + n * (bh + gap);
		_x = (g_system->getOverlayWidth() - w) / 2;
		_y = MAX<int>(10, (g_system->getOverlayHeight() - h) / 2);
		_w = w;
		_h = h;
		new GUI::StaticTextWidget(this, 10, 12, w - 20, 20,
			Common::U32String(monkey ? "Near-Grog: Escape from Monkey Island" : "Near-Grog: Grim Fandango"), Graphics::kTextAlignCenter);
		for (int i = 0; i < n; i++)
			new GUI::ButtonWidget(this, 20, 42 + i * (bh + gap), w - 40, bh, Common::U32String(labels[i]), Common::U32String(), cmds[i]);
	}

	void handleCommand(GUI::CommandSender *sender, uint32 cmd, uint32 data) override {
		setResult(cmd);
		close();
	}
};

bool isMonkey() {
	return g_grim->getGameType() == GType_MONKEY4;
}

void message(const char *text) {
	GUI::MessageDialog dialog{Common::U32String(text)};
	dialog.runModal();
}

} // End of anonymous namespace

void NearGrogTray::open() {
	uint32 cmd;
	{
		PauseToken pause = g_grim->pauseEngine();
		TrayDialog tray(isMonkey());
		cmd = tray.runModal();
	}
	switch (cmd) {
	case kCmdChapters:
		chapters();
		break;
	case kCmdCamera:
		freeCamera();
		break;
	case kCmdPhoto:
		// Taken from a frame drawn after the tray has gone
		_photoFrames = 2;
		break;
	case kCmdCheats:
		cheats();
		break;
	case kCmdScummVM:
		g_grim->Engine::openMainMenuDialog();
		break;
	default:
		break;
	}
}

void NearGrogTray::chapters() {
	if (!isMonkey()) {
		message("Grim Fandango's chapter list is coming: it needs a look at the game's scripts first.");
		return;
	}
	Common::U32StringArray names;
	for (const Chapter &c : kMonkeyChapters)
		names.push_back(Common::U32String(c.name));
	int picked;
	{
		PauseToken pause = g_grim->pauseEngine();
		GUI::ChooserDialog chooser(Common::U32String("Jump to (best from a new game):"));
		chooser.setList(names);
		picked = chooser.runModal();
	}
	if (picked < 0 || picked >= (int)ARRAYSIZE(kMonkeyChapters))
		return;
	g_grim->debugLua("dofile(\"_jumpscripts.lua\")\n");
	g_grim->debugLua(Common::String::format("start_script(jump_script,\"%s\")", kMonkeyChapters[picked].jump));
}

void NearGrogTray::freeCamera() {
	if (!isMonkey()) {
		message("Grim Fandango's free camera is coming: it needs a look at the game's scripts first.");
		return;
	}
	// The developers' FlyCam (_flycam.lua): arrows turn and move, A/Z move
	// in and out, page up/down orbit, Esc leaves
	g_grim->debugLua("dofile(\"_flycam.lua\")\n");
	g_grim->debugLua("__ng_fn__ = function()\nflycam:init()\nend\nstart_script(__ng_fn__)");
	g_system->displayMessageOnOSD(Common::U32String("Free camera: stick moves it, B leaves"));
}

void NearGrogTray::cheats() {
	if (!isMonkey()) {
		message("Grim Fandango's cheats are coming: they need a look at the game's scripts first.");
		return;
	}
	Common::U32StringArray names;
	names.push_back(Common::U32String(_objectNames ? "Show object names: on" : "Show object names: off"));
	names.push_back(Common::U32String(_roomName ? "Show room name: on" : "Show room name: off"));
	int picked;
	{
		PauseToken pause = g_grim->pauseEngine();
		GUI::ChooserDialog chooser(Common::U32String("Cheats"));
		chooser.setList(names);
		picked = chooser.runModal();
	}
	// The developers' own helpers in _debug.lua
	g_grim->debugLua("dofile(\"_debug.lua\")\n");
	if (picked == 0) {
		_objectNames = !_objectNames;
		g_grim->debugLua(_objectNames ?
			"__ng_fn__ = function()\nsystem.object_names_showing = TRUE\nsystem.currentSet:update_object_names()\nend\nstart_script(__ng_fn__)" :
			"__ng_fn__ = function()\nsystem.object_names_showing = FALSE\nsystem.currentSet:kill_object_names()\nend\nstart_script(__ng_fn__)");
	} else if (picked == 1) {
		_roomName = !_roomName;
		g_grim->debugLua(_roomName ?
			"__ng_fn__ = function()\nsystem.currentSet:display_room_name()\nend\nstart_script(__ng_fn__)" :
			"__ng_fn__ = function()\nsystem.currentSet:kill_room_name()\nend\nstart_script(__ng_fn__)");
	}
}

void NearGrogTray::beforeFlip() {
	if (_photoFrames > 0 && --_photoFrames == 0)
		savePhoto();
}

// Saves the screen as a PNG next to the saved games, where the Files app
// can reach it
void NearGrogTray::savePhoto() {
	Bitmap *shot = g_driver->getScreenshot(g_driver->getScreenWidth(), g_driver->getScreenHeight(), false);
	if (!shot)
		return;
	shot->setActiveImage(0);
	TimeDate t;
	g_system->getTimeAndDate(t);
	Common::String name = Common::String::format("Near-Grog photo %04d-%02d-%02d %02d%02d%02d.png",
		t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
	Common::OutSaveFile *out = g_system->getSavefileManager()->openForSaving(name, false);
	if (out) {
		Image::writePNG(*out, shot->getData());
		out->finalize();
		delete out;
		g_system->displayMessageOnOSD(Common::U32String("Photo saved with your saved games"));
	}
	delete shot;
}

} // End of namespace Grim
