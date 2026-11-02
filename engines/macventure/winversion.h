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

/*
 * Near-Grog: support for the Windows 3.1 versions (1993) of the MacVenture
 * games. Their data files are the Mac containers with a 4-byte tag in front,
 * and the settings the Mac version keeps in its resource fork live in the
 * Windows program. At start we build a Mac-style resource fork in memory
 * from the program, and serve the data files under Mac-like names with the
 * tag removed, so the rest of the engine runs unchanged.
 */

#ifndef MACVENTURE_WINVERSION_H
#define MACVENTURE_WINVERSION_H

#include "common/archive.h"
#include "common/array.h"
#include "common/hashmap.h"
#include "common/path.h"
#include "common/ptr.h"
#include "common/str.h"

namespace MacVenture {

enum {
	kWinScreenWidth = 640,
	kWinScreenHeight = 480
};

/**
 * An archive of in-memory files.
 */
class WinMemoryArchive : public Common::Archive {
public:
	void add(const Common::Path &path, const Common::Array<byte> &data);

	bool hasFile(const Common::Path &path) const override;
	int listMembers(Common::ArchiveMemberList &list) const override;
	const Common::ArchiveMemberPtr getMember(const Common::Path &path) const override;
	Common::SeekableReadStream *createReadStreamForMember(const Common::Path &path) const override;

private:
	typedef Common::HashMap<Common::Path, Common::SharedPtr<Common::Array<byte> >, Common::Path::IgnoreCase_Hash, Common::Path::IgnoreCase_EqualTo> FileMap;
	FileMap _files;
};

/**
 * Loads a Windows version: reads the program and data files, and registers
 * an archive in SearchMan that holds the made-up resource fork and the
 * untagged data files.
 */
class WinVersionLoader {
public:
	WinVersionLoader();
	~WinVersionLoader();

	/**
	 * @param exeName	the Windows program, e.g. "SHADOW.EXE"
	 * @return the name to open with MacResManager, or an empty path on failure
	 */
	Common::Path load(const Common::String &exeName);

	/** The game's 256-colour palette (RGB triplets). */
	const byte *getPalette() const { return _palette; }

	/** Is a data file tagged as a Windows MacVenture file? (for detection) */
	static bool isTaggedDataFile(Common::SeekableReadStream &stream);

private:
	struct NEResource {
		uint16 type;      // numeric type, or 0 for named types
		Common::String typeName;
		uint16 id;
		uint32 offset;
		uint32 length;
	};

	bool readNEResources();
	const NEResource *findResource(uint16 type, uint16 id) const;
	Common::String getNEString(uint id) const;

	bool findSettings(Common::Array<byte> &gnrl);
	bool findTextHuffman(Common::Array<byte> &gnrl);

	Common::Array<byte> _exe;
	Common::Array<NEResource> _resources;
	Common::String _prefix;
	byte _palette[256 * 3];
	WinMemoryArchive *_archive;
	Common::String _gameName;
};

} // End of namespace MacVenture

#endif
