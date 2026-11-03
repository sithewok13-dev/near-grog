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
#include "common/events.h"
#include "common/fs.h"
#include "common/md5.h"
#include "common/savefile.h"
#include "common/system.h"
#include "audio/audiostream.h"
#include "audio/decoders/raw.h"
#include "engines/util.h"
#include "graphics/paletteman.h"

#include "nesventure/nes.h"
#include "nesventure/nesui.h"
#include "nesventure/nesventure.h"
#include "gui/dialog.h"
#include "gui/gui-manager.h"
#include "gui/widget.h"

namespace NesVenture {

NesVentureEngine::NesVentureEngine(OSystem *syst, const NesGameDescription *desc) : Engine(syst), _desc(desc), _nes(nullptr),
		_audio(nullptr), _keys(0), _ui(nullptr) {
}

NesVentureEngine::~NesVentureEngine() {
	delete _ui;
	delete _nes;
}

bool NesVentureEngine::hasFeature(EngineFeature f) const {
	return f == kSupportsReturnToLauncher || f == kSupportsLoadingDuringRuntime || f == kSupportsSavingDuringRuntime;
}

// Save states: the whole NES (the games' own saves stay in battery RAM)
Common::Error NesVentureEngine::saveGameStream(Common::WriteStream *stream, bool isAutosave) {
	_nes->saveState(*stream);
	return Common::kNoError;
}

Common::Error NesVentureEngine::loadGameStream(Common::SeekableReadStream *stream) {
	return _nes->loadState(*stream) ? Common::kNoError : Common::kReadingFailed;
}

void NesVentureEngine::quickSave() {
	Common::Error err = saveGameState(99, "Quick save");
	g_system->displayMessageOnOSD(Common::U32String(err.getCode() == Common::kNoError ? "Saved" : "Could not save"));
}

void NesVentureEngine::quickLoad() {
	if (!_saveFileMan->exists(getSaveStateName(99))) {
		g_system->displayMessageOnOSD(Common::U32String("No quick save yet"));
		return;
	}
	Common::Error err = loadGameState(99);
	g_system->displayMessageOnOSD(Common::U32String(err.getCode() == Common::kNoError ? "Loaded" : "Could not load"));
}

// The phone layouts in true colour, the shape of the screen
void NesVentureEngine::applyLayout() {
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
	const bool portrait = NesUI::portraitScreen();
	const int w = portrait ? 646 : 1400, h = portrait ? 1400 : 646;
	if (g_system->getWidth() != w || g_system->getHeight() != h || g_system->getScreenFormat() != _format)
		initGraphics(w, h, &_format);
	_ui->setup(w, h, portrait, NesUI::configLook(), _format);
}

// The Near-Grog tray (a hold on MENU, or the menu key)
namespace {

enum { kCmdLook = 'look', kCmdScummVM = 'smvm', kCmdResume = 'resm' };

class TrayDialog : public GUI::Dialog {
public:
	TrayDialog() : GUI::Dialog(0, 0, 0, 0) {
		const Common::String labels[] = {
			Common::String::format("Look: %s", NesUI::configLook() == NesUI::kLookDark ? "Dark" : "NES"),
			"ScummVM menu", "Resume"
		};
		const uint32 cmds[] = { kCmdLook, kCmdScummVM, kCmdResume };
		const int n = ARRAYSIZE(labels);
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
		setResult(cmd);
		close();
	}
};

} // End of anonymous namespace

void NesVentureEngine::openMainMenuDialog() {
	while (true) {
		uint32 cmd;
		{
			PauseToken pause = pauseEngine();
			TrayDialog tray;
			cmd = tray.runModal();
		}
		if (cmd == kCmdLook) {
			ConfMan.set("neargrog_look", NesUI::configLook() == NesUI::kLookDark ? "nes" : "dark");
			ConfMan.flushToDisk();
			applyLayout();
			present();
			continue;
		}
		if (cmd == kCmdScummVM)
			Engine::openMainMenuDialog();
		return;
	}
}

// The cartridge: the file detection named, or any .nes file in the game's
// folder with the same contents
bool NesVentureEngine::loadCartridge() {
	const ADGameFileDescription *want = _desc->desc.filesDescriptions;
	Common::FSNode dir(ConfMan.getPath("path"));
	Common::FSList files;
	if (!dir.getChildren(files, Common::FSNode::kListFilesOnly))
		return false;
	for (const Common::FSNode &node : files) {
		if (!node.getFileName().hasSuffixIgnoreCase(".nes"))
			continue;
		Common::ScopedPtr<Common::SeekableReadStream> s(node.createReadStream());
		if (!s || s->size() != want->fileSize || Common::computeStreamMD5AsString(*s, 5000) != want->md5)
			continue;
		s->seek(0);
		Common::Array<byte> rom(s->size());
		s->read(rom.data(), rom.size());
		return _nes->load(rom);
	}
	return false;
}

Common::String NesVentureEngine::batteryName() const {
	return _targetName + ".bat";
}

void NesVentureEngine::loadBattery() {
	Common::ScopedPtr<Common::InSaveFile> in(_saveFileMan->openForLoading(batteryName()));
	if (in)
		in->read(_nes->batteryRam(), 0x2000);
	_nes->batteryDirty = false;
}

void NesVentureEngine::saveBattery() {
	if (!_nes->hasBattery())
		return;
	Common::ScopedPtr<Common::OutSaveFile> out(_saveFileMan->openForSaving(batteryName(), false));
	if (out) {
		out->write(_nes->batteryRam(), 0x2000);
		out->finalize();
	}
	_nes->batteryDirty = false;
}

// The keyboard and game controllers drive the NES's controller
bool NesVentureEngine::handleEvents() {
	Common::Event ev;
	while (g_system->getEventManager()->pollEvent(ev)) {
		uint8 bit = 0;
		switch (ev.type) {
		case Common::EVENT_QUIT:
		case Common::EVENT_RETURN_TO_LAUNCHER:
			return false;
		case Common::EVENT_SCREEN_CHANGED:
			applyLayout();
			break;
		case Common::EVENT_LBUTTONDOWN:
			_ui->press(ev.mouse);
			break;
		case Common::EVENT_LBUTTONUP:
			switch (_ui->release(ev.mouse)) {
			case NesUI::kSaveDialog: saveGameDialog(); break;
			case NesUI::kLoadDialog: loadGameDialog(); break;
			case NesUI::kTray: openMainMenuDialog(); break;
			default: break;
			}
			break;
		case Common::EVENT_WHEELUP:
			_ui->scroll(1);
			break;
		case Common::EVENT_WHEELDOWN:
			_ui->scroll(-1);
			break;
		case Common::EVENT_KEYDOWN:
		case Common::EVENT_KEYUP:
			switch (ev.kbd.keycode) {
			case Common::KEYCODE_UP: bit = Nes::kUp; break;
			case Common::KEYCODE_DOWN: bit = Nes::kDown; break;
			case Common::KEYCODE_LEFT: bit = Nes::kLeft; break;
			case Common::KEYCODE_RIGHT: bit = Nes::kRight; break;
			case Common::KEYCODE_z: case Common::KEYCODE_SPACE: bit = Nes::kA; break;
			case Common::KEYCODE_x: case Common::KEYCODE_BACKSPACE: bit = Nes::kB; break;
			case Common::KEYCODE_RETURN: bit = Nes::kStart; break;
			case Common::KEYCODE_RSHIFT: case Common::KEYCODE_TAB: bit = Nes::kSelect; break;
			default: break;
			}
			break;
		case Common::EVENT_JOYBUTTON_DOWN:
		case Common::EVENT_JOYBUTTON_UP:
			switch (ev.joystick.button) {
			case Common::JOYSTICK_BUTTON_A: bit = Nes::kA; break;
			case Common::JOYSTICK_BUTTON_B: bit = Nes::kB; break;
			case Common::JOYSTICK_BUTTON_START: bit = Nes::kStart; break;
			case Common::JOYSTICK_BUTTON_BACK: bit = Nes::kSelect; break;
			case Common::JOYSTICK_BUTTON_DPAD_UP: bit = Nes::kUp; break;
			case Common::JOYSTICK_BUTTON_DPAD_DOWN: bit = Nes::kDown; break;
			case Common::JOYSTICK_BUTTON_DPAD_LEFT: bit = Nes::kLeft; break;
			case Common::JOYSTICK_BUTTON_DPAD_RIGHT: bit = Nes::kRight; break;
			default: break;
			}
			break;
		default:
			break;
		}
		if (bit) {
			if (ev.type == Common::EVENT_KEYDOWN || ev.type == Common::EVENT_JOYBUTTON_DOWN)
				_keys |= bit;
			else
				_keys &= ~bit;
		}
	}
	return !shouldQuit();
}

void NesVentureEngine::present() {
	if (!_ui->draw())
		return;
	const Graphics::ManagedSurface &o = _ui->output();
	g_system->copyRectToScreen(o.getPixels(), o.pitch, 0, 0, o.w, o.h);
	g_system->updateScreen();
}

Common::Error NesVentureEngine::run() {
	_nes = new Nes();
	if (!loadCartridge())
		return Common::Error(Common::kNoGameDataFoundError, "Could not read the NES cartridge (.nes file)");
	loadBattery();
	_nes->reset();

	_ui = new NesUI(_nes, _desc->gameId);
	applyLayout();

	_audio = Audio::makeQueuingAudioStream(Nes::kSampleRate, false);
	_mixer->playStream(Audio::Mixer::kMusicSoundType, &_audioHandle, _audio);

	// The NES's own rate: 60.0988 frames a second
	const double frameMs = 1000.0 / 60.0988;
	double next = g_system->getMillis();
	uint32 lastBattery = g_system->getMillis();
	while (handleEvents()) {
		switch (_ui->tick()) {
		case NesUI::kQuickSave: quickSave(); break;
		case NesUI::kQuickLoad: quickLoad(); break;
		default: break;
		}
		// While the layout steers the pointer, the NES runs four frames to one
		const int frames = _ui->steering() ? 4 : 1;
		for (int i = 0; i < frames; i++) {
			_nes->buttons = _keys | _ui->buttons();
			_nes->runFrame();
			_ui->update();
		}
		if (!_nes->audio.empty() && _audio->numQueuedStreams() < 8) {
			const uint bytes = _nes->audio.size() * 2;
			int16 *buf = (int16 *)malloc(bytes);
			memcpy(buf, _nes->audio.data(), bytes);
			byte flags = Audio::FLAG_16BITS;
#ifdef SCUMM_LITTLE_ENDIAN
			flags |= Audio::FLAG_LITTLE_ENDIAN;
#endif
			_audio->queueBuffer((byte *)buf, bytes, DisposeAfterUse::YES, flags);
		}
		present();
		// The games save to battery RAM; keep it a second after it settles
		if (_nes->batteryDirty && g_system->getMillis() - lastBattery > 1000) {
			saveBattery();
			lastBattery = g_system->getMillis();
		}
		next += frameMs;
		const double now = g_system->getMillis();
		if (next > now)
			g_system->delayMillis((uint32)(next - now));
		else if (now - next > 250)
			next = now; // fell behind (a pause): don't race to catch up
	}
	if (_nes->batteryDirty)
		saveBattery();
	_mixer->stopHandle(_audioHandle);
	return Common::kNoError;
}

} // End of namespace NesVenture
