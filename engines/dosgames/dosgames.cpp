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

#include "audio/fmopl.h"
#include "audio/softsynth/pcspk.h"
#include "common/algorithm.h"
#include "common/config-manager.h"
#include "common/events.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/func.h"
#include "common/savefile.h"
#include "common/system.h"
#include "engines/util.h"
#include "graphics/fonts/dosfont.h"
#include "graphics/paletteman.h"
#include "engines/neargrog.h"
#include "engines/neargrog_cheats.h"
#include "dosgames/dosgames.h"

namespace DosGames {

// Near-Grog: ScummVM's own buttons (touch mode, menu) in the corner, for The
// Oregon Trail's keys: off unless the tray asks for them
static bool configOn(const char *key) {
	return ConfMan.hasKey(key) && ConfMan.getBool(key);
}

static bool scummvmButtons() {
	return ConfMan.hasKey("neargrog_scummvm_buttons") && ConfMan.getBool("neargrog_scummvm_buttons");
}

DosGamesEngine::DosGamesEngine(OSystem *syst, const DosGameDescription *desc) :
		Engine(syst), _desc(desc), _pc(nullptr), _opl(nullptr), _pcSpeaker(nullptr), _files(nullptr), _pad(nullptr), _keyboardUp(false), _namesFrames(0), _namesKeyboard(false), _padTicks(0), _instructionsWait(0) {
}

DosGamesEngine::~DosGamesEngine() {
	if (_opl) {
		_opl->stop();
		delete _opl;
	}
	delete _pcSpeaker;
	if (_pad) {
		NearGrog::wholeScreen(false);
		ConfMan.removeKey("neargrog_lock_direct", Common::ConfigManager::kTransientDomain);
		ConfMan.removeKey("neargrog_hide_buttons", Common::ConfigManager::kTransientDomain);
	}
	delete _pad;
	delete _pc;
	delete _files;
	_screen.free();
}

bool DosGamesEngine::hasFeature(EngineFeature f) const {
	return f == kSupportsReturnToLauncher ||
		f == kSupportsLoadingDuringRuntime ||
		f == kSupportsSavingDuringRuntime;
}

// Files the game writes (saved games, settings) live in ScummVM's save
// folder, named after the target
Common::String DosGamesEngine::savedFileName(const Common::String &name) const {
	return _targetName + "-file-" + name;
}

bool DosGamesEngine::readFile(const Common::String &name, Common::Array<byte> &data) {
	Common::ScopedPtr<Common::InSaveFile> saved(_saveFileMan->openForLoading(savedFileName(name)));
	if (saved) {
		data.resize(saved->size());
		saved->read(data.data(), data.size());
		return true;
	}
	return _files->read(name, data);
}

void DosGamesEngine::writeFile(const Common::String &name, const Common::Array<byte> &data) {
	Common::ScopedPtr<Common::OutSaveFile> f(_saveFileMan->openForSaving(savedFileName(name), false));
	if (!f) {
		warning("DosGames: could not save %s", name.c_str());
		return;
	}
	f->write(data.data(), data.size());
	f->finalize();
}

Common::StringArray DosGamesEngine::listFiles() {
	Common::StringArray names = _files->names();
	Common::String prefix = savedFileName("");
	for (const Common::String &s : _saveFileMan->listSavefiles(prefix + "*")) {
		Common::String n = s.substr(prefix.size());
		bool known = false;
		for (const Common::String &o : names)
			known |= o == n;
		if (!known)
			names.push_back(n);
	}
	return names;
}

// The PC speaker (The Oregon Trail's beeps and tunes)
void DosGamesEngine::speaker(bool on, int freq) {
	if (!_pcSpeaker)
		return;
	if (on && freq >= 20 && freq <= 20000)
		_pcSpeaker->play(Audio::PCSpeaker::kWaveFormSquare, freq, -1);
	else
		_pcSpeaker->stop();
}

void DosGamesEngine::initSound() {
	_pcSpeaker = new Audio::PCSpeaker();
	if (!_pcSpeaker->init()) {
		delete _pcSpeaker;
		_pcSpeaker = nullptr;
	}
	_opl = OPL::Config::create();
	if (!_opl || !_opl->init()) {
		warning("DosGames: no AdLib emulator, playing without music");
		delete _opl;
		_opl = nullptr;
		return;
	}
	_opl->start(new Common::Functor0Mem<void, DosGamesEngine>(this, &DosGamesEngine::onOplTimer), 500);
}

void DosGamesEngine::oplWrite(int reg, int value) {
	Common::StackLock lock(_oplMutex);
	_oplWrites.push_back((reg << 8) | value);
}

void DosGamesEngine::onOplTimer() {
	Common::StackLock lock(_oplMutex);
	for (uint16 w : _oplWrites)
		_opl->writeReg(w >> 8, w & 0xFF);
	_oplWrites.clear();
}

void DosGamesEngine::setUpPrograms() {
	if (_desc->gameId == kGameFoolsErrand) {
		// The game reads its graphics driver's name from GAME.CFG; give it
		// the VGA/MCGA one, whatever the copy was installed for
		Common::Array<byte> cfg;
		if (readFile("GAME.CFG", cfg)) {
			for (uint i = 0; i + 8 < cfg.size(); i++) {
				if (!memcmp(&cfg[i], "EGA.DVR", 7) || !memcmp(&cfg[i], "TAN.DVR", 7)) {
					memmove(&cfg[i + 1], &cfg[i], cfg.size() - i - 1);
					memcpy(&cfg[i], "MCGA.DVR", 8);
					break;
				}
			}
			_pc->overrideFile("GAME.CFG", cfg);
		}
		_pc->addProgram("F.EXE", "");
	} else if (_desc->gameId == kGameDondra) {
		// As QM.BAT does: the AdLib driver stays resident, then the game
		_pc->addProgram("SOUND.COM", "B6000");
		_pc->addProgram("QM1.EXE", "/VGA");
	} else if (_desc->gameId == kGameCountdown) {
		// The game asks for its input and sound devices: mouse, AdLib
		_pc->addBootKeys("12");
		_pc->addProgram("COUNT.EXE", "");
	} else if (_desc->gameId == kGameOregonTrail) {
		// MECC's 1990 release: VGA (its README's "OREGON VGA")
		_pc->addProgram("OREGON.EXE", "VGA");
	} else {
		// As the game's own batch file runs it: VGA, mouse and AdLib
		_pc->addProgram("EXE.EXE", "MCGA Mouse AdLib");
	}
}

// PC scan codes for the keys a game needs
static uint8 scanCode(const Common::KeyState &k) {
	static const struct { Common::KeyCode key; uint8 scan; } kMap[] = {
		{ Common::KEYCODE_ESCAPE, 0x01 }, { Common::KEYCODE_KP7, 0x47 }, { Common::KEYCODE_KP8, 0x48 },
		{ Common::KEYCODE_KP9, 0x49 }, { Common::KEYCODE_KP4, 0x4B }, { Common::KEYCODE_KP6, 0x4D },
		{ Common::KEYCODE_KP1, 0x4F }, { Common::KEYCODE_KP2, 0x50 }, { Common::KEYCODE_KP3, 0x51 }, { Common::KEYCODE_BACKSPACE, 0x0E }, { Common::KEYCODE_TAB, 0x0F },
		{ Common::KEYCODE_RETURN, 0x1C }, { Common::KEYCODE_KP_ENTER, 0x1C }, { Common::KEYCODE_SPACE, 0x39 },
		{ Common::KEYCODE_UP, 0x48 }, { Common::KEYCODE_DOWN, 0x50 }, { Common::KEYCODE_LEFT, 0x4B },
		{ Common::KEYCODE_RIGHT, 0x4D }, { Common::KEYCODE_HOME, 0x47 }, { Common::KEYCODE_END, 0x4F },
		{ Common::KEYCODE_PAGEUP, 0x49 }, { Common::KEYCODE_PAGEDOWN, 0x51 }, { Common::KEYCODE_INSERT, 0x52 },
		{ Common::KEYCODE_DELETE, 0x53 }, { Common::KEYCODE_F1, 0x3B }, { Common::KEYCODE_F2, 0x3C },
		{ Common::KEYCODE_F3, 0x3D }, { Common::KEYCODE_F4, 0x3E }, { Common::KEYCODE_F5, 0x3F },
		{ Common::KEYCODE_F6, 0x40 }, { Common::KEYCODE_F7, 0x41 }, { Common::KEYCODE_F8, 0x42 },
		{ Common::KEYCODE_F9, 0x43 }, { Common::KEYCODE_F10, 0x44 }
	};
	for (const auto &m : kMap)
		if (m.key == k.keycode)
			return m.scan;
	static const char kRows[] = "1234567890-=\0\0qwertyuiop[]\0\0asdfghjkl;'`\0\\zxcvbnm,./";
	char c = k.ascii >= 'A' && k.ascii <= 'Z' ? k.ascii + 32 : k.ascii;
	for (uint i = 0; i < sizeof(kRows); i++)
		if (kRows[i] && kRows[i] == c)
			return 0x02 + i;
	return 0;
}

bool DosGamesEngine::handleEvents() {
	Common::Event event;
	while (g_system->getEventManager()->pollEvent(event)) {
		if (_pad) {
			// The touch keys take the touches (the game has no mouse)
			TouchPad::Action a = TouchPad::kNone;
			switch (event.type) {
			case Common::EVENT_LBUTTONDOWN: a = _pad->press(event.mouse); break;
			case Common::EVENT_MOUSEMOVE: _pad->move(event.mouse); break;
			case Common::EVENT_LBUTTONUP: a = _pad->release(event.mouse); break;
			case Common::EVENT_SCREEN_CHANGED: applyLayout(); break;
			default: break;
			}
			switch (a) {
			case TouchPad::kSaveDialog: saveGameDialog(); break;
			case TouchPad::kLoadDialog: loadGameDialog(); break;
			case TouchPad::kGameMenu: Engine::openMainMenuDialog(); break; // (a tap: ScummVM's)
			case TouchPad::kKeyboard:
				// opened, or (pressed again while it's up) closed
				if (g_system->hasFeature(OSystem::kFeatureVirtualKeyboard)) {
					const bool open = !(_keyboardUp && g_system->getFeatureState(OSystem::kFeatureVirtualKeyboard));
					g_system->setFeatureState(OSystem::kFeatureVirtualKeyboard, open);
					_keyboardUp = open;
				}
				break;
			default: break;
			}
			if (event.type != Common::EVENT_KEYDOWN && event.type != Common::EVENT_KEYUP)
				continue;
		}
		switch (event.type) {
		case Common::EVENT_KEYDOWN: {
			uint8 ascii = event.kbd.ascii < 128 ? event.kbd.ascii : 0;
			if (event.kbd.keycode == Common::KEYCODE_RETURN || event.kbd.keycode == Common::KEYCODE_KP_ENTER)
				ascii = 13;
			uint8 scan = scanCode(event.kbd);
			if (scan || ascii)
				_pc->keyDown(scan, ascii);
			break;
		}
		case Common::EVENT_KEYUP: {
			uint8 scan = scanCode(event.kbd);
			if (scan)
				_pc->keyUp(scan);
			break;
		}
		case Common::EVENT_MOUSEMOVE:
			_pc->mouseMove(event.mouse.x, event.mouse.y);
			break;
		case Common::EVENT_LBUTTONDOWN:
		case Common::EVENT_LBUTTONUP:
			_pc->mouseMove(event.mouse.x, event.mouse.y);
			_pc->mouseButton(0, event.type == Common::EVENT_LBUTTONDOWN);
			break;
		case Common::EVENT_RBUTTONDOWN:
		case Common::EVENT_RBUTTONUP:
			_pc->mouseMove(event.mouse.x, event.mouse.y);
			_pc->mouseButton(1, event.type == Common::EVENT_RBUTTONDOWN);
			break;
		default:
			break;
		}
	}
	return !shouldQuit();
}

Common::Error DosGamesEngine::saveGameStream(Common::WriteStream *stream, bool isAutosave) {
	_pc->saveState(*stream);
	return Common::kNoError;
}

Common::Error DosGamesEngine::loadGameStream(Common::SeekableReadStream *stream) {
	if (!_pc->loadState(*stream))
		return Common::kReadingFailed;
	if (_pad)
		_pad->setPalette(_pc->palette());
	return Common::kNoError;
}

Common::Error DosGamesEngine::run() {
	// The game folder, and any zips in it
	_files = new GameFiles(Common::FSNode(ConfMan.getPath("path")));
	if (!_files->has(_desc->desc.filesDescriptions[0].fileName) && !_files->tooBigZip().empty()) {
		GUIErrorMessage(Common::U32String::format("%s is over 50 MB, which is slow to play from. Please unzip it into the game's folder.",
			_files->tooBigZip().c_str()));
		return Common::kNoError;
	}

	if (_desc->gameId == kGameOregonTrail) {
		// Near-Grog: the touch keys (touches as they are: a hold holds)
		ConfMan.setBool("neargrog_raw_touch", true, Common::ConfigManager::kTransientDomain);
		// direct touch only (no pointer), and ScummVM's corner buttons hidden
		// unless the tray says otherwise (the Menu key opens ScummVM's menu)
		ConfMan.setBool("neargrog_lock_direct", true, Common::ConfigManager::kTransientDomain);
		ConfMan.setBool("neargrog_hide_buttons", !scummvmButtons(), Common::ConfigManager::kTransientDomain);
		_pad = new TouchPad();
		applyLayout();
	} else {
		initGraphics(DosPC::kScreenW, DosPC::kScreenH);
	}
	_screen.create(DosPC::kScreenW, DosPC::kScreenH, Graphics::PixelFormat::createFormatCLUT8());

	_pc = new DosPC(this);
	{
		// Text mode characters from ScummVM's DOS font
		byte glyphs[256 * 8];
		Graphics::DosFont font;
		Graphics::Surface glyph;
		glyph.create(8, 8, Graphics::PixelFormat::createFormatCLUT8());
		for (int c = 0; c < 256; c++) {
			glyph.fillRect(Common::Rect(0, 0, 8, 8), 0);
			font.drawChar(&glyph, c, 0, 0, 1);
			for (int row = 0; row < 8; row++) {
				byte bits = 0;
				for (int col = 0; col < 8; col++)
					if (*(const byte *)glyph.getBasePtr(col, row))
						bits |= 0x80 >> col;
				glyphs[c * 8 + row] = bits;
			}
		}
		glyph.free();
		_pc->setFont(glyphs);
	}
	setUpPrograms();
	if (!_pc->start())
		return Common::kNoGameDataFoundError;
	initSound();

	if (ConfMan.hasKey("save_slot"))
		loadGameState(ConfMan.getInt("save_slot"));

	uint32 next = g_system->getMillis();
	while (handleEvents()) {
		if (_pad)
			runPad();
		if (!_pc->runFrame())
			break;
		if (_pad) {
			presentPad();
		} else {
			if (_pc->paletteChanged())
				g_system->getPaletteManager()->setPalette(_pc->palette(), 0, 256);
			_pc->render((byte *)_screen.getPixels(), _screen.pitch);
			g_system->copyRectToScreen(_screen.getPixels(), _screen.pitch, 0, 0, _screen.w, _screen.h);
			g_system->updateScreen();
		}

		next += 20;
		int32 wait = (int32)(next - g_system->getMillis());
		if (wait > 0)
			g_system->delayMillis(wait);
		else if (wait < -200)
			next = g_system->getMillis();
	}
	return Common::kNoError;
}

// --- Near-Grog: The Oregon Trail's touch keys (dosgames/touchpad.h) ---

void DosGamesEngine::applyLayout() {
	bool found = false;
	Common::List<Graphics::PixelFormat> formats = g_system->getSupportedFormats();
	for (Common::List<Graphics::PixelFormat>::const_iterator it = formats.begin(); it != formats.end(); ++it)
		if (it->bytesPerPixel == 4) {
			_format = *it;
			found = true;
			break;
		}
	if (!found)
		_format = Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0);
	const bool portrait = TouchPad::portraitScreen();
	int w, h;
	NearGrog::canvasSize(portrait, w, h);
	NearGrog::wholeScreen(true);
	if (g_system->getWidth() == w && g_system->getHeight() == h && g_system->getScreenFormat() == _format &&
		_pad->output().w == w && _pad->output().h == h)
		return; // (iOS says the screen changed at every touch: nothing to lay out again, or a touch would be lost)
	if (g_system->getWidth() != w || g_system->getHeight() != h || g_system->getScreenFormat() != _format)
		initGraphics(w, h, &_format);
	_pad->setup(w, h, portrait, _format);
	if (_pc)
		_pad->setPalette(_pc->palette());
}

