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

#include "audio/audiostream.h"
#include "audio/decoders/raw.h"
#include "audio/mixer.h"
#include "audio/sid.h"
#include "common/config-manager.h"
#include "common/events.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/func.h"
#include "common/memstream.h"
#include "common/translation.h"
#include "common/savefile.h"
#include "common/system.h"
#include "engines/util.h"
#include "graphics/fonts/dosfont.h"
#include "graphics/paletteman.h"
#include "darklord/c64ui.h"
#include "darklord/darklord.h"
#include "gui/dialog.h"
#include "gui/gui-manager.h"
#include "gui/widget.h"
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
		Engine(syst), _desc(desc), _machine(nullptr), _apple(nullptr), _speaker(nullptr), _sid(nullptr),
		_cheat(false), _snapshotLine(0), _uninvited(false), _touching(false), _fireFrames(0), _ui(nullptr), _menuClick(0) {
	memset(_c64Screen, 0, sizeof(_c64Screen));
	_gameId = desc->gameId;
	_uninvited = _gameId == "uninvitedc64";
}

DarkLordEngine::~DarkLordEngine() {
	if (_sid) {
		_sid->stop();
		delete _sid;
	}
	_mixer->stopAll();
	delete _ui;
	delete _machine;
	delete _apple;
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

// The Apple II text screen, drawn from the same DOS font (ASCII order)
void DarkLordEngine::buildAppleFont() {
	byte glyphs[128 * 8];
	Graphics::DosFont font;
	Graphics::Surface glyph;
	glyph.create(8, 8, Graphics::PixelFormat::createFormatCLUT8());
	for (int c = 0; c < 128; c++) {
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
	_apple->setFont(glyphs);
}

void DarkLordEngine::appleSound(const int16 *samples, int count) {
	if (!_speaker)
		return;
	// Don't let sound pile up if the game runs ahead of the mixer
	if (_speaker->numQueuedStreams() > 8)
		return;
	int16 *buf = (int16 *)malloc(count * sizeof(int16));
	memcpy(buf, samples, count * sizeof(int16));
	byte flags = Audio::FLAG_16BITS;
#ifdef SCUMM_LITTLE_ENDIAN
	flags |= Audio::FLAG_LITTLE_ENDIAN;
#endif
	_speaker->queueBuffer((byte *)buf, count * sizeof(int16), DisposeAfterUse::YES, flags);
}

void DarkLordEngine::typeChar(int ch) {
	if (_apple)
		_apple->typeChar(ch);
	else
		onTypedKey(ch);
}

// Uninvited's start-up runs by itself until then
static const uint32 kUninvitedBootFrames = 1600;

// --- Helpers for the C64 games ---

void DarkLordEngine::setUpGameHelpers() {
	auto answer = [&](const char *text, const char *keys, bool flip) {
		PromptAnswer a = { text, keys, flip, false };
		_answers.push_back(a);
	};
	if (_gameId == "transylvania84") {
		// The crack's intro, and its fast loader (the KERNAL's loads are
		// what this machine answers)
		answer("to serve and preserve", " ", false);
		answer("isable fastloader", "d", false);
		_deathTexts.push_back("restart the game");
	} else if (_gameId != "darklord") {
		// Polarware's Comprehend games: "Press 'R' if you would like to /
		// restart the novel." (the last line shows only after a key)
		_deathTexts.push_back("if you would like to");
		_deathTexts.push_back("restart the novel");
	}
	if (_gameId == "crimsoncrownc64") {
		// The second disk: put it in and carry on
		answer("insert disk", "\r", true);
		answer("insert side", "\r", true);
		answer("turn the disk", "\r", true);
		answer("flip the disk", "\r", true);
	}
}

// Every key the player types passes here: the cheat word, and the line
// being typed (to snapshot the game when it is entered)
void DarkLordEngine::onTypedKey(int ch) {
	if (_gameId != "darklord") {
		if (ch >= 32 && ch < 127) {
			_recent += (char)tolower(ch);
			if (_recent.size() > 8)
				_recent.deleteChar(0);
			if (_recent == "sithewok") {
				_cheat = !_cheat;
				_recent.clear();
				_machine->typeChar(ch);
				// Take the word back out of the game's input line
				for (int i = 0; i < 8; i++)
					_machine->typeChar(8);
				_line.clear();
				_snapshot.clear();
				g_system->displayMessageOnOSD(_cheat ?
					Common::U32String("Sithewok: no death, no clock") : Common::U32String("Sithewok off"));
				return;
			}
			_line += (char)ch;
		} else if (ch == 8) {
			if (!_line.empty())
				_line.deleteLastChar();
		} else if (ch == '\r' && _cheat && !showsDeath()) {
			// The state just before this move, to come back to (not if a
			// death is already on screen: keep the one before it)
			Common::MemoryWriteStreamDynamic w(DisposeAfterUse::YES);
			_machine->saveState(w);
			_snapshot = Common::Array<byte>(w.getData(), w.size());
			_snapshotLine = _line.size();
		}
		if (ch == '\r')
			_line.clear();
	}
	_machine->typeChar(ch);
}

void DarkLordEngine::applyCheatHolds() {
	if (_gameId == "transylvania84") {
		// The clock: moves this hour, at $1A03, checked against 71 at $143B
		static const byte kClock[] = { 0xAD, 0x03, 0x1A, 0xC9, 0x47 };
		bool match = true;
		for (uint i = 0; i < sizeof(kClock); i++)
			match &= _machine->peek(0x143B + i) == kClock[i];
		if (match)
			_machine->poke(0x1A03, 0);
	}
}

// Transylvania 1985: the werewolf (object 33) and vampire (38) come and go
// as usual; one that would have killed you leaves instead. The game data at
// $3000 starts with Comprehend's table of pointers; entry 22 is where
// object locations live.
void DarkLordEngine::sendMonstersAway() {
	if (_gameId != "transylvania85" && _gameId != "transylvania85b")
		return;
	uint16 locations = _machine->peek(0x302C) | (_machine->peek(0x302D) << 8);
	if (locations >= 0x3000 && locations < 0x9000) {
		_machine->poke(locations + 33, 0xFF);
		_machine->poke(locations + 38, 0xFF);
	}
}

bool DarkLordEngine::showsDeath() const {
	Common::String text = _machine->screenText();
	for (const Common::String &d : _deathTexts)
		if (text.contains(d))
			return true;
	return false;
}

void DarkLordEngine::runHelpers() {
	if (_gameId == "darklord" || !_machine)
		return;
	if (_cheat)
		applyCheatHolds();
	if (_machine->frameCount() % 5)
		return;
	Common::String text = _machine->screenText();
	for (PromptAnswer &a : _answers) {
		bool shown = text.contains(a.text);
		if (shown && !a.shown) {
			if (a.flipDisk)
				_machine->flipDisk();
			for (const char *k = a.keys; *k; k++)
				_machine->typeChar(*k);
		}
		a.shown = shown;
	}
	if (_cheat && !_snapshot.empty()) {
		for (const Common::String &d : _deathTexts) {
			if (!text.contains(d))
				continue;
			// Back to just before the fatal move, with that move erased
			Common::MemoryReadStream r(_snapshot.data(), _snapshot.size());
			_machine->loadState(r);
			for (uint i = 0; i < _snapshotLine; i++)
				_machine->typeChar(8);
			_snapshot.clear();
			sendMonstersAway();
			g_system->displayMessageOnOSD(Common::U32String("Sithewok: that move would have killed you"));
			break;
		}
	}
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
	if (_apple)
		_apple->saveState(*stream);
	else
		_machine->saveState(*stream);
	return Common::kNoError;
}

Common::Error DarkLordEngine::loadGameStream(Common::SeekableReadStream *stream) {
	if (!(_apple ? _apple->loadState(*stream) : _machine->loadState(*stream)))
		return Common::kReadingFailed;
	return Common::kNoError;
}

// --- Uninvited ---

// The pointer is sprite 0, its tip at its top left; the sprite's position
// is 24 right and 50 down of the screen pixel it points at. The game keeps
// its own copy: the sprite's position at $0E (x, low), $10 (x, high) and
// $0F (y), and the screen pixel at $38 (y) and $39 (x / 2).
void DarkLordEngine::pointerTo(Common::Point p) {
	_touch = Common::Point(CLIP<int16>(p.x, 0, 316), CLIP<int16>(p.y, 0, Machine::kScreenH - 1));
	placePointer();
}

void DarkLordEngine::placePointer() {
	const int sx = _touch.x + 24, sy = _touch.y + 50;
	_machine->setSprite(0, sx, sy);
	_machine->poke(0x0E, sx & 0xFF);
	_machine->poke(0x10, sx >> 8);
	_machine->poke(0x0F, sy);
	_machine->poke(0x38, _touch.y);
	_machine->poke(0x39, _touch.x / 2);
}

// Starting up: the crack's intro (a key), then "PLEASE INSERT SIDE TWO AND
// PRESS RETURN" - always at the same moment, the machine being exact. Run
// fast meanwhile, the player has nothing to do.
void DarkLordEngine::runUninvitedBoot() {
	const uint32 f = _machine->frameCount();
	_machine->setJoystick(1, f >= 300 && f < 310 ? 16 : 0);
	if (f == 1400)
		_machine->flipDisk();
	if (f == 1500)
		_machine->typeChar('\r');
	if (f == kUninvitedBootFrames && _ui)
		_ui->clearText(); // (the disk prompt)
}

void DarkLordEngine::applyLayout() {
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
	const bool portrait = C64UI::portraitScreen();
	const int w = portrait ? 646 : 1400, h = portrait ? 1400 : 646;
	if (g_system->getWidth() != w || g_system->getHeight() != h || g_system->getScreenFormat() != _format)
		initGraphics(w, h, &_format);
	_ui->setup(w, h, portrait, C64UI::configLook(), C64UI::configLayout(), _format);
}

void DarkLordEngine::present() {
	if (!_ui->draw(_c64Screen))
		return;
	const Graphics::ManagedSurface &o = _ui->output();
	g_system->copyRectToScreen(o.getPixels(), o.pitch, 0, 0, o.w, o.h);
	g_system->updateScreen();
}

void DarkLordEngine::quickSave() {
	Common::Error err = saveGameState(99, "Quick save");
	g_system->displayMessageOnOSD(Common::U32String(err.getCode() == Common::kNoError ? "Saved" : "Could not save"));
}

void DarkLordEngine::quickLoad() {
	if (!_saveFileMan->exists(getSaveStateName(99))) {
		g_system->displayMessageOnOSD(Common::U32String("No quick save yet"));
		return;
	}
	Common::Error err = loadGameState(99);
	g_system->displayMessageOnOSD(Common::U32String(err.getCode() == Common::kNoError ? "Loaded" : "Could not load"));
}

namespace {

enum { kCmdLook = 'look', kCmdLayout = 'layt', kCmdGameMenu = 'gmnu', kCmdScummVM = 'smvm', kCmdResume = 'resm' };

// Near-Grog's tray for Uninvited
class TrayDialog : public GUI::Dialog {
public:
	TrayDialog() : GUI::Dialog(0, 0, 0, 0), _opened(g_system->getMillis()) {
		const bool portrait = C64UI::portraitScreen();
		Common::Array<Common::String> labels;
		Common::Array<uint32> cmds;
		labels.push_back(Common::String::format("Look: %s", C64UI::configLook() == C64UI::kLookDark ? "Dark" : "Commodore 64"));
		cmds.push_back(kCmdLook);
		if (!portrait) { // (portrait has the one layout)
			labels.push_back(Common::String::format("Layout: %s", C64UI::layoutName(C64UI::configLayout())));
			cmds.push_back(kCmdLayout);
		}
		labels.push_back("Game menu (?=)");
		cmds.push_back(kCmdGameMenu);
		labels.push_back("ScummVM menu");
		cmds.push_back(kCmdScummVM);
		labels.push_back("Resume");
		cmds.push_back(kCmdResume);
		const int n = labels.size();
		const int sw = g_system->getOverlayWidth(), sh = g_system->getOverlayHeight();
		const int fh = g_gui.getFontHeight();
		const int bh = MAX(fh * 2, MIN(sw, sh) / 8), gap = bh / 4, margin = fh;
		const int bw = MIN(MAX(MIN(sw, sh) / 2, fh * 12), sw - 40);
		_w = margin * 2 + bw;
		_h = margin * 2 + fh * 2 + n * (bh + gap);
		_x = (sw - _w) / 2;
		_y = MAX<int>(10, (sh - _h) / 2);
		new GUI::StaticTextWidget(this, margin, margin, bw, fh + 4, Common::U32String("Near-Grog"), Graphics::kTextAlignCenter);
		for (int i = 0; i < n; i++)
			new GUI::ButtonWidget(this, margin, margin + fh * 2 + i * (bh + gap), bw, bh, Common::U32String(labels[i]), Common::U32String(), cmds[i]);
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

} // End of anonymous namespace

void DarkLordEngine::openMainMenuDialog() {
	if (!_uninvited || !_ui) {
		Engine::openMainMenuDialog();
		return;
	}
	while (true) {
		uint32 cmd;
		{
			PauseToken pause = pauseEngine();
			TrayDialog tray;
			cmd = tray.runModal();
		}
		if (cmd == kCmdLook) {
			ConfMan.set("neargrog_look", C64UI::configLook() == C64UI::kLookDark ? "c64" : "dark");
			ConfMan.flushToDisk();
			applyLayout();
			present();
			continue;
		}
		if (cmd == kCmdLayout) {
			const C64UI::Layout l = C64UI::configLayout();
			ConfMan.set("neargrog_layout", l == C64UI::kLayoutC2 ? "e1" : l == C64UI::kLayoutE1 ? "e2" : l == C64UI::kLayoutE2 ? "original" : "c2");
			ConfMan.flushToDisk();
			applyLayout();
			present();
			continue;
		}
		if (cmd == kCmdGameMenu)
			_menuClick = 8;
		if (cmd == kCmdScummVM)
			Engine::openMainMenuDialog();
		return;
	}
}

bool DarkLordEngine::handleEvents() {
	Common::Event event;
	while (g_system->getEventManager()->pollEvent(event)) {
		if (_uninvited && _ui) {
			C64UI::Action a = C64UI::kNone;
			const bool ready = _machine->frameCount() > kUninvitedBootFrames;
			switch (event.type) {
			case Common::EVENT_LBUTTONDOWN:
				a = _ui->press(event.mouse);
				break;
			case Common::EVENT_MOUSEMOVE:
				_ui->move(event.mouse);
				break;
			case Common::EVENT_LBUTTONUP:
				a = _ui->release(event.mouse);
				break;
			case Common::EVENT_WHEELUP:
				_ui->wheel(1);
				break;
			case Common::EVENT_WHEELDOWN:
				_ui->wheel(-1);
				break;
			case Common::EVENT_SCREEN_CHANGED:
				applyLayout();
				break;
			default:
				break;
			}
			switch (a) {
			case C64UI::kSaveDialog: if (ready) saveGameDialog(); break;
			case C64UI::kLoadDialog: loadGameDialog(); break;
			case C64UI::kTray: openMainMenuDialog(); break;
			default: break;
			}
		}
		if (event.type != Common::EVENT_KEYDOWN)
			continue;
		switch (event.kbd.keycode) {
		case Common::KEYCODE_RETURN:
		case Common::KEYCODE_KP_ENTER:
			typeChar('\r');
			break;
		case Common::KEYCODE_BACKSPACE:
		case Common::KEYCODE_DELETE:
			typeChar(8);
			break;
		default:
			if (event.kbd.ascii >= 32 && event.kbd.ascii < 127)
				typeChar(event.kbd.ascii);
			break;
		}
	}
	return !shouldQuit();
}

Common::Error DarkLordEngine::run() {
	// Both sides, loose or zipped, under any names
	Common::FSList files;
	Common::FSNode(ConfMan.getPath("path")).getChildren(files, Common::FSNode::kListFilesOnly);
	DiskFinder finder;
	finder.scan(files);
	bool apple = _desc->platform == Common::kPlatformApple2;
	bool darkLord = _gameId == "darklord";
	if (apple ? !finder.appleFound() : darkLord ? !finder.c64Found() : !finder.gameFound(_gameId))
		return Common::Error(Common::kNoGameDataFoundError, "the game's disks");

	if (apple) {
		initGraphics(Apple2::kScreenW, Apple2::kScreenH);
		g_system->getPaletteManager()->setPalette(Apple2::palette(), 0, 16);
		_screen.create(Apple2::kScreenW, Apple2::kScreenH, Graphics::PixelFormat::createFormatCLUT8());
		_apple = new Apple2(this);
		for (int i = 0; i < 2; i++)
			_apple->addDisk(finder.appleSide[i]);
		if (!_apple->boot())
			return Common::kNoGameDataFoundError;
		buildAppleFont();
		_speaker = Audio::makeQueuingAudioStream(Apple2::kSampleRate, false);
		_mixer->playStream(Audio::Mixer::kPlainSoundType, nullptr, _speaker);
	} else {
		_machine = new Machine(this);
		if (_uninvited) {
			_ui = new C64UI(_machine);
			applyLayout();
		} else {
			initGraphics(Machine::kScreenW, Machine::kScreenH);
			g_system->getPaletteManager()->setPalette(kPalette, 0, 16);
		}
		_screen.create(Machine::kScreenW, Machine::kScreenH, Graphics::PixelFormat::createFormatCLUT8());
		// Dark Lord: side 2 goes last, the game's saves are sectors on it.
		// Other games: their disks in order, the first in the drive.
		const Common::Array<Common::Array<byte> > *disks = darkLord ? nullptr : &finder.c64Games[_gameId];
		uint count = darkLord ? 2 : disks->size();
		for (uint i = 0; i < count; i++) {
			const Common::Array<byte> &img = darkLord ? finder.side[i] : (*disks)[i];
			Common::MemoryReadStream s(img.data(), img.size());
			if (!_machine->addDisk(s))
				return Common::kNoGameDataFoundError;
		}
		setUpGameHelpers();
		if (!_machine->boot())
			return Common::kNoGameDataFoundError;
		buildCharRom();
		initSound();
	}

	// A text adventure: keep the on-screen keyboard up where there is one
	// (not for Uninvited: it's played by pointing)
	if (!_uninvited && g_system->hasFeature(OSystem::kFeatureVirtualKeyboard))
		g_system->setFeatureState(OSystem::kFeatureVirtualKeyboard, true);

	if (ConfMan.hasKey("save_slot"))
		loadGameState(ConfMan.getInt("save_slot"));

	uint32 next = g_system->getMillis();
	while (handleEvents()) {
		if (_apple) {
			_apple->runFrame();
			_apple->render((byte *)_screen.getPixels(), _screen.pitch);
		} else if (_uninvited) {
			// several frames at a time while it starts up
			const int n = _machine->frameCount() < kUninvitedBootFrames ? 6 : 1;
			switch (_ui->tick()) {
			case C64UI::kQuickSave: quickSave(); break;
			case C64UI::kQuickLoad: quickLoad(); break;
			default: break;
			}
			for (int i = 0; i < n; i++) {
				if (_machine->frameCount() <= kUninvitedBootFrames)
					runUninvitedBoot();
				else {
					// The finger: where the layout says, the button with it
					Common::Point c;
					bool button = false;
					const bool touching = _ui->pointer(c, button) && !_menuClick;
					if (touching) {
						if (!_touching || c != _touch)
							pointerTo(c);
						placePointer();
						if (button && !_touching)
							_fireFrames = 4; // (a quick tap still holds it long enough to count)
					}
					if (_menuClick) {
						// the tray's "Game menu": a click on the game's ?= box
						pointerTo(Common::Point(305, 8));
						_fireFrames = --_menuClick > 2 ? 2 : 0;
					}
					_touching = touching && button;
					_machine->setJoystick(2, _touching || _fireFrames > 0 ? 16 : 0);
					if (_fireFrames > 0)
						_fireFrames--;
				}
				_machine->runFrame();
				_machine->render(_c64Screen, 320);
				_ui->frame(_c64Screen);
			}
		} else {
			_machine->runFrame();
			runHelpers();
			_machine->render((byte *)_screen.getPixels(), _screen.pitch);
		}
		if (_uninvited) {
			present();
		} else {
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

} // End of namespace DarkLord
