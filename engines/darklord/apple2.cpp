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

#include "common/debug.h"
#include "common/endian.h"
#include "common/stream.h"
#include "darklord/apple2.h"

namespace DarkLord {

enum {
	kDiskSize = 35 * 16 * 256,
	kTextRows = 24
};

// Where DOS 3.3 order puts each logical sector on the disk surface
static const int kSkew[16] = { 0, 13, 11, 9, 7, 5, 3, 1, 14, 12, 10, 8, 6, 4, 2, 15 };

// 6-and-2 disk bytes
static const byte kWriteTable[64] = {
	0x96, 0x97, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F, 0xA6, 0xA7, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB2, 0xB3,
	0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xCB, 0xCD, 0xCE, 0xCF, 0xD3,
	0xD6, 0xD7, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE5, 0xE6, 0xE7, 0xE9, 0xEA, 0xEB, 0xEC,
	0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF
};

static const byte kPalette[16 * 3] = {
	0x00, 0x00, 0x00, 0x90, 0x17, 0x40, 0x40, 0x1C, 0xF7, 0xD7, 0x34, 0xFD,
	0x00, 0x5D, 0x40, 0x80, 0x80, 0x80, 0x24, 0x97, 0xFF, 0xBE, 0xB7, 0xFF,
	0x40, 0x63, 0x00, 0xE6, 0x6F, 0x00, 0x80, 0x80, 0x80, 0xFF, 0x94, 0xBC,
	0x19, 0xD7, 0x00, 0xBF, 0xE3, 0x08, 0x58, 0xF4, 0xBF, 0xFF, 0xFF, 0xFF
};

enum {
	kBlack = 0, kViolet = 3, kBlue = 6, kOrange = 9, kGreen = 12, kWhite = 15
};

static uint16 textRow(int row) {
	return (row & 7) * 0x80 + (row >> 3) * 40;
}

Apple2::Apple2(Apple2Host *host) : _host(host), _cpu(this), _lcRead(false), _lcWrite(false),
		_lcBank2(true), _lcPrewrite(0), _text(true), _mixed(false), _page2(false), _hires(false),
		_key(0), _keyDelay(0), _speaker(false), _level(0), _dc(0), _frame(0), _frameCycles(0),
		_side(0), _halfTrack(0), _phases(0), _motor(false), _q6(false), _q7(false), _spinDown(0), _byteReady(false), _latch(0),
		_nibbleTrack(-1), _pos(0), _trackDirty(false), _promptShown(false) {
	memset(_ram, 0, sizeof(_ram));
	memset(_lcBank, 0, sizeof(_lcBank));
	memset(_lcHigh, 0, sizeof(_lcHigh));
	memset(_font, 0, sizeof(_font));
	// The stand-in ROM: RTS everywhere, plus the bytes programs look at to
	// recognise an Apple II+ with Applesoft
	memset(_rom, 0x60, sizeof(_rom));
	_rom[0xE000 - 0xD000] = 0x4C;     // Applesoft cold start is a JMP
	_rom[0xFBB3 - 0xD000] = 0xEA;     // Apple II+ ID byte
	_rom[0xFBC0 - 0xD000] = 0xEA;
	_rom[0xF457 - 0xD000] = 0x20;     // first bytes of Applesoft's HPLOT
	_rom[0xF53A - 0xD000] = 0x48;     // and HLIN, which the game checks
	_rom[0xFFFC - 0xD000] = 0x62;     // RESET -> $FA62
	_rom[0xFFFD - 0xD000] = 0xFA;
}

const byte *Apple2::palette() {
	return kPalette;
}

void Apple2::setFont(const byte *glyphs) {
	memcpy(_font, glyphs, sizeof(_font));
}

bool Apple2::addDisk(const Common::Array<byte> &dsk) {
	if (dsk.size() < kDiskSize)
		return false;
	Common::Array<byte> side(dsk.data(), kDiskSize);
	_sides.push_back(side);
	return true;
}

bool Apple2::boot() {
	if (_sides.empty())
		return false;
	// What the Disk II boot ROM does: read track 0 sector 0 to $0800 and
	// start it with the slot number times 16 in X
	readSector(_side, 0, 0, &_ram[0x0800]);
	_ram[0x26] = 0x00;
	_ram[0x27] = 0x09;
	_ram[0x2B] = 0x60;
	_ram[0x3D] = 0x01;
	_cpu.reset(0x0801);
	_cpu.x = 0x60;
	return true;
}

int Apple2::physicalToLogical(int p) {
	for (int l = 0; l < 16; l++)
		if (kSkew[l] == p)
			return l;
	return 0;
}

void Apple2::readSector(int side, int track, int logical, byte *out) {
	Common::Array<byte> data;
	if (_host && _host->diskLoad(Common::String::format("A%dT%02dS%02d", side + 1, track, logical), data) && data.size() == 256) {
		memcpy(out, data.data(), 256);
		return;
	}
	memcpy(out, &_sides[side][(track * 16 + logical) * 256], 256);
}

void Apple2::writeSector(int side, int track, int logical, const byte *in) {
	byte current[256];
	readSector(side, track, logical, current);
	if (!memcmp(current, in, 256))
		return;
	Common::Array<byte> data(in, 256);
	if (_host)
		_host->diskSave(Common::String::format("A%dT%02dS%02d", side + 1, track, logical), data);
	debug(1, "DarkLord: Apple write side %d track %d sector %d", side + 1, track, logical);
}

// Lays out one track as the bytes a Disk II reads from a standard 16-sector
// disk: address and data fields with 4-and-4 and 6-and-2 encoding
void Apple2::loadTrack() {
	int track = _halfTrack / 2;
	_nibbles.clear();
	auto put = [&](byte b) { _nibbles.push_back(b); };
	auto put44 = [&](byte b) { put((b >> 1) | 0xAA); put(b | 0xAA); };
	const byte volume = 254;
	for (int i = 0; i < 48; i++)
		put(0xFF);
	for (int p = 0; p < 16; p++) {
		byte data[256];
		readSector(_side, track, physicalToLogical(p), data);
		put(0xD5); put(0xAA); put(0x96);
		put44(volume); put44(track); put44(p); put44(volume ^ track ^ p);
		put(0xDE); put(0xAA); put(0xEB);
		for (int i = 0; i < 6; i++)
			put(0xFF);
		put(0xD5); put(0xAA); put(0xAD);
		byte buf[342];
		memset(buf, 0, 86);
		for (int j = 0; j < 256; j++) {
			int v = data[j] & 3;
			buf[j % 86] |= (((v & 1) << 1) | (v >> 1)) << ((j / 86) * 2);
			buf[86 + j] = data[j] >> 2;
		}
		byte prev = 0;
		for (int i = 0; i < 342; i++) {
			put(kWriteTable[buf[i] ^ prev]);
			prev = buf[i];
		}
		put(kWriteTable[prev]);
		put(0xDE); put(0xAA); put(0xEB);
		for (int i = 0; i < 20; i++)
			put(0xFF);
	}
	_nibbleTrack = track;
	_pos %= _nibbles.size();
	_trackDirty = false;
}

// Reads back the sectors the game wrote to the current track
void Apple2::storeTrack() {
	if (!_trackDirty || _nibbleTrack < 0)
		return;
	_trackDirty = false;
	byte readTable[256];
	memset(readTable, 0xFF, sizeof(readTable));
	for (int i = 0; i < 64; i++)
		readTable[kWriteTable[i]] = i;
	const uint n = _nibbles.size();
	auto at = [&](uint i) { return _nibbles[i % n]; };
	for (uint i = 0; i < n; i++) {
		if (at(i) != 0xD5 || at(i + 1) != 0xAA || at(i + 2) != 0x96)
			continue;
		int sector = ((at(i + 7) << 1) | 1) & at(i + 8);
		if (sector > 15)
			continue;
		uint j = i + 14;
		uint end = j + 48;
		while (j < end && !(at(j) == 0xD5 && at(j + 1) == 0xAA && at(j + 2) == 0xAD))
			j++;
		if (j == end)
			continue;
		j += 3;
		byte buf[342];
		byte prev = 0;
		bool ok = true;
		for (int k = 0; k < 342 && ok; k++) {
			byte v = readTable[at(j + k)];
			ok = v != 0xFF;
			buf[k] = prev = v ^ prev;
		}
		if (!ok || readTable[at(j + 342)] != prev)
			continue;
		byte data[256];
		for (int k = 0; k < 256; k++) {
			int bits = (buf[k % 86] >> ((k / 86) * 2)) & 3;
			data[k] = (buf[86 + k] << 2) | ((bits & 1) << 1) | (bits >> 1);
		}
		writeSector(_side, _nibbleTrack, physicalToLogical(sector), data);
	}
}

void Apple2::flipDisk() {
	if (_sides.size() < 2)
		return;
	storeTrack();
	_side ^= 1;
	_nibbleTrack = -1;
}

uint8 Apple2::disk(int reg, bool writing, uint8 v) {
	if (reg < 8) {
		int phase = reg >> 1;
		if (reg & 1) {
			_phases |= 1 << phase;
			int diff = (phase - (_halfTrack & 3)) & 3;
			if (diff == 1 && _halfTrack < 69)
				_halfTrack++;
			else if (diff == 3 && _halfTrack > 0)
				_halfTrack--;
		} else {
			_phases &= ~(1 << phase);
		}
		return 0;
	}
	switch (reg) {
	case 8:
		// The drive keeps turning for about a second
		if (_motor)
			_spinDown = 50;
		_motor = false;
		storeTrack();
		return 0;
	case 9:
		_motor = true;
		return 0;
	case 12:
		_q6 = false;
		break;
	case 13:
		_q6 = true;
		break;
	case 14:
		_q7 = false;
		break;
	case 15:
		_q7 = true;
		break;
	default:
		return 0;
	}
	if (writing && _q6 && _q7)
		_latch = v;
	if (_halfTrack / 2 != _nibbleTrack) {
		storeTrack();
		loadTrack();
	}
	if (reg == 12) {
		if (_q7) {
			// Write mode: the latch goes onto the disk
			_nibbles[_pos] = _latch;
			_pos = (_pos + 1) % _nibbles.size();
			_trackDirty = true;
			return 0;
		}
		if (!_motor && !_spinDown)
			return _nibbles[_pos]; // standing still: the latch holds
		// A disk byte takes longer to arrive than a read loop: every other
		// read finds the latch still filling, with the top bit clear
		_byteReady = !_byteReady;
		if (!_byteReady)
			return _nibbles[_pos] >> 1;
		byte b = _nibbles[_pos];
		_pos = (_pos + 1) % _nibbles.size();
		return b;
	}
	// Write-protect sense and the rest: never protected
	return 0;
}

uint8 Apple2::io(uint16 addr, bool writing, uint8 v) {
	if (addr < 0xC010)
		return _key;
	if (addr < 0xC020) {
		_key &= 0x7F;
		return _key;
	}
	if (addr >= 0xC030 && addr < 0xC040) {
		_clicks.push_back(_frameCycles);
		return 0;
	}
	if (addr >= 0xC050 && addr < 0xC058) {
		switch (addr & 7) {
		case 0: _text = false; break;
		case 1: _text = true; break;
		case 2: _mixed = false; break;
		case 3: _mixed = true; break;
		case 4: _page2 = false; break;
		case 5: _page2 = true; break;
		case 6: _hires = false; break;
		case 7: _hires = true; break;
		}
		return 0;
	}
	if (addr >= 0xC080 && addr < 0xC090) {
		// Language card
		int n = addr & 15;
		_lcBank2 = !(n & 8);
		_lcRead = (n & 3) == 0 || (n & 3) == 3;
		if (n & 1) {
			if (!writing && ++_lcPrewrite >= 2)
				_lcWrite = true;
		} else {
			_lcWrite = false;
			_lcPrewrite = 0;
		}
		return 0;
	}
	if (addr >= 0xC0E0 && addr < 0xC0F0)
		return disk(addr & 15, writing, v);
	return 0;
}

uint8 Apple2::read(uint16 addr) {
	if (addr < 0xC000)
		return _ram[addr];
	if (addr < 0xC100)
		return io(addr, false, 0);
	if (addr < 0xD000) {
		// Slot 6 holds a Disk II: the bytes software checks to find one
		static const byte kDiskRomId[8] = { 0xA2, 0x20, 0xA0, 0x00, 0xA2, 0x03, 0x86, 0x3C };
		if ((addr & 0xFF00) == 0xC600 && (addr & 0xFF) < 8)
			return kDiskRomId[addr & 0xFF];
		return 0;
	}
	if (!_lcRead)
		return _rom[addr - 0xD000];
	if (addr < 0xE000)
		return _lcBank[_lcBank2 ? 1 : 0][addr - 0xD000];
	return _lcHigh[addr - 0xE000];
}

void Apple2::write(uint16 addr, uint8 v) {
	if (addr < 0xC000) {
		_ram[addr] = v;
		return;
	}
	if (addr < 0xC100) {
		io(addr, true, v);
		return;
	}
	if (addr < 0xD000 || !_lcWrite)
		return;
	if (addr < 0xE000)
		_lcBank[_lcBank2 ? 1 : 0][addr - 0xD000] = v;
	else
		_lcHigh[addr - 0xE000] = v;
}

void Apple2::rts() {
	uint8 lo = read(0x100 + (uint8)(_cpu.sp + 1));
	uint8 hi = read(0x100 + (uint8)(_cpu.sp + 2));
	_cpu.sp += 2;
	_cpu.pc = ((lo | (hi << 8)) + 1) & 0xFFFF;
}

// Monitor COUT for the 40-column screen
void Apple2::cout(uint8 c) {
	uint8 &ch = _ram[0x24], &cv = _ram[0x25];
	if ((c & 0x7F) == 0x0D) {
		ch = 0;
		cv++;
	} else if ((c & 0x7F) >= 0x20) {
		if (cv < kTextRows)
			_ram[0x400 + textRow(cv) + ch] = c | 0x80;
		if (++ch >= 40) {
			ch = 0;
			cv++;
		}
	}
	if (cv >= kTextRows) {
		// Scroll up a line
		for (int r = 0; r < kTextRows - 1; r++)
			memcpy(&_ram[0x400 + textRow(r)], &_ram[0x400 + textRow(r + 1)], 40);
		memset(&_ram[0x400 + textRow(kTextRows - 1)], 0xA0, 40);
		cv = kTextRows - 1;
	}
	uint16 base = 0x400 + textRow(cv);
	_ram[0x28] = base & 0xFF;
	_ram[0x29] = base >> 8;
}

// Applesoft's hi-res graphics, which the game draws its pictures with.
// Zero page as Applesoft keeps it: $E0/$E1 x, $E2 y, $E4 colour, $E6 page.
void Apple2::hposn(int x, int y) {
	_ram[0xE0] = x & 0xFF;
	_ram[0xE1] = x >> 8;
	_ram[0xE2] = y;
}

void Apple2::hplot(int x, int y) {
	hposn(x, y);
	if (x < 0 || x >= 280 || y < 0 || y >= 192)
		return;
	int col = x / 7;
	uint16 addr = (_ram[0xE6] << 8) + (y & 7) * 0x400 + ((y >> 3) & 7) * 0x80 + (y >> 6) * 40 + col;
	byte colour = _ram[0xE4];
	// The two-colour patterns run across byte pairs: odd bytes use the
	// pattern's other phase
	if ((col & 1) && ((colour & 0x7F) == 0x2A || (colour & 0x7F) == 0x55))
		colour ^= 0x7F;
	byte mask = 0x80 | (1 << (x % 7));
	if (addr < 0xC000)
		_ram[addr] = ((colour ^ _ram[addr]) & mask) ^ _ram[addr];
}

void Apple2::hlin(int x1, int y1) {
	int x0 = _ram[0xE0] | (_ram[0xE1] << 8), y0 = _ram[0xE2];
	int dx = ABS(x1 - x0), dy = -ABS(y1 - y0);
	int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;
	for (int guard = 0; guard < 1024; guard++) {
		hplot(x0, y0);
		if (x0 == x1 && y0 == y1)
			break;
		int e2 = 2 * err;
		if (e2 >= dy) {
			err += dy;
			x0 += sx;
		}
		if (e2 <= dx) {
			err += dx;
			y0 += sy;
		}
	}
	hposn(x1, y1);
}

// The Disk II boot ROM's sector reader, which the boot sector calls to load
// more of track 0: read physical sector ($3D) to page ($27), then return to
// the boot sector at $0801
void Apple2::bootSector() {
	readSector(_side, 0, physicalToLogical(_ram[0x3D] & 15), &_ram[_ram[0x27] << 8]);
	_ram[0x27]++;
	_ram[0x3D]++;
	_cpu.x = _ram[0x2B];
	_cpu.pc = 0x0801;
}

// Stand-ins for the Monitor ROM routines the game calls
bool Apple2::trap() {
	uint16 pc = _cpu.pc;
	if (pc == 0xC65C) {
		bootSector();
		return true;
	}
	if (pc < 0xD000 || _lcRead)
		return false;
	switch (pc) {
	case 0xFC58: // HOME
		for (int r = 0; r < kTextRows; r++)
			memset(&_ram[0x400 + textRow(r)], 0xA0, 40);
		_ram[0x24] = _ram[0x25] = 0;
		_ram[0x28] = 0x00;
		_ram[0x29] = 0x04;
		break;
	case 0xFB2F: // INIT
		_text = true;
		_mixed = _page2 = _hires = false;
		break;
	case 0xFDED: // COUT
	case 0xFDF0: // COUT1
		cout(_cpu.a);
		break;
	case 0xFC22: { // VTAB
		uint16 base = 0x400 + textRow(_ram[0x25] % kTextRows);
		_ram[0x28] = base & 0xFF;
		_ram[0x29] = base >> 8;
		break;
	}
	case 0xFD0C: // RDKEY
	case 0xFD1B: // KEYIN
		if (!(_key & 0x80))
			return true; // wait here until a key comes
		_cpu.a = _key;
		_key &= 0x7F;
		break;
	case 0xF411: // HPOSN: A = y, X/Y = x
		hposn(_cpu.x | (_cpu.y << 8), _cpu.a);
		break;
	case 0xF457: // HPLOT: A = y, X/Y = x
		hplot(_cpu.x | (_cpu.y << 8), _cpu.a);
		break;
	case 0xF53A: // HLIN: A/X = x, Y = y, from the last point
		hlin(_cpu.a | (_cpu.x << 8), _cpu.y);
		break;
	case 0xF3E2: // HGR
	case 0xF3D8: // HGR2
		_ram[0xE6] = pc == 0xF3E2 ? 0x20 : 0x40;
		_text = false;
		_hires = true;
		_mixed = pc == 0xF3E2;
		_page2 = pc == 0xF3D8;
		memset(&_ram[_ram[0xE6] << 8], 0, 0x2000);
		break;
	case 0xF3F2: // HCLR
		memset(&_ram[_ram[0xE6] << 8], 0, 0x2000);
		break;
	case 0xFCA8: // WAIT
		_cpu.a = 0;
		break;
	default:
		// SETKBD, SETVID, IORTS, BELL and anything else: nothing to do
		debug(2, "DarkLord: Apple ROM call $%04X", pc);
		break;
	}
	rts();
	return true;
}

void Apple2::typeChar(int ch) {
	if (ch == '\n' || ch == '\r')
		ch = 0x0D;
	else if (ch == 127)
		ch = 8;
	if (ch < 0 || ch > 0x7F)
		return;
	// An Apple II+ keyboard only types capitals
	if (ch >= 'a' && ch <= 'z')
		ch -= 32;
	_keys.push_back(ch);
}

void Apple2::mixSound() {
	int16 out[kSamplesPerFrame];
	uint next = 0;
	for (int i = 0; i < kSamplesPerFrame; i++) {
		int until = (int)((int64)(i + 1) * kCyclesPerFrame / kSamplesPerFrame);
		// Average the speaker over the sample
		int from = (int)((int64)i * kCyclesPerFrame / kSamplesPerFrame);
		int32 sum = 0;
		int t = from;
		while (next < _clicks.size() && _clicks[next] < until) {
			sum += (_speaker ? 1 : -1) * (_clicks[next] - t);
			t = _clicks[next];
			_speaker = !_speaker;
			next++;
		}
		sum += (_speaker ? 1 : -1) * (until - t);
		int32 level = sum * 8000 / (until - from);
		// Remove the steady level a resting speaker would hold
		_dc += (level - _dc) / 64;
		int32 v = level - _dc;
		out[i] = (int16)CLIP<int32>(v, -32767, 32767);
	}
	_clicks.clear();
	if (_host)
		_host->appleSound(out, kSamplesPerFrame);
}

bool Apple2::screenShows(const char *text) const {
	// Only the text rows on screen
	if (!_text && !_mixed)
		return false;
	const uint len = strlen(text);
	const uint16 page = _page2 ? 0x800 : 0x400;
	{
		for (int r = _text ? 0 : 20; r < kTextRows; r++) {
			const byte *row = &_ram[page + textRow(r)];
			for (uint x = 0; x + len <= 40; x++) {
				uint i = 0;
				while (i < len && (row[x + i] & 0x7F) == (byte)text[i])
					i++;
				if (i == len)
					return true;
			}
		}
	}
	return false;
}

// The game came on two disk sides and asks for them to be swapped. Both
// are always at hand here, so the swap and the key press happen by
// themselves, once each time the request appears.
void Apple2::answerDiskPrompt() {
	bool shown = screenShows("TURN OVER DISK") || screenShows("FLIP DISK");
	if (shown && !_promptShown && _keys.empty()) {
		flipDisk();
		_keys.push_back(0x0D);
		debug(1, "DarkLord: Apple disk flipped to side %d", _side + 1);
	}
	_promptShown = shown;
}

void Apple2::runFrame() {
	while (_frameCycles < kCyclesPerFrame) {
		int cycles;
		if (trap())
			cycles = 6;
		else
			cycles = _cpu.step();
		_frameCycles += cycles;
	}
	mixSound();
	answerDiskPrompt();
	if (_spinDown > 0)
		_spinDown--;
	_frameCycles -= kCyclesPerFrame;
	_frame++;
	// Typed keys go in one at a time, after the game has taken the last
	if (_keyDelay > 0)
		_keyDelay--;
	else if (!(_key & 0x80) && !_keys.empty()) {
		_key = _keys[0] | 0x80;
		_keys.remove_at(0);
		_keyDelay = 2;
	}
}

void Apple2::render(byte *dst, int pitch) const {
	const int graphicsRows = _text ? 0 : (_mixed ? 160 : 192);
	const uint16 hiresBase = _page2 ? 0x4000 : 0x2000;
	const uint16 textBase = _page2 ? 0x800 : 0x400;

	for (int y = 0; y < 192; y++) {
		byte line[280];
		if (y < graphicsRows && _hires) {
			const byte *src = &_ram[hiresBase + (y & 7) * 0x400 + ((y >> 3) & 7) * 0x80 + (y >> 6) * 40];
			bool bits[282];
			bool pal[280];
			bits[0] = bits[281] = false;
			for (int x = 0; x < 280; x++) {
				byte b = src[x / 7];
				bits[x + 1] = (b >> (x % 7)) & 1;
				pal[x] = b & 0x80;
			}
			auto colour = [&](int x) {
				if (x & 1)
					return pal[x] ? kOrange : kGreen;
				return pal[x] ? kBlue : kViolet;
			};
			for (int x = 0; x < 280; x++) {
				bool on = bits[x + 1], l = bits[x], r = bits[x + 2];
				if (on)
					line[x] = (l || r) ? kWhite : colour(x);
				else
					line[x] = (l && r) ? colour(x - 1) : kBlack;
			}
		} else if (y < graphicsRows) {
			// Lo-res: two blocks per text cell
			const byte *src = &_ram[textBase + textRow(y >> 3)];
			for (int x = 0; x < 280; x++) {
				byte b = src[x / 7];
				line[x] = (y & 4) ? (b >> 4) : (b & 15);
			}
		} else {
			const byte *src = &_ram[textBase + textRow(y >> 3)];
			for (int cx = 0; cx < 40; cx++) {
				byte c = src[cx];
				bool inverse = c < 0x80;
				int ascii;
				if ((c & 0x7F) < 0x20 || (c >= 0x40 && c < 0x60) || (c >= 0xC0 && c < 0xE0))
					ascii = 0x40 + (c & 0x1F);
				else if (c >= 0xE0)
					ascii = c & 0x7F;
				else
					ascii = 0x20 + (c & 0x1F);
				byte bitsRow = _font[ascii * 8 + (y & 7)];
				for (int p = 0; p < 7; p++) {
					bool on = (bitsRow >> (7 - p)) & 1;
					line[cx * 7 + p] = (on != inverse) ? kWhite : kBlack;
				}
			}
		}
		byte *o = dst + (y * 2) * pitch;
		for (int x = 0; x < 280; x++)
			o[2 * x] = o[2 * x + 1] = line[x];
		memcpy(o + pitch, o, kScreenW);
	}
}

void Apple2::saveState(Common::WriteStream &s) {
	storeTrack();
	s.writeUint32BE(MKTAG('D', 'L', 'A', '2'));
	s.writeUint32LE(1);
	s.write(_ram, sizeof(_ram));
	s.write(_lcBank, sizeof(_lcBank));
	s.write(_lcHigh, sizeof(_lcHigh));
	s.writeByte(_lcRead | (_lcWrite << 1) | (_lcBank2 << 2));
	s.writeByte(_lcPrewrite);
	s.writeByte(_text | (_mixed << 1) | (_page2 << 2) | (_hires << 3));
	s.writeUint16LE(_cpu.pc);
	s.writeByte(_cpu.a);
	s.writeByte(_cpu.x);
	s.writeByte(_cpu.y);
	s.writeByte(_cpu.sp);
	s.writeByte(_cpu.p);
	s.writeByte(_side);
	s.writeByte(_halfTrack);
	s.writeByte(_phases);
	s.writeByte(_motor | (_q6 << 1) | (_q7 << 2));
	s.writeUint32LE(_frame);
}

bool Apple2::loadState(Common::ReadStream &s) {
	if (s.readUint32BE() != MKTAG('D', 'L', 'A', '2') || s.readUint32LE() != 1)
		return false;
	s.read(_ram, sizeof(_ram));
	s.read(_lcBank, sizeof(_lcBank));
	s.read(_lcHigh, sizeof(_lcHigh));
	byte lc = s.readByte();
	_lcRead = lc & 1;
	_lcWrite = lc & 2;
	_lcBank2 = lc & 4;
	_lcPrewrite = s.readByte();
	byte video = s.readByte();
	_text = video & 1;
	_mixed = video & 2;
	_page2 = video & 4;
	_hires = video & 8;
	_cpu.pc = s.readUint16LE();
	_cpu.a = s.readByte();
	_cpu.x = s.readByte();
	_cpu.y = s.readByte();
	_cpu.sp = s.readByte();
	_cpu.p = s.readByte();
	_side = s.readByte() % (_sides.empty() ? 1 : _sides.size());
	_halfTrack = s.readByte();
	_phases = s.readByte();
	byte drive = s.readByte();
	_motor = drive & 1;
	_q6 = drive & 2;
	_q7 = drive & 4;
	_frame = s.readUint32LE();
	_nibbleTrack = -1;
	_key = 0;
	_keys.clear();
	return !s.err();
}

} // End of namespace DarkLord
