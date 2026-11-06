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

#ifndef DOSGAMES_DOSGAMES_H
#define DOSGAMES_DOSGAMES_H

#include "common/array.h"
#include "common/mutex.h"
#include "engines/engine.h"
#include "graphics/surface.h"
#include "dosgames/detection.h"
#include "dosgames/dospc.h"
#include "dosgames/gamefiles.h"
#include "dosgames/touchpad.h"

namespace OPL {
class OPL;
}

namespace Audio {
class PCSpeaker;
}

namespace DosGames {

/**
 * DOS games run from their original programs on a minimal PC (see DosPC):
 * The Fool's Errand, Dondra: A New Beginning (Questmaster I), Countdown and
 * Altered Destiny, The Oregon Trail (with Near-Grog's touch keys, touchpad.h).
 */
class DosGamesEngine : public Engine, public DosHost {
public:
	DosGamesEngine(OSystem *syst, const DosGameDescription *desc);
	~DosGamesEngine() override;

	Common::Error run() override;
	bool hasFeature(EngineFeature f) const override;
	bool canLoadGameStateCurrently(Common::U32String *msg = nullptr) override { return true; }
	bool canSaveGameStateCurrently(Common::U32String *msg = nullptr) override { return true; }
	Common::Error saveGameStream(Common::WriteStream *stream, bool isAutosave = false) override;
	Common::Error loadGameStream(Common::SeekableReadStream *stream) override;
	void openMainMenuDialog() override;

	// DosHost
	bool readFile(const Common::String &name, Common::Array<byte> &data) override;
	void writeFile(const Common::String &name, const Common::Array<byte> &data) override;
	Common::StringArray listFiles() override;
	void oplWrite(int reg, int value) override;
	void speaker(bool on, int freq) override;

private:
	GameFiles *_files;
	const DosGameDescription *_desc;
	DosPC *_pc;
	Graphics::Surface _screen;
	OPL::OPL *_opl;
	Audio::PCSpeaker *_pcSpeaker;
	Common::Mutex _oplMutex;
	Common::Array<uint16> _oplWrites;
	// Near-Grog: The Oregon Trail's touch keys
	TouchPad *_pad;
	bool _keyboardUp; // (opened by the Keyboard key)
	Graphics::PixelFormat _format;
	void applyLayout();
	bool oregonHunting() const;
	bool oregonNamesScreen() const;
	bool oregonInstructionsScreen() const;
	int _namesFrames;   // the names screen seen (or not) this many checks running
	bool _namesKeyboard; // the keyboard opened for it
	uint32 _padTicks, _instructionsWait;
	void runPad();
	void presentPad();
	void drawEdition(byte *scr);
	void drawWagonS(byte *scr);
	Common::Array<byte> _shown;
	Common::Rect _wagonKey;            // the canvas the S was placed on
	Common::Array<Common::Point> _wagonS; // its pixels (320 x 200)
	Common::Array<Common::Point> _wagonWhite; // the canvas whitened behind it
	int _wagonDx = 0, _wagonDy = 0;    // nudged half a game pixel, to centre it
	void quickSave();
	void quickLoad();

	void setUpPrograms();
	void initSound();
	void onOplTimer();
	bool handleEvents();
	Common::String savedFileName(const Common::String &name) const;
};

} // End of namespace DosGames

#endif
