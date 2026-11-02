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

#include "common/config-manager.h"
#include "common/savefile.h"
#include "common/system.h"
#include "common/translation.h"
#include "graphics/surface.h"
#include "gui/chooser.h"
#include "gui/dialog.h"
#include "gui/gui-manager.h"
#include "gui/widget.h"
#include "image/png.h"
#include "engines/grim/bitmap.h"
#include "engines/grim/gfx_base.h"
#include "engines/grim/grim.h"
#include "engines/grim/neargrog_tray.h"
#include "engines/grim/set.h"
#include "engines/grim/textobject.h"
#include "engines/grim/font.h"
#include "engines/grim/movie/movie.h"
#include "engines/grim/lua/lua.h"

#ifdef IPHONE_IOS7
// The app's build, from its Info.plist (backends/platform/ios7/ios7_misc.mm)
Common::String iOS7_getBuildVersion();
#include "backends/platform/ios7/ios7_neargrog_touch.h"
#else
#include "base/version.h"
#endif

namespace Grim {

int NearGrogTray::_photoFrames = 0;
Common::Array<int> NearGrogTray::_keys;
bool NearGrogTray::_keyDown = false;
uint32 NearGrogTray::_lastTap = 0;
bool NearGrogTray::_objectNames = false;
bool NearGrogTray::_roomName = false;
bool NearGrogTray::_noWalls = false;

namespace {

enum {
	kCmdResume = 'resm',
	kCmdChapters = 'chap',
	kCmdCamera = 'fcam',
	kCmdPhoto = 'phot',
	kCmdCheats = 'chea',
	kCmdScummVM = 'smvm',
	kCmdKeyboard = 'kbrd',
	kCmdControls = 'ctrl'
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

// Grim Fandango's developer jump points (jump_script in _system.lua). The
// developers left none for Year 3.
const Chapter kGrimChapters[] = {
	{ "100", "Year 1: Bruno already reaped" },
	{ "200", "Year 1: Meche already reaped" },
	{ "gb", "Year 1: the packing room" },
	{ "tb", "Year 1: the tube room" },
	{ "tc", "Year 1: the tube room, cards in hand" },
	{ "mm", "Year 1: Manny and Meche" },
	{ "sd", "Year 1: Glottis' shop and Salvador" },
	{ "cr", "Year 1: the ledge, with the coral" },
	{ "pb", "Year 1: the roof and the pigeons" },
	{ "rip", "Petrified Forest: Glottis' heart" },
	{ "forest", "Petrified Forest: the spider web" },
	{ "gh", "Petrified Forest: Glottis gets his heart back" },
	{ "tp", "Petrified Forest: the tree pump" },
	{ "tf", "Year 2: Rubacava, the tattoo parlour" },
	{ "dm", "Year 2: the morgue" },
	{ "shanghai2", "Year 2: Meche shanghaied, the Limbo dock" },
	{ "hrkitchen", "Year 2: the high rollers' kitchen" },
	{ "winecellar", "Year 2: the wine cellar" },
	{ "mf", "Year 4: the flower meadow" },
	{ "ts", "Year 4: the shootout" }
};

class TrayDialog : public GUI::Dialog {
public:
	TrayDialog(bool monkey) : GUI::Dialog(0, 0, 0, 0) {
		const char *const labels[] = { "Resume", "Chapter select", monkey ? "Free camera" : "Camera angles", "Take a photo", "Cheats", "Keyboard",
			NearGrogTray::nearGrogControls() ? "Controls: Near-Grog" : "Controls: ScummVM", "ScummVM menu" };
		const uint32 cmds[] = { kCmdResume, kCmdChapters, kCmdCamera, kCmdPhoto, kCmdCheats, kCmdKeyboard, kCmdControls, kCmdScummVM };
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
			textW = MAX(textW, g_gui.getStringWidth(Common::String(labels[i])));
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
			Common::U32String(monkey ? "Near-Grog: Escape from Monkey Island" : "Near-Grog: Grim Fandango"), Graphics::kTextAlignCenter);
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

bool isMonkey() {
	return g_grim->getGameType() == GType_MONKEY4;
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
	case kCmdKeyboard:
		// The iOS keyboard (MENU closes it again)
		g_system->setFeatureState(OSystem::kFeatureVirtualKeyboard, true);
		break;
	case kCmdControls:
		// Near-Grog's touch controls, or ScummVM's own (its corner icons,
		// gestures and, if switched on, Apple's gamepad); kept for this game.
		// ScummVM's menu button still opens this tray.
		ConfMan.setBool("neargrog_touch", !nearGrogControls());
		ConfMan.flushToDisk();
		g_system->displayMessageOnOSD(Common::U32String(nearGrogControls() ?
			"Near-Grog touch controls" : "ScummVM's touch controls (the menu button opens this tray)"));
		break;
	case kCmdScummVM:
		g_grim->Engine::openMainMenuDialog();
		break;
	default:
		break;
	}
}

void NearGrogTray::chapters() {
	const bool monkey = isMonkey();
	const Chapter *table = monkey ? kMonkeyChapters : kGrimChapters;
	const int count = monkey ? ARRAYSIZE(kMonkeyChapters) : ARRAYSIZE(kGrimChapters);
	Common::U32StringArray names;
	for (int i = 0; i < count; i++)
		names.push_back(Common::U32String(table[i].name));
	int picked;
	{
		PauseToken pause = g_grim->pauseEngine();
		GUI::ChooserDialog chooser(Common::U32String("Jump to (best from a new game):"));
		chooser.setList(names);
		picked = chooser.runModal();
	}
	if (picked < 0 || picked >= count)
		return;
	// Escape from Monkey Island keeps its jump script in a file of its own
	if (monkey)
		g_grim->debugLua("dofile(\"_jumpscripts.lua\")\n");
	g_grim->debugLua(Common::String::format("start_script(jump_script,\"%s\")", table[picked].jump));
}

void NearGrogTray::freeCamera() {
	if (isMonkey()) {
		// The developers' FlyCam (_flycam.lua): arrows turn and move, A/Z
		// move in and out, page up/down orbit, Esc leaves
		g_grim->debugLua("dofile(\"_flycam.lua\")\n");
		g_grim->debugLua("__ng_fn__ = function()\nflycam:init()\nend\nstart_script(__ng_fn__)");
		g_system->displayMessageOnOSD(Common::U32String("Free camera: stick moves it, B leaves"));
		return;
	}

	// Grim Fandango's rooms are painted from fixed cameras: pick any of the
	// room's angles. The game's cameraman, who follows Manny, waits meanwhile.
	Set *set = g_grim->getCurrSet();
	if (!set || set->getNumSetups() < 1)
		return;
	Common::U32StringArray names;
	names.push_back(Common::U32String("Follow Manny (normal)"));
	for (int i = 0; i < set->getNumSetups(); i++)
		names.push_back(Common::U32String(Common::String::format("Angle %d: %s%s", i + 1,
			set->getSetup(i)->_name.c_str(), i == set->getSetup() ? " (now)" : "")));
	int picked;
	{
		PauseToken pause = g_grim->pauseEngine();
		GUI::ChooserDialog chooser(Common::U32String("Camera angle"));
		chooser.setList(names);
		picked = chooser.runModal();
	}
	if (picked < 0)
		return;
	if (picked == 0) {
		g_grim->debugLua("cameraman_disabled = FALSE");
		g_system->displayMessageOnOSD(Common::U32String("The camera follows Manny again when he moves"));
		return;
	}
	g_grim->debugLua("cameraman_disabled = TRUE");
	g_grim->makeCurrentSetup(picked - 1);
}

void NearGrogTray::cheats() {
	Common::U32StringArray names;
	names.push_back(Common::U32String(_objectNames ? "Show object names: on" : "Show object names: off"));
	names.push_back(Common::U32String(_roomName ? "Show room name: on" : "Show room name: off"));
	names.push_back(Common::U32String(_noWalls ? "Walk through walls: on" : "Walk through walls: off"));
	int picked;
	{
		PauseToken pause = g_grim->pauseEngine();
		GUI::ChooserDialog chooser(Common::U32String("Cheats"));
		chooser.setList(names);
		picked = chooser.runModal();
	}
	const char *code = nullptr;
	// The developers' own helpers (_debug.lua in Monkey Island; built into
	// Grim Fandango's _sets.lua and _controls.lua)
	if (isMonkey())
		g_grim->debugLua("dofile(\"_debug.lua\")\n");
	if (picked == 0) {
		_objectNames = !_objectNames;
		code = _objectNames ?
			"system.object_names_showing = TRUE\nsystem.currentSet:update_object_names()\n"
			"if system.currentSet.make_objects_visible then system.currentSet:make_objects_visible() end" :
			"system.object_names_showing = FALSE\nsystem.currentSet:kill_object_names()\n"
			"if system.currentSet.make_objects_visible then system.currentSet:make_objects_visible() end";
	} else if (picked == 1) {
		_roomName = !_roomName;
		code = _roomName ? "system.currentSet:display_room_name()" : "system.currentSet:kill_room_name()";
	} else if (picked == 2) {
		_noWalls = !_noWalls;
		code = _noWalls ?
			"if system.currentActor.ignore_boxes then system.currentActor:ignore_boxes() end" :
			"if system.currentActor.follow_boxes then system.currentActor:follow_boxes() end";
	}
	if (code)
		g_grim->debugLua(Common::String::format("__ng_fn__ = function()\n%s\nend\nstart_script(__ng_fn__)", code));
}

void NearGrogTray::beforeFlip() {
	pressKeys();
	if (_photoFrames > 0 && --_photoFrames == 0)
		savePhoto();
}

void NearGrogTray::pressKeys() {
	if (_keys.empty())
		return;
	Common::KeyState k((Common::KeyCode)_keys[0]);
	if (!_keyDown) {
		g_grim->handleControls(Common::EVENT_KEYDOWN, k);
		_keyDown = true;
	} else {
		g_grim->handleControls(Common::EVENT_KEYUP, k);
		_keyDown = false;
		_keys.remove_at(0);
	}
}

namespace {

// A number from the game's scripts (0 if it isn't one)
float luaNumber(const char *name) {
	lua_beginblock();
	lua_Object o = lua_rawgetglobal(name);
	float v = lua_isnumber(o) ? lua_getnumber(o) : 0;
	lua_endblock();
	return v;
}

bool inCutscene() {
	return luaNumber("cutSceneLevel") > 0;
}

} // End of anonymous namespace

// Near-Grog's touch map (for now, while menu taps are being worked out):
// every tap is described on screen and in neargrog-touch-log.txt, beside the
// saved games, with all the text the game was showing.
static Common::String _touchLog;

static void touchLog(const Common::String &entry, const Common::String &summary) {
	_touchLog += entry;
	if (_touchLog.size() > 60000)
		_touchLog = _touchLog.substr(_touchLog.size() - 50000);
	Common::OutSaveFile *out = g_system->getSavefileManager()->openForSaving("neargrog-touch-log.txt", false);
	if (out) {
		out->writeString(_touchLog);
		out->finalize();
		delete out;
	}
	g_system->displayMessageOnOSD(Common::U32String(summary));
}

static Common::String colorName(const Color &c) {
	return Common::String::format("#%02x%02x%02x", c.getRed(), c.getGreen(), c.getBlue());
}

bool NearGrogTray::tap(int x, int y) {
	if (!_keys.empty())
		return false;
	Common::String log = Common::String::format("Tap at %d,%d (screen %dx%d, mode %d, cutscene %d)\n",
		x, y, g_driver->getScreenWidth(), g_driver->getScreenHeight(), (int)g_grim->getMode(), inCutscene() ? 1 : 0);

	// The text on screen in rows: a list, a menu, the nearby objects. The
	// row in a colour none of the others has is the highlighted one.
	struct Row {
		int top, bottom, left, right, width;
		Color color; // the colour of its widest text
		Common::String text; // its widest text
	};
	Common::Array<Row> rows;
	const float sx = g_driver->getScreenWidth(), sy = g_driver->getScreenHeight();
	for (TextObject *t : TextObject::getPool()) {
		if (t->isSpeech() || t->getNumLines() < 1 || !t->getFont())
			continue;
		// Text is placed in 640x480 (coords 0) or 1920x1080 (coords 1 and 2)
		const float scaleX = sx / (t->getCoords() == 0 ? 640.f : 1920.f);
		const float scaleY = sy / (t->getCoords() == 0 ? 480.f : 1080.f);
		log += Common::String::format("  text \"%s\" at %d,%d coords %d justify %d colour %s lines %d\n",
			t->getLines()[0].c_str(), t->getX(), t->getY(), t->getCoords(), t->getJustify(), colorName(t->getFGColor()).c_str(), t->getNumLines());
		for (int i = 0; i < t->getNumLines(); i++) {
			Row r;
			r.text = t->getLines()[i];
			r.top = (int)(t->getLineY(i) * scaleY);
			r.bottom = (int)((t->getLineY(i) + t->getFont()->getKernedHeight()) * scaleY);
			r.left = (int)(t->getLineX(i) * scaleX);
			r.width = (int)(t->getFont()->getKernedStringLength(t->getLines()[i]) * scaleX);
			r.right = r.left + r.width;
			r.color = t->getFGColor();
			if (r.width <= 0)
				continue;
			// Join text on the same row (a coloured first letter, say)
			bool joined = false;
			for (Row &o : rows) {
				const int mid = (r.top + r.bottom) / 2;
				if (mid >= o.top && mid < o.bottom) {
					o.left = MIN(o.left, r.left);
					o.right = MAX(o.right, r.right);
					if (r.width > o.width) {
						o.width = r.width;
						o.color = r.color;
						o.text = r.text;
					}
					joined = true;
					break;
				}
			}
			if (!joined)
				rows.push_back(r);
		}
	}
	Common::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) { return a.top < b.top; });
	for (uint i = 0; i < rows.size(); i++)
		log += Common::String::format("  row %u: \"%s\" x %d-%d y %d-%d colour %s\n", i, rows[i].text.c_str(),
			rows[i].left, rows[i].right, rows[i].top, rows[i].bottom, colorName(rows[i].color).c_str());
	if (rows.size() < 2) {
		touchLog(log + "  -> not a list (fewer than 2 rows)\n\n", Common::String::format("Tap %d,%d: no list here", x, y));
		return false;
	}