// Hunting: the game hooks the timer (INT 1Ch) - as it does while the wagon
// rolls - and sets its hunting flag, a byte at a fixed place in its data
// (data segment = the timer handler's code segment + 1306h, offset 2DBAh)
bool DosGamesEngine::oregonHunting() const {
	const byte *m = _pc->memory();
	const uint16 seg = READ_LE_UINT16(m + 0x1C * 4 + 2);
	if (seg == 0 || seg >= 0xA000)
		return false;
	const uint32 flag = ((uint32)(seg + 0x1306) << 4) + 0x2DBA;
	return flag < 0xA0000 && m[flag] == 1;
}

// The party's names are asked under the picture of the wagon and family:
// points of it, as the game draws it
bool DosGamesEngine::oregonNamesScreen() const {
	static const struct { int16 x, y; byte r, g, b; } kPoints[] = {
		{ 282, 66, 255, 255, 255 }, { 300, 28, 255, 255, 255 }, { 228, 126, 255, 255, 255 },
		{ 318, 56, 154, 154, 154 }, { 508, 202, 219, 239, 255 }, { 50, 184, 170, 170, 170 },
		{ 516, 192, 219, 239, 255 }, { 534, 160, 0, 109, 182 }, { 280, 110, 255, 235, 223 },
		{ 292, 42, 154, 154, 154 }, { 574, 198, 255, 219, 219 }, { 240, 14, 154, 154, 154 }
	};
	const byte *pal = _pc->palette();
	const byte *scr = (const byte *)_screen.getPixels();
	for (const auto &p : kPoints) {
		const byte *c = pal + scr[p.y * _screen.pitch + p.x] * 3;
		if (ABS(c[0] - p.r) > 8 || ABS(c[1] - p.g) > 8 || ABS(c[2] - p.b) > 8)
			return false;
	}
	return true;
}

