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

// Near-Grog: loader for the Windows 3.1 versions, see winversion.h

#include "common/archive.h"
#include "common/debug.h"
#include "common/endian.h"
#include "common/file.h"
#include "common/memstream.h"
#include "common/textconsole.h"

#include "macventure/container.h"
#include "macventure/controls.h"
#include "macventure/debug.h"
#include "macventure/windows.h"
#include "macventure/winversion.h"

namespace MacVenture {

static const char *const kArchiveName = "macventure-windows";

// WinMemoryArchive

void WinMemoryArchive::add(const Common::Path &path, const Common::Array<byte> &data) {
	_files[path] = Common::SharedPtr<Common::Array<byte> >(new Common::Array<byte>(data));
}

bool WinMemoryArchive::hasFile(const Common::Path &path) const {
	return _files.contains(path);
}

int WinMemoryArchive::listMembers(Common::ArchiveMemberList &list) const {
	for (FileMap::const_iterator it = _files.begin(); it != _files.end(); ++it)
		list.push_back(Common::ArchiveMemberPtr(new Common::GenericArchiveMember(it->_key, *this)));
	return _files.size();
}

const Common::ArchiveMemberPtr WinMemoryArchive::getMember(const Common::Path &path) const {
	if (!hasFile(path))
		return Common::ArchiveMemberPtr();
	return Common::ArchiveMemberPtr(new Common::GenericArchiveMember(path, *this));
}

Common::SeekableReadStream *WinMemoryArchive::createReadStreamForMember(const Common::Path &path) const {
	if (!_files.contains(path))
		return nullptr;
	const Common::Array<byte> &data = *_files[path];
	byte *copy = (byte *)malloc(MAX<uint32>(data.size(), 1));
	if (data.size())
		memcpy(copy, &data[0], data.size());
	return new Common::MemoryReadStream(copy, data.size(), DisposeAfterUse::YES);
}

// Writers for big-endian Mac resource data

namespace {

class ByteWriter {
public:
	Common::Array<byte> data;

	void u8(uint v) { data.push_back(v & 0xff); }
	void u16(uint v) { u8(v >> 8); u8(v); }
	void u24(uint32 v) { u8(v >> 16); u8(v >> 8); u8(v); }
	void u32(uint32 v) { u16(v >> 16); u16(v); }
	void pstr(const Common::String &s) {
		uint len = MIN<uint>(s.size(), 255);
		u8(len);
		for (uint i = 0; i < len; i++)
			u8(s[i]);
	}
	void bytes(const Common::Array<byte> &b) {
		for (uint i = 0; i < b.size(); i++)
			data.push_back(b[i]);
	}
	void rect(int top, int left, int bottom, int right) {
		u16(top); u16(left); u16(bottom); u16(right);
	}
};

class ForkBuilder {
public:
	void add(uint32 tag, uint16 id, const Common::Array<byte> &data) {
		Res r;
		r.tag = tag;
		r.id = id;
		r.data = data;
		_res.push_back(r);
	}

