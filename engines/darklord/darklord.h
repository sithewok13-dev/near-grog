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

#ifndef DARKLORD_DARKLORD_H
#define DARKLORD_DARKLORD_H

#include "common/array.h"
#include "common/mutex.h"
#include "engines/advancedDetector.h"
#include "engines/engine.h"
#include "graphics/surface.h"
#include "darklord/machine.h"

namespace SID {
class SID;
}

namespace DarkLord {

/**
 * Dark Lord (Datasoft, 1987) for the Commodore 64.
 *
 * Runs the game's original 6502 code on a minimal C64 (see Machine), so the
 * parser, puzzles, pictures, animations and music all behave as they did.
 */
class DarkLordEngine : public Engine, public MachineHost {
public:
	DarkLordEngine(OSystem *syst, const ADGameDescription *desc);
	~DarkLordEngine() override;

	Common::Error run() override;
	bool hasFeature(EngineFeature f) const override;
	bool canLoadGameStateCurrently(Common::U32String *msg = nullptr) override { return true; }
	bool canSaveGameStateCurrently(Common::U32String *msg = nullptr) override { return true; }
	Common::Error saveGameStream(Common::WriteStream *stream, bool isAutosave = false) override;
	Common::Error loadGameStream(Common::SeekableReadStream *stream) override;

	// MachineHost
	void sidWrite(int reg, int value) override;
	void diskSave(const Common::String &name, const Common::Array<byte> &data) override;
	bool diskLoad(const Common::String &name, Common::Array<byte> &data) override;

private:
	const ADGameDescription *_desc;
	Machine *_machine;
	SID::SID *_sid;
	Common::Mutex _sidMutex;
	Common::Array<uint16> _sidWrites;
	Graphics::Surface _screen;

	void initSound();
	void onSidTimer();
	void buildCharRom();
	bool handleEvents();
	Common::String diskFileName(const Common::String &name) const;
};

} // End of namespace DarkLord

#endif