// The hunting instructions ("Press SPACE BAR to continue"): points of the
// keypad drawn on it, white keys and the black between
bool DosGamesEngine::oregonInstructionsScreen() const {
	static const struct { int16 x, y; bool white; } kPoints[] = {
		{ 424, 150, true }, { 66, 284, true }, { 62, 266, true }, { 136, 106, true }, { 138, 220, true },
		{ 162, 286, true }, { 154, 248, true }, { 138, 294, true }, { 164, 278, true }, { 466, 70, true },
		{ 80, 288, true }, { 438, 22, true }, { 96, 194, false }, { 102, 200, false }, { 72, 186, false },
		{ 128, 266, false }, { 92, 218, false }, { 94, 230, false }
	};
	const byte *pal = _pc->palette();
	const byte *scr = (const byte *)_screen.getPixels();
	for (const auto &p : kPoints) {
		const byte *c = pal + scr[p.y * _screen.pitch + p.x] * 3;
		const bool white = c[0] > 240 && c[1] > 240 && c[2] > 240, black = c[0] < 16 && c[1] < 16 && c[2] < 16;
		if (p.white ? !white : !black)
			return false;
	}
	return true;
}

void DosGamesEngine::runPad() {
	switch (_pad->tick()) {
	case TouchPad::kQuickSave: quickSave(); break;
	case TouchPad::kQuickLoad: quickLoad(); break;
	case TouchPad::kTray: openMainMenuDialog(); break; // (Menu held)
	default: break;
	}
	// The names: the keyboard comes up for them, and goes once they're
	// settled (the picture gone: "Are these names correct?" answered Yes)
	if (++_padTicks % 5 == 0 && g_system->hasFeature(OSystem::kFeatureVirtualKeyboard)) {
		const bool names = oregonNamesScreen();
		if (names != _namesKeyboard) {
			if (++_namesFrames >= 4) { // (steady a moment: not a screen being drawn)
				_namesFrames = 0;
				_namesKeyboard = names;
				if (names || _keyboardUp)
					g_system->setFeatureState(OSystem::kFeatureVirtualKeyboard, names);
				_keyboardUp = names;
			}
		} else {
			_namesFrames = 0;
		}
	}
	// The hunting instructions skipped, unless the tray shows them
	if (_padTicks % 5 == 0 && _padTicks >= _instructionsWait && !configOn("neargrog_hunt_instructions") && oregonInstructionsScreen()) {
		_pad->typeKey(0x39, ' ');
		_instructionsWait = _padTicks + 50; // (once: it takes a moment to go)
	}
	const bool hunting = oregonHunting();
	_pad->setHunting(hunting);
	if (hunting) {
		// the hunter: his red shirt (nothing else on the field is that red)
		const byte *pal = _pc->palette();
		bool shirt[256];
		for (int i = 0; i < 256; i++)
			shirt[i] = pal[i * 3] >= 240 && pal[i * 3 + 1] >= 16 && pal[i * 3 + 1] <= 48 && pal[i * 3 + 2] >= 16 && pal[i * 3 + 2] <= 48;
		const byte *scr = (const byte *)_screen.getPixels();
		int n = 0, sx = 0, sy = 0;
		for (int y = 0; y < DosPC::kScreenH; y++)
			for (int x = 0; x < DosPC::kScreenW; x++)
				if (shirt[scr[y * _screen.pitch + x]]) {
					n++;
					sx += x;
					sy += y;
				}
		_pad->setHunter(n > 20 ? Common::Point(sx / n, sy / n) : Common::Point(), n > 20);
	}
	TouchPad::Key k;
	while (_pad->takeKey(k)) {
		if (k.down)
			_pc->keyDown(k.scan, k.ascii);
		else
			_pc->keyUp(k.scan);
	}
}