	int tapped = -1;
	for (uint i = 0; i < rows.size(); i++) {
		const int pad = (rows[i].bottom - rows[i].top) / 4;
		if (y >= rows[i].top - pad && y < rows[i].bottom + pad && x >= rows[i].left - 40 && x < rows[i].right + 40)
			tapped = i;
	}
	if (tapped < 0) {
		touchLog(log + "  -> missed every row\n\n", Common::String::format("Tap %d,%d: missed the %u rows", x, y, rows.size()));
		return false;
	}

	int highlighted = -1;
	for (uint i = 0; i < rows.size(); i++) {
		int same = 0;
		for (uint j = 0; j < rows.size(); j++)
			if (i != j && rows[j].color.getRed() == rows[i].color.getRed() &&
				rows[j].color.getGreen() == rows[i].color.getGreen() && rows[j].color.getBlue() == rows[i].color.getBlue())
				same++;
		if (same == 0) {
			if (highlighted >= 0) {
				touchLog(log + "  -> more than one row in a colour of its own\n\n",
					Common::String::format("Tap on \"%s\": can't tell which is highlighted", rows[tapped].text.c_str()));
				return false; // not a list we understand
			}
			highlighted = i;
		}
	}
	if (highlighted < 0) {
		touchLog(log + "  -> no row in a colour of its own\n\n",
			Common::String::format("Tap on \"%s\": no highlighted row", rows[tapped].text.c_str()));
		return false;
	}

