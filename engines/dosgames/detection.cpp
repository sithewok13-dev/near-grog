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
#include "common/md5.h"
#include "dosgames/detection.h"
#include "dosgames/gamefiles.h"

namespace DosGames {

static const PlainGameDescriptor dosGames[] = {
	{ "foolsdos", "The Fool's Errand (DOS)" },
	{ "dondra", "Dondra: A New Beginning (Questmaster I)" },
	{ "countdown", "Countdown" },
	{ "altereddestiny", "Altered Destiny" },
	{ "oregontrail", "The Oregon Trail (DOS, 1990)" },
	{ nullptr, nullptr }
};

static const DosGameDescription gameDescriptions[] = {
	{
		{
			"foolsdos",
			"",
			AD_ENTRY2s("F.EXE", "fe862b465cc0500db06d0cecb2c7dc1a", 189663,
			           "PICTURES.RES", "840f680f5b4668b671c3325d9e6698ae", 265276),
			Common::EN_ANY,
			Common::kPlatformDOS,
			ADGF_TESTING,
			GUIO2(GUIO_NOMIDI, GUIO_NOSPEECH)
		},
		kGameFoolsErrand
	},
	{
		{
			"dondra",
			"",
			AD_ENTRY2s("QM1.EXE", "509b86ef2f37491cbe3745d63b1f11af", 153904,
			           "D0.BGA", "cdffd66897ec742a21a02b4f1535c7e1", 112726),
			Common::EN_ANY,
			Common::kPlatformDOS,
			ADGF_TESTING,
			GUIO1(GUIO_NOSPEECH)
		},
		kGameDondra
	},
	{
		{
			"countdown",
			"",
			AD_ENTRY2s("COUNT.EXE", "5211ede6da34e87b36507eb3c1399892", 105205,
			           "TITLE.CNT", "c2d31dfd3cb398a83e70e025469f36d3", 153299),
			Common::EN_ANY,
			Common::kPlatformDOS,
			ADGF_TESTING,
			GUIO1(GUIO_NOSPEECH)
		},
		kGameCountdown
	},
	{
		{
			"altereddestiny",
			"",
			AD_ENTRY2s("exe.exe", "edf303e9416b1931117a037199aa641c", 273163,
			           "dataz.dat", "3c31839d43c30f99b48842442ada9ff5", 48576),
			Common::EN_ANY,
			Common::kPlatformDOS,
			ADGF_TESTING,
			GUIO1(GUIO_NOSPEECH)
		},
		kGameAlteredDestiny
	},
	{
		{
			"oregontrail",
			"",
			AD_ENTRY2s("OREGON.EXE", "14d58e8d23be05807b596ab9d7ceb47e", 81896,
			           "OTMCGA.PCL", "b7372781457ebe940a2680ce2e6a5335", 321139),
			Common::EN_ANY,
			Common::kPlatformDOS,
			ADGF_TESTING,
			GUIO2(GUIO_NOMIDI, GUIO_NOSPEECH)
		},
		kGameOregonTrail
	},
	{ AD_TABLE_END_MARKER, kGameFoolsErrand }
};

} // End of namespace DosGames

DosGamesMetaEngineDetection::DosGamesMetaEngineDetection() : AdvancedMetaEngineDetection(DosGames::gameDescriptions, DosGames::dosGames) {
}

// The games inside .zip files: the same files, checked the same way
ADDetectedGame DosGamesMetaEngineDetection::fallbackDetect(const FileMap &allFiles, const Common::FSList &fslist, ADDetectedGameExtraInfo **extra) const {
	bool anyZip = false;
	for (const Common::FSNode &node : fslist)
		anyZip |= !node.isDirectory() && node.getFileName().hasSuffixIgnoreCase(".zip");
	if (!anyZip)
		return ADDetectedGame();

	DosGames::GameFiles files(fslist, true);
	for (const DosGames::DosGameDescription *g = DosGames::gameDescriptions; g->desc.gameId; g++) {
		bool match = true;
		for (const ADGameFileDescription *f = g->desc.filesDescriptions; match && f->fileName; f++) {
			Common::ScopedPtr<Common::SeekableReadStream> s(files.open(f->fileName));
			match = s && (f->fileSize == AD_NO_SIZE || s->size() == f->fileSize) &&
				(!f->md5 || Common::computeStreamMD5AsString(*s, 5000) == f->md5);
		}
		if (match)
			return ADDetectedGame(&g->desc);
	}
	return ADDetectedGame();
}

REGISTER_PLUGIN_STATIC(DOSGAMES_DETECTION, PLUGIN_TYPE_ENGINE_DETECTION, DosGamesMetaEngineDetection);
