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

#ifndef DARKLORD_DISKS_H
#define DARKLORD_DISKS_H

#include "common/archive.h"
#include "common/array.h"
#include "common/compression/unzip.h"
#include "common/fs.h"
#include "common/ptr.h"
#include "common/stream.h"

namespace DarkLord {

/**
 * Finds the two Dark Lord disk sides among a folder's files, whatever they
 * are called, either loose or inside .zip files. Sides are told apart by
 * their directories: side 1 holds the boot program "DL" and "B1", side 2
 * holds the location files "P1" to "P92".
 *
 * Shared by detection and the engine, so it is all inline.
 */
class DiskFinder {
public:
	enum {
		kD64Size = 174848,
		kD64SizeWithErrors = 175531,
		kMaxZipSize = 8 * 1024 * 1024
	};

	Common::Array<byte> side[2];

	bool found() const { return !side[0].empty() && !side[1].empty(); }

	void scan(const Common::FSList &files) {
		for (const Common::FSNode &node : files) {
			if (found())
				return;
			if (node.isDirectory())
				continue;
			Common::String name = node.getFileName();
			if (name.hasSuffixIgnoreCase(".d64")) {
				Common::ScopedPtr<Common::SeekableReadStream> s(node.createReadStream());
				if (s)
					offer(*s);
			} else if (name.hasSuffixIgnoreCase(".zip")) {
				scanZip(node);
			}
		}
	}

	/** Returns 1 or 2 for a Dark Lord disk side, 0 for anything else. */
	static int sideOf(const Common::Array<byte> &img) {
		bool dl = false, b1 = false, p1 = false, p92 = false;
		int t = 18, s = 1, guard = 0;
		while (t >= 1 && t <= 35 && guard++ < 32) {
			int off = sectorOffset(t, s);
			if (off < 0 || off + 256 > (int)img.size())
				break;
			const byte *dir = &img[off];
			for (int i = 0; i < 8; i++) {
				const byte *e = dir + i * 32;
				if ((e[2] & 7) == 0)
					continue;
				Common::String name;
				for (int k = 0; k < 16 && e[5 + k] != 0xA0; k++)
					name += (char)e[5 + k];
				dl |= name == "DL";
				b1 |= name == "B1";
				p1 |= name == "P1";
				p92 |= name == "P92";
			}
			t = dir[0];
			s = dir[1];
		}
		if (dl && b1)
			return 1;
		if (p1 && p92)
			return 2;
		return 0;
	}

private:
	static int sectorOffset(int track, int sector) {
		static const int kSectors[35] = {
			21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
			19, 19, 19, 19, 19, 19, 19, 18, 18, 18, 18, 18, 18, 17, 17, 17, 17, 17
		};
		if (sector >= kSectors[track - 1])
			return -1;
		int o = 0;
		for (int i = 0; i < track - 1; i++)
			o += kSectors[i];
		return (o + sector) * 256;
	}

	void offer(Common::SeekableReadStream &s) {
		int64 size = s.size();
		if (size != kD64Size && size != kD64SizeWithErrors)
			return;
		Common::Array<byte> img(kD64Size);
		if (s.read(img.data(), kD64Size) != kD64Size)
			return;
		int n = sideOf(img);
		if (n && side[n - 1].empty())
			side[n - 1] = img;
	}

	void scanZip(const Common::FSNode &node) {
		{
			Common::ScopedPtr<Common::SeekableReadStream> s(node.createReadStream());
			if (!s || s->size() > kMaxZipSize)
				return;
		}
		Common::ScopedPtr<Common::Archive> zip(Common::makeZipArchive(node, true));
		if (!zip)
			return;
		Common::ArchiveMemberList members;
		zip->listMembers(members);
		for (const Common::ArchiveMemberPtr &m : members) {
			if (found())
				return;
			if (!m->getFileName().hasSuffixIgnoreCase(".d64"))
				continue;
			Common::ScopedPtr<Common::SeekableReadStream> s(m->createReadStream());
			if (s)
				offer(*s);
		}
	}
};

} // End of namespace DarkLord

#endif // DARKLORD_DISKS_H