// Near-Grog's standalone app (base/neargrog_standalone.h): its edition's
// name under "Version 2.1" on MECC's title, in the game's own letters - read
// from its font in memory (each letter's eight rows 256 bytes apart), once
// they're found to draw "Version" just as it's on the screen
void DosGamesEngine::drawEdition(byte *scr) {
	const Common::String edition = ConfMan.get("neargrog_edition");
	if (edition.empty())
		return;
	const uint32 kFont = 0x46606;
	const byte *mem = _pc->memory();
	const int pitch = _screen.pitch;
	const byte *pal = _pc->palette();
	// (the title's 320 x 200, doubled)
	auto at = [&](int x, int y) { return scr + 2 * y * pitch + 2 * x; };
	auto dark = [&](int x, int y) { const byte *c = pal + *at(x, y) * 3; return c[0] + c[1] + c[2] < 100; };
	static const char kWord[] = "Version";
	int ink = -1;
	for (int i = 0; kWord[i]; i++)
		for (int r = 0; r < 8; r++) {
			const byte bits = mem[kFont + r * 256 + (byte)kWord[i]];
			for (int k = 0; k < 8; k++) {
				const bool on = bits & (0x80 >> k);
				if (on != dark(116 + i * 8 + k, 41 + r))
					return; // (not the title, or not this font)
				if (on && ink < 0)
					ink = *at(116 + i * 8 + k, 41 + r);
			}
		}
	if (ink < 0)
		return;
	// a line under it, centred as the title's lines are
	const int x0 = 160 - (int)edition.size() * 4, y0 = 52;
	for (uint i = 0; i < edition.size(); i++)
		for (int r = 0; r < 8; r++) {
			const byte bits = mem[kFont + r * 256 + (byte)edition[i]];
			for (int k = 0; k < 8; k++)
				if (bits & (0x80 >> k)) {
					byte *p = at(x0 + i * 8 + k, y0 + r);
					p[0] = p[1] = p[pitch] = p[pitch + 1] = ink;
				}
		}
}

