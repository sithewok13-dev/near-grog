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

#ifndef DOSGAMES_GAMEFILES_H
#define DOSGAMES_GAMEFILES_H

#include "common/archive.h"
#include "common/array.h"
#include "common/compression/unzip.h"
#include "common/fs.h"
#include "common/hash-str.h"
#include "common/hashmap.h"
#include "common/ptr.h"
#include "common/str-array.h"
#include "common/stream.h"

namespace DosGames {

/**
 * A game's files, loose in its folder or inside .zip files there (in any
 * folder of the zip). Nothing is unpacked up front: a file is read out of
 * its zip when the game asks for it. Zips over 50 MB are left for the
 * player to unzip, since a big zip is slow to read files from. Loose files
 * win over zipped ones of the same name.
 */
class GameFiles {
public:
	enum {
		kMaxZipSize = 50 * 1024 * 1024
	};

	/** bigZips: also use zips over the limit (detection only, to say so). */
	explicit GameFiles(const Common::FSList &folder, bool bigZips = false) : _bigZips(bigZips) {
		for (const Common::FSNode &node : folder) {
			if (node.isDirectory())
				continue;
			Common::String name = node.getFileName();
			if (name.hasSuffixIgnoreCase(".zip"))
				addZip(node);
		}
		for (const Common::FSNode &node : folder) {
			if (node.isDirectory())
				continue;
			Entry e;
			e.node = node;
			_files[node.getFileName()] = e;
		}
	}

	explicit GameFiles(const Common::FSNode &dir, bool bigZips = false) : GameFiles(list(dir), bigZips) {}

	~GameFiles() {
		for (Common::Archive *zip : _zips)
			delete zip;
	}

	bool has(const Common::String &name) const {
		return _files.contains(name);
	}

	/** A file's contents, or nullptr. */
	Common::SeekableReadStream *open(const Common::String &name) const {
		if (!_files.contains(name))
			return nullptr;
		const Entry &e = _files[name];
		return e.member ? e.member->createReadStream() : e.node.createReadStream();
	}

	bool read(const Common::String &name, Common::Array<byte> &data) const {
		Common::ScopedPtr<Common::SeekableReadStream> s(open(name));
		if (!s)
			return false;
		data.resize(s->size());
		s->read(data.data(), data.size());
		return true;
	}

	/** Every file's name, upper case. */
	Common::StringArray names() const {
		Common::StringArray all;
		for (FileMap::const_iterator i = _files.begin(); i != _files.end(); ++i) {
			Common::String n = i->_key;
			n.toUppercase();
			all.push_back(n);
		}
		return all;
	}

	/** A zip that was too big to use, if there was one. */
	const Common::String &tooBigZip() const { return _tooBig; }

private:
	struct Entry {
		Common::FSNode node;
		Common::ArchiveMemberPtr member;
	};
	typedef Common::HashMap<Common::String, Entry, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> FileMap;

	FileMap _files;
	Common::Array<Common::Archive *> _zips;
	Common::String _tooBig;
	bool _bigZips;

	static Common::FSList list(const Common::FSNode &dir) {
		Common::FSList files;
		dir.getChildren(files, Common::FSNode::kListFilesOnly);
		return files;
	}

	void addZip(const Common::FSNode &node) {
		{
			Common::ScopedPtr<Common::SeekableReadStream> s(node.createReadStream());
			if (!s)
				return;
			if (s->size() > kMaxZipSize) {
				_tooBig = node.getFileName();
				if (!_bigZips)
					return;
			}
		}
		Common::Archive *zip = Common::makeZipArchive(node, true);
		if (!zip)
			return;
		_zips.push_back(zip);
		Common::ArchiveMemberList members;
		zip->listMembers(members);
		for (const Common::ArchiveMemberPtr &m : members) {
			if (m->isDirectory())
				continue;
			Common::String name = m->getFileName();
			if (_files.contains(name))
				continue;
			Entry e;
			e.member = m;
			_files[name] = e;
		}
	}
};

} // End of namespace DosGames

#endif
