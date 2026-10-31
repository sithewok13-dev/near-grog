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
#include "common/stream.h"
#include "darklord/machine.h"

namespace DarkLord {

// Fake KERNAL entry points the game uses (the real ROM is not included)
enum {
	kTrapIrq = 0xEA31,
	kTrapLoad = 0xF4A5,
	kTrapSave = 0xF5ED,
	kTrapAcptr = 0xFFA5,
	kTrapTalk = 0xFFB4,
	kTrapReadst = 0xFFB7,
	kTrapSetlfs = 0xFFBA,
	kTrapSetnam = 0xFFBD,
	kTrapOpen = 0xFFC0,
	kTrapClose = 0xFFC3,
	kTrapChkin = 0xFFC6,
	kTrapChkout = 0xFFC9,
	kTrapChrin = 0xFFCF,
	kTrapChrout = 0xFFD2,
	kTrapStop = 0xFFE1,
	kTrapGetin = 0xFFE4,
	kTrapClrchn = 0xFFCC,
	kTrapSecond = 0xFF93,
	kTrapTksa = 0xFF96,
	kTrapCiout = 0xFFA8,
	kTrapUntlk = 0xFFAB,
	kTrapUnlsn = 0xFFAE,
	kTrapListen = 0xFFB1,
	kTrapClall = 0xFFE7,
	kTrapPlot = 0xFFF0,
	kTrapCint = 0xFF81,
	kTrapCintBody = 0xFF5B,
	kTrapIoinit = 0xFF84,
	kTrapRestor = 0xFF8A,
	// Screen editor routines programs call directly
	kTrapClearScreen = 0xE544,
	kTrapHome = 0xE566,
	kTrapPrint = 0xE716,
	kTrapLinePtr = 0xE9F0,
	kTrapClearLine = 0xE9FF,
	kTrapColourPtr = 0xEA24
};

static const char *const kStatusOk = "00, OK,00,00\r";
static const char *const kStatusNotFound = "62,FILE NOT FOUND,00,00\r";

// Keyboard matrix: [column (CIA1 port A bit)][row (port B bit)]
enum { kKeyNone = 0, kKeyReturn = 256, kKeyDel, kKeyShift };
static const int kMatrix[8][8] = {
	{ kKeyDel, kKeyReturn, 0, 0, 0, 0, 0, 0 },
	{ '3', 'w', 'a', '4', 'z', 's', 'e', kKeyShift },
	{ '5', 'r', 'd', '6', 'c', 'f', 't', 'x' },
	{ '7', 'y', 'g', '8', 'b', 'h', 'u', 'v' },
	{ '9', 'i', 'j', '0', 'm', 'k', 'o', 'n' },
	{ '+', 'p', 'l', '-', '.', ':', '@', ',' },
	{ 0, '*', ';', 0, 0, '=', 0, '/' },
	{ '1', 0, 0, '2', ' ', 0, 'q', 0 }
};

// Characters typed with SHIFT held, and the key they share
static const char kShifted[][2] = {
	{ '!', '1' }, { '"', '2' }, { '#', '3' }, { '$', '4' }, { '%', '5' }, { '&', '6' },
	{ '\'', '7' }, { '(', '8' }, { ')', '9' }, { '?', '/' }, { '<', ',' }, { '>', '.' },
	{ '[', ':' }, { ']', ';' }
};

/**
 * The game was shipped on two disk sides and asks the player to turn the disk
 * over after the start menu. Both sides are always available here, so the
 * code that prints the request and waits for RETURN (in DLART) is replaced
 * with NOPs. The patch is position independent because the game copies DLART
 * elsewhere in memory before running it. It goes into the disk image too,
 * because the original release reads DLART sector by sector.
 */
static void skipDiskFlip(Common::Array<byte> &data, const Common::Array<int> &where, Common::Array<byte> &img) {
	// LDX #$27; LDA abs,X; STA abs,X; DEX; BPL -9 (print the request)
	// JSR $124C; BEQ -5; CMP #$8D; BNE -9 (wait for RETURN)
	static const int kPattern[] = {
		0xA2, 0x27, 0xBD, -1, -1, 0x9D, -1, -1, 0xCA, 0x10, 0xF7,
		0x20, 0x4C, 0x12, 0xF0, 0xFB, 0xC9, 0x8D, 0xD0, 0xF7
	};
	const uint len = ARRAYSIZE(kPattern);
	for (uint i = 0; i + len <= data.size(); i++) {
		uint k = 0;
		while (k < len && (kPattern[k] < 0 || data[i + k] == kPattern[k]))
			k++;
		if (k == len) {
			for (k = 0; k < len; k++) {
				data[i + k] = 0xEA;
				img[where[i + k]] = 0xEA;
			}
			return;
		}
	}
}

Machine::Machine(MachineHost *host) : _host(host), _cpu(this), _currentDisk(0), _raster(0), _lineCycles(0),
		_rasterCmp(0), _vicIrq(0), _stall(0), _denLatched(false), _timerA(0x4025), _timerALatch(0x4025), _cia1IcrMask(0),
		_cia1Icr(0), _frame(0), _keyHold(0), _keyGap(0), _status(kStatusOk), _statusPos(0),
		_inLfn(0), _outLfn(0), _lineReady(false), _iecListening(false), _iecOpening(false),
		_iecSa(0), _iecTalkSa(15), _kernalKeyboard(false) {
	for (int i = 0; i < 16; i++) {
		_drive[i].kind = kChanNone;
		_drive[i].pos = 0;
		_drive[i].buffer = 0;
	}
	memset(_driveRam, 0, sizeof(_driveRam));
	_chainTrack = _chainSector = _chainLeft = 0;
	memset(_ram, 0, sizeof(_ram));
	memset(_charRom, 0, sizeof(_charRom));
	memset(_color, 0, sizeof(_color));
	memset(_sid, 0, sizeof(_sid));
	memset(_vic, 0, sizeof(_vic));
	memset(_cia1, 0, sizeof(_cia1));
	memset(_cia2, 0, sizeof(_cia2));
	memset(_matrix, 0, sizeof(_matrix));
	memset(_lines, 0, sizeof(_lines));
	installKernal();
}

bool Machine::addDisk(Common::SeekableReadStream &d64) {
	static const int kSectors[35] = {
		21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
		19, 19, 19, 19, 19, 19, 19, 18, 18, 18, 18, 18, 18, 17, 17, 17, 17, 17
	};
	Common::Array<byte> img(d64.size());
	if (img.empty() || d64.read(img.data(), img.size()) != img.size())
		return false;
	auto offset = [&](int t, int s) -> int {
		int o = 0;
		for (int i = 0; i < t - 1 && i < 35; i++)
			o += kSectors[i];
		return (o + s) * 256;
	};
	auto valid = [&](int t, int s) { return t >= 1 && t <= 35 && s < kSectors[t - 1] && offset(t, s) + 256 <= (int)img.size(); };

	int t = 18, s = 1, guard = 0;
	while (t && valid(t, s) && guard++ < 32) {
		const byte *dir = &img[offset(t, s)];
		for (int i = 0; i < 8; i++) {
			const byte *e = dir + i * 32;
			if ((e[2] & 7) == 0)
				continue;
			Common::String name;
			for (int k = 0; k < 16 && e[5 + k] != 0xA0; k++)
				name += (char)e[5 + k];
			Common::Array<byte> data;
			Common::Array<int> where; // image offset of each byte
			int ft = e[3], fs = e[4], blocks = 0;
			while (ft && valid(ft, fs) && blocks++ < 800) {
				int o = offset(ft, fs);
				const byte *blk = &img[o];
				int last = blk[0] == 0 ? blk[1] : 255;
				for (int k = 2; k <= last; k++) {
					data.push_back(blk[k]);
					where.push_back(o + k);
				}
				if (blk[0] == 0)
					break;
				ft = blk[0];
				fs = blk[1];
			}
			if (name == "DLART")
				skipDiskFlip(data, where, img);
			if (!_files.contains(name)) {
				_files[name] = data;
				_fileDisk[name] = _disks.size();
			}
		}
		t = dir[0];
		s = dir[1];
	}
	_lastDisk = img;
	_disks.push_back(img);
	return !_files.empty();
}

int Machine::sectorOffset(int track, int sector) {
	static const int kSectors[35] = {
		21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
		19, 19, 19, 19, 19, 19, 19, 18, 18, 18, 18, 18, 18, 17, 17, 17, 17, 17
	};
	if (track < 1 || track > 35 || sector < 0 || sector >= kSectors[track - 1])
		return -1;
	int o = 0;
	for (int i = 0; i < track - 1; i++)
		o += kSectors[i];
	return (o + sector) * 256;
}

bool Machine::boot() {
	if (!_files.contains("DL") && _files.contains("LOADSTAR") && _files.contains("OS64")) {
		// The original release: LOAD"*",8,1 brings in LOADSTAR, which takes
		// over BASIC's idle vector and loads the game's own OS64
		const Common::Array<byte> &ls = _files["LOADSTAR"];
		uint16 la = ls[0] | (ls[1] << 8);
		for (uint i = 2; i < ls.size(); i++)
			_ram[(uint16)(la + i - 2)] = ls[i];
		_ram[0] = 0x2F;
		_ram[1] = 0x37;
		_ram[0xBA] = 8;
		_ram[0xB9] = 1;
		_ram[0x0314] = 0x31; _ram[0x0315] = 0xEA;
		_ram[0x0330] = 0xA5; _ram[0x0331] = 0xF4;
		_ram[0x0332] = 0xED; _ram[0x0333] = 0xF5;
		_cpu.reset(_ram[0x0302] | (_ram[0x0303] << 8));
		return true;
	}
	if (!_files.contains("DL")) {
		// Other games: the boot program as LOAD"*",8,1 and RUN would start it
		static const char *const kBoot[] = { "B", "CRIMSON CROWN", "TRANSYLVANIA" };
		for (const char *name : kBoot) {
			if (!_files.contains(name) || _files[name].size() < 4)
				continue;
			const Common::Array<byte> &prg = _files[name];
			uint16 la = prg[0] | (prg[1] << 8);
			for (uint i = 2; i < prg.size(); i++)
				_ram[(uint16)(la + i - 2)] = prg[i];
			_ram[0] = 0x2F;
			_ram[1] = 0x37;
			_ram[0xBA] = 8;
			_ram[0x288] = 0x04;
			_ram[0x286] = 14;
			_ram[0x289] = 10;
			_vic[0x18] = 0x15;
			_vic[0x20] = 14;
			_vic[0x21] = 6;
			_ram[0x0314] = 0x31; _ram[0x0315] = 0xEA;
			_ram[0x0330] = 0xA5; _ram[0x0331] = 0xF4;
			_ram[0x0332] = 0xED; _ram[0x0333] = 0xF5;
			uint16 start;
			if (la == 0x0801) {
				// A BASIC line with SYS nnnn
				start = 0;
				for (uint i = 6; i < prg.size() && prg[i]; i++) {
					if (prg[i] == 0x9E) {
						for (i++; i < prg.size() && (prg[i] == ' ' || (prg[i] >= '0' && prg[i] <= '9')); i++)
							if (prg[i] != ' ')
								start = start * 10 + (prg[i] - '0');
						break;
					}
				}
				if (!start)
					start = 0x0801;
			} else {
				// Loaded over the KERNAL vectors: CLALL's vector starts it
				start = _ram[0x032C] | (_ram[0x032D] << 8);
			}
			_cpu.reset(start);
			_cpu.sp = 0xF6;
			_kernalKeyboard = true;
			debug(1, "DarkLord: booting %s at $%04X", name, start);
			return true;
		}
		return false;
	}
	const Common::Array<byte> &dl = _files["DL"];
	uint16 la = dl[0] | (dl[1] << 8);
	for (uint i = 2; i < dl.size(); i++)
		_ram[(uint16)(la + i - 2)] = dl[i];
	_ram[0] = 0x2F;
	_ram[1] = 0x37;
	_ram[0x0314] = 0x31; _ram[0x0315] = 0xEA;
	_ram[0x0330] = 0xA5; _ram[0x0331] = 0xF4;
	_ram[0x0332] = 0xED; _ram[0x0333] = 0xF5;
	_cpu.reset(0x080B);
	return true;
}

void Machine::installKernal() {
	memset(_kernal, 0x60, sizeof(_kernal)); // RTS everywhere by default
	auto put = [&](uint16 addr, const byte *code, int len) {
		memcpy(&_kernal[addr - 0xE000], code, len);
	};
	// IRQ entry: save registers, check for BRK, JMP ($0314)
	static const byte irqEntry[] = { 0x48, 0x8A, 0x48, 0x98, 0x48, 0xBA, 0xBD, 0x04, 0x01,
		0x29, 0x10, 0xF0, 0x03, 0x6C, 0x16, 0x03, 0x6C, 0x14, 0x03 };
	static const byte irqExit[] = { 0x68, 0xA8, 0x68, 0xAA, 0x68, 0x40 };
	static const byte rti[] = { 0x40 };
	static const byte vectors[] = { 0x47, 0xFE, 0xE2, 0xFC, 0x48, 0xFF };
	// LOAD and SAVE go through the vectors at $0330/$0332, as on a real C64
	static const byte load[] = { 0x4C, 0x9E, 0xF4 };
	static const byte loadEntry[] = { 0x86, 0xC3, 0x84, 0xC4, 0x6C, 0x30, 0x03 };
	static const byte save[] = { 0x4C, 0xDD, 0xF5 };
	static const byte saveEntry[] = { 0x86, 0xAE, 0x84, 0xAF, 0xAA, 0xB5, 0x00, 0x85, 0xC1,
		0xB5, 0x01, 0x85, 0xC2, 0x6C, 0x32, 0x03 };
	put(0xFF48, irqEntry, sizeof(irqEntry));
	put(0xEA81, irqExit, sizeof(irqExit));
	put(0xFE47, rti, sizeof(rti));
	put(0xFFFA, vectors, sizeof(vectors));
	put(0xFFD5, load, sizeof(load));
	put(0xF49E, loadEntry, sizeof(loadEntry));
	put(0xFFD8, save, sizeof(save));
	put(0xF5DD, saveEntry, sizeof(saveEntry));
}

void Machine::setCharRom(const byte *data) {
	memcpy(_charRom, data, sizeof(_charRom));
}

Machine::MapType Machine::mapOf(uint16 a) const {
	uint8 port = _ram[1] & 7;
	bool lo = port & 1, hi = port & 2, ch = port & 4;
	if (a >= 0xD000 && a < 0xE000 && (lo || hi))
		return ch ? kMapIo : kMapRom;
	if (a >= 0xE000 && hi)
		return kMapKernal;
	if (a >= 0xA000 && a < 0xC000 && lo && hi)
		return kMapRom;
	return kMapRam;
}

uint8 Machine::read(uint16 a) {
	switch (mapOf(a)) {
	case kMapRam:
		return _ram[a];
	case kMapIo:
		return ioRead(a);
	case kMapKernal:
		return _kernal[a - 0xE000];
	default:
		// Character ROM ($D000-$DFFF with I/O switched out): programs that
		// copy it to make their own fonts get the stand-in character set
		if (a >= 0xD000 && a < 0xE000)
			return _charRom[a & 0x0FFF];
		return 0;
	}
}

void Machine::write(uint16 a, uint8 v) {
	if (mapOf(a) == kMapIo)
		ioWrite(a, v);
	else
		_ram[a] = v;
}

uint8 Machine::ioRead(uint16 a) {
	if (a < 0xD400) {
		int r = a & 0x3F;
		switch (r) {
		case 0x11:
			return (_vic[0x11] & 0x7F) | ((_raster >> 1) & 0x80);
		case 0x12:
			return _raster & 0xFF;
		case 0x19:
			return _vicIrq | ((_vicIrq & _vic[0x1A]) ? 0x80 : 0) | 0x70;
		case 0x1E:
		case 0x1F:
			return 0;
		default:
			return _vic[r];
		}
	}
	if (a < 0xD800) {
		if ((a & 0x1F) == 0x1B)
			return (byte)(_frame * 31 + _raster * 7 + _lineCycles);
		return 0;
	}
	if (a < 0xDC00)
		return _color[a - 0xD800] | 0xF0;
	if (a < 0xDD00) {
		int r = a & 15;
		switch (r) {
		case 0x00:
			return 0xFF;
		case 0x01:
			return keyboardRows(_cia1[0]);
		case 0x04:
			return _timerA & 0xFF;
		case 0x05:
			return (_timerA >> 8) & 0xFF;
		case 0x0D: {
			uint8 v = _cia1Icr | ((_cia1Icr & _cia1IcrMask) ? 0x80 : 0);
			_cia1Icr = 0;
			return v;
		}
		default:
			return _cia1[r];
		}
	}
	if (a < 0xDE00) {
		int r = a & 15;
		if (r == 0)
			return (_cia2[0] & 0x3F) | 0xC0;
		if (r == 0x0D)
			return 0;
		return _cia2[r];
	}
	return 0;
}

void Machine::ioWrite(uint16 a, uint8 v) {
	if (a < 0xD400) {
		int r = a & 0x3F;
		switch (r) {
		case 0x11:
			_rasterCmp = (_rasterCmp & 0xFF) | ((v & 0x80) << 1);
			_vic[r] = v;
			break;
		case 0x12:
			_rasterCmp = (_rasterCmp & 0x100) | v;
			break;
		case 0x19:
			_vicIrq &= ~v;
			break;
		default:
			_vic[r] = v;
			break;
		}
	} else if (a < 0xD800) {
		_sid[a & 0x1F] = v;
		if (_host)
			_host->sidWrite(a & 0x1F, v);
	} else if (a < 0xDC00) {
		_color[a - 0xD800] = v & 15;
	} else if (a < 0xDD00) {
		int r = a & 15;
		switch (r) {
		case 0x04:
			_timerALatch = (_timerALatch & 0xFF00) | v;
			break;
		case 0x05:
			_timerALatch = (_timerALatch & 0xFF) | (v << 8);
			if (!(_cia1[0x0E] & 1))
				_timerA = _timerALatch;
			break;
		case 0x0D:
			if (v & 0x80)
				_cia1IcrMask |= v & 0x1F;
			else
				_cia1IcrMask &= ~(v & 0x1F);
			break;
		case 0x0E:
			if (v & 0x10)
				_timerA = _timerALatch;
			_cia1[r] = v & ~0x10;
			break;
		default:
			_cia1[r] = v;
			break;
		}
	} else if (a < 0xDE00) {
		_cia2[a & 15] = v;
	}
}

uint8 Machine::keyboardRows(uint8 columns) const {
	uint8 rows = 0xFF;
	for (int pa = 0; pa < 8; pa++)
		if (!(columns & (1 << pa)))
			rows &= ~_matrix[pa];
	return rows;
}

void Machine::typeChar(int ch) {
	TypedKey k = { 0, 0, false, 0 };
	// PETSCII as the KERNAL would buffer it (upper/graphics character set)
	if (ch == '\r' || ch == '\n')
		k.petscii = 13;
	else if (ch == 8 || ch == 127)
		k.petscii = 20;
	else if (ch >= 'a' && ch <= 'z')
		k.petscii = ch - 32;
	else if (ch >= 'A' && ch <= 'Z')
		k.petscii = ch + 0x80;
	else if (ch >= 32 && ch < 64)
		k.petscii = ch;
	int key = ch;
	if (ch == '\r' || ch == '\n')
		key = kKeyReturn;
	else if (ch == 8 || ch == 127)
		key = kKeyDel;
	else if (ch >= 'A' && ch <= 'Z')
		key = ch - 'A' + 'a';
	for (uint i = 0; i < ARRAYSIZE(kShifted); i++) {
		if (ch == kShifted[i][0]) {
			key = kShifted[i][1];
			k.shift = true;
		}
	}
	for (int pa = 0; pa < 8; pa++) {
		for (int pb = 0; pb < 8; pb++) {
			if (kMatrix[pa][pb] == key && key != kKeyNone) {
				k.pa = pa;
				k.pb = pb;
				_typing.push_back(k);
				return;
			}
		}
	}
}

void Machine::keyboardFrame() {
	// Each typed key is held for 3 frames, then released for 2,
	// because the game scans the keyboard from its interrupt.
	if (_keyHold > 0) {
		if (--_keyHold == _keyGap)
			memset(_matrix, 0, sizeof(_matrix));
		return;
	}
	if (_typing.empty())
		return;
	TypedKey k = _typing.front();
	_typing.remove_at(0);
	memset(_matrix, 0, sizeof(_matrix));
	_matrix[k.pa] |= 1 << k.pb;
	if (k.shift)
		_matrix[1] |= 0x80;
	// The KERNAL's interrupt would put it in the keyboard buffer (for games
	// that use it; Dark Lord reads the keyboard itself)
	if (_kernalKeyboard && k.petscii && _ram[0xC6] < 10) {
		_ram[0x277 + _ram[0xC6]] = k.petscii;
		_ram[0xC6]++;
	}
	_keyGap = 2;
	_keyHold = 3 + _keyGap;
}

void Machine::rts() {
	uint16 lo = _ram[0x100 | (uint8)(_cpu.sp + 1)];
	uint16 hi = _ram[0x100 | (uint8)(_cpu.sp + 2)];
	_cpu.sp += 2;
	_cpu.pc = ((hi << 8) | lo) + 1;
}

Common::String Machine::zpFilename() const {
	uint16 ptr = _ram[0xBB] | (_ram[0xBC] << 8);
	Common::String name;
	for (int i = 0; i < _ram[0xB7]; i++)
		name += (char)_ram[(uint16)(ptr + i)];
	// "@0:NAME" and "0:NAME" mean the same file
	if (name.hasPrefix("@0:"))
		name = name.substr(3);
	else if (name.hasPrefix("0:"))
		name = name.substr(2);
	return name;
}

void Machine::kernalLoad() {
	Common::String name = zpFilename();
	Common::Array<byte> data;
	bool found = _host && _host->diskLoad(name, data);
	if (!found) {
		if (_files.contains(name)) {
			data = _files[name];
			found = true;
		} else if (name.hasSuffix("*")) {
			Common::String prefix = name.substr(0, name.size() - 1);
			for (auto &f : _files) {
				if (f._key.hasPrefix(prefix)) {
					data = f._value;
					found = true;
					break;
				}
			}
		}
	}
	if (!found || data.size() < 2) {
		debug(1, "DarkLord: LOAD \"%s\" not found", name.c_str());
		_status = kStatusNotFound;
		_cpu.p |= Cpu6502::kFlagC;
		_cpu.a = 4;
		rts();
		return;
	}
	uint16 la = data[0] | (data[1] << 8);
	if (_ram[0xB9] == 0)
		la = _ram[0xC3] | (_ram[0xC4] << 8);
	uint16 end = la;
	for (uint i = 2; i < data.size(); i++)
		_ram[end++] = data[i];
	debug(1, "DarkLord: LOAD \"%s\" -> $%04X-$%04X", name.c_str(), la, (uint16)(end - 1));
	_status = kStatusOk;
	_ram[0xAE] = end & 0xFF;
	_ram[0xAF] = end >> 8;
	_cpu.x = end & 0xFF;
	_cpu.y = end >> 8;
	_cpu.p &= ~Cpu6502::kFlagC;
	rts();
}

void Machine::kernalSave() {
	Common::String name = zpFilename();
	uint16 start = _ram[0xC1] | (_ram[0xC2] << 8);
	uint16 end = _ram[0xAE] | (_ram[0xAF] << 8);
	Common::Array<byte> data;
	data.push_back(start & 0xFF);
	data.push_back(start >> 8);
	for (uint16 a = start; a != end; a++)
		data.push_back(_ram[a]);
	debug(1, "DarkLord: SAVE \"%s\" $%04X-$%04X", name.c_str(), start, (uint16)(end - 1));
	if (_host)
		_host->diskSave(name, data);
	_status = kStatusOk;
	_cpu.p &= ~Cpu6502::kFlagC;
	rts();
}

bool Machine::trap() {
	switch (_cpu.pc) {
	case kTrapIrq:
		_cia1Icr = 0;
		_cpu.pc = 0xEA81;
		return true;
	case kTrapLoad:
		kernalLoad();
		return true;
	case kTrapSave:
		kernalSave();
		return true;
	case kTrapTalk:
		_iecTalkSa = 15;
		_statusPos = 0;
		_ram[0x90] = 0;
		rts();
		return true;
	case kTrapTksa:
		_iecTalkSa = _cpu.a & 0x0F;
		rts();
		return true;
	case kTrapAcptr:
		_cpu.a = driveRead(_iecTalkSa);
		_cpu.p &= ~Cpu6502::kFlagC;
		rts();
		return true;
	case kTrapUntlk:
		rts();
		return true;
	case kTrapListen:
		_iecListening = true;
		_iecOpening = false;
		_ram[0x90] = 0;
		rts();
		return true;
	case kTrapSecond:
		_iecSa = _cpu.a & 0x0F;
		if ((_cpu.a & 0xF0) == 0xF0) {
			_iecOpening = true;
			_iecName.clear();
		} else if ((_cpu.a & 0xF0) == 0xE0) {
			driveClose(_iecSa);
		}
		rts();
		return true;
	case kTrapCiout:
		if (_iecOpening)
			_iecName += (char)_cpu.a;
		else
			driveWrite(_iecSa, _cpu.a);
		_cpu.p &= ~Cpu6502::kFlagC;
		rts();
		return true;
	case kTrapUnlsn:
		if (_iecOpening)
			driveOpen(_iecSa, _iecName);
		else if (_iecSa == 15 && !_driveCmd.empty()) {
			driveCommand(_driveCmd);
			_driveCmd.clear();
		}
		_iecOpening = _iecListening = false;
		rts();
		return true;
	case kTrapSetlfs:
		_ram[0xB8] = _cpu.a;
		_ram[0xBA] = _cpu.x;
		_ram[0xB9] = _cpu.y;
		rts();
		return true;
	case kTrapSetnam:
		_ram[0xB7] = _cpu.a;
		_ram[0xBB] = _cpu.x;
		_ram[0xBC] = _cpu.y;
		rts();
		return true;
	case kTrapOpen:
		kernalOpen();
		_cpu.p &= ~Cpu6502::kFlagC;
		rts();
		return true;
	case kTrapClose:
		kernalClose(_cpu.a);
		_cpu.p &= ~Cpu6502::kFlagC;
		rts();
		return true;
	case kTrapChkin:
	case kTrapChkout: {
		Channel *c = channel(_cpu.x);
		if (!c && _cpu.x != 0) {
			_cpu.a = 3; // file not open
			_cpu.p |= Cpu6502::kFlagC;
		} else {
			(_cpu.pc == kTrapChkin ? _inLfn : _outLfn) = _cpu.x;
			_ram[0x90] = 0;
			_cpu.p &= ~Cpu6502::kFlagC;
		}
		rts();
		return true;
	}
	case kTrapClrchn:
		if (!_driveCmd.empty()) {
			driveCommand(_driveCmd);
			_driveCmd.clear();
		}
		_inLfn = _outLfn = 0;
		rts();
		return true;
	case kTrapClall:
		while (!_channels.empty())
			kernalClose(_channels[0].lfn);
		_channels.clear();
		_inLfn = _outLfn = 0;
		rts();
		return true;
	case kTrapChrout:
		kernalChrout(_cpu.a);
		_cpu.p &= ~Cpu6502::kFlagC;
		rts();
		return true;
	case kTrapReadst:
		_cpu.a = _ram[0x90];
		rts();
		return true;
	case kTrapGetin:
		if (_inLfn == 0) {
			_cpu.a = 0;
			if (_ram[0xC6]) {
				_cpu.a = _ram[0x277];
				memmove(&_ram[0x277], &_ram[0x278], 9);
				_ram[0xC6]--;
			}
		} else {
			uint8 v = 0;
			kernalChrin(v);
			_cpu.a = v;
		}
		_cpu.p &= ~Cpu6502::kFlagC;
		rts();
		return true;
	case kTrapChrin: {
		uint8 v = 0;
		if (!kernalChrin(v))
			return true; // waiting for a line of input: stay here
		_cpu.a = v;
		_cpu.p &= ~Cpu6502::kFlagC;
		rts();
		return true;
	}
	case kTrapLinePtr:
	case kTrapClearLine: {
		uint16 screen = (_ram[0x288] ? _ram[0x288] : 4) << 8;
		uint16 line = screen + (_cpu.x % 25) * 40;
		_ram[0xD1] = line & 0xFF;
		_ram[0xD2] = line >> 8;
		if (_cpu.pc == kTrapClearLine) {
			memset(&_ram[line], 32, 40);
			for (int i = 0; i < 40; i++)
				_color[(_cpu.x % 25) * 40 + i] = _ram[0x286] & 15;
		}
		rts();
		return true;
	}
	case kTrapColourPtr: {
		uint16 screen = (_ram[0x288] ? _ram[0x288] : 4) << 8;
		uint16 c = 0xD800 + (uint16)((_ram[0xD1] | (_ram[0xD2] << 8)) - screen);
		_ram[0xF3] = c & 0xFF;
		_ram[0xF4] = c >> 8;
		rts();
		return true;
	}
	case kTrapCint:
	case kTrapCintBody:
		// Screen editor start-up: the VIC as the KERNAL leaves it, a clear
		// screen in light blue on blue
		_vic[0x11] = 0x1B;
		_vic[0x16] = 0xC8;
		_vic[0x18] = 0x15;
		_vic[0x20] = 14;
		_vic[0x21] = 6;
		_cia2[0] = (_cia2[0] & ~3) | 3;
		_ram[0x288] = 0x04;
		_ram[0x286] = 14;
		_ram[0xC6] = 0;
		screenOut(147);
		rts();
		return true;
	case kTrapIoinit:
		_cia2[0] = (_cia2[0] & ~3) | 3;
		_cia1IcrMask = 0x01;
		_timerALatch = 0x4025;
		_cia1[0x0E] |= 0x01;
		rts();
		return true;
	case kTrapRestor:
		_ram[0x0314] = 0x31; _ram[0x0315] = 0xEA;
		_ram[0x0316] = 0x66; _ram[0x0317] = 0xFE;
		_ram[0x0318] = 0x47; _ram[0x0319] = 0xFE;
		_ram[0x0330] = 0xA5; _ram[0x0331] = 0xF4;
		_ram[0x0332] = 0xED; _ram[0x0333] = 0xF5;
		rts();
		return true;
	case kTrapClearScreen:
		screenOut(147);
		rts();
		return true;
	case kTrapHome:
		screenOut(19);
		rts();
		return true;
	case kTrapPrint:
		screenOut(_cpu.a);
		rts();
		return true;
	case kTrapPlot:
		if (_cpu.p & Cpu6502::kFlagC) {
			_cpu.x = _ram[0xD6];
			_cpu.y = _ram[0xD3];
		} else {
			_ram[0xD6] = _cpu.x;
			_ram[0xD3] = _cpu.y;
		}
		rts();
		return true;
	case kTrapStop:
		_cpu.p &= ~Cpu6502::kFlagZ;
		rts();
		return true;
	default:
		return false;
	}
}

// --- KERNAL channels and the drive ---

Machine::Channel *Machine::channel(uint8 lfn) {
	for (Channel &c : _channels)
		if (c.lfn == lfn)
			return &c;
	return nullptr;
}

void Machine::kernalOpen() {
	uint8 lfn = _ram[0xB8], dev = _ram[0xBA], sa = _ram[0xB9];
	kernalClose(lfn);
	Channel c;
	c.lfn = lfn;
	c.dev = dev;
	c.sa = sa & 0x0F;
	_ram[0x90] = 0;
	if (dev == 0)
		c.kind = kChanKeyboard;
	else if (dev == 3)
		c.kind = kChanScreen;
	else {
		c.kind = kChanDrive;
		driveOpen(c.sa, zpFilename());
	}
	_channels.push_back(c);
}

void Machine::kernalClose(uint8 lfn) {
	for (uint i = 0; i < _channels.size(); i++) {
		if (_channels[i].lfn != lfn)
			continue;
		if (_channels[i].kind == kChanDrive)
			driveClose(_channels[i].sa);
		_channels.remove_at(i);
		break;
	}
	if (_inLfn == lfn)
		_inLfn = 0;
	if (_outLfn == lfn)
		_outLfn = 0;
}

// Returns false while waiting for the player to type a line
bool Machine::kernalChrin(uint8 &v) {
	Channel *c = channel(_inLfn);
	if (c && c->kind == kChanDrive) {
		v = driveRead(c->sa);
		return true;
	}
	if (!_lineReady) {
		// The screen editor: echo typed keys until RETURN
		while (_ram[0xC6]) {
			uint8 k = _ram[0x277];
			memmove(&_ram[0x277], &_ram[0x278], 9);
			_ram[0xC6]--;
			if (k == 13) {
				screenOut(13);
				_line += '\r';
				_lineReady = true;
				break;
			}
			if (k == 20) {
				if (!_line.empty()) {
					_line.deleteLastChar();
					screenOut(157);
					screenOut(' ');
					screenOut(157);
				}
				continue;
			}
			_line += (char)k;
			screenOut(k);
		}
		if (!_lineReady)
			return false;
	}
	v = _line[0];
	_line.deleteChar(0);
	if (_line.empty())
		_lineReady = false;
	return true;
}

void Machine::kernalChrout(uint8 v) {
	Channel *c = channel(_outLfn);
	if (c && c->kind == kChanDrive)
		driveWrite(c->sa, v);
	else
		screenOut(v);
}

// --- The drive behind device 8 ---

void Machine::driveOpen(uint8 sa, Common::String name) {
	DriveChannel &d = _drive[sa & 15];
	d.data.clear();
	d.pos = 0;
	d.name.clear();
	debug(1, "DarkLord: drive open %d \"%s\"", sa, name.c_str());
	if (sa == 15) {
		d.kind = kChanCommand;
		_status = kStatusOk;
		_statusPos = 0;
		if (!name.empty())
			driveCommand(name);
		return;
	}
	if (name.hasPrefix("#")) {
		// A buffer in drive memory: "#n" picks one, "#" takes a free one
		d.kind = kChanBuffer;
		d.buffer = name.size() > 1 && name[1] >= '0' && name[1] <= '4' ? name[1] - '0' : 2;
		return;
	}
	// A file: "0:NAME,S,W" and the like
	int colon = name.findFirstOf(':');
	if (colon >= 0 && colon < 3)
		name = name.substr(colon + 1);
	bool writing = sa == 1;
	int comma = name.findFirstOf(',');
	if (comma >= 0) {
		writing |= name.contains(",W");
		name = name.substr(0, comma);
	}
	d.name = name;
	if (writing) {
		d.kind = kChanWrite;
		return;
	}
	d.kind = kChanRead;
	if (!(_host && _host->diskLoad(name, d.data)) && _files.contains(name))
		d.data = _files[name];
	if (d.data.empty()) {
		_status = kStatusNotFound;
		_statusPos = 0;
	}
}

void Machine::driveClose(uint8 sa) {
	DriveChannel &d = _drive[sa & 15];
	if (d.kind == kChanWrite) {
		_files[d.name] = d.data;
		if (_host)
			_host->diskSave(d.name, d.data);
	}
	if (sa == 15 && !_driveCmd.empty()) {
		driveCommand(_driveCmd);
		_driveCmd.clear();
	}
	d.kind = kChanNone;
	d.data.clear();
}

uint8 Machine::driveRead(uint8 sa) {
	DriveChannel &d = _drive[sa & 15];
	if ((sa & 15) == 15 || d.kind == kChanCommand || d.kind == kChanNone) {
		uint8 v = _status.empty() ? 13 : (uint8)_status[_statusPos % _status.size()];
		_statusPos++;
		if (_statusPos >= _status.size())
			_ram[0x90] |= 0x40;
		return v;
	}
	if (d.kind == kChanBuffer) {
		uint8 v = _driveRam[0x300 + d.buffer * 256 + (d.pos & 0xFF)];
		d.pos = (d.pos + 1) & 0xFF;
		return v;
	}
	if (d.pos < d.data.size()) {
		uint8 v = d.data[d.pos++];
		if (d.pos == d.data.size())
			_ram[0x90] |= 0x40;
		return v;
	}
	_ram[0x90] |= 0x42;
	return 13;
}

void Machine::driveWrite(uint8 sa, uint8 v) {
	DriveChannel &d = _drive[sa & 15];
	if ((sa & 15) == 15) {
		if (v == 13) {
			driveCommand(_driveCmd);
			_driveCmd.clear();
		} else {
			_driveCmd += (char)v;
		}
		return;
	}
	if (d.kind == kChanBuffer) {
		_driveRam[0x300 + d.buffer * 256 + (d.pos & 0xFF)] = v;
		d.pos = (d.pos + 1) & 0xFF;
	} else if (d.kind == kChanWrite) {
		d.data.push_back(v);
	}
}

// The KERNAL screen editor's output, enough for status and prompt text
void Machine::screenOut(uint8 v) {
	uint8 &col = _ram[0xD3], &row = _ram[0xD6];
	uint16 screen = _ram[0x288] << 8;
	if (!screen)
		screen = 0x400;
	switch (v) {
	case 13:
	case 141:
		col = 0;
		row++;
		_ram[0xC7] = 0;
		break;
	case 147:
		for (int i = 0; i < 1000; i++) {
			_ram[screen + i] = 32;
			_color[i] = _ram[0x286] & 15;
		}
		col = row = 0;
		break;
	case 19:
		col = row = 0;
		break;
	case 17: row++; break;
	case 145: if (row) row--; break;
	case 29: if (++col >= 40) { col = 0; row++; } break;
	case 157: if (col) col--; break;
	case 18: _ram[0xC7] = 1; break;
	case 146: _ram[0xC7] = 0; break;
	case 14: _vic[0x18] |= 2; break;
	case 142: _vic[0x18] &= ~2; break;
	default: {
		uint8 code;
		if (v >= 0x20 && v < 0x40) code = v;
		else if (v >= 0x40 && v < 0x60) code = v - 0x40;
		else if (v >= 0x60 && v < 0x80) code = v - 0x20;
		else if (v >= 0xA0 && v < 0xC0) code = v - 0x40;
		else if (v >= 0xC0) code = v - 0x80;
		else break; // colours and other controls
		if (_ram[0xC7])
			code |= 0x80;
		if (row < 25 && col < 40) {
			_ram[screen + row * 40 + col] = code;
			_color[row * 40 + col] = _ram[0x286] & 15;
		}
		if (++col >= 40) {
			col = 0;
			row++;
		}
		break;
	}
	}
	if (row >= 25) {
		memmove(&_ram[screen], &_ram[screen + 40], 960);
		memmove(&_color[0], &_color[40], 960);
		memset(&_ram[screen + 960], 32, 40);
		row = 24;
	}
}

// Disk sectors for programs that read and write blocks themselves; what
// they write goes to the host, per disk side
void Machine::readDiskSector(int track, int sector, byte *out) {
	Common::Array<byte> data;
	if (_host && _host->diskLoad(Common::String::format("D%uT%02dS%02d", _currentDisk + 1, track, sector), data) && data.size() == 256) {
		memcpy(out, data.data(), 256);
		return;
	}
	int o = sectorOffset(track, sector);
	if (_currentDisk < _disks.size() && o >= 0 && o + 256 <= (int)_disks[_currentDisk].size())
		memcpy(out, &_disks[_currentDisk][o], 256);
	else
		memset(out, 0, 256);
}

void Machine::writeDiskSector(int track, int sector, const byte *in) {
	Common::Array<byte> data(in, 256);
	if (_host)
		_host->diskSave(Common::String::format("D%uT%02dS%02d", _currentDisk + 1, track, sector), data);
}

void Machine::driveCommand(const Common::String &command) {
	Common::String cmd = command;
	while (!cmd.empty() && (cmd.lastChar() == 13 || cmd.lastChar() == ' '))
		cmd.deleteLastChar();
	debug(1, "DarkLord: drive command \"%s\"", cmd.c_str());
	_status = kStatusOk;
	_statusPos = 0;
	// Numbers after the command letters, in any separator style
	Common::Array<int> n;
	int i = 0;
	while (i < (int)cmd.size() && !(cmd[i] == ':' || (cmd[i] >= '0' && cmd[i] <= '9' && i >= 2)))
		i++;
	while (i < (int)cmd.size()) {
		if (cmd[i] >= '0' && cmd[i] <= '9') {
			int v = 0;
			while (i < (int)cmd.size() && cmd[i] >= '0' && cmd[i] <= '9')
				v = v * 10 + (cmd[i++] - '0');
			n.push_back(v);
		} else {
			i++;
		}
	}
	Common::String op = cmd.substr(0, 3);
	op.toUppercase();
	bool read = op.hasPrefix("U1") || op.hasPrefix("UA") || op.hasPrefix("B-R");
	bool write = op.hasPrefix("U2") || op.hasPrefix("UB") || op.hasPrefix("B-W");
	if ((read || write) && n.size() >= 4) {
		DriveChannel *c = &_drive[n[0] & 15];
		if (c->kind != kChanBuffer)
			return;
		if (sectorOffset(n[2], n[3]) < 0) {
			_status = "66,ILLEGAL TRACK OR SECTOR,00,00\r";
			return;
		}
		byte *buf = &_driveRam[0x300 + c->buffer * 256];
		if (read) {
			readDiskSector(n[2], n[3], buf);
			c->pos = op.hasPrefix("B-R") ? 1 : 0;
		} else {
			writeDiskSector(n[2], n[3], buf);
		}
	} else if (op.hasPrefix("B-P") && n.size() >= 2) {
		if (_drive[n[0] & 15].kind == kChanBuffer)
			_drive[n[0] & 15].pos = n[1] & 0xFF;
	} else if (cmd.hasPrefix("M-W") && cmd.size() >= 6) {
		// Memory write: address, count, bytes
		uint16 addr = (byte)cmd[3] | ((byte)cmd[4] << 8);
		uint count = (byte)cmd[5];
		for (uint k = 0; k < count && 6 + k < cmd.size(); k++)
			_driveRam[(addr + k) & 0x7FF] = cmd[6 + k];
	} else if (cmd.hasPrefix("M-R") && cmd.size() >= 5) {
		uint16 addr = (byte)cmd[3] | ((byte)cmd[4] << 8);
		uint count = cmd.size() >= 6 ? (byte)cmd[5] : 1;
		_status.clear();
		for (uint k = 0; k < MAX(count, 1u); k++)
			_status += (char)_driveRam[(addr + k) & 0x7FF];
	}
}

// Polarware's Comprehend games load through a drive program that streams
// a file's sectors two bits at a time. Answered here on the C64 side: the
// routine that sends the track, sector and count and receives the first
// sector, and the one that receives each next sector of the chain. Both
// land in a 256-byte buffer the routine's code points at.
bool Machine::polarwareTrap() {
	static const byte kStart[] = { 0x2C, -1 & 0xFF, -1 & 0xFF, 0x10, 0x08, 0x20 };
	static const byte kNext[] = { 0xA0, 0x00, 0x78, 0xA9, 0x0B, 0x8D, 0x00, 0xDD, 0xAD, 0x00, 0xDD, 0x10, 0xFB };
	(void)kStart;
	uint16 pc = _cpu.pc;
	if (pc < 0x0800 || pc > 0x7F00 || mapOf(pc) != kMapRam)
		return false;
	bool next = !memcmp(&_ram[pc], kNext, sizeof(kNext));
	// The start routine: BIT flag / BPL / JSR upload / ... / JSR listen ...
	// then, 0x33 bytes on, the receive loop
	bool start = !next && _ram[pc] == 0x2C && _ram[pc + 3] == 0x10 && _ram[pc + 5] == 0x20 &&
		!memcmp(&_ram[pc + 0x33], kNext, sizeof(kNext));
	if (!start && !next)
		return false;
	uint16 recv = next ? pc : pc + 0x33;
	// Where the received bytes go: STA abs,Y at recv + 0x32
	if (_ram[recv + 0x32] != 0x99)
		return false;
	uint16 dest = _ram[recv + 0x33] | (_ram[recv + 0x34] << 8);
	if (start) {
		// The M-W command holds track, sector and count
		uint16 flag = _ram[pc + 1] | (_ram[pc + 2] << 8);
		_ram[flag] = 0;
		uint16 params = 0;
		for (uint16 a = 0x0800; a < 0x7F00; a++) {
			if (_ram[a] == 'M' && _ram[a + 1] == '-' && _ram[a + 2] == 'W' && _ram[a + 3] == 0x03 && _ram[a + 4] == 0x04 && _ram[a + 5] == 0x03) {
				params = a + 6;
				break;
			}
		}
		if (!params)
			return false;
		_chainTrack = _ram[params];
		_chainSector = _ram[params + 1];
		_chainLeft = _ram[params + 2];
	}
	byte sector[256];
	if (_chainTrack && _chainLeft > 0) {
		readDiskSector(_chainTrack, _chainSector, sector);
		_chainTrack = sector[0];
		_chainSector = sector[1];
		_chainLeft--;
	} else {
		memset(sector, 0, sizeof(sector));
	}
	for (int i = 0; i < 256; i++)
		_ram[(uint16)(dest + i)] = sector[i];
	_cpu.y = 0;
	rts();
	return true;
}

void Machine::advance(int cycles) {
	_lineCycles += cycles;
	while (_lineCycles >= kCyclesPerLine) {
		_lineCycles -= kCyclesPerLine;
		LineRegs &l = _lines[_raster];
		l.d011 = _vic[0x11];
		l.d016 = _vic[0x16];
		l.d018 = _vic[0x18];
		l.d021 = _vic[0x21];
		l.dd00 = _cia2[0];
		_raster = (_raster + 1) % kLinesPerFrame;
		if (_raster == 0) {
			_frame++;
			keyboardFrame();
		}
		if (_raster == _rasterCmp)
			_vicIrq |= 1;
		// On a bad line the VIC-II fetches a character row and holds the
		// CPU for about 40 cycles. The game's raster split depends on it.
		if (_raster == 0x30)
			_denLatched = (_vic[0x11] & 0x10) != 0;
		if (_denLatched && _raster >= 0x30 && _raster <= 0xF7 && (_raster & 7) == (_vic[0x11] & 7))
			_stall += 40;
	}
	if (_cia1[0x0E] & 1) {
		_timerA -= cycles;
		while (_timerA <= 0) {
			_cia1Icr |= 1;
			if (_cia1[0x0E] & 0x08) { // one-shot
				_cia1[0x0E] &= ~1;
				_timerA = _timerALatch;
				break;
			}
			_timerA += _timerALatch ? _timerALatch : 1;
		}
	}
}

// The game's own disk loader keeps a jump table at $FC00. Saved games use
// its sector calls: $FC06 reads and $FC09 writes 256 bytes, with A pointing
// at a zero-page buffer address, X = track and Y = sector. Saves live on
// track 35 of the second disk side; written sectors go to the host.
// Saved games are raw sectors on track 35; they go to the host's save files
void Machine::writeSector(int track, int sector, uint16 buf) {
	Common::Array<byte> data(256);
	for (int i = 0; i < 256; i++)
		data[i] = _ram[(uint16)(buf + i)];
	if (_host)
		_host->diskSave(Common::String::format("T%02dS%02d", track, sector), data);
	debug(1, "DarkLord: write track %d sector %d from $%04X", track, sector, buf);
}

void Machine::readSector(const Common::Array<byte> &disk, int track, int sector, uint16 buf) {
	Common::Array<byte> data;
	if (!(_host && _host->diskLoad(Common::String::format("T%02dS%02d", track, sector), data) && data.size() == 256)) {
		data.resize(256);
		int o = sectorOffset(track, sector);
		bool valid = o >= 0 && o + 256 <= (int)disk.size();
		for (int i = 0; i < 256; i++)
			data[i] = valid ? disk[o + i] : 0;
		// A save slot nobody has written yet: answer like a fresh original
		// disk, whose slots carry the game's marker byte
		if (track == 35)
			data[0x61] = _ram[0xCD61];
	}
	for (int i = 0; i < 256; i++)
		_ram[(uint16)(buf + i)] = data[i];
	debug(1, "DarkLord: read track %d sector %d to $%04X", track, sector, buf);
}

// The original release replaces the KERNAL with its own OS64, which loads
// through a fast loader it sends to the 1541; the game later moves a copy of
// that loader to $FC00. Instead of emulating the drive, the routines that
// talk to it are recognised by their code and answered here, wherever they
// sit. The file and directory code above them runs unchanged.
bool Machine::os64Trap() {
	uint16 pc = _cpu.pc;
	if (pc != 0x0500 && pc < 0xF000)
		return false;
	if (mapOf(pc) != kMapRam)
		return false;
	const byte *code = &_ram[pc];
	auto at = [&](const int *pattern, uint len) {
		if (pc + len > 0x10000)
			return false;
		for (uint i = 0; i < len; i++)
			if (pattern[i] >= 0 && code[i] != pattern[i])
				return false;
		return true;
	};

	// Uploads the drive code and waits for the drive to start it
	static const int kSendDriveCode[] = { 0xA5, 0x01, 0x48, 0xA9, 0x36, 0x85, 0x01, 0x20, 0x85, 0x05 };
	// STA status; STX track; STY sector; LDA #$20 (read) or #$23 (write); JSR send
	static const int kSectorIo[] = { 0x8D, -1, -1, 0x8E, -1, -1, 0x8C, -1, -1, 0xA9, -1, 0x20 };
	// Starting a file load from a parameter block (address, count, name)
	static const int kLoadOs64[] = { 0x86, 0x03, 0x84, 0x04, 0xA0, 0x00, 0xB1, 0x03, 0x8D };
	static const int kLoadGame[] = { 0x84, 0x04, 0xA0, 0x00, 0xB1, 0x03, 0x8D };
	static const int kSendByte[] = { 0x85, 0x02, 0xAD, 0x00, 0xDD, 0x49, 0x20, 0x8D, 0x00, 0xDD };
	static const int kReceiveByte[] = { 0xA9, 0x01, 0x85, 0x02, 0x2C, 0x00, 0xDD, 0x50, 0xFB };
	static const int kHandshake[] = { 0x2C, 0x00, 0xDD, 0x50, 0xFB, 0xAD, 0x00, 0xDD, 0x09, 0x10 };

	if (pc == 0x0500) {
		if (!at(kSendDriveCode, ARRAYSIZE(kSendDriveCode)))
			return false;
		_cpu.p &= ~Cpu6502::kFlagC;
		rts();
		return true;
	}
	if (at(kLoadOs64, ARRAYSIZE(kLoadOs64)) || at(kLoadGame, ARRAYSIZE(kLoadGame))) {
		// Read the file from the disk side that holds it, then carry on
		uint16 block = (code[0] == 0x86 ? _cpu.x : _ram[3]) | (_cpu.y << 8);
		Common::String name;
		for (int i = 0; i < 16 && _ram[(uint16)(block + 4 + i)]; i++)
			name += (char)_ram[(uint16)(block + 4 + i)];
		if (_fileDisk.contains(name))
			_currentDisk = _fileDisk[name];
		debug(1, "DarkLord: loader opens \"%s\" on side %u", name.c_str(), _currentDisk + 1);
		return false;
	}
	if (at(kSectorIo, ARRAYSIZE(kSectorIo)) && (code[10] == 0x20 || code[10] == 0x23)) {
		bool writing = code[10] == 0x23;
		uint16 buf = _ram[_cpu.a] | (_ram[(uint8)(_cpu.a + 1)] << 8);
		_ram[code[4] | (code[5] << 8)] = _cpu.x;
		_ram[code[7] | (code[8] << 8)] = _cpu.y;
		if (writing)
			writeSector(_cpu.x, _cpu.y, buf);
		else if (_currentDisk < _disks.size())
			readSector(_disks[_currentDisk], _cpu.x, _cpu.y, buf);
		// Drive status 0, compared with 1 as the original does: carry clear
		_cpu.a = 0;
		_cpu.p = (_cpu.p & ~(Cpu6502::kFlagC | Cpu6502::kFlagZ)) | Cpu6502::kFlagN;
		rts();
		return true;
	}
	if (at(kSendByte, ARRAYSIZE(kSendByte))) {
		debug(2, "DarkLord: loader sends drive byte $%02X", _cpu.a);
		rts();
		return true;
	}
	if (at(kReceiveByte, ARRAYSIZE(kReceiveByte))) {
		_cpu.a = 0;
		_cpu.p = (_cpu.p & ~Cpu6502::kFlagN) | Cpu6502::kFlagZ;
		rts();
		return true;
	}
	if (at(kHandshake, ARRAYSIZE(kHandshake))) {
		rts();
		return true;
	}
	return false;
}

bool Machine::loaderTrap() {
	static const byte kJumpTable[] = { 0x4C, 0x2C, 0xFC, 0x4C, 0x20, 0xFC, 0x4C, 0x0C, 0xFC, 0x4C, 0xA8, 0xFC };
	if ((_cpu.pc != 0xFC06 && _cpu.pc != 0xFC09) || mapOf(_cpu.pc) != kMapRam ||
			memcmp(&_ram[0xFC00], kJumpTable, sizeof(kJumpTable)) != 0)
		return false;

	uint16 buf = _ram[_cpu.a] | (_ram[(uint8)(_cpu.a + 1)] << 8);
	if (_cpu.pc == 0xFC09)
		writeSector(_cpu.x, _cpu.y, buf);
	else
		readSector(_lastDisk, _cpu.x, _cpu.y, buf);
	_cpu.p &= ~Cpu6502::kFlagC;
	rts();
	return true;
}

void Machine::runFrame() {
	uint32 frame = _frame;
	int guard = kCyclesPerFrame * 2;
	while (_frame == frame && guard > 0) {
		int cycles;
		if (_stall) {
			cycles = _stall;
			_stall = 0;
			advance(cycles);
			guard -= cycles;
			continue;
		}
		if ((mapOf(_cpu.pc) == kMapKernal && trap()) || loaderTrap() || os64Trap() || polarwareTrap())
			cycles = 6;
		else
			cycles = _cpu.step();
		advance(cycles);
		guard -= cycles;
		if ((_vicIrq & _vic[0x1A] & 0x0F) || (_cia1Icr & _cia1IcrMask & 0x1F)) {
			if (_cpu.irq())
				advance(7);
		}
	}
}

byte Machine::vicRead(uint16 bankBase, uint16 offset) const {
	offset &= 0x3FFF;
	// Banks 0 and 2 see the character ROM at $1000-$1FFF
	if ((bankBase == 0x0000 || bankBase == 0x8000) && offset >= 0x1000 && offset < 0x2000)
		return _charRom[offset & 0x0FFF];
	return _ram[bankBase + offset];
}

void Machine::render(byte *dst, int pitch) const {
	int vcBase = 0, rc = 0;
	bool display = false, den = false;
	byte fg[kScreenW];
	// Screen codes and colours are fetched on bad lines only and reused for
	// the rest of the character row, as on the real chip
	byte rowScreen[40], rowColor[40];
	memset(rowScreen, 0, sizeof(rowScreen));
	memset(rowColor, 0, sizeof(rowColor));

	for (int line = 0; line < kLinesPerFrame; line++) {
		const LineRegs &r = _lines[line];
		if (line == 0x30)
			den = (r.d011 & 0x10) != 0;
		bool bad = line >= 0x30 && line <= 0xF7 && den && (line & 7) == (r.d011 & 7);
		if (bad) {
			display = true;
			rc = 0;
			uint16 bank = (3 - (r.dd00 & 3)) * 0x4000;
			uint16 scr = ((r.d018 >> 4) & 15) * 0x400;
			for (int cx = 0; cx < 40; cx++) {
				int idx = (vcBase + cx) & 0x3FF;
				rowScreen[cx] = vicRead(bank, scr + idx);
				rowColor[cx] = _color[idx] & 15;
			}
		}

		if (line >= kFirstVisibleLine && line < kFirstVisibleLine + kScreenH) {
			byte *out = dst + (line - kFirstVisibleLine) * pitch;
			memset(fg, 0, sizeof(fg));
			uint16 bank = (3 - (r.dd00 & 3)) * 0x4000;
			uint16 bmp = ((r.d018 >> 3) & 1) * 0x2000;
			uint16 chars = ((r.d018 >> 1) & 7) * 0x800;
			uint8 bg = r.d021 & 15;
			bool bitmap = (r.d011 & 0x20) != 0;
			bool multi = (r.d016 & 0x10) != 0;

			for (int cx = 0; cx < 40; cx++) {
				int idx = (vcBase + cx) & 0x3FF;
				byte s = rowScreen[cx];
				byte c = rowColor[cx];
				byte b = 0;
				if (display)
					b = bitmap ? vicRead(bank, bmp + ((idx * 8 + rc) & 0x1FFF)) : vicRead(bank, chars + s * 8 + rc);
				byte *o = out + cx * 8;
				byte *f = fg + cx * 8;
				if (!display) {
					memset(o, bg, 8);
				} else if (bitmap && multi) {
					for (int p = 0; p < 4; p++) {
						int v = (b >> (6 - 2 * p)) & 3;
						byte col = v == 0 ? bg : v == 1 ? (s >> 4) : v == 2 ? (s & 15) : c;
						o[2 * p] = o[2 * p + 1] = col;
						f[2 * p] = f[2 * p + 1] = (v & 2) ? 1 : 0;
					}
				} else if (bitmap) {
					for (int p = 0; p < 8; p++) {
						bool on = (b >> (7 - p)) & 1;
						o[p] = on ? (s >> 4) : (s & 15);
						f[p] = on;
					}
				} else if (multi && (c & 8)) {
					for (int p = 0; p < 4; p++) {
						int v = (b >> (6 - 2 * p)) & 3;
						byte col = v == 0 ? bg : v == 1 ? (_vic[0x22] & 15) : v == 2 ? (_vic[0x23] & 15) : (c & 7);
						o[2 * p] = o[2 * p + 1] = col;
						f[2 * p] = f[2 * p + 1] = (v & 2) ? 1 : 0;
					}
				} else {
					for (int p = 0; p < 8; p++) {
						bool on = (b >> (7 - p)) & 1;
						o[p] = on ? c : bg;
						f[p] = on;
					}
				}
			}

			// Sprites, lowest priority first
			for (int n = 7; n >= 0; n--) {
				if (!(_vic[0x15] & (1 << n)))
					continue;
				bool yexp = _vic[0x17] & (1 << n), xexp = _vic[0x1D] & (1 << n);
				int sy = _vic[1 + 2 * n];
				int row = line - sy;
				if (yexp)
					row /= 2;
				if (line < sy || row >= 21)
					continue;
				int sx = (_vic[2 * n] | (((_vic[0x10] >> n) & 1) << 8)) - 24;
				uint16 ptr = vicRead(bank, ((r.d018 >> 4) & 15) * 0x400 + 0x3F8 + n) * 64;
				bool behind = _vic[0x1B] & (1 << n);
				bool smc = _vic[0x1C] & (1 << n);
				uint32 bits = (vicRead(bank, ptr + row * 3) << 16) | (vicRead(bank, ptr + row * 3 + 1) << 8) | vicRead(bank, ptr + row * 3 + 2);
				int w = xexp ? 2 : 1;
				for (int p = 0; p < 24; p++) {
					int col = -1;
					if (smc) {
						int v = (bits >> (22 - (p & ~1))) & 3;
						if (v == 1) col = _vic[0x25] & 15;
						else if (v == 2) col = _vic[0x27 + n] & 15;
						else if (v == 3) col = _vic[0x26] & 15;
					} else if ((bits >> (23 - p)) & 1) {
						col = _vic[0x27 + n] & 15;
					}
					if (col < 0)
						continue;
					for (int k = 0; k < w; k++) {
						int x = sx + p * w + k;
						if (x < 0 || x >= kScreenW || (behind && fg[x]))
							continue;
						out[x] = col;
					}
				}
			}
		}

		if (display) {
			if (rc == 7) {
				vcBase = (vcBase + 40) & 0x3FF;
				display = false;
			}
			rc = (rc + 1) & 7;
		}
		if (line == 0)
			vcBase = 0;
	}
}

// Save states hold the whole machine, so they work anywhere in the game
static const uint32 kStateVersion = 3;

Common::String Machine::screenText() const {
	// The text area of a split screen sits low down: take its registers
	const LineRegs &r = _lines[kFirstVisibleLine + 190];
	uint16 bank = (3 - (r.dd00 & 3)) * 0x4000;
	uint16 scr = bank + ((r.d018 >> 4) & 15) * 0x400;
	Common::String text;
	for (int row = 0; row < 25; row++) {
		for (int col = 0; col < 40; col++) {
			uint8 c = _ram[(uint16)(scr + row * 40 + col)] & 0x7F;
			char ch = ' ';
			if (c >= 1 && c <= 26)
				ch = 'a' + c - 1;
			else if (c >= 65 && c <= 90)
				ch = 'a' + c - 65;
			else if (c >= 32 && c < 64)
				ch = c;
			text += ch;
		}
		text += '\n';
	}
	return text;
}

void Machine::saveState(Common::WriteStream &s) const {
	s.writeUint32BE(MKTAG('D', 'L', 'S', 'T'));
	s.writeUint32LE(kStateVersion);
	s.write(_ram, sizeof(_ram));
	s.write(_color, sizeof(_color));
	s.write(_vic, sizeof(_vic));
	s.write(_cia1, sizeof(_cia1));
	s.write(_cia2, sizeof(_cia2));
	s.writeUint16LE(_cpu.pc);
	s.writeByte(_cpu.a);
	s.writeByte(_cpu.x);
	s.writeByte(_cpu.y);
	s.writeByte(_cpu.sp);
	s.writeByte(_cpu.p);
	s.writeSint32LE(_raster);
	s.writeSint32LE(_lineCycles);
	s.writeSint32LE(_rasterCmp);
	s.writeByte(_vicIrq);
	s.writeSint32LE(_timerA);
	s.writeUint16LE(_timerALatch);
	s.writeByte(_cia1IcrMask);
	s.writeByte(_cia1Icr);
	s.writeUint32LE(_frame);
	s.write(_sid, sizeof(_sid));
	s.writeByte(_currentDisk);
}

bool Machine::loadState(Common::ReadStream &s) {
	if (s.readUint32BE() != MKTAG('D', 'L', 'S', 'T'))
		return false;
	uint32 version = s.readUint32LE();
	if (version < 1 || version > kStateVersion)
		return false;
	s.read(_ram, sizeof(_ram));
	s.read(_color, sizeof(_color));
	s.read(_vic, sizeof(_vic));
	s.read(_cia1, sizeof(_cia1));
	s.read(_cia2, sizeof(_cia2));
	_cpu.pc = s.readUint16LE();
	_cpu.a = s.readByte();
	_cpu.x = s.readByte();
	_cpu.y = s.readByte();
	_cpu.sp = s.readByte();
	_cpu.p = s.readByte();
	_raster = s.readSint32LE();
	_lineCycles = s.readSint32LE();
	_rasterCmp = s.readSint32LE();
	_vicIrq = s.readByte();
	_timerA = s.readSint32LE();
	_timerALatch = s.readUint16LE();
	_cia1IcrMask = s.readByte();
	_cia1Icr = s.readByte();
	_frame = s.readUint32LE();
	if (version >= 2) {
		// Put the sound chip back as it was, or notes hang until the
		// music next touches each register
		s.read(_sid, sizeof(_sid));
		if (_host)
			for (int r = 0; r < 25; r++)
				_host->sidWrite(r, _sid[r]);
	}
	if (version >= 3)
		_currentDisk = s.readByte() % MAX<uint>(_disks.size(), 1);
	memset(_matrix, 0, sizeof(_matrix));
	_typing.clear();
	_keyHold = 0;
	return !s.err();
}

} // End of namespace DarkLord