// The standalone's S on the covered wagon's canvas, wherever the game draws
// it (on the trail, over the party's names), in the game's own letter and
// between the seams: the canvas is three or more panels side by side - split
// by black seams with the grey as shading (the big wagon), or by grey seams
// (the small one) - the first, the front with its opening, left out. The S
// goes in one panel, all of it on that panel, as large and as near its
// middle as it fits.
void DosGamesEngine::drawWagonS(byte *scr) {
	if (ConfMan.get("neargrog_edition").empty())
		return;
	const uint32 kFont = 0x46606;
	const byte *mem = _pc->memory();
	byte glyph[8];
	int rows = 0;
	for (int r = 0; r < 8; r++)
		rows += (glyph[r] = mem[kFont + r * 256 + 'S']) != 0;
	if (rows < 5 || glyph[7])
		return; // (no font: not loaded)
	enum { kW = 320, kH = 200, kMaxBlobs = 64 };
	const int pitch = _screen.pitch;
	const byte *pal = _pc->palette();
	auto at = [&](int x, int y) { return scr + 2 * y * pitch + 2 * x; };
	byte kind[256]; // 2 white, 1 light grey, 0 anything else
	int black = -1;
	for (int i = 0; i < 256; i++) {
		const byte r = pal[i * 3], g = pal[i * 3 + 1], b = pal[i * 3 + 2];
		kind[i] = r >= 250 && g >= 250 && b >= 250 ? 2 : (r >= 140 && ABS(r - g) < 12 && ABS(r - b) < 12) ? 1 : 0;
		if (black < 0 && r < 8 && g < 8 && b < 8)
			black = i;
	}
	if (black < 0)
		return;
	static int16 label[kW * kH];
	static int16 stack[kW * kH * 2];
	struct Blob { int n, x0, y0, x1, y1; int64 sx, sy; } blobs[kMaxBlobs];
	for (int pass = 0; pass < 2; pass++) {
		const int minKind = pass == 0 ? 1 : 2; // white and grey, then white only
		memset(label, 0, sizeof(label));
		int count = 0;
		for (int y = 0; y < kH; y++)
			for (int x = 0; x < kW; x++) {
				if (label[y * kW + x] || kind[*at(x, y)] < minKind)
					continue;
				Blob bl = { 0, x, y, x, y, 0, 0 };
				const int id = count < kMaxBlobs ? count + 1 : -1;
				int sp = 0;
				stack[sp++] = x;
				stack[sp++] = y;
				label[y * kW + x] = id;
				while (sp) {
					const int cy = stack[--sp], cx = stack[--sp];
					bl.n++;
					bl.sx += cx;
					bl.sy += cy;
					bl.x0 = MIN(bl.x0, cx); bl.x1 = MAX(bl.x1, cx);
					bl.y0 = MIN(bl.y0, cy); bl.y1 = MAX(bl.y1, cy);
					static const int kDx[4] = { 1, -1, 0, 0 }, kDy[4] = { 0, 0, 1, -1 };
					for (int d = 0; d < 4; d++) {
						const int nx = cx + kDx[d], ny = cy + kDy[d];
						if (nx < 0 || ny < 0 || nx >= kW || ny >= kH || label[ny * kW + nx] || kind[*at(nx, ny)] < minKind)
							continue;
						label[ny * kW + nx] = id;
						stack[sp++] = nx;
						stack[sp++] = ny;
					}
				}
				if (id > 0) {
					blobs[count++] = bl;
					if (bl.n < 60 || bl.n >= 1500)
						blobs[count - 1].n = 0; // (too small or large to be a panel)
				}
			}
		// panels side by side (by their left edges), tops level
		int order[kMaxBlobs], n = 0;
		for (int i = 0; i < count; i++)
			if (blobs[i].n)
				order[n++] = i;
		Common::sort(order, order + n, [&](int a, int b) { return blobs[a].x0 < blobs[b].x0; });
		int run[kMaxBlobs], runLen = 0;
		for (int i = 0; i < n; i++) {
			int cur[kMaxBlobs], len = 0;
			cur[len++] = order[i];
			for (int j = i + 1; j < n; j++) {
				const Blob &first = blobs[cur[0]], &last = blobs[cur[len - 1]], &b = blobs[order[j]];
				if (ABS(b.y0 - first.y0) <= 4 && b.x0 - last.x1 <= 4 && b.x0 >= last.x0)
					cur[len++] = order[j];
			}
			if (len >= 3 && len > runLen) {
				memcpy(run, cur, len * sizeof(int));
				runLen = len;
			}
		}
		if (!runLen)
			continue;
		// the canvas as found: the same as last time, the same place
		const Common::Rect key(blobs[run[0]].x0, blobs[run[0]].y0, blobs[run[runLen - 1]].x1, blobs[run[runLen - 1]].y1 + pass * 1000);
		if (key != _wagonKey) {
			_wagonKey = key;
			_wagonS.clear();
			for (int sc = 2; sc >= 1 && _wagonS.empty(); sc--) {
				int bestD = 1 << 30;
				for (int k = 1; k < runLen; k++) {
					const Blob &p = blobs[run[k]];
					const int id = run[k] + 1;
					const int mx = (int)(p.sx / p.n), my = (int)(p.sy / p.n);
					for (int cy = p.y0; cy <= p.y1; cy++)
						for (int cx = p.x0; cx <= p.x1; cx++) {
							const int d = ABS(cx + 4 * sc - mx) + ABS(cy + 4 * sc - my) - p.n / 100;
							if (d >= bestD)
								continue;
							bool fits = true;
							for (int r = 0; r < 8 && fits; r++)
								for (int b = 0; b < 8 && fits; b++)
									if (glyph[r] & (0x80 >> b))
										for (int dy = 0; dy < sc && fits; dy++)
											for (int dx = 0; dx < sc && fits; dx++) {
												const int x = cx + b * sc + dx, y = cy + r * sc + dy;
												fits = x < kW && y < kH && label[y * kW + x] == id;
											}
							if (!fits)
								continue;
							bestD = d;
							_wagonS.clear();
							for (int r = 0; r < 8; r++)
								for (int b = 0; b < 8; b++)
									if (glyph[r] & (0x80 >> b))
										for (int dy = 0; dy < sc; dy++)
											for (int dx = 0; dx < sc; dx++)
												_wagonS.push_back(Common::Point(cx + b * sc + dx, cy + r * sc + dy));
						}
				}
			}
		}
		for (const Common::Point &c : _wagonS) {
			byte *p = at(c.x, c.y);
			p[0] = p[1] = p[pitch] = p[pitch + 1] = black;
		}
		return;
	}
	_wagonKey = Common::Rect();
	_wagonS.clear();
}

