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

#ifndef NESVENTURE_DETECTION_H
#define NESVENTURE_DETECTION_H

#include "engines/advancedDetector.h"

namespace NesVenture {

enum GameId {
	kGameShadowgate,
	kGameDejaVu,
	kGameUninvited
};

struct NesGameDescription {
	ADGameDescription desc;
	GameId gameId;

	uint32 sizeBuffer() const {
		return desc.sizeBuffer();
	}

	void *toBuffer(void *buffer) {
		return desc.toBuffer(buffer);
	}
};

} // End of namespace NesVenture

class NesVentureMetaEngineDetection : public AdvancedMetaEngineDetection<NesVenture::NesGameDescription> {
public:
	NesVentureMetaEngineDetection();
	~NesVentureMetaEngineDetection() override {}

	ADDetectedGame fallbackDetect(const FileMap &allFiles, const Common::FSList &fslist, ADDetectedGameExtraInfo **extra) const override;

	const char *getName() const override {
		return "nesventure";
	}

	const char *getEngineName() const override {
		return "NES MacVentures (Kemco-Seika)";
	}

	const char *getOriginalCopyright() const override {
		return "Shadowgate, Deja Vu, Uninvited (C) ICOM Simulations; NES versions (C) Kemco-Seika";
	}
};

#endif // NESVENTURE_DETECTION_H
