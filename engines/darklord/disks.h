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
 * Turns a G64 or NIB image (the raw magnetic data of a 1541 disk, as kept
 * for copy-protected originals) into a D64 image of its standard sectors.
 * Sectors the copy protection made unreadable stay zero; the engine never
 * needs them.
 */
class GcrDecoder {
public:
	static bool toD64(const Common::Array<byte> &raw, Common::Array<byte> &d64) {
		d64.clear();
		d64.resize(174848);
		bool any = false;
		if (raw.size() > 12 && !memcmp(raw.data(), "GCR-1541", 8)) {
			int halfTracks = raw[9];
			for (int track = 1; track <= 35 && (track - 1) * 2 < halfTracks; track++) {
				uint32 p = 12 + (track - 1) * 2 * 4;
				if (p + 4 > raw.size())
					break;
				uint32 off = raw[p] | (raw[p + 1] << 8) | (raw[p + 2] << 16) | ((uint32)raw[p + 3] << 24);
				if (!off || off + 2 > raw.size())
					continue;
				uint len = raw[off] | (raw[off + 1] << 8);
				if (off + 2 + len > raw.size())
					continue;
				any |= decodeTrack(&raw[off + 2], len, track, d64);
			}
		} else if (raw.size() > 0x100 && !memcmp(raw.data(), "MNIB-1541-RAW", 13)) {
			bool done[36] = {};
			for (uint i = 0x10, n = 0; i + 1 < 0x100 && raw[i]; i += 2, n++) {
				int track = raw[i] / 2;
				uint32 off = 0x100 + n * 0x2000;
				if ((raw[i] & 1) || track < 1 || track > 35 || done[track] || off + 0x2000 > raw.size())
					continue;
				done[track] = true;
				any |= decodeTrack(&raw[off], 0x2000, track, d64);
			}
		}
		return any;
	}

private:
	static int sectorsOn(int track) {
		return track <= 17 ? 21 : track <= 24 ? 19 : track <= 30 ? 18 : 17;
	}

	static bool decodeTrack(const byte *data, uint len, int track, Common::Array<byte> &d64) {
		static const int8 kGcr[32] = {
			-1, -1, -1, -1, -1, -1, -1, -1, -1, 8, 0, 1, -1, 12, 4, 5,
			-1, -1, 2, 3, -1, 15, 6, 7, -1, 9, 10, 11, -1, 13, 14, -1
		};
		// The track is a loop: read it twice so blocks across the end decode
		const uint bits = len * 8;
		auto bit = [&](uint i) { i %= bits; return (data[i >> 3] >> (7 - (i & 7))) & 1; };
		auto nibbleAt = [&](uint i) {
			int v = 0;
			for (int k = 0; k < 5; k++)
				v = (v << 1) | bit(i + k);
			return kGcr[v];
		};
		// Decodes n bytes starting at bit i; false if any group is invalid
		auto bytesAt = [&](uint i, byte *out, int n) {
			for (int b = 0; b < n; b++) {
				int hi = nibbleAt(i + b * 10), lo = nibbleAt(i + b * 10 + 5);
				if (hi < 0 || lo < 0)
					return false;
				out[b] = (hi << 4) | lo;
			}
			return true;
		};

		int base = 0;
		for (int t = 1; t < track; t++)
			base += sectorsOn(t);
		bool got[21] = {};
		bool any = false;
		int sector = -1;
		uint i = 0, ones = 0;
		while (i < bits * 2) {
			if (bit(i)) {
				ones++;
				i++;
				continue;
			}
			if (ones < 10) {
				ones = 0;
				i++;
				continue;
			}
			ones = 0;
			// A sync mark ends here: what follows is a header or a data block
			byte head[8];
			if (bytesAt(i, head, 6) && head[0] == 0x08) {
				sector = head[3] == track ? head[2] : -1;
			} else if (sector >= 0 && sector < sectorsOn(track) && !got[sector]) {
				byte block[258];
				if (bytesAt(i, block, 258) && block[0] == 0x07) {
					byte sum = 0;
					for (int k = 1; k <= 256; k++)
						sum ^= block[k];
					if (sum == block[257]) {
						memcpy(&d64[(base + sector) * 256], block + 1, 256);
						got[sector] = any = true;
					}
				}
				sector = -1;
			}
			i += 10;
		}
		return any;
	}
};

/**
 * Finds the two Dark Lord disk sides among a folder's files, whatever they
 * are called, either loose or inside .zip files, as D64 images or as G64
 * or NIB images of the original disks. Sides are told apart by their
 * directories: side 1 holds "B1" and a boot program ("DL" on the cracked
 * release, "OS64" on the original), side 2 the location files "P1" to "P92".
 *
 * Shared by detection and the engine, so it is all inline.
 */
class DiskFinder {
public:
	enum {
		kD64Size = 174848,
		kD64SizeWithErrors = 175531,
		kMaxZipSize = 8 * 1024 * 1024,
		kMaxGcrSize = 1024 * 1024
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
			if (isDiskName(name)) {
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
		bool dl = false, os64 = false, b1 = false, p1 = false, p92 = false;
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
				os64 |= name == "OS64";
				b1 |= name == "B1";
				p1 |= name == "P1";
				p92 |= name == "P92";
			}
			t = dir[0];
			s = dir[1];
		}
		if ((dl || os64) && b1)
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

	static bool isDiskName(const Common::String &name) {
		return name.hasSuffixIgnoreCase(".d64") || name.hasSuffixIgnoreCase(".g64") ||
			name.hasSuffixIgnoreCase(".nib");
	}

	void offer(Common::SeekableReadStream &s) {
		int64 size = s.size();
		Common::Array<byte> img;
		if (size == kD64Size || size == kD64SizeWithErrors) {
			img.resize(kD64Size);
			if (s.read(img.data(), kD64Size) != kD64Size)
				return;
		} else if (size > 0 && size <= kMaxGcrSize) {
			Common::Array<byte> raw(size);
			if (s.read(raw.data(), size) != (uint32)size || !GcrDecoder::toD64(raw, img))
				return;
		} else {
			return;
		}
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
			if (!isDiskName(m->getFileName()))
				continue;
			Common::ScopedPtr<Common::SeekableReadStream> s(m->createReadStream());
			if (s)
				offer(*s);
		}
	}
};

} // End of namespace DarkLord

#endif // DARKLORD_DISKS_H