	// Monkey Island's lists at the bottom of the screen (nearby objects,
	// dialogue) move with Page Up / Page Down and take Enter for dialogue: a
	// tap highlights, a tap on the highlighted line picks. The games' menus
	// (in the middle of the screen) move with the arrow keys: a tap picks.
	const bool bottomList = rows[0].top > sy * 0.6f;
	const Common::KeyCode down = bottomList ? Common::KEYCODE_PAGEDOWN : Common::KEYCODE_DOWN;
	const Common::KeyCode up = bottomList ? Common::KEYCODE_PAGEUP : Common::KEYCODE_UP;
	for (int i = highlighted; i < tapped; i++)
		_keys.push_back(down);
	for (int i = highlighted; i > tapped; i--)
		_keys.push_back(up);
	if (!bottomList || tapped == highlighted)
		_keys.push_back(Common::KEYCODE_RETURN);
	_keyDown = false;
	touchLog(log + Common::String::format("  -> tapped row %d, highlighted row %d, %s, %d keys\n\n", tapped, highlighted,
		bottomList ? "bottom list" : "menu", _keys.size()),
		Common::String::format("\"%s\" (highlighted: \"%s\")", rows[tapped].text.c_str(), rows[highlighted].text.c_str()));
	return true;
}

bool NearGrogTray::skip(bool movie) {
	if (!_keys.empty())
		return false;
	if (movie && isMonkey()) {
		// Monkey Island's movies only listen for Escape, outside the controls
		g_movie->stop();
		return true;
	}
	// A tap skips the line being spoken ('.'). In a movie, or with a second
	// tap straight after the first, Escape: the game's own skip, for a
	// movie, a cutscene or an intro.
	const uint32 now = g_system->getMillis();
	const bool doubleTap = now - _lastTap < 400;
	_lastTap = now;
	_keys.push_back(movie || doubleTap ? Common::KEYCODE_ESCAPE : Common::KEYCODE_PERIOD);
	_keyDown = false;
	return true;
}

bool NearGrogTray::nearGrogControls() {
	return !ConfMan.hasKey("neargrog_touch") || ConfMan.getBool("neargrog_touch");
}

void NearGrogTray::reportTouchMode(bool playing) {
	if (!nearGrogControls())
		playing = false;
	// With Near-Grog's stick, walk the way the stick points on screen: the
	// games' own camera-relative mode (MarioControl), switched on once
	// through their own option, which keeps it in their preferences
	static bool cameraRelativeDone = false;
	if (playing && !cameraRelativeDone && !inCutscene()) {
		cameraRelativeDone = true;
		g_grim->debugLua("if not MarioControl and system_prefs and system_prefs.toggle_movement_mode then system_prefs:toggle_movement_mode() end");
	}
	// Not over the game's cutscenes either (they keep a count in the scripts)
	if (playing && inCutscene())
		playing = false;
#ifdef IPHONE_IOS7
	iOS7_setNearGrogTouchMode(!playing ? kNearGrogTouchHidden : (isMonkey() ? kNearGrogTouchMonkey : kNearGrogTouchGrim));
#endif
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
