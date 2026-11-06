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

DosGamesEngine::DosGamesEngine(OSystem *syst, const DosGameDescription *desc) :
		Engine(syst), _desc(desc), _pc(nullptr), _opl(nullptr), _files(nullptr), _pad(nullptr), _keyboardUp(false) {
}

DosGamesEngine::~DosGamesEngine() {
	if (_opl) {
		_opl->stop();
		delete _opl;
	}
	if (_pad)
		NearGrog::wholeScreen(false);
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

void DosGamesEngine::initSound() {
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
			case TouchPad::kTray: openMainMenuDialog(); break;
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

void DosGamesEngine::runPad() {
	switch (_pad->tick()) {
	case TouchPad::kQuickSave: quickSave(); break;
	case TouchPad::kQuickLoad: quickLoad(); break;
	default: break;
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

void DosGamesEngine::presentPad() {
	if (_pc->paletteChanged())
		_pad->setPalette(_pc->palette());
	_pc->render((byte *)_screen.getPixels(), _screen.pitch);
	const Common::Rect r = _pad->draw((const byte *)_screen.getPixels());
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
enum { kCmdAim = 'aim ', kCmdScummVM = 'smvm', kCmdResume = 'resm' };
}

// Menu: Near-Grog's tray - how the rifle is pointed while hunting
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
			labels.push_back(Common::String::format("Hunting aim: %s", TouchPad::aimName(TouchPad::configAim())));
			cmds.push_back(kCmdAim);
			labels.push_back("ScummVM menu");
			cmds.push_back(kCmdScummVM);
			labels.push_back("Resume");
			cmds.push_back(kCmdResume);
			NearGrog::TrayPage tray("Near-Grog", labels, cmds);
			cmd = tray.runModal();
		}
		if (cmd == (uint32)kCmdAim) {
			TouchPad::setConfigAim((TouchPad::Aim)((TouchPad::configAim() + 1) % 4));
			ConfMan.flushToDisk();
			_pad->refresh();
			presentPad();
			continue;
		}
		if (cmd == (uint32)kCmdScummVM)
			Engine::openMainMenuDialog();
		return;
	}
}

} // End of namespace DosGames
