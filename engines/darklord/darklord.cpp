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

#include "audio/sid.h"
#include "common/config-manager.h"
#include "common/events.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/func.h"
#include "common/memstream.h"
#include "common/savefile.h"
#include "common/system.h"
#include "engines/util.h"
#include "graphics/fonts/dosfont.h"
#include "graphics/paletteman.h"
#include "darklord/darklord.h"
#include "darklord/disks.h"

namespace DarkLord {

// The usual "Pepto" C64 palette
static const byte kPalette[16 * 3] = {
	0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x68, 0x37, 0x2B, 0x70, 0xA4, 0xB2,
	0x6F, 0x3D, 0x86, 0x58, 0x8D, 0x43, 0x35, 0x28, 0x79, 0xB8, 0xC7, 0x6F,
	0x6F, 0x4F, 0x25, 0x43, 0x39, 0x00, 0x9A, 0x67, 0x59, 0x44, 0x44, 0x44,
	0x6C, 0x6C, 0x6C, 0x9A, 0xD2, 0x84, 0x6C, 0x5E, 0xB5, 0x95, 0x95, 0x95
};

DarkLordEngine::DarkLordEngine(OSystem *syst, const ADGameDescription *desc) :
		Engine(syst), _desc(desc), _machine(nullptr), _sid(nullptr) {
}

DarkLordEngine::~DarkLordEngine() {
	if (_sid) {
		_sid->stop();
		delete _sid;
	}
	delete _machine;
	_screen.free();
}

bool DarkLordEngine::hasFeature(EngineFeature f) const {
	return f == kSupportsReturnToLauncher ||
		f == kSupportsLoadingDuringRuntime ||
		f == kSupportsSavingDuringRuntime;
}

// The text window uses the C64's built-in character set. Its shapes are
// drawn here from ScummVM's DOS font instead of Commodore's ROM.
void DarkLordEngine::buildCharRom() {
	byte rom[4096];
	Graphics::DosFont font;
	Graphics::Surface glyph;
	glyph.create(8, 8, Graphics::PixelFormat::createFormatCLUT8());

	for (int set = 0; set < 2; set++) {
		for (int code = 0; code < 128; code++) {
			int ch = ' ';
			if (code == 0)
				ch = '@';
			else if (code <= 26)
				ch = (set ? 'a' : 'A') + code - 1;
			else if (code == 27)
				ch = '[';
			else if (code == 28)
				ch = 0x9C; // pound sign
			else if (code == 29)
				ch = ']';
			else if (code == 30)
				ch = 0x18; // up arrow
			else if (code == 31)
				ch = 0x1B; // left arrow
			else if (code < 64)
				ch = code;
			else if (set && code >= 65 && code <= 90)
				ch = 'A' + code - 65; // the lower case set has capitals here
			else if (code == 64 || code == 67 || code == 68 || code == 69)
				ch = 0xC4; // horizontal line
			else if (code == 66 || code == 93)
				ch = 0xB3; // vertical line

			glyph.fillRect(Common::Rect(0, 0, 8, 8), 0);
			font.drawChar(&glyph, ch, 0, 0, 1);
			for (int row = 0; row < 8; row++) {
				byte bits = 0;
				for (int col = 0; col < 8; col++)
					if (*(const byte *)glyph.getBasePtr(col, row))
						bits |= 0x80 >> col;
				rom[set * 0x800 + code * 8 + row] = bits;
				rom[set * 0x800 + (code + 128) * 8 + row] = bits ^ 0xFF;
			}
		}
	}
	glyph.free();
	_machine->setCharRom(rom);
}

void DarkLordEngine::initSound() {
	_sid = SID::Config::create(SID::Config::kSidPAL);
	if (!_sid || !_sid->init()) {
		warning("DarkLord: no SID emulator, playing without sound");
		delete _sid;
		_sid = nullptr;
		return;
	}
	_sid->start(new Common::Functor0Mem<void, DarkLordEngine>(this, &DarkLordEngine::onSidTimer), 50);
}

void DarkLordEngine::sidWrite(int reg, int value) {
	Common::StackLock lock(_sidMutex);
	_sidWrites.push_back((reg << 8) | value);
}

void DarkLordEngine::onSidTimer() {
	Common::StackLock lock(_sidMutex);
	for (uint16 w : _sidWrites)
		_sid->writeReg(w >> 8, w & 0xFF);
	_sidWrites.clear();
}

Common::String DarkLordEngine::diskFileName(const Common::String &name) const {
	Common::String safe;
	for (char c : name)
		safe += Common::isAlnum(c) ? c : '_';
	return _targetName + "-disk-" + safe;
}

void DarkLordEngine::diskSave(const Common::String &name, const Common::Array<byte> &data) {
	Common::OutSaveFile *f = _saveFileMan->openForSaving(diskFileName(name), false);
	if (!f) {
		warning("DarkLord: could not save \"%s\"", name.c_str());
		return;
	}
	f->write(data.data(), data.size());
	f->finalize();
	delete f;
}

bool DarkLordEngine::diskLoad(const Common::String &name, Common::Array<byte> &data) {
	Common::InSaveFile *f = _saveFileMan->openForLoading(diskFileName(name));
	if (!f)
		return false;
	data.resize(f->size());
	f->read(data.data(), data.size());
	delete f;
	return true;
}

Common::Error DarkLordEngine::saveGameStream(Common::WriteStream *stream, bool isAutosave) {
	_machine->saveState(*stream);
	return Common::kNoError;
}

Common::Error DarkLordEngine::loadGameStream(Common::SeekableReadStream *stream) {
	if (!_machine->loadState(*stream))
		return Common::kReadingFailed;
	return Common::kNoError;
}

bool DarkLordEngine::handleEvents() {
	Common::Event event;
	while (g_system->getEventManager()->pollEvent(event)) {
		if (event.type != Common::EVENT_KEYDOWN)
			continue;
		switch (event.kbd.keycode) {
		case Common::KEYCODE_RETURN:
		case Common::KEYCODE_KP_ENTER:
			_machine->typeChar('\r');
			break;
		case Common::KEYCODE_BACKSPACE:
		case Common::KEYCODE_DELETE:
			_machine->typeChar(8);
			break;
		default:
			if (event.kbd.ascii >= 32 && event.kbd.ascii < 127)
				_machine->typeChar(event.kbd.ascii);
			break;
		}
	}
	return !shouldQuit();
}

Common::Error DarkLordEngine::run() {
	initGraphics(Machine::kScreenW, Machine::kScreenH);
	g_system->getPaletteManager()->setPalette(kPalette, 0, 16);
	_screen.create(Machine::kScreenW, Machine::kScreenH, Graphics::PixelFormat::createFormatCLUT8());

	_machine = new Machine(this);
	// Both sides, loose or zipped, under any names. Side 2 goes last: the
	// game's saves are sectors on that side.
	Common::FSList files;
	Common::FSNode(ConfMan.getPath("path")).getChildren(files, Common::FSNode::kListFilesOnly);
	DiskFinder finder;
	finder.scan(files);
	if (!finder.found())
		return Common::Error(Common::kNoGameDataFoundError, "both Dark Lord disk sides (.d64)");
	for (int i = 0; i < 2; i++) {
		Common::MemoryReadStream s(finder.side[i].data(), finder.side[i].size());
		if (!_machine->addDisk(s))
			return Common::kNoGameDataFoundError;
	}
	if (!_machine->boot())
		return Common::kNoGameDataFoundError;
	buildCharRom();
	initSound();

	// A text adventure: keep the on-screen keyboard up where there is one
	if (g_system->hasFeature(OSystem::kFeatureVirtualKeyboard))
		g_system->setFeatureState(OSystem::kFeatureVirtualKeyboard, true);

	if (ConfMan.hasKey("save_slot"))
		loadGameState(ConfMan.getInt("save_slot"));

	uint32 next = g_system->getMillis();
	while (handleEvents()) {
		_machine->runFrame();
		_machine->render((byte *)_screen.getPixels(), _screen.pitch);
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

} // End of namespace DarkLord