	// Writes a raw resource fork, the format MacResManager reads from ".rsrc" files
	Common::Array<byte> build() const {
		Common::Array<uint32> tags;
		for (uint i = 0; i < _res.size(); i++) {
			bool found = false;
			for (uint t = 0; t < tags.size(); t++)
				found |= (tags[t] == _res[i].tag);
			if (!found)
				tags.push_back(_res[i].tag);
		}

		// Resource data
		ByteWriter dataPart;
		Common::Array<uint32> offsets;
		for (uint i = 0; i < _res.size(); i++) {
			offsets.push_back(dataPart.data.size());
			dataPart.u32(_res[i].data.size());
			dataPart.bytes(_res[i].data);
		}

		// Resource map
		ByteWriter typeList;
		ByteWriter refLists;
		uint typeListSize = 2 + tags.size() * 8;
		typeList.u16(tags.size() - 1);
		for (uint t = 0; t < tags.size(); t++) {
			uint count = 0;
			uint refStart = typeListSize + refLists.data.size();
			for (uint i = 0; i < _res.size(); i++) {
				if (_res[i].tag != tags[t])
					continue;
				count++;
				refLists.u16(_res[i].id);
				refLists.u16(0xffff); // no name
				refLists.u8(0);       // attributes
				refLists.u24(offsets[i]);
				refLists.u32(0);
			}
			typeList.u32(tags[t]);
			typeList.u16(count - 1);
			typeList.u16(refStart);
		}

		ByteWriter map;
		for (uint i = 0; i < 16 + 4 + 2 + 2; i++)
			map.u8(0);
		map.u16(28); // type list offset
		map.u16(28 + typeList.data.size() + refLists.data.size()); // name list offset (empty)
		map.bytes(typeList.data);
		map.bytes(refLists.data);

		ByteWriter fork;
		const uint32 dataOffset = 256;
		const uint32 mapOffset = dataOffset + dataPart.data.size();
		fork.u32(dataOffset);
		fork.u32(mapOffset);
		fork.u32(dataPart.data.size());
		fork.u32(map.data.size());
		while (fork.data.size() < dataOffset)
			fork.u8(0);
		fork.bytes(dataPart.data);
		fork.bytes(map.data);
		return fork.data;
	}

private:
	struct Res {
		uint32 tag;
		uint16 id;
		Common::Array<byte> data;
	};
	Common::Array<Res> _res;
};

Common::Array<byte> stringList(const Common::Array<Common::String> &strings) {
	ByteWriter w;
	w.u16(strings.size());
	for (uint i = 0; i < strings.size(); i++)
		w.pstr(strings[i]);
	return w.data;
}

Common::Array<byte> pascalString(const Common::String &s) {
	ByteWriter w;
	w.pstr(s);
	return w.data;
}

// WIND: top, left, bottom, right of the content, type, visible, close box, refcon, title
Common::Array<byte> windowRes(int left, int top, int width, int height, MVWindowType type, bool closeBox, const Common::String &title) {
	ByteWriter w;
	w.rect(top, left, top + height, left + width);
	w.u16(type);
	w.u16(1);
	w.u16(closeBox ? 1 : 0);
	w.u32(0);
	w.pstr(title);
	return w.data;
}

// CNTL: bounds, value, visible, unused, max, min, procID, refcon, title
Common::Array<byte> controlRes(int left, int top, int width, int height, ControlAction action, const Common::String &title) {
	ByteWriter w;
	w.rect(top, left, top + height, left + width);
	w.u16(0);
	w.u8(1);
	w.u8(0);
	w.u16(0);
	w.u16(0);
	w.u16(0);
	w.u32(action);
	w.pstr(title);
	return w.data;
}

} // End of anonymous namespace

// WinVersionLoader

WinVersionLoader::WinVersionLoader() : _archive(nullptr) {
	memset(_palette, 0, sizeof(_palette));
}

WinVersionLoader::~WinVersionLoader() {
	if (_archive)
		SearchMan.remove(kArchiveName); // frees the archive
}

bool WinVersionLoader::isTaggedDataFile(Common::SeekableReadStream &stream) {
	byte tag[4];
	if (stream.read(tag, 4) != 4)
		return false;
	static const char *const kTags[] = { "SHG1", "UNV1", "DVU1", "DVU2", nullptr };
	for (int i = 0; kTags[i]; i++) {
		if (!memcmp(tag, kTags[i], 4))
			return true;
	}
	return false;
}

bool WinVersionLoader::readNEResources() {
	if (_exe.size() < 0x40)
		return false;
	uint32 ne = READ_LE_UINT32(&_exe[0x3c]);
	if (ne + 0x40 > _exe.size() || _exe[ne] != 'N' || _exe[ne + 1] != 'E')
		return false;

	uint32 table = ne + READ_LE_UINT16(&_exe[ne + 0x24]);
	uint shift = READ_LE_UINT16(&_exe[table]);
	uint32 p = table + 2;
	while (p + 8 <= _exe.size()) {
		uint16 type = READ_LE_UINT16(&_exe[p]);
		if (type == 0)
			break;
		uint16 count = READ_LE_UINT16(&_exe[p + 2]);
		p += 8;

		Common::String typeName;
		if (!(type & 0x8000)) {
			uint32 n = table + type;
			typeName = Common::String((const char *)&_exe[n + 1], _exe[n]);
			type = 0;
		} else {
			type &= 0x7fff;
		}

		for (uint i = 0; i < count && p + 12 <= _exe.size(); i++, p += 12) {
			NEResource r;
			r.type = type;
			r.typeName = typeName;
			r.offset = (uint32)READ_LE_UINT16(&_exe[p]) << shift;
			r.length = (uint32)READ_LE_UINT16(&_exe[p + 2]) << shift;
			r.id = READ_LE_UINT16(&_exe[p + 6]) & 0x7fff;
			if (r.offset + r.length <= _exe.size())
				_resources.push_back(r);
		}
	}
	return !_resources.empty();
}

const WinVersionLoader::NEResource *WinVersionLoader::findResource(uint16 type, uint16 id) const {
	for (uint i = 0; i < _resources.size(); i++) {
		if (_resources[i].type == type && _resources[i].id == id)
			return &_resources[i];
	}
	return nullptr;
}

Common::String WinVersionLoader::getNEString(uint id) const {
	const NEResource *r = findResource(6, (id >> 4) + 1); // RT_STRING, 16 strings a block
	if (!r)
		return Common::String();
	uint32 p = r->offset;
	for (uint i = 0; i < 16; i++) {
		uint len = _exe[p];
		if (i == (id & 15))
			return Common::String((const char *)&_exe[p + 1], len);
		p += 1 + len;
	}
	return Common::String();
}

/**
 * The Mac GNRL 0x80 settings live in the program's data, little-endian
 * and reordered: argument counts and command flags for the 12 actions,
 * then numObjects, numGlobals, attribute indices, shifts and masks.
 */
bool WinVersionLoader::findSettings(Common::Array<byte> &gnrl) {
	static const byte kCommandRun[24] = {
		0x02, 0x01, 0x02, 0x01, 0x02, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x01,
		0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01
	};
	const uint kNumCommands = 12;

	int start = -1;
	for (uint32 i = 0; i + 24 + 4 + 40 * 4 < _exe.size(); i++) {
		if (!memcmp(&_exe[i], kCommandRun, 24)) {
			start = i;
			break;
		}
	}
	if (start < 0)
		return false;

	uint32 p = start + 24;
	uint16 numObjects = READ_LE_UINT16(&_exe[p]);
	uint16 numGlobals = READ_LE_UINT16(&_exe[p + 2]);
	p += 4;

	// The attribute count is not stored; take the largest count whose
	// masks agree with their shifts
	int numAttributes = -1;
	for (int n = 20; n < 40; n++) {
		const byte *idx = &_exe[p];
		const byte *shifts = &_exe[p + n];
		bool ok = idx[0] < 0x80 && !shifts[0] && !shifts[1] && !shifts[2] && !shifts[3];
		for (int i = 0; ok && i < n; i++) {
			uint16 mask = READ_LE_UINT16(&_exe[p + 2 * n + 2 * i]);
			if (!mask || shifts[i] > 15 || (uint16)((mask >> shifts[i]) << shifts[i]) != mask)
				ok = false;
		}
		if (ok)
			numAttributes = n;
	}
	if (numAttributes < 0)
		return false;

	int numGroups = 0;
	for (int i = 0; i < numAttributes; i++) {
		if (_exe[p + i] < 0x80)
			numGroups = MAX<int>(numGroups, _exe[p + i] + 1);
	}

	debugC(1, kMVDebugMain, "Windows settings: %d objects, %d globals, %d attributes, %d groups",
		numObjects, numGlobals, numAttributes, numGroups);

	ByteWriter w;
	w.u16(numObjects);
	w.u16(numGlobals);
	w.u16(kNumCommands);
	w.u16(numAttributes);
	w.u16(numGroups);
	w.u16(0);   // unknown
	w.u16(248); // inventory top
	w.u16(350); // inventory left
	w.u16(100); // inventory height
	w.u16(270); // inventory width
	w.u16(12);  // inventory offset y
	w.u16(12);  // inventory offset x
	w.u16(0);   // default font
	w.u16(12);  // default size
	for (int i = 0; i < numAttributes; i++)
		w.u8(_exe[p + i]);
	for (int i = 0; i < numAttributes; i++)
		w.u16(READ_LE_UINT16(&_exe[p + 2 * numAttributes + 2 * i]));
	for (int i = 0; i < numAttributes; i++)
		w.u8(_exe[p + numAttributes + i]);
	for (uint i = 0; i < kNumCommands; i++)
		w.u8(kCommandRun[i]);
	for (uint i = 0; i < kNumCommands; i++)
		w.u8(kCommandRun[kNumCommands + i]);
	gnrl = w.data;
	return true;
}

/**
 * The text Huffman table (Mac GNRL 0x83) follows the article strings in
 * the program: lower bounds as little-endian words starting at 0, then the
 * code lengths and the symbols, one byte each.
 */
bool WinVersionLoader::findTextHuffman(Common::Array<byte> &gnrl) {
	static const char kArticles[] = "\x04the \x02" "a \x03" "an \x02he\x03she\x02it";
	const uint32 len = sizeof(kArticles) - 1;
	int start = -1;
	for (uint32 i = 0; i + len + 64 < _exe.size(); i++) {
		if (!memcmp(&_exe[i], kArticles, len)) {
			start = i + len + 1; // after the empty string that ends the pronouns
			break;
		}
	}
	if (start < 0 || READ_LE_UINT16(&_exe[start]) != 0)
		return false;

	Common::Array<uint16> bounds;
	uint32 p = start;
	while (p + 2 <= _exe.size()) {
		uint16 v = READ_LE_UINT16(&_exe[p]);
		if (!bounds.empty() && v <= bounds.back())
			break;
		bounds.push_back(v);
		p += 2;
	}
	uint n = bounds.size();
	if (n < 4 || p + 2 * n > _exe.size())
		return false;

	ByteWriter w;
	w.u16(n);
	w.u16(0);
	for (uint i = 1; i < n; i++)
		w.u16(bounds[i]); // the Mac table holds the upper bounds
	for (uint i = 0; i < n; i++)
		w.u8(_exe[p + i]);
	for (uint i = 0; i < n; i++)
		w.u8(_exe[p + n + i]);
	gnrl = w.data;
	return true;
}

Common::Path WinVersionLoader::load(const Common::String &exeName) {
	Common::File exe;
	if (!exe.open(Common::Path(exeName))) {
		warning("WinVersionLoader: Could not open %s", exeName.c_str());
		return Common::Path();
	}
	_exe.resize(exe.size());
	exe.read(&_exe[0], _exe.size());
	exe.close();

	if (!readNEResources()) {
		warning("WinVersionLoader: %s is not a Windows program", exeName.c_str());
		return Common::Path();
	}

	_gameName = getNEString(23);
	_archive = new WinMemoryArchive();
	SearchMan.add(kArchiveName, _archive, 100, true);

	// The data files, without their tag
	static const uint kFileStrings[] = { 30, 31, 32, 33, 34, 35 }; // KGRAPHIC FILTER OBJECT TEXT GRAPHICS GAMEFILE
	Common::String names[6];
	for (uint i = 0; i < ARRAYSIZE(kFileStrings); i++) {
		names[i] = getNEString(kFileStrings[i]);
		Common::File f;
		if (names[i].empty() || !f.open(Common::Path(names[i]))) {
			warning("WinVersionLoader: Could not open data file '%s'", names[i].c_str());
			return Common::Path();
		}
		if (!isTaggedDataFile(f)) {
			warning("WinVersionLoader: '%s' is not a MacVenture data file", names[i].c_str());
			return Common::Path();
		}
		Common::Array<byte> data;
		data.resize(f.size() - 4);
		f.read(&data[0], data.size());
		names[i] = "mvwin-" + names[i];
		_archive->add(Common::Path(names[i]), data);
	}
	const Common::String &kgraphic = names[0], &filter = names[1], &object = names[2];
	const Common::String &text = names[3], &graphics = names[4], &gamefile = names[5];

	// Sounds: IMSOUND resources 1000 + sound id
	for (uint i = 0; i < _resources.size(); i++) {
		const NEResource &r = _resources[i];
		if (r.type || !r.typeName.equalsIgnoreCase("IMSOUND") || r.id < 1000)
			continue;
		Common::Array<byte> data(&_exe[r.offset], r.length);
		_archive->add(Common::Path(Common::String::format("mvwin-sound-%d", r.id - 1000)), data);
	}

	// Palette (RCDATA 1002: a LOGPALETTE)
	const NEResource *pal = findResource(10, 1002);
	if (pal && pal->length >= 4 + 256 * 4) {
		for (int i = 0; i < 256; i++) {
			_palette[i * 3 + 0] = _exe[pal->offset + 4 + i * 4 + 0];
			_palette[i * 3 + 1] = _exe[pal->offset + 4 + i * 4 + 1];
			_palette[i * 3 + 2] = _exe[pal->offset + 4 + i * 4 + 2];
		}
	} else {
		warning("WinVersionLoader: No palette found");
	}

	// The title picture and the diploma are in KGRAPHIC, served as PPIC 0
	// of their own resource forks, as the Mac versions have them
	Common::String titleName = "mvwin-title", diplomaName = "mvwin-diploma";
	{
		Container kg{Common::Path(kgraphic)};
		static const uint kItems[2] = { 2, 4 };
		const Common::String *forkNames[2] = { &titleName, &diplomaName };
		for (int i = 0; i < 2; i++) {
			uint32 size = kg.getItemByteSize(kItems[i]);
			Common::SeekableReadStream *s = kg.getItem(kItems[i]);
			Common::Array<byte> data;
			data.resize(size);
			if (size)
				s->read(&data[0], size);
			delete s;
			ForkBuilder fork;
			fork.add(MKTAG('P', 'P', 'I', 'C'), 0, data);
			_archive->add(Common::Path(*forkNames[i] + ".rsrc"), fork.build());
		}
	}

	ForkBuilder fork;
	Common::Array<byte> gnrl;
	if (!findSettings(gnrl)) {
		warning("WinVersionLoader: Could not find the game settings in %s", exeName.c_str());
		return Common::Path();
	}
	fork.add(MKTAG('G', 'N', 'R', 'L'), 0x80, gnrl);
	if (findTextHuffman(gnrl))
		fork.add(MKTAG('G', 'N', 'R', 'L'), 0x83, gnrl);
	else
		warning("WinVersionLoader: No text Huffman table found");

	{
		// Diploma name: font, size, top, left, bottom, right
		ByteWriter w;
		w.u16(0);
		w.u16(12);
		w.rect(146, 107, 162, 395);
		fork.add(MKTAG('G', 'N', 'R', 'L'), 0x81, w.data);
	}

	// File names
	Common::Array<Common::String> files;
	files.push_back(_gameName);
	files.push_back(titleName);
	files.push_back("mvwin-no-subdirectory");
	files.push_back(object);
	files.push_back(filter);
	files.push_back(text);
	files.push_back(graphics);
	files.push_back("mvwin-sound"); // the sounds are served as "mvwin-sound-<id>"
	fork.add(MKTAG('S', 'T', 'R', '#'), 0x81, stringList(files));

	// Articles; the program's tables are "", "the ", "a ", "an " and "he", "she", "it", ""
	Common::Array<Common::String> articles;
	articles.push_back("the ");
	articles.push_back("a ");
	articles.push_back("an ");
	fork.add(MKTAG('S', 'T', 'R', '#'), 0x82, stringList(articles));
	articles.insert_at(0, "");
	fork.add(MKTAG('S', 'T', 'R', '#'), 0x83, stringList(articles));
	Common::Array<Common::String> pronouns;
	pronouns.push_back("he");
	pronouns.push_back("she");
	pronouns.push_back("it");
	pronouns.push_back("");
	fork.add(MKTAG('S', 'T', 'R', '#'), 0x84, stringList(pronouns));

	fork.add(MKTAG('S', 'T', 'R', ' '), 0x82, pascalString(getNEString(20)));
	fork.add(MKTAG('S', 'T', 'R', ' '), 0x83, pascalString(diplomaName));
	fork.add(MKTAG('S', 'T', 'R', ' '), 0x84, pascalString(getNEString(255)));
	fork.add(MKTAG('S', 'T', 'R', ' '), 0x85, pascalString(gamefile));

	// Windows, in a 640x480 layout. The order sets their references:
	// commands, main game, console, self, exits, diploma.
	fork.add(MKTAG('W', 'I', 'N', 'D'), 0x80, windowRes(344, 28, 288, 64, kPlainDBox, false, ""));
	fork.add(MKTAG('W', 'I', 'N', 'D'), 0x81, windowRes(10, 44, 320, 200, kNoGrowDoc, false, ""));
	fork.add(MKTAG('W', 'I', 'N', 'D'), 0x82, windowRes(10, 372, 605, 100, kZoomDoc, false, "Untitled"));
	fork.add(MKTAG('W', 'I', 'N', 'D'), 0x83, windowRes(344, 104, 112, 72, kPlainDBox, false, ""));
	fork.add(MKTAG('W', 'I', 'N', 'D'), 0x84, windowRes(520, 124, 96, 96, kRDoc4, false, "Exits"));
	fork.add(MKTAG('W', 'I', 'N', 'D'), 0x85, windowRes(62, 60, 516, 278, kPlainDBox, false, ""));

	// Command buttons, in ControlType order after the exit box
	fork.add(MKTAG('C', 'N', 'T', 'L'), 0x80, controlRes(0, 0, 0, 0, kActivateObject, ""));
	static const struct {
		ControlAction action;
		uint stringId;
	} kButtons[8] = {
		{ kExamine, 263 }, { kOpen, 266 }, { kClose, 258 }, { kSpeak, 268 },
		{ kOperate, 267 }, { kGo, 264 }, { kHit, 265 }, { kConsume, 262 }
	};
	for (int i = 0; i < 8; i++) {
		fork.add(MKTAG('C', 'N', 'T', 'L'), 0x81 + i,
			controlRes(4 + (i % 4) * 70, 4 + (i / 4) * 30, 66, 26, kButtons[i].action, getNEString(kButtons[i].stringId)));
	}

	// Diploma dialog
	{
		ByteWriter w;
		w.rect(352, 160, 452, 480);
		w.u16(1);
		w.u8(1); w.u8(0);
		w.u8(0); w.u8(0);
		w.u32(0);
		w.u16(0x87);
		w.pstr("");
		fork.add(MKTAG('D', 'L', 'O', 'G'), 0x87, w.data);

		ByteWriter items;
		items.u16(4 - 1);
		struct DialogItem {
			int top, left, bottom, right, type;
			const char *title;
		} kItems[4] = {
			{ 10, 10, 26, 310, 8, "Enter your name:" },
			{ 32, 10, 50, 310, 0x10, "" },
			{ 64, 60, 84, 140, 4, "Print" },
			{ 64, 180, 84, 260, 4, "Quit" }
		};
		for (int i = 0; i < 4; i++) {
			items.u32(0);
			items.rect(kItems[i].top, kItems[i].left, kItems[i].bottom, kItems[i].right);
			items.u8(kItems[i].type);
			items.pstr(kItems[i].title);
			if (strlen(kItems[i].title) & 1)
				items.u8(0);
		}
		fork.add(MKTAG('D', 'I', 'T', 'L'), 0x87, items.data);
	}

	Common::String forkName = "mvwin-" + exeName;
	_archive->add(Common::Path(forkName + ".rsrc"), fork.build());

	return Common::Path(forkName);
}

} // End of namespace MacVenture