void DosGamesEngine::presentPad() {
	if (_pc->paletteChanged())
		_pad->setPalette(_pc->palette());
	_pc->render((byte *)_screen.getPixels(), _screen.pitch);
	const byte *shown = (const byte *)_screen.getPixels();
	if (!ConfMan.get("neargrog_edition").empty()) {
		// the standalone's touches, on a copy (what's read of the screen -
		// the names, the hunter - stays the game's own)
		_shown.resize(_screen.pitch * _screen.h);
		memcpy(_shown.data(), shown, _shown.size());
		drawEdition(_shown.data());
		drawWagonS(_shown.data());
		shown = _shown.data();
	}
	const Common::Rect r = _pad->draw(shown);
	if (r.isEmpty())
		return;
	const Graphics::ManagedSurface &o = _pad->output();
	g_system->copyRectToScreen(o.getBasePtr(r.left, r.top), o.pitch, r.left, r.top, r.width(), r.height());
	g_system->updateScreen();
}

void DosGamesEngine::quickSave() {
	Common::Error err = saveGameState(99, "Quick save");
	g_system->displayMessageOnOSD(Common::U32String(err.getCode() == Common::kNoError ? "Saved" : "Could not save"));
}

void DosGamesEngine::quickLoad() {
	if (!_saveFileMan->exists(getSaveStateName(99))) {
		g_system->displayMessageOnOSD(Common::U32String("No quick save yet"));
		return;
	}
	Common::Error err = loadGameState(99);
	g_system->displayMessageOnOSD(Common::U32String(err.getCode() == Common::kNoError ? "Loaded" : "Could not load"));
}

