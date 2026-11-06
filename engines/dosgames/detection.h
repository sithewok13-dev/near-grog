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

#ifndef DOSGAMES_DETECTION_H
#define DOSGAMES_DETECTION_H

#include "engines/advancedDetector.h"

namespace DosGames {

enum GameId {
	kGameFoolsErrand,
	kGameDondra,
	kGameCountdown,
	kGameAlteredDestiny,
	kGameOregonTrail
};

struct DosGameDescription {
	ADGameDescription desc;
	GameId gameId;

	uint32 sizeBuffer() const {
		return desc.sizeBuffer();
	}

	void *toBuffer(void *buffer) {
		return desc.toBuffer(buffer);
	}
};

} // End of namespace DosGames

class DosGamesMetaEngineDetection : public AdvancedMetaEngineDetection<DosGames::DosGameDescription> {
public:
	DosGamesMetaEngineDetection();
	~DosGamesMetaEngineDetection() override {}

	ADDetectedGame fallbackDetect(const FileMap &allFiles, const Common::FSList &fslist, ADDetectedGameExtraInfo **extra) const override;

	const char *getName() const override {
		return "dosgames";
	}

	const char *getEngineName() const override {
		return "DOS games (original programs)";
	}

	const char *getOriginalCopyright() const override {
		return "The Fool's Errand (C) 1987-89 Cliff Johnson; Dondra (C) 1991 Miles Computing; "
			"Countdown (C) 1990 Access Software; Altered Destiny (C) 1990 Accolade";
	}
};

#endif // DOSGAMES_DETECTION_H
