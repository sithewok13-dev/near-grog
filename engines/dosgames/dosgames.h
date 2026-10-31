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

namespace OPL {
class OPL;
}

namespace DosGames {

/**
 * DOS games run from their original programs on a minimal PC (see DosPC):
 * The Fool's Errand and Dondra: A New Beginning (Questmaster I).
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

	// DosHost
	bool readFile(const Common::String &name, Common::Array<byte> &data) override;
	void writeFile(const Common::String &name, const Common::Array<byte> &data) override;
	Common::StringArray listFiles() override;
	void oplWrite(int reg, int value) override;

private:
	const DosGameDescription *_desc;
	DosPC *_pc;
	Graphics::Surface _screen;
	OPL::OPL *_opl;
	Common::Mutex _oplMutex;
	Common::Array<uint16> _oplWrites;

	void setUpPrograms();
	void initSound();
	void onOplTimer();
	bool handleEvents();
	Common::String savedFileName(const Common::String &name) const;
};

} // End of namespace DosGames

#endif
