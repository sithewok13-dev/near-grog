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
#include "dosgames/dosgames.h"

namespace DosGames {

DosGamesEngine::DosGamesEngine(OSystem *syst, const DosGameDescription *desc) :
		Engine(syst), _desc(desc), _pc(nullptr), _opl(nullptr) {
}

DosGamesEngine::~DosGamesEngine() {
	if (_opl) {
		_opl->stop();
		delete _opl;
	}
	delete _pc;
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
	Common::FSList files;
	Common::FSNode(ConfMan.getPath("path")).getChildren(files, Common::FSNode::kListFilesOnly);
	for (const Common::FSNode &node : files) {
		if (!node.getFileName().equalsIgnoreCase(name))
			continue;
		Common::ScopedPtr<Common::SeekableReadStream> s(node.createReadStream());
		if (!s)
			return false;
		data.resize(s->size());
		s->read(data.data(), data.size());
		return true;
	}
	return false;
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
	Common::StringArray names;
	Common::FSList files;
	Common::FSNode(ConfMan.getPath("path")).getChildren(files, Common::FSNode::kListFilesOnly);
	for (const Common::FSNode &node : files) {
		Common::String n = node.getFileName();
		n.toUppercase();
		names.push_back(n);
	}
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
	} else {
		// As QM.BAT does: the AdLib driver stays resident, then the game
		_pc->addProgram("SOUND.COM", "B6000");
		_pc->addProgram("QM1.EXE", "/VGA");
	}
}

// PC scan codes for the keys a game needs
static uint8 scanCode(const Common::KeyState &k) {
	static const struct { Common::KeyCode key; uint8 scan; } kMap[] = {
		{ Common::KEYCODE_ESCAPE, 0x01 }, { Common::KEYCODE_BACKSPACE, 0x0E }, { Common::KEYCODE_TAB, 0x0F },
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
	return Common::kNoError;
}

Common::Error DosGamesEngine::run() {
	initGraphics(DosPC::kScreenW, DosPC::kScreenH);
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
		if (!_pc->runFrame())
			break;
		if (_pc->paletteChanged())
			g_system->getPaletteManager()->setPalette(_pc->palette(), 0, 256);
		_pc->render((byte *)_screen.getPixels(), _screen.pitch);
		g_system->copyRectToScreen(_screen.getPixels(), _screen.pitch, 0, 0, _screen.w, _screen.h);
		g_system->updateScreen();

		next += 20;
		int32 wait = (int32)(next - g_system->getMillis());
		if (wait > 0)
			g_system->delayMillis(wait);
		else if (wait < -200)
			next = g_system->getMillis();
	}
	return Common::kNoError;
}

} // End of namespace DosGames