namespace {
enum { kCmdSide = 'side', kCmdEnter = 'entr', kCmdClassic = 'clsc', kCmdInstructions = 'inst', kCmdButtons = 'btns', kCmdScummVM = 'smvm', kCmdResume = 'resm' };
}

// Menu held: Near-Grog's tray - the hunting controls, its instructions,
// ScummVM's corner buttons
void DosGamesEngine::openMainMenuDialog() {
	if (!_pad) {
		Engine::openMainMenuDialog();
		return;
	}
	while (true) {
		uint32 cmd;
		{
			PauseToken pause = pauseEngine();
			Common::Array<Common::String> labels;
			Common::Array<uint32> cmds;
			labels.push_back(Common::String::format("Keys: %s", TouchPad::keysLeft() ? "Left" : "Right"));
			cmds.push_back(kCmdSide);
			labels.push_back(TouchPad::enterRight() ? "Keys: Space | Enter" : "Keys: Enter | Space");
			cmds.push_back(kCmdEnter);
			labels.push_back(Common::String::format("Classic hunt controls: %s", configOn("neargrog_hunt_classic") ? "On" : "Off"));
			cmds.push_back(kCmdClassic);
			labels.push_back(Common::String::format("Hunting instructions: %s", configOn("neargrog_hunt_instructions") ? "Shown" : "Skipped"));
			cmds.push_back(kCmdInstructions);
			labels.push_back(Common::String::format("ScummVM buttons: %s", scummvmButtons() ? "On" : "Off"));
			cmds.push_back(kCmdButtons);
			labels.push_back("ScummVM menu");
			cmds.push_back(kCmdScummVM);
			labels.push_back("Resume");
			cmds.push_back(kCmdResume);
			NearGrog::TrayPage tray("Near-Grog", labels, cmds);
			cmd = tray.runModal();
		}
		if (cmd == (uint32)kCmdEnter) {
			ConfMan.setBool("neargrog_enter_left", TouchPad::enterRight());
			ConfMan.flushToDisk();
			_pad->refresh();
			presentPad();
			continue;
		}
		if (cmd == (uint32)kCmdSide) {
			ConfMan.setBool("neargrog_keys_left", !TouchPad::keysLeft());
			ConfMan.flushToDisk();
			_pad->refresh();
			presentPad();
			continue;
		}
		if (cmd == (uint32)kCmdClassic || cmd == (uint32)kCmdInstructions) {
			if (cmd == (uint32)kCmdClassic) {
				// the game's own keys, and with them its instructions screen
				const bool on = !configOn("neargrog_hunt_classic");
				ConfMan.setBool("neargrog_hunt_classic", on);
				ConfMan.setBool("neargrog_hunt_instructions", on);
			} else {
				ConfMan.setBool("neargrog_hunt_instructions", !configOn("neargrog_hunt_instructions"));
			}
			ConfMan.flushToDisk();
			_pad->refresh();
			presentPad();
			continue;
		}
		if (cmd == (uint32)kCmdButtons) {
			ConfMan.setBool("neargrog_scummvm_buttons", !scummvmButtons());
			ConfMan.setBool("neargrog_hide_buttons", !scummvmButtons(), Common::ConfigManager::kTransientDomain);
			ConfMan.flushToDisk();
			continue; // (shown or hidden as the tray closes)
		}
		if (cmd == (uint32)kCmdScummVM)
			Engine::openMainMenuDialog();
		return;
	}
}

} // End of namespace DosGames
