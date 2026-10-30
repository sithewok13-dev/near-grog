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

#ifndef DARKLORD_DETECTION_H
#define DARKLORD_DETECTION_H

#include "engines/advancedDetector.h"

class DarkLordMetaEngineDetection : public AdvancedMetaEngineDetection<ADGameDescription> {
public:
	DarkLordMetaEngineDetection();
	~DarkLordMetaEngineDetection() override {}

	const char *getName() const override {
		return "darklord";
	}

	const char *getEngineName() const override {
		return "Dark Lord";
	}

	const char *getOriginalCopyright() const override {
		return "Dark Lord (C) 1987 Kyle Freeman, Datasoft";
	}
};

#endif // DARKLORD_DETECTION_H
