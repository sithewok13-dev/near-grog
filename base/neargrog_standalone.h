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

#ifndef BASE_NEARGROG_STANDALONE_H
#define BASE_NEARGROG_STANDALONE_H

#include "base/neargrog_build.h"

#ifdef NEARGROG_STANDALONE

#include "common/compression/unzip.h"
#include "common/config-manager.h"
#include "common/fs.h"
#include "gui/message.h"

/**
 * Near-Grog's standalone app, The Oregon Trail (Susy Edition): no launcher.
 * It finds the game's files where the player put them - the app's own
 * folder in the Files app, loose or zipped, at most a few folders down -
 * and starts it straight away, without the "testing" warning; when the game
 * ends, it starts again. The files are the player's: none come with the app.
 */
namespace NearGrog {

static const char *const kStandaloneTarget = "oregontrail";

inline bool findOregonTrail(const Common::FSNode &dir, Common::FSNode &found, int depth) {
	Common::FSList items;
	if (!dir.getChildren(items, Common::FSNode::kListAll))
		return false;
	for (const Common::FSNode &n : items) {
		if (n.isDirectory())
			continue;
		const Common::String name = n.getName();
		if (name.equalsIgnoreCase("OREGON.EXE")) {
			found = dir;
			return true;
		}
		if (name.hasSuffixIgnoreCase(".zip")) {
			Common::Archive *zip = Common::makeZipArchive(n, true);
			const bool has = zip && zip->hasFile(Common::Path("OREGON.EXE"));
			delete zip;
			if (has) {
				found = dir;
				return true;
			}
		}
	}
	if (depth > 0)
		for (const Common::FSNode &n : items)
			if (n.isDirectory() && !n.getName().hasPrefix(".") && findOregonTrail(n, found, depth - 1))
				return true;
	return false;
}

/** In place of the launcher: the game set up and chosen. */
inline bool standaloneStart() {
	ConfMan.setBool("enable_unsupported_game_warning", false);
	while (true) {
		Common::FSNode dir;
		if (findOregonTrail(Common::FSNode(Common::Path("/")), dir, 3)) {
			if (!ConfMan.hasGameDomain(kStandaloneTarget))
				ConfMan.addGameDomain(kStandaloneTarget);
			ConfMan.set("gameid", "oregontrail", kStandaloneTarget);
			ConfMan.set("engineid", "dosgames", kStandaloneTarget);
			ConfMan.set("description", "The Oregon Trail (Susy Edition)", kStandaloneTarget);
			ConfMan.setPath("path", dir.getPath(), kStandaloneTarget);
			ConfMan.set("neargrog_edition", "Susy Edition", kStandaloneTarget);
			ConfMan.flushToDisk();
			ConfMan.setActiveDomain(kStandaloneTarget);
			return true;
		}
		GUI::MessageDialog dialog(Common::U32String(
			"The Oregon Trail's files aren't here yet.\n\n"
			"In the Files app, copy them (OREGON.EXE and the rest, or their zip) "
			"into On My iPhone > Oregon Trail, then tap OK."));
		dialog.runModal();
	}
}

} // End of namespace NearGrog

#endif // NEARGROG_STANDALONE

#endif
