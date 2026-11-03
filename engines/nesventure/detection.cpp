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
#include "common/file.h"
#include "nesventure/detection.h"

namespace NesVenture {

static const PlainGameDescriptor nesGames[] = {
	{ "shadowgatenes", "Shadowgate (NES)" },
	{ "dejavunes", "Deja Vu (NES)" },
	{ "uninvitednes", "Uninvited (NES)" },
	{ nullptr, nullptr }
};

// The USA cartridges, headered (iNES), named as No-Intro names them; any
// other name is found by contents (fallbackDetect)
static const NesGameDescription gameDescriptions[] = {
	{
		{
			"shadowgatenes", "",
			AD_ENTRY1s("Shadowgate (USA).nes", "620e64efdcb8cc350eef0448e3b9bbf0", 262160),
			Common::EN_USA, Common::kPlatformNES, ADGF_TESTING, GUIO1(GUIO_NOSPEECH)
		},
		kGameShadowgate
	},
	{
		{
			"dejavunes", "",
			AD_ENTRY1s("Deja Vu (USA).nes", "85956925e321516e87d9e0f5aba8d9c1", 393232),
			Common::EN_USA, Common::kPlatformNES, ADGF_TESTING, GUIO1(GUIO_NOSPEECH)
		},
		kGameDejaVu
	},
	{
		{
			"uninvitednes", "",
			AD_ENTRY1s("Uninvited (USA).nes", "16b6bbc8c8f81cb0f3aace45cdeeb09a", 262160),
			Common::EN_USA, Common::kPlatformNES, ADGF_TESTING, GUIO1(GUIO_NOSPEECH)
		},
		kGameUninvited
	},
	{ AD_TABLE_END_MARKER, kGameShadowgate }
};

} // End of namespace NesVenture

NesVentureMetaEngineDetection::NesVentureMetaEngineDetection() : AdvancedMetaEngineDetection(NesVenture::gameDescriptions, NesVenture::nesGames) {
}

// A cartridge under any file name: any .nes file with the same contents
ADDetectedGame NesVentureMetaEngineDetection::fallbackDetect(const FileMap &allFiles, const Common::FSList &fslist, ADDetectedGameExtraInfo **extra) const {
	for (const Common::FSNode &node : fslist) {
		if (node.isDirectory() || !node.getFileName().hasSuffixIgnoreCase(".nes"))
			continue;
		Common::ScopedPtr<Common::SeekableReadStream> s(node.createReadStream());
		if (!s)
			continue;
		const Common::String md5 = Common::computeStreamMD5AsString(*s, 5000);
		for (const NesVenture::NesGameDescription *g = NesVenture::gameDescriptions; g->desc.gameId; g++) {
			const ADGameFileDescription *f = g->desc.filesDescriptions;
			if (s->size() == f->fileSize && md5 == f->md5)
				return ADDetectedGame(&g->desc);
		}
	}
	return ADDetectedGame();
}

REGISTER_PLUGIN_STATIC(NESVENTURE_DETECTION, PLUGIN_TYPE_ENGINE_DETECTION, NesVentureMetaEngineDetection);
