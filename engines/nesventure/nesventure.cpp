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
#include "nesventure/nesventure.h"

namespace NesVenture {

NesVentureEngine::NesVentureEngine(OSystem *syst, const NesGameDescription *desc) : Engine(syst), _desc(desc), _nes(nullptr),
		_audio(nullptr), _keys(0) {
}

NesVentureEngine::~NesVentureEngine() {
	delete _nes;
}

bool NesVentureEngine::hasFeature(EngineFeature f) const {
	return f == kSupportsReturnToLauncher;
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
	g_system->copyRectToScreen(_nes->screen(), Nes::kScreenW, 0, 0, Nes::kScreenW, Nes::kScreenH);
	g_system->updateScreen();
}

Common::Error NesVentureEngine::run() {
	_nes = new Nes();
	if (!loadCartridge())
		return Common::Error(Common::kNoGameDataFoundError, "Could not read the NES cartridge (.nes file)");
	loadBattery();
	_nes->reset();

	initGraphics(Nes::kScreenW, Nes::kScreenH);
	g_system->getPaletteManager()->setPalette(Nes::kPalette, 0, 64);

	_audio = Audio::makeQueuingAudioStream(Nes::kSampleRate, false);
	_mixer->playStream(Audio::Mixer::kMusicSoundType, &_audioHandle, _audio);

	// The NES's own rate: 60.0988 frames a second
	const double frameMs = 1000.0 / 60.0988;
	double next = g_system->getMillis();
	uint32 lastBattery = g_system->getMillis();
	while (handleEvents()) {
		_nes->buttons = _keys;
		_nes->runFrame();
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
