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

#ifndef NESVENTURE_NESVENTURE_H
#define NESVENTURE_NESVENTURE_H

#include "audio/audiostream.h"
#include "audio/mixer.h"
#include "engines/engine.h"
#include "engines/neargrog_cheats.h"
#include "graphics/pixelformat.h"
#include "nesventure/detection.h"

namespace NesVenture {

class Nes;
class NesUI;

/**
 * Near-Grog: Kemco-Seika's NES versions of the MacVentures, run on
 * Near-Grog's own NES (nes.cpp). The cartridge's battery RAM - where the
 * games keep their saves - is kept as <target>.bat in the save folder.
 */
class NesVentureEngine : public Engine {
public:
	NesVentureEngine(OSystem *syst, const NesGameDescription *desc);
	~NesVentureEngine() override;

	Common::Error run() override;
	bool hasFeature(EngineFeature f) const override;
	bool canLoadGameStateCurrently(Common::U32String *msg = nullptr) override { return _nes != nullptr; }
	bool canSaveGameStateCurrently(Common::U32String *msg = nullptr) override { return _nes != nullptr; }
	Common::Error saveGameStream(Common::WriteStream *stream, bool isAutosave = false) override;
	Common::Error loadGameStream(Common::SeekableReadStream *stream) override;
	void openMainMenuDialog() override; // the Near-Grog tray

private:
	const NesGameDescription *_desc;
	Nes *_nes;
	Audio::QueuingAudioStream *_audio;
	Audio::SoundHandle _audioHandle;
	uint8 _keys; // controller buttons held on the keyboard / a game controller
	NesUI *_ui;  // the phone layouts
	Graphics::PixelFormat _format;

	bool loadCartridge();
	Common::String batteryName() const;
	void loadBattery();
	void saveBattery();
	bool handleEvents();
	void present();
	void applyLayout();
	void quickSave();
	void quickLoad();
	// The cheats
	NearGrog::UndoStack _undo;
	NearGrog::Countdowns _countdowns;
	uint32 _cheatFrame = 0;
	void runCheats();
	void undoMove(const char *message);
};

} // End of namespace NesVenture

#endif
