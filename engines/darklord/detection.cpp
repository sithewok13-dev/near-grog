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

#include "base/plugins.h"
#include "darklord/detection.h"
#include "darklord/disks.h"

namespace DarkLord {

static const PlainGameDescriptor darkLordGames[] = {
	{ "darklord", "Dark Lord" },
	{ nullptr, nullptr }
};

// The two disk sides start and end with identical (empty) sectors, so the
// file names and sizes do most of the work here.
static const ADGameDescription gameDescriptions[] = {
	{
		"darklord",
		"",
		AD_ENTRY2s("DARKLRD0.D64", "6bf95a48f366bdf8af3a198c7b723c77", 174848,
		           "DARKLRD1.D64", "6bf95a48f366bdf8af3a198c7b723c77", 174848),
		Common::EN_ANY,
		Common::kPlatformC64,
		ADGF_TESTING,
		GUIO2(GUIO_NOMIDI, GUIO_NOSPEECH)
	},
	AD_TABLE_END_MARKER
};

// Disks under other names, or inside a .zip: found by their contents
static const ADGameDescription fallbackApple2 = {
	"darklord",
	"",
	AD_ENTRY1(nullptr, nullptr),
	Common::EN_ANY,
	Common::kPlatformApple2,
	ADGF_TESTING,
	GUIO2(GUIO_NOMIDI, GUIO_NOSPEECH)
};

static const ADGameDescription fallbackDescription = {
	"darklord",
	"",
	AD_ENTRY1(nullptr, nullptr),
	Common::EN_ANY,
	Common::kPlatformC64,
	ADGF_TESTING,
	GUIO2(GUIO_NOMIDI, GUIO_NOSPEECH)
};

} // End of namespace DarkLord

DarkLordMetaEngineDetection::DarkLordMetaEngineDetection() : AdvancedMetaEngineDetection(DarkLord::gameDescriptions, DarkLord::darkLordGames) {
}

ADDetectedGame DarkLordMetaEngineDetection::fallbackDetect(const FileMap &allFiles, const Common::FSList &fslist, ADDetectedGameExtraInfo **extra) const {
	DarkLord::DiskFinder finder;
	finder.scan(fslist);
	if (finder.c64Found())
		return ADDetectedGame(&DarkLord::fallbackDescription);
	if (finder.appleFound())
		return ADDetectedGame(&DarkLord::fallbackApple2);
	return ADDetectedGame();
}

REGISTER_PLUGIN_STATIC(DARKLORD_DETECTION, PLUGIN_TYPE_ENGINE_DETECTION, DarkLordMetaEngineDetection);
