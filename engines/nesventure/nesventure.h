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
#include "nesventure/detection.h"

namespace NesVenture {

class Nes;

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

private:
	const NesGameDescription *_desc;
	Nes *_nes;
	Audio::QueuingAudioStream *_audio;
	Audio::SoundHandle _audioHandle;
	uint8 _keys; // controller buttons held on the keyboard / a game controller

	bool loadCartridge();
	Common::String batteryName() const;
	void loadBattery();
	void saveBattery();
	bool handleEvents();
	void present();
};

} // End of namespace NesVenture

#endif
