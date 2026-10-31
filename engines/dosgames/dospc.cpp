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
#include "common/textconsole.h"
#include "dosgames/dospc.h"

namespace DosGames {

enum {
	kMemSize = 0x110000,
	kBiosSeg = 0xF000,
	kFirstMcb = 0x0800,
	kMemTop = 0xA000,
	kBda = 0x400,
	kFrameMs = 20,
	kPitHz = 1193182
};

// Errors DOS returns in AX with carry set
enum {
	kErrFunction = 1, kErrFileNotFound = 2, kErrPathNotFound = 3, kErrTooMany = 4,
	kErrAccess = 5, kErrHandle = 6, kErrMemory = 8, kErrBlock = 9, kErrFormat = 11,
	kErrNoMoreFiles = 18
};

static const byte kEgaColours[16 * 3] = {
	0, 0, 0, 0, 0, 42, 0, 42, 0, 0, 42, 42, 42, 0, 0, 42, 0, 42, 42, 21, 0, 42, 42, 42,
	21, 21, 21, 21, 21, 63, 21, 63, 21, 21, 63, 63, 63, 21, 21, 63, 21, 63, 63, 63, 21, 63, 63, 63
};

DosPC::DosPC(DosHost *host) : _host(host), _cpu(this), _nextProgram(0), _currentPsp(0),
		_returnCode(0), _finished(false), _foundPos(0), _videoMode(3), _paletteDirty(true),
		_dacWrite(0), _dacRead(0), _dacComp(0), _retrace(0), _port60(0), _picMask(0), _picInService(0),
		_mouseX(320), _mouseY(100), _mouseButtons(0), _mouseMinX(0), _mouseMaxX(639),
		_mouseMinY(0), _mouseMaxY(199), _mouseCursor(-1), _pitDivisor(65536), _pitLatchLow(0),
		_pitLowNext(true), _tickAccum(0), _oplReg(0), _oplTimerCtl(0), _waitingForKey(false),
		_frame(0) {
	_mem = new byte[kMemSize];
	memset(_mem, 0, kMemSize);
	memset(_dac, 0, sizeof(_dac));
	memset(_rgb, 0, sizeof(_rgb));
	memcpy(_dac, kEgaColours, sizeof(kEgaColours));
	for (int i = 0; i < 3; i++)
		_mousePresses[i] = _mouseReleases[i] = 0;
	_dta[0] = 0x80;
	_dta[1] = 0;
	// Handles 0-4 are the standard devices
	for (int i = 0; i < 5; i++) {
		File f;
		f.pos = 0;
		f.open = f.device = true;
		f.dirty = false;
		_files.push_back(f);
	}
	installBios();
}

void DosPC::addProgram(const Common::String &name, const Common::String &args) {
	Program p;
	p.name = name;
	p.args = args;
	_programs.push_back(p);
}

void DosPC::overrideFile(const Common::String &name, const Common::Array<byte> &data) {
	_overrides[name] = data;
}

// The interrupt table points at little stubs in the BIOS segment. A stub
// is F1 nn (a trap into interrupt()) followed by IRET; the timer's also
// calls INT 1Ch, as the real BIOS does. Programs can hook any vector and
// chain to the stub as they would to the BIOS or DOS.
void DosPC::installBios() {
	for (int n = 0; n < 256; n++) {
		uint32 a = (kBiosSeg << 4) + stub(n);
		_mem[a] = 0xF1;
		_mem[a + 1] = n;
		if (n == 0x08) {
			_mem[a + 2] = 0xCD;
			_mem[a + 3] = 0x1C;
			_mem[a + 4] = 0xCF;
		} else {
			_mem[a + 2] = 0xCF;
		}
		WRITE_LE_UINT16(&_mem[n * 4], stub(n));
		WRITE_LE_UINT16(&_mem[n * 4 + 2], kBiosSeg);
	}
	// BIOS data area
	WRITE_LE_UINT16(&_mem[kBda + 0x10], 0x0021); // equipment: VGA colour, mouse port
	WRITE_LE_UINT16(&_mem[kBda + 0x13], 640);
	_mem[kBda + 0x49] = 3;
	WRITE_LE_UINT16(&_mem[kBda + 0x4A], 80);
	WRITE_LE_UINT16(&_mem[kBda + 0x63], 0x3D4);
	_mem[kBda + 0x84] = 24;
	_mem[(kBiosSeg << 4) + 0xFFFE] = 0xFC; // model byte: AT
	// One free memory block from kFirstMcb to 640K
	uint32 m = kFirstMcb << 4;
	_mem[m] = 'Z';
	WRITE_LE_UINT16(&_mem[m + 1], 0);
	WRITE_LE_UINT16(&_mem[m + 3], kMemTop - kFirstMcb - 1);
}

bool DosPC::start() {
	_nextProgram = 0;
	if (_programs.empty())
		return false;
	const Program &p = _programs[_nextProgram++];
	return exec(p.name, p.args, false);
}

uint8 DosPC::read8(uint32 addr) {
	return _mem[addr & 0xFFFFF];
}

void DosPC::write8(uint32 addr, uint8 v) {
	addr &= 0xFFFFF;
	if (addr >= 0xF0000)
		return; // ROM
	_mem[addr] = v;
}

void DosPC::setStackedFlag(uint16 flag, bool on) {
	// Inside a stub the stack holds IP, CS, FLAGS of the INT
	uint16 off = _cpu.r[Cpu8086::kSP] + 4;
	uint16 f = _cpu.mem16(_cpu.seg[Cpu8086::kSS], off);
	f = on ? (f | flag) : (f & ~flag);
	_cpu.setMem16(_cpu.seg[Cpu8086::kSS], off, f);
}

// --- Ports ---

uint8 DosPC::in8(uint16 port) {
	switch (port) {
	case 0x21:
		return _picMask;
	case 0x40:
		return (_frame * 37) & 0xFF;
	case 0x60:
		return _port60;
	case 0x61:
		return 0x20;
	case 0x64:
		return 0x1C;
	case 0x388:
	case 0x228:
		// AdLib status: timers 1 and 2 have run out once started
		return (_oplTimerCtl & 1) ? 0xC0 : ((_oplTimerCtl & 2) ? 0xA0 : 0);
	case 0x3C7:
		return 3;
	case 0x3C8:
		return _dacWrite;
	case 0x3C9: {
		byte v = _dac[_dacRead * 3 + _dacComp];
		if (++_dacComp == 3) {
			_dacComp = 0;
			_dacRead++;
		}
		return v;
	}
	case 0x3DA:
		// Vertical retrace for a slice of each 70 Hz refresh
		_retrace++;
		return ((_retrace % 64) < 6) ? 0x09 : 0x00;
	default:
		return 0xFF;
	}
}

void DosPC::out8(uint16 port, uint8 v) {
	switch (port) {
	case 0x20:
		// End of interrupt: the highest priority one in service is done
		if ((v & 0xE0) == 0x20) {
			for (int i = 0; i < 8; i++) {
				if (_picInService & (1 << i)) {
					_picInService &= ~(1 << i);
					break;
				}
			}
		}
		break;
	case 0x21:
		_picMask = v;
		break;
	case 0x40:
		if (_pitLowNext) {
			_pitLatchLow = v;
		} else {
			_pitDivisor = _pitLatchLow | (v << 8);
			if (_pitDivisor == 0)
				_pitDivisor = 65536;
		}
		_pitLowNext = !_pitLowNext;
		break;
	case 0x43:
		if ((v & 0xC0) == 0)
			_pitLowNext = true;
		break;
	case 0x388:
	case 0x228:
		_oplReg = v;
		break;
	case 0x389:
	case 0x229:
		if (_oplReg == 4) {
			if (v & 0x80)
				_oplTimerCtl = 0;
			else
				_oplTimerCtl = v & 3;
		}
		if (_host)
			_host->oplWrite(_oplReg, v);
		break;
	case 0x3C7:
		_dacRead = v;
		_dacComp = 0;
		break;
	case 0x3C8:
		_dacWrite = v;
		_dacComp = 0;
		break;
	case 0x3C9:
		_dac[_dacWrite * 3 + _dacComp] = v & 63;
		_paletteDirty = true;
		if (++_dacComp == 3) {
			_dacComp = 0;
			_dacWrite++;
		}
		break;
	default:
		break;
	}
}

bool DosPC::raiseIrq(int irq) {
	// Masked, or this or a higher priority one is still being handled
	if ((_picMask & (1 << irq)) || (_picInService & ((2 << irq) - 1)))
		return false;
	if (!_cpu.irq(irq < 8 ? 8 + irq : 0x70 + irq - 8))
		return false;
	_picInService |= 1 << irq;
	return true;
}

// --- Services ---

bool DosPC::interrupt(uint8 n) {
	switch (n) {
	case 0x00:
		warning("DosPC: divide error at %04X:%04X", _cpu.mem16(_cpu.seg[Cpu8086::kSS], _cpu.r[Cpu8086::kSP] + 2),
			_cpu.mem16(_cpu.seg[Cpu8086::kSS], _cpu.r[Cpu8086::kSP]));
		terminate(0xFF, false, 0);
		break;
	case 0x08:
		// BIOS tick count and end of interrupt; the stub goes on to INT 1Ch
		WRITE_LE_UINT32(&_mem[kBda + 0x6C], READ_LE_UINT32(&_mem[kBda + 0x6C]) + 1);
		_picInService &= ~1;
		break;
	case 0x09:
		_picInService &= ~2;
		// BIOS keyboard handler: keys already reach the BIOS buffer as they
		// are typed, so there is nothing to do
		break;
	case 0x10: int10(); break;
	case 0x11: _cpu.r[Cpu8086::kAX] = READ_LE_UINT16(&_mem[kBda + 0x10]); break;
	case 0x12: _cpu.r[Cpu8086::kAX] = 640; break;
	case 0x15:
		_cpu.wb(4, 0x86);
		setStackedFlag(Cpu8086::kCF, true);
		break;
	case 0x16: keyboardBios(); break;
	case 0x1A: int1a(); break;
	case 0x20: terminate(0, false, 0); break;
	case 0x21: int21(); break;
	case 0x27: terminate(0, true, (_cpu.r[Cpu8086::kDX] + 15) >> 4); break;
	case 0x2F: _cpu.wb(0, 0); break;
	case 0x33: int33(); break;
	default:
		break;
	}
	return true;
}

void DosPC::int1a() {
	switch (_cpu.rb(4)) {
	case 0x00: {
		uint32 t = READ_LE_UINT32(&_mem[kBda + 0x6C]);
		_cpu.r[Cpu8086::kCX] = t >> 16;
		_cpu.r[Cpu8086::kDX] = t & 0xFFFF;
		_cpu.wb(0, 0);
		break;
	}
	case 0x02: {
		uint32 secs = _frame * kFrameMs / 1000;
		auto bcd = [](int v) { return ((v / 10) << 4) | (v % 10); };
		_cpu.wb(5, bcd(12 + (secs / 3600) % 12));
		_cpu.wb(1, bcd((secs / 60) % 60));
		_cpu.wb(6, bcd(secs % 60));
		setStackedFlag(Cpu8086::kCF, false);
		break;
	}
	case 0x04:
		_cpu.r[Cpu8086::kCX] = 0x1990;
		_cpu.r[Cpu8086::kDX] = 0x0101;
		setStackedFlag(Cpu8086::kCF, false);
		break;
	default:
		break;
	}
}

// Text mode output through the BIOS
void DosPC::scrollText(int top, int bottom, int lines, uint8 attr) {
	byte *vram = &_mem[0xB8000];
	if (lines == 0 || lines > bottom - top) {
		for (int r = top; r <= bottom; r++)
			for (int c = 0; c < 80; c++) {
				vram[(r * 80 + c) * 2] = ' ';
				vram[(r * 80 + c) * 2 + 1] = attr;
			}
		return;
	}
	for (int r = top; r <= bottom - lines; r++)
		memcpy(&vram[r * 160], &vram[(r + lines) * 160], 160);
	for (int r = bottom - lines + 1; r <= bottom; r++)
		for (int c = 0; c < 80; c++) {
			vram[(r * 80 + c) * 2] = ' ';
			vram[(r * 80 + c) * 2 + 1] = attr;
		}
}

void DosPC::putChar(uint8 c, uint8 attr, bool advance) {
	uint8 &col = _mem[kBda + 0x50], &row = _mem[kBda + 0x51];
	if (!advance) {
		if (_videoMode == 3) {
			_mem[0xB8000 + (row * 80 + col) * 2] = c;
			_mem[0xB8000 + (row * 80 + col) * 2 + 1] = attr;
		}
		return;
	}
	switch (c) {
	case 7:
		return;
	case 8:
		if (col > 0)
			col--;
		return;
	case 10:
		row++;
		break;
	case 13:
		col = 0;
		return;
	default:
		if (_videoMode == 3) {
			_mem[0xB8000 + (row * 80 + col) * 2] = c;
			if (attr)
				_mem[0xB8000 + (row * 80 + col) * 2 + 1] = attr;
		}
		if (++col >= 80) {
			col = 0;
			row++;
		}
		break;
	}
	if (row >= 25) {
		if (_videoMode == 3)
			scrollText(0, 24, 1, 7);
		row = 24;
	}
}

void DosPC::int10() {
	uint8 ah = _cpu.rb(4), al = _cpu.rb(0);
	switch (ah) {
	case 0x00:
		_videoMode = al & 0x7F;
		_mem[kBda + 0x49] = _videoMode;
		_mem[kBda + 0x50] = _mem[kBda + 0x51] = 0;
		memcpy(_dac, kEgaColours, sizeof(kEgaColours));
		if (_videoMode == 0x13) {
			// The VGA's default colours after the 16 EGA ones: a grey ramp
			for (int i = 16; i < 32; i++)
				_dac[i * 3] = _dac[i * 3 + 1] = _dac[i * 3 + 2] = (i - 16) * 4;
			if (!(al & 0x80))
				memset(&_mem[0xA0000], 0, 64000);
		} else if (!(al & 0x80)) {
			scrollText(0, 24, 0, 7);
		}
		_paletteDirty = true;
		break;
	case 0x01:
		break;
	case 0x02:
		_mem[kBda + 0x50] = _cpu.rb(2) % 80; // DL
		_mem[kBda + 0x51] = _cpu.rb(6) % 25; // DH
		break;
	case 0x03:
		_cpu.wb(2, _mem[kBda + 0x50]);
		_cpu.wb(6, _mem[kBda + 0x51]);
		_cpu.r[Cpu8086::kCX] = 0x0607;
		break;
	case 0x05:
		break;
	case 0x06:
	case 0x07:
		if (_videoMode == 3)
			scrollText(_cpu.rb(5), MIN<int>(_cpu.rb(6), 24), al, _cpu.rb(7));
		break;
	case 0x08: {
		int pos = (_mem[kBda + 0x51] * 80 + _mem[kBda + 0x50]) * 2;
		_cpu.r[Cpu8086::kAX] = _mem[0xB8000 + pos] | (_mem[0xB8000 + pos + 1] << 8);
		break;
	}
	case 0x09:
	case 0x0A: {
		int count = _cpu.r[Cpu8086::kCX];
		uint8 col = _mem[kBda + 0x50];
		for (int i = 0; i < count && col + i < 80; i++) {
			_mem[kBda + 0x50] = col + i;
			putChar(al, ah == 0x09 ? _cpu.rb(3) : _mem[0xB8000 + (_mem[kBda + 0x51] * 80 + col + i) * 2 + 1], false);
		}
		_mem[kBda + 0x50] = col;
		break;
	}
	case 0x0C:
		if (_videoMode == 0x13 && _cpu.r[Cpu8086::kCX] < 320 && _cpu.r[Cpu8086::kDX] < 200)
			_mem[0xA0000 + _cpu.r[Cpu8086::kDX] * 320 + _cpu.r[Cpu8086::kCX]] = al;
		break;
	case 0x0D:
		if (_videoMode == 0x13 && _cpu.r[Cpu8086::kCX] < 320 && _cpu.r[Cpu8086::kDX] < 200)
			_cpu.wb(0, _mem[0xA0000 + _cpu.r[Cpu8086::kDX] * 320 + _cpu.r[Cpu8086::kCX]]);
		break;
	case 0x0E:
		putChar(al, 0, true);
		break;
	case 0x0F:
		_cpu.wb(0, _videoMode);
		_cpu.wb(4, _videoMode == 0x13 ? 40 : 80);
		_cpu.wb(7, 0);
		break;
	case 0x10:
		switch (al) {
		case 0x10: {
			uint8 i = _cpu.r[Cpu8086::kBX] & 0xFF;
			_dac[i * 3] = _cpu.rb(6) & 63;
			_dac[i * 3 + 1] = _cpu.rb(5) & 63;
			_dac[i * 3 + 2] = _cpu.rb(1) & 63;
			_paletteDirty = true;
			break;
		}
		case 0x12: {
			uint16 first = _cpu.r[Cpu8086::kBX], count = _cpu.r[Cpu8086::kCX];
			for (uint i = 0; i < count && first + i < 256; i++)
				for (int c = 0; c < 3; c++)
					_dac[(first + i) * 3 + c] = _cpu.mem8(_cpu.seg[Cpu8086::kES], _cpu.r[Cpu8086::kDX] + i * 3 + c) & 63;
			_paletteDirty = true;
			break;
		}
		case 0x15: {
			uint8 i = _cpu.r[Cpu8086::kBX] & 0xFF;
			_cpu.wb(6, _dac[i * 3]);
			_cpu.wb(5, _dac[i * 3 + 1]);
			_cpu.wb(1, _dac[i * 3 + 2]);
			break;
		}
		case 0x17: {
			uint16 first = _cpu.r[Cpu8086::kBX], count = _cpu.r[Cpu8086::kCX];
			for (uint i = 0; i < count && first + i < 256; i++)
				for (int c = 0; c < 3; c++)
					_cpu.setMem8(_cpu.seg[Cpu8086::kES], _cpu.r[Cpu8086::kDX] + i * 3 + c, _dac[(first + i) * 3 + c]);
			break;
		}
		default:
			break;
		}
		break;
	case 0x11:
		if (al == 0x30) {
			_cpu.r[Cpu8086::kCX] = 8;
			_cpu.wb(2, 24);
		}
		break;
	case 0x12:
		if (_cpu.rb(3) == 0x10) {
			_cpu.wb(7, 0);
			_cpu.wb(3, 3);
			_cpu.r[Cpu8086::kCX] = 0x0009;
		}
		break;
	case 0x1A:
		if (al == 0) {
			_cpu.wb(0, 0x1A);
			_cpu.r[Cpu8086::kBX] = 0x0008; // VGA with colour display
		}
		break;
	default:
		break;
	}
}

void DosPC::keyboardBios() {
	uint8 ah = _cpu.rb(4);
	switch (ah) {
	case 0x00:
	case 0x10:
		if (_keys.empty()) {
			// Wait here, with interrupts on so the timer and keyboard run
			_cpu.ip -= 2;
			_cpu.flags |= Cpu8086::kIF;
			_waitingForKey = true;
			return;
		}
		_cpu.r[Cpu8086::kAX] = _keys[0];
		_keys.remove_at(0);
		break;
	case 0x01:
	case 0x11:
		if (_keys.empty()) {
			setStackedFlag(Cpu8086::kZF, true);
			_waitingForKey = true;
		} else {
			_cpu.r[Cpu8086::kAX] = _keys[0];
			setStackedFlag(Cpu8086::kZF, false);
		}
		break;
	case 0x02:
	case 0x12:
		_cpu.wb(0, _mem[kBda + 0x17]);
		break;
	default:
		break;
	}
}

// --- Keyboard and mouse from the host ---

void DosPC::keyDown(uint8 scan, uint8 ascii) {
	_scanQueue.push_back(scan);
	_keys.push_back((scan << 8) | ascii);
}

void DosPC::keyUp(uint8 scan) {
	_scanQueue.push_back(scan | 0x80);
}

void DosPC::mouseMove(int x, int y) {
	_mouseX = CLIP(x, _mouseMinX, _mouseMaxX);
	_mouseY = CLIP(y / 2, _mouseMinY, _mouseMaxY);
}

void DosPC::mouseButton(int button, bool down) {
	if (button < 0 || button > 2)
		return;
	if (down) {
		_mouseButtons |= 1 << button;
		_mousePresses[button]++;
	} else {
		_mouseButtons &= ~(1 << button);
		_mouseReleases[button]++;
	}
}

void DosPC::int33() {
	uint16 ax = _cpu.r[Cpu8086::kAX];
	switch (ax) {
	case 0x00:
	case 0x21:
		_cpu.r[Cpu8086::kAX] = 0xFFFF;
		_cpu.r[Cpu8086::kBX] = 2;
		_mouseMinX = 0;
		_mouseMaxX = 639;
		_mouseMinY = 0;
		_mouseMaxY = 199;
		_mouseCursor = -1;
		break;
	case 0x01:
		_mouseCursor = MIN(_mouseCursor + 1, 0);
		break;
	case 0x02:
		_mouseCursor--;
		break;
	case 0x03:
		_cpu.r[Cpu8086::kBX] = _mouseButtons;
		_cpu.r[Cpu8086::kCX] = _mouseX;
		_cpu.r[Cpu8086::kDX] = _mouseY;
		break;
	case 0x04:
		_mouseX = CLIP<int>(_cpu.r[Cpu8086::kCX], _mouseMinX, _mouseMaxX);
		_mouseY = CLIP<int>(_cpu.r[Cpu8086::kDX], _mouseMinY, _mouseMaxY);
		break;
	case 0x05:
	case 0x06: {
		int b = MIN<int>(_cpu.r[Cpu8086::kBX], 2);
		_cpu.r[Cpu8086::kAX] = _mouseButtons;
		_cpu.r[Cpu8086::kBX] = ax == 5 ? _mousePresses[b] : _mouseReleases[b];
		if (ax == 5)
			_mousePresses[b] = 0;
		else
			_mouseReleases[b] = 0;
		_cpu.r[Cpu8086::kCX] = _mouseX;
		_cpu.r[Cpu8086::kDX] = _mouseY;
		break;
	}
	case 0x07:
		_mouseMinX = MIN<int16>(_cpu.r[Cpu8086::kCX], _cpu.r[Cpu8086::kDX]);
		_mouseMaxX = MAX<int16>(_cpu.r[Cpu8086::kCX], _cpu.r[Cpu8086::kDX]);
		break;
	case 0x08:
		_mouseMinY = MIN<int16>(_cpu.r[Cpu8086::kCX], _cpu.r[Cpu8086::kDX]);
		_mouseMaxY = MAX<int16>(_cpu.r[Cpu8086::kCX], _cpu.r[Cpu8086::kDX]);
		break;
	case 0x0B:
		_cpu.r[Cpu8086::kCX] = 0;
		_cpu.r[Cpu8086::kDX] = 0;
		break;
	case 0x24:
		_cpu.r[Cpu8086::kBX] = 0x0626;
		_cpu.r[Cpu8086::kCX] = 0x0400;
		break;
	default:
		break;
	}
}

// --- DOS ---

Common::String DosPC::readString(uint16 s, uint16 o, int max) {
	Common::String str;
	for (int i = 0; i < max; i++) {
		uint8 c = _cpu.mem8(s, o + i);
		if (!c)
			break;
		str += (char)c;
	}
	return str;
}

// All files live in one folder: drop drive and directories
Common::String DosPC::dosName(const Common::String &path) const {
	Common::String name = path;
	for (int i = (int)name.size() - 1; i >= 0; i--) {
		if (name[i] == '\\' || name[i] == '/' || name[i] == ':') {
			name = name.substr(i + 1);
			break;
		}
	}
	while (!name.empty() && name.lastChar() == ' ')
		name.deleteLastChar();
	name.toUppercase();
	return name;
}

bool DosPC::loadFile(const Common::String &name, Common::Array<byte> &data) {
	if (_overrides.contains(name)) {
		data = _overrides[name];
		return true;
	}
	return _host && _host->readFile(name, data);
}

int DosPC::openHandle(const Common::String &name, bool create) {
	File f;
	f.name = dosName(name);
	f.pos = 0;
	f.open = true;
	f.device = false;
	f.dirty = create;
	if (f.name == "NUL" || f.name == "CON") {
		f.device = true;
	} else if (!create && !loadFile(f.name, f.data)) {
		return -kErrFileNotFound;
	}
	for (uint i = 5; i < _files.size(); i++) {
		if (!_files[i].open) {
			_files[i] = f;
			return i;
		}
	}
	if (_files.size() >= 64)
		return -kErrTooMany;
	_files.push_back(f);
	return _files.size() - 1;
}

// Memory control blocks, as DOS keeps them
uint16 DosPC::allocate(uint16 paras, uint16 owner, uint16 *largest) {
	uint16 m = kFirstMcb, best = 0;
	*largest = 0;
	for (int guard = 0; guard < 1000; guard++) {
		uint32 a = m << 4;
		uint16 size = READ_LE_UINT16(&_mem[a + 3]);
		if (READ_LE_UINT16(&_mem[a + 1]) == 0) {
			// Merge with free blocks that follow
			while (_mem[a] == 'M') {
				uint32 n = (m + size + 1) << 4;
				if (READ_LE_UINT16(&_mem[n + 1]) != 0)
					break;
				_mem[a] = _mem[n];
				size += READ_LE_UINT16(&_mem[n + 3]) + 1;
				WRITE_LE_UINT16(&_mem[a + 3], size);
			}
			if (size >= paras) {
				best = m;
				break;
			}
			*largest = MAX(*largest, size);
		}
		if (_mem[a] == 'Z')
			break;
		m += size + 1;
	}
	if (!best)
		return 0;
	uint32 a = best << 4;
	uint16 size = READ_LE_UINT16(&_mem[a + 3]);
	if (size > paras + 1) {
		uint32 n = (best + paras + 1) << 4;
		_mem[n] = _mem[a];
		WRITE_LE_UINT16(&_mem[n + 1], 0);
		WRITE_LE_UINT16(&_mem[n + 3], size - paras - 1);
		_mem[a] = 'M';
		WRITE_LE_UINT16(&_mem[a + 3], paras);
	}
	WRITE_LE_UINT16(&_mem[a + 1], owner);
	return best + 1;
}

bool DosPC::resize(uint16 segment, uint16 paras, uint16 *largest) {
	uint16 m = segment - 1;
	uint32 a = m << 4;
	uint16 size = READ_LE_UINT16(&_mem[a + 3]);
	// Take in free blocks that follow
	while (_mem[a] == 'M') {
		uint32 n = (m + size + 1) << 4;
		if (READ_LE_UINT16(&_mem[n + 1]) != 0)
			break;
		_mem[a] = _mem[n];
		size += READ_LE_UINT16(&_mem[n + 3]) + 1;
	}
	WRITE_LE_UINT16(&_mem[a + 3], size);
	if (paras > size) {
		*largest = size;
		return false;
	}
	if (size > paras + 1) {
		uint32 n = (m + paras + 1) << 4;
		_mem[n] = _mem[a];
		WRITE_LE_UINT16(&_mem[n + 1], 0);
		WRITE_LE_UINT16(&_mem[n + 3], size - paras - 1);
		_mem[a] = 'M';
		WRITE_LE_UINT16(&_mem[a + 3], paras);
	}
	return true;
}

void DosPC::freeBlock(uint16 segment) {
	WRITE_LE_UINT16(&_mem[((segment - 1) << 4) + 1], 0);
}

void DosPC::freeOwnedBy(uint16 psp) {
	uint16 m = kFirstMcb;
	for (int guard = 0; guard < 1000; guard++) {
		uint32 a = m << 4;
		if (READ_LE_UINT16(&_mem[a + 1]) == psp)
			WRITE_LE_UINT16(&_mem[a + 1], 0);
		if (_mem[a] == 'Z')
			break;
		m += READ_LE_UINT16(&_mem[a + 3]) + 1;
	}
}

bool DosPC::exec(const Common::String &path, const Common::String &args, bool fromProgram) {
	Common::String name = dosName(path);
	Common::Array<byte> image;
	if (!loadFile(name, image) || image.size() < 2) {
		warning("DosPC: cannot run %s", name.c_str());
		return false;
	}
	bool exe = image[0] == 'M' && image[1] == 'Z';
	uint16 largest;

	// Environment: no variables, then the program's own path
	Common::String full = "C:\\" + name;
	uint16 env = allocate(4 + (full.size() + 16) / 16, 0xFFFF, &largest);
	if (!env)
		return false;
	uint32 e = env << 4;
	memset(&_mem[e], 0, 64);
	memcpy(&_mem[e], "PATH=C:\\", 8);
	WRITE_LE_UINT16(&_mem[e + 10], 1);
	memcpy(&_mem[e + 12], full.c_str(), full.size());

	// The program gets the biggest block
	allocate(0xFFFF, 0, &largest);
	uint16 psp = allocate(largest, 0xFFFF, &largest);
	if (!psp) {
		freeBlock(env);
		return false;
	}
	uint16 blockParas = READ_LE_UINT16(&_mem[((psp - 1) << 4) + 3]);
	WRITE_LE_UINT16(&_mem[((psp - 1) << 4) + 1], psp);
	WRITE_LE_UINT16(&_mem[((env - 1) << 4) + 1], psp);

	uint32 p = psp << 4;
	memset(&_mem[p], 0, 256);
	_mem[p] = 0xCD;
	_mem[p + 1] = 0x20;
	WRITE_LE_UINT16(&_mem[p + 2], psp + blockParas);
	WRITE_LE_UINT16(&_mem[p + 0x16], _currentPsp ? _currentPsp : psp);
	for (int i = 0; i < 20; i++)
		_mem[p + 0x18 + i] = i < 5 ? i : 0xFF;
	WRITE_LE_UINT16(&_mem[p + 0x2C], env);
	WRITE_LE_UINT16(&_mem[p + 0x32], 20);
	WRITE_LE_UINT16(&_mem[p + 0x34], 0x18);
	WRITE_LE_UINT16(&_mem[p + 0x36], psp);
	_mem[p + 0x50] = 0xCD;
	_mem[p + 0x51] = 0x21;
	_mem[p + 0x52] = 0xCB;
	memset(&_mem[p + 0x5C], ' ', 11);
	_mem[p + 0x5C] = 0;
	memset(&_mem[p + 0x6C], ' ', 11);
	_mem[p + 0x6C] = 0;
	Common::String tail = args.empty() ? "" : " " + args;
	_mem[p + 0x80] = MIN<uint>(tail.size(), 126);
	memcpy(&_mem[p + 0x81], tail.c_str(), _mem[p + 0x80]);
	_mem[p + 0x81 + _mem[p + 0x80]] = 0x0D;

	Process proc;
	proc.psp = psp;
	proc.ss = fromProgram ? _cpu.seg[Cpu8086::kSS] : 0;
	proc.sp = fromProgram ? _cpu.r[Cpu8086::kSP] : 0;

	uint16 cs, ipv, ss, sp;
	if (exe) {
		uint16 lastPage = READ_LE_UINT16(&image[2]), pages = READ_LE_UINT16(&image[4]);
		uint16 relocs = READ_LE_UINT16(&image[6]), header = READ_LE_UINT16(&image[8]) * 16;
		uint16 minAlloc = READ_LE_UINT16(&image[10]);
		uint32 size = pages * 512 - (lastPage ? 512 - lastPage : 0) - header;
		if (header + size > image.size())
			size = image.size() - header;
		uint16 load = psp + 0x10;
		if ((size + 15) / 16 + minAlloc + 0x10 > blockParas) {
			freeOwnedBy(psp);
			warning("DosPC: not enough memory for %s", name.c_str());
			return false;
		}
		memcpy(&_mem[load << 4], &image[header], size);
		uint16 relocTable = READ_LE_UINT16(&image[0x18]);
		for (uint i = 0; i < relocs; i++) {
			uint32 at = relocTable + i * 4;
			if (at + 4 > image.size())
				break;
			uint32 a = ((load + READ_LE_UINT16(&image[at + 2])) << 4) + READ_LE_UINT16(&image[at]);
			WRITE_LE_UINT16(&_mem[a & 0xFFFFF], READ_LE_UINT16(&_mem[a & 0xFFFFF]) + load);
		}
		ss = load + READ_LE_UINT16(&image[0x0E]);
		sp = READ_LE_UINT16(&image[0x10]);
		ipv = READ_LE_UINT16(&image[0x14]);
		cs = load + READ_LE_UINT16(&image[0x16]);
	} else {
		memcpy(&_mem[(p + 0x100) & 0xFFFFF], image.data(), MIN<uint>(image.size(), 0xFF00));
		cs = ss = psp;
		ipv = 0x100;
		sp = 0xFFFE;
		WRITE_LE_UINT16(&_mem[p + 0xFFFE], 0);
	}

	_processes.push_back(proc);
	_currentPsp = psp;
	_cpu.seg[Cpu8086::kCS] = cs;
	_cpu.ip = ipv;
	_cpu.seg[Cpu8086::kSS] = ss;
	_cpu.r[Cpu8086::kSP] = sp;
	_cpu.seg[Cpu8086::kDS] = _cpu.seg[Cpu8086::kES] = psp;
	_cpu.r[Cpu8086::kAX] = 0;
	_cpu.r[Cpu8086::kBX] = 0;
	_cpu.r[Cpu8086::kCX] = 0xFF;
	_cpu.r[Cpu8086::kDX] = psp;
	_cpu.r[Cpu8086::kSI] = ipv;
	_cpu.r[Cpu8086::kDI] = sp;
	_cpu.r[Cpu8086::kBP] = 0x091C;
	_cpu.flags = 0xF202;
	_cpu.halted = false;
	_dta[0] = 0x80;
	_dta[1] = psp;
	debug(1, "DosPC: running %s %s at %04X:%04X, PSP %04X", name.c_str(), args.c_str(), cs, ipv, psp);
	return true;
}

void DosPC::terminate(uint8 code, bool keep, uint16 keepParas) {
	if (_processes.empty()) {
		_finished = true;
		return;
	}
	Process proc = _processes.back();
	_processes.pop_back();
	if (keep) {
		uint16 largest;
		resize(proc.psp, MAX<uint16>(keepParas, 0x10), &largest);
	} else {
		freeOwnedBy(proc.psp);
	}
	_returnCode = code;
	debug(1, "DosPC: program ended with %d%s", code, keep ? " (resident)" : "");

	if (proc.ss) {
		// Back to the parent, just after its EXEC call
		_currentPsp = _processes.empty() ? 0 : _processes.back().psp;
		_cpu.seg[Cpu8086::kSS] = proc.ss;
		_cpu.r[Cpu8086::kSP] = proc.sp;
		_cpu.seg[Cpu8086::kCS] = kBiosSeg;
		_cpu.ip = stub(0x21) + 2;
		setStackedFlag(Cpu8086::kCF, false);
		return;
	}
	// A program from our own list: start the next one
	_currentPsp = 0;
	if (_nextProgram < _programs.size()) {
		const Program &p = _programs[_nextProgram++];
		if (exec(p.name, p.args, false))
			return;
	}
	_finished = true;
}

bool DosPC::wildcardMatch(const Common::String &pattern, const Common::String &name) {
	Common::String pb = pattern, pe, nb = name, ne;
	int dot = pattern.findLastOf('.');
	if (dot >= 0) {
		pb = pattern.substr(0, dot);
		pe = pattern.substr(dot + 1);
	}
	dot = name.findLastOf('.');
	if (dot >= 0) {
		nb = name.substr(0, dot);
		ne = name.substr(dot + 1);
	}
	auto part = [](const Common::String &p, const Common::String &n) {
		for (uint i = 0; i < p.size(); i++) {
			if (p[i] == '*')
				return true;
			if (p[i] == '?')
				continue;
			if (i >= n.size() || p[i] != n[i])
				return false;
		}
		return n.size() <= p.size();
	};
	if (pattern == "*.*")
		return true;
	return part(pb, nb) && part(pe.empty() && dot < 0 ? "" : pe, ne);
}

bool DosPC::findNext() {
	while (_foundPos < _found.size()) {
		const Common::String &name = _found[_foundPos++];
		Common::Array<byte> data;
		if (!loadFile(name, data))
			continue;
		uint32 d = (_dta[1] << 4) + _dta[0];
		_mem[d + 0x15] = 0x20;
		WRITE_LE_UINT16(&_mem[d + 0x16], 0x6000);
		WRITE_LE_UINT16(&_mem[d + 0x18], 0x1321);
		WRITE_LE_UINT32(&_mem[d + 0x1A], data.size());
		memset(&_mem[d + 0x1E], 0, 13);
		memcpy(&_mem[d + 0x1E], name.c_str(), MIN<uint>(name.size(), 12));
		return true;
	}
	return false;
}

void DosPC::int21() {
	uint8 ah = _cpu.rb(4), al = _cpu.rb(0);
	uint16 &ax = _cpu.r[Cpu8086::kAX], &bx = _cpu.r[Cpu8086::kBX], &cx = _cpu.r[Cpu8086::kCX], &dx = _cpu.r[Cpu8086::kDX];
	uint16 ds = _cpu.seg[Cpu8086::kDS];
	auto fail = [&](int err) {
		ax = err;
		setStackedFlag(Cpu8086::kCF, true);
	};
	auto ok = [&]() { setStackedFlag(Cpu8086::kCF, false); };

	switch (ah) {
	case 0x00:
		terminate(0, false, 0);
		break;
	case 0x01:
	case 0x07:
	case 0x08:
		if (_keys.empty()) {
			_cpu.ip -= 2;
			_cpu.flags |= Cpu8086::kIF;
			_waitingForKey = true;
			return;
		}
		_cpu.wb(0, _keys[0] & 0xFF);
		if ((_keys[0] & 0xFF) == 0)
			_keys[0] = _keys[0] >> 8; // the scan code comes next time
		else
			_keys.remove_at(0);
		break;
	case 0x02:
		putChar(_cpu.rb(2), 7, true);
		break;
	case 0x06:
		if (_cpu.rb(2) == 0xFF) {
			if (_keys.empty()) {
				setStackedFlag(Cpu8086::kZF, true);
				_cpu.wb(0, 0);
			} else {
				setStackedFlag(Cpu8086::kZF, false);
				_cpu.wb(0, _keys[0] & 0xFF);
				_keys.remove_at(0);
			}
		} else {
			putChar(_cpu.rb(2), 7, true);
		}
		break;
	case 0x09:
		for (int i = 0; i < 4096; i++) {
			uint8 c = _cpu.mem8(ds, dx + i);
			if (c == '$')
				break;
			putChar(c, 7, true);
		}
		break;
	case 0x0B:
		_cpu.wb(0, _keys.empty() ? 0 : 0xFF);
		break;
	case 0x0C:
		_keys.clear();
		if (al == 0x01 || al == 0x06 || al == 0x07 || al == 0x08) {
			_cpu.wb(4, al);
			int21();
		}
		break;
	case 0x0E:
		_cpu.wb(0, 26);
		break;
	case 0x19:
		_cpu.wb(0, 2);
		break;
	case 0x1A:
		_dta[0] = dx;
		_dta[1] = ds;
		break;
	case 0x25:
		_cpu.setMem16(0, al * 4, dx);
		_cpu.setMem16(0, al * 4 + 2, ds);
		break;
	case 0x29:
		_cpu.wb(0, 0);
		break;
	case 0x2A:
		cx = 1990;
		dx = 0x0101;
		_cpu.wb(0, 1);
		break;
	case 0x2C: {
		uint32 hs = _frame * kFrameMs / 10;
		_cpu.wb(5, 12 + (hs / 360000) % 12);
		_cpu.wb(1, (hs / 6000) % 60);
		_cpu.wb(6, (hs / 100) % 60);
		_cpu.wb(2, hs % 100);
		break;
	}
	case 0x2F:
		bx = _dta[0];
		_cpu.seg[Cpu8086::kES] = _dta[1];
		break;
	case 0x30:
		ax = 0x0005;
		bx = 0;
		cx = 0;
		break;
	case 0x31:
		terminate(al, true, dx);
		break;
	case 0x33:
		if (al == 0)
			_cpu.wb(2, 0);
		else if (al == 5)
			_cpu.wb(2, 3);
		break;
	case 0x35:
		bx = _cpu.mem16(0, al * 4);
		_cpu.seg[Cpu8086::kES] = _cpu.mem16(0, al * 4 + 2);
		break;
	case 0x36:
		ax = 4;
		bx = 4000;
		cx = 512;
		dx = 8000;
		break;
	case 0x3B:
		ok();
		break;
	case 0x3C:
	case 0x3D: {
		int h = openHandle(readString(ds, dx), ah == 0x3C);
		if (h < 0) {
			fail(-h);
		} else {
			ax = h;
			ok();
		}
		debug(1, "DosPC: open %s -> %d", readString(ds, dx).c_str(), h);
		break;
	}
	case 0x3E:
		if (bx < _files.size() && _files[bx].open) {
			File &f = _files[bx];
			if (f.dirty && !f.device && _host)
				_host->writeFile(f.name, f.data);
			if (bx >= 5)
				f.open = false;
			ok();
		} else {
			fail(kErrHandle);
		}
		break;
	case 0x3F:
		if (bx < _files.size() && _files[bx].open) {
			File &f = _files[bx];
			uint32 n = 0;
			if (!f.device) {
				n = MIN<uint32>(cx, f.pos < f.data.size() ? f.data.size() - f.pos : 0);
				for (uint32 i = 0; i < n; i++)
					_cpu.setMem8(ds, dx + i, f.data[f.pos + i]);
				f.pos += n;
			}
			ax = n;
			ok();
		} else {
			fail(kErrHandle);
		}
		break;
	case 0x40:
		if (bx < _files.size() && _files[bx].open) {
			File &f = _files[bx];
			if (f.device || bx < 5) {
				if (bx == 1 || bx == 2)
					for (uint i = 0; i < cx; i++)
						putChar(_cpu.mem8(ds, dx + i), 7, true);
			} else if (cx == 0) {
				f.data.resize(f.pos);
				f.dirty = true;
			} else {
				if (f.pos + cx > f.data.size())
					f.data.resize(f.pos + cx);
				for (uint i = 0; i < cx; i++)
					f.data[f.pos + i] = _cpu.mem8(ds, dx + i);
				f.pos += cx;
				f.dirty = true;
			}
			ax = cx;
			ok();
		} else {
			fail(kErrHandle);
		}
		break;
	case 0x41:
		ok();
		break;
	case 0x42:
		if (bx < _files.size() && _files[bx].open) {
			File &f = _files[bx];
			int32 off = (int32)(((uint32)cx << 16) | dx);
			int32 base = al == 0 ? 0 : al == 1 ? (int32)f.pos : (int32)f.data.size();
			f.pos = MAX<int32>(0, base + off);
			ax = f.pos & 0xFFFF;
			dx = f.pos >> 16;
			ok();
		} else {
			fail(kErrHandle);
		}
		break;
	case 0x43: {
		Common::Array<byte> data;
		if (loadFile(dosName(readString(ds, dx)), data)) {
			cx = 0x20;
			ok();
		} else {
			fail(kErrFileNotFound);
		}
		break;
	}
	case 0x44:
		switch (al) {
		case 0x00:
			if (bx < _files.size() && _files[bx].open) {
				dx = (_files[bx].device || bx < 5) ? 0x80D3 : 0x0002;
				ok();
			} else {
				fail(kErrHandle);
			}
			break;
		case 0x08:
			ax = 1;
			ok();
			break;
		default:
			ok();
			break;
		}
		break;
	case 0x47:
		_cpu.setMem8(ds, _cpu.r[Cpu8086::kSI], 0);
		ok();
		break;
	case 0x48: {
		uint16 largest;
		uint16 s = allocate(bx, _currentPsp, &largest);
		if (s) {
			ax = s;
			ok();
		} else {
			bx = largest;
			fail(kErrMemory);
		}
		break;
	}
	case 0x49:
		freeBlock(_cpu.seg[Cpu8086::kES]);
		ok();
		break;
	case 0x4A: {
		uint16 largest;
		if (resize(_cpu.seg[Cpu8086::kES], bx, &largest)) {
			ok();
		} else {
			bx = largest;
			fail(kErrMemory);
		}
		break;
	}
	case 0x4B: {
		if (al != 0) {
			fail(kErrFunction);
			break;
		}
		Common::String name = readString(ds, dx);
		uint16 pb = bx, es = _cpu.seg[Cpu8086::kES];
		uint16 tailOff = _cpu.mem16(es, pb + 2), tailSeg = _cpu.mem16(es, pb + 4);
		uint8 len = _cpu.mem8(tailSeg, tailOff);
		Common::String args;
		for (int i = 0; i < len; i++)
			args += (char)_cpu.mem8(tailSeg, tailOff + 1 + i);
		args.trim();
		if (!exec(name, args, true))
			fail(kErrFileNotFound);
		break;
	}
	case 0x4C:
		terminate(al, false, 0);
		break;
	case 0x4D:
		ax = _returnCode;
		break;
	case 0x4E:
	case 0x4F:
		if (ah == 0x4E) {
			Common::String pattern = dosName(readString(ds, dx));
			_found.clear();
			_foundPos = 0;
			if (_host)
				for (const Common::String &n : _host->listFiles())
					if (wildcardMatch(pattern, n))
						_found.push_back(n);
			for (auto &o : _overrides)
				if (wildcardMatch(pattern, o._key))
					_found.push_back(o._key);
		}
		if (findNext())
			ok();
		else
			fail(ah == 0x4E ? kErrFileNotFound : kErrNoMoreFiles);
		break;
	case 0x50:
		_currentPsp = bx;
		break;
	case 0x51:
	case 0x62:
		bx = _currentPsp;
		break;
	case 0x56:
	case 0x57:
		cx = 0x6000;
		dx = 0x1321;
		ok();
		break;
	case 0x58:
		ax = 0;
		ok();
		break;
	case 0x59:
		ax = 0;
		break;
	default:
		debug(1, "DosPC: unhandled DOS call %02X", ah);
		fail(kErrFunction);
		break;
	}
}

// --- Running ---

bool DosPC::runFrame() {
	if (_finished)
		return false;
	// Timer interrupts spread across the frame; ones the PC can't take yet
	// (interrupts off, or the last still in service) wait their turn
	_tickAccum += (int32)(((uint64)kPitHz * kFrameMs * 1000 / _pitDivisor) / 1000);
	int ticks = _tickAccum / 1000;
	_tickAccum %= 1000;
	int interval = kInstructionsPerFrame / MAX(ticks, 1);
	int nextTick = interval / 2;
	int pending = 0;
	_waitingForKey = false;
	for (int i = 0; i < kInstructionsPerFrame && !_finished; i++) {
		_cpu.step();
		if (i == nextTick && ticks > 0) {
			pending++;
			ticks--;
			nextTick += interval;
		}
		if (pending && raiseIrq(0))
			pending--;
		if ((i & 1023) == 0 && !_scanQueue.empty() && _cpu.flag(Cpu8086::kIF) && !(_picMask & 2) && !(_picInService & 3)) {
			_port60 = _scanQueue[0];
			_scanQueue.remove_at(0);
			raiseIrq(1);
		}
	}
	_frame++;
	for (int i = 0; i < 256 * 3; i++)
		_rgb[i] = (_dac[i] << 2) | (_dac[i] >> 4);
	return !_finished;
}

void DosPC::render(byte *dst, int pitch) {
	if (_videoMode == 0x13) {
		const byte *src = &_mem[0xA0000];
		for (int y = 0; y < 200; y++) {
			byte *o = dst + y * 2 * pitch;
			for (int x = 0; x < 320; x++)
				o[2 * x] = o[2 * x + 1] = src[y * 320 + x];
			memcpy(o + pitch, o, kScreenW);
		}
	} else {
		// Text: 80x25 from the font the engine supplied at memory F000:A000
		const byte *vram = &_mem[0xB8000];
		const byte *font = &_mem[0xFA000];
		for (int row = 0; row < 25; row++) {
			for (int col = 0; col < 80; col++) {
				byte ch = vram[(row * 80 + col) * 2], attr = vram[(row * 80 + col) * 2 + 1];
				for (int y = 0; y < 16; y++) {
					byte bits = font[ch * 8 + y / 2];
					byte *o = dst + (row * 16 + y) * pitch + col * 8;
					for (int x = 0; x < 8; x++)
						o[x] = (bits & (0x80 >> x)) ? (attr & 15) : (attr >> 4) & 7;
				}
			}
		}
	}
	// The mouse driver's arrow, if shown
	if (_mouseCursor >= 0 && _videoMode == 0x13) {
		static const char *const kArrow[] = {
			"X.........", "XX........", "XWX.......", "XWWX......", "XWWWX.....", "XWWWWX....",
			"XWWWWWX...", "XWWWWWWX..", "XWWWWWWWX.", "XWWWWXXXXX", "XWXWWX....", "XX.XWWX...",
			"X...XWWX..", "....XWWX..", ".....XX..."
		};
		int mx = _mouseX, my = _mouseY * 2;
		for (int y = 0; y < 15 * 2; y++) {
			for (int x = 0; x < 10 * 2; x++) {
				char c = kArrow[y / 2][x / 2];
				int px = mx + x, py = my + y;
				if (c == '.' || px >= kScreenW || py >= kScreenH)
					continue;
				dst[py * pitch + px] = c == 'X' ? 0 : 15;
			}
		}
	}
}

void DosPC::saveState(Common::WriteStream &s) {
	s.writeUint32BE(MKTAG('D', 'O', 'S', 'P'));
	s.writeUint32LE(1);
	s.write(_mem, 0x100000);
	for (int i = 0; i < 8; i++)
		s.writeUint16LE(_cpu.r[i]);
	for (int i = 0; i < 4; i++)
		s.writeUint16LE(_cpu.seg[i]);
	s.writeUint16LE(_cpu.ip);
	s.writeUint16LE(_cpu.flags);
	s.writeByte(_cpu.halted);
	s.write(_dac, sizeof(_dac));
	s.writeByte(_videoMode);
	s.writeUint16LE(_currentPsp);
	s.writeUint16LE(_dta[0]);
	s.writeUint16LE(_dta[1]);
	s.writeUint32LE(_pitDivisor);
	s.writeByte(_picMask);
	s.writeUint32LE(_frame);
	s.writeUint32LE(_nextProgram);
	s.writeUint32LE(_processes.size());
	for (const Process &p : _processes) {
		s.writeUint16LE(p.psp);
		s.writeUint16LE(p.ss);
		s.writeUint16LE(p.sp);
	}
	s.writeUint32LE(_files.size());
	for (const File &f : _files) {
		s.writeByte(f.open | (f.dirty << 1) | (f.device << 2));
		s.writeUint32LE(f.pos);
		s.writeUint32LE(f.name.size());
		s.writeString(f.name);
		s.writeUint32LE(f.data.size());
		if (!f.data.empty())
			s.write(f.data.data(), f.data.size());
	}
	s.writeSint32LE(_mouseCursor);
}

bool DosPC::loadState(Common::ReadStream &s) {
	if (s.readUint32BE() != MKTAG('D', 'O', 'S', 'P') || s.readUint32LE() != 1)
		return false;
	s.read(_mem, 0x100000);
	for (int i = 0; i < 8; i++)
		_cpu.r[i] = s.readUint16LE();
	for (int i = 0; i < 4; i++)
		_cpu.seg[i] = s.readUint16LE();
	_cpu.ip = s.readUint16LE();
	_cpu.flags = s.readUint16LE();
	_cpu.halted = s.readByte();
	s.read(_dac, sizeof(_dac));
	_paletteDirty = true;
	_videoMode = s.readByte();
	_currentPsp = s.readUint16LE();
	_dta[0] = s.readUint16LE();
	_dta[1] = s.readUint16LE();
	_pitDivisor = s.readUint32LE();
	_picMask = s.readByte();
	_frame = s.readUint32LE();
	_nextProgram = s.readUint32LE();
	_processes.resize(s.readUint32LE());
	for (Process &p : _processes) {
		p.psp = s.readUint16LE();
		p.ss = s.readUint16LE();
		p.sp = s.readUint16LE();
	}
	_files.resize(s.readUint32LE());
	for (File &f : _files) {
		byte fl = s.readByte();
		f.open = fl & 1;
		f.dirty = fl & 2;
		f.device = fl & 4;
		f.pos = s.readUint32LE();
		f.name = s.readString(0, s.readUint32LE());
		f.data.resize(s.readUint32LE());
		if (!f.data.empty())
			s.read(f.data.data(), f.data.size());
	}
	_mouseCursor = s.readSint32LE();
	_keys.clear();
	_scanQueue.clear();
	_finished = false;
	return !s.err();
}

} // End of namespace DosGames
