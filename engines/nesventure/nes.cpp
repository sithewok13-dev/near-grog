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

#include "common/textconsole.h"
#include "nesventure/nes.h"

namespace NesVenture {

// The 2C02's colours (a common measured palette)
const byte Nes::kPalette[64 * 3] = {
	 84,  84,  84,   0,  30, 116,   8,  16, 144,  48,   0, 136,  68,   0, 100,  92,   0,  48,  84,   4,   0,  60,  24,   0,
	 32,  42,   0,   8,  58,   0,   0,  64,   0,   0,  60,   0,   0,  50,  60,   0,   0,   0,   0,   0,   0,   0,   0,   0,
	152, 150, 152,   8,  76, 196,  48,  50, 236,  92,  30, 228, 136,  20, 176, 160,  20, 100, 152,  34,  32, 120,  60,   0,
	 84,  90,   0,  40, 114,   0,   8, 124,   0,   0, 118,  40,   0, 102, 120,   0,   0,   0,   0,   0,   0,   0,   0,   0,
	236, 238, 236,  76, 154, 236, 120, 124, 236, 176,  98, 236, 228,  84, 236, 236,  88, 180, 236, 106, 100, 212, 136,  32,
	160, 170,   0, 116, 196,   0,  76, 208,  32,  56, 204, 108,  56, 180, 204,  60,  60,  60,   0,   0,   0,   0,   0,   0,
	236, 238, 236, 168, 204, 236, 188, 188, 236, 212, 178, 236, 236, 174, 236, 236, 174, 212, 236, 180, 176, 228, 196, 144,
	204, 210, 120, 180, 222, 120, 168, 226, 144, 152, 226, 180, 160, 214, 228, 160, 162, 160,   0,   0,   0,   0,   0,   0
};

Nes::Nes() : buttons(0), batteryDirty(false), cpu(this), _chrRam(false), _battery(false), _mapper(0), _mirror(kVertical),
		_mmc1Shift(0), _mmc1Count(0), _mmc1Ctrl(0x0C), _mmc1Chr0(0), _mmc1Chr1(0), _mmc1Prg(0), _mmc1LastWrite(0),
		_mmc3Select(0), _mmc3Latch(0), _mmc3Counter(0), _mmc3Reload(false), _mmc3IrqOn(false), _mmc3Irq(false),
		_cycles(0), _stall(0), _padShift(0), _padStrobe(0),
		_ctrl(0), _mask(0), _status(0), _oamAddr(0), _readBuf(0), _fineX(0), _v(0), _t(0), _w(false), _nmiPending(false), _line(0),
		_apu(nullptr) {
	cpu.noDecimal = true;
	memset(_prgRam, 0, sizeof(_prgRam));
	memset(_mmc3Regs, 0, sizeof(_mmc3Regs));
	memset(_prgMap, 0, sizeof(_prgMap));
	memset(_chrMap, 0, sizeof(_chrMap));
	memset(_ram, 0, sizeof(_ram));
	memset(_vram, 0, sizeof(_vram));
	memset(_palRam, 0, sizeof(_palRam));
	memset(_oam, 0, sizeof(_oam));
	memset(_screen, 0, sizeof(_screen));
	apuInit();
}

Nes::~Nes() {
	apuFree();
}

bool Nes::load(const Common::Array<byte> &ines) {
	if (ines.size() < 16 || ines[0] != 'N' || ines[1] != 'E' || ines[2] != 'S' || ines[3] != 0x1A)
		return false;
	const uint prgSize = ines[4] * 16384, chrSize = ines[5] * 8192;
	_mapper = (ines[6] >> 4) | (ines[7] & 0xF0);
	_battery = (ines[6] & 2) != 0;
	_mirror = (ines[6] & 1) ? kVertical : kHorizontal;
	const uint start = 16 + ((ines[6] & 4) ? 512 : 0);
	if (_mapper != 0 && _mapper != 1 && _mapper != 4) {
		warning("NES: cartridge chip (mapper) %d isn't supported", _mapper);
		return false;
	}
	if (start + prgSize + chrSize > ines.size() || prgSize == 0)
		return false;
	_prg = Common::Array<byte>(&ines[start], prgSize);
	_chrRam = chrSize == 0;
	if (_chrRam)
		_chr.resize(8192);
	else
		_chr = Common::Array<byte>(&ines[start + prgSize], chrSize);
	updateMaps();
	return true;
}

void Nes::reset() {
	_mmc1Ctrl = 0x0C;
	updateMaps();
	_ctrl = _mask = _status = 0;
	_w = false;
	_cycles = 0;
	cpu.reset(read(0xFFFC) | (read(0xFFFD) << 8));
}

// Cartridge chips

void Nes::updateMaps() {
	const uint prgBanks8 = _prg.size() / 0x2000;
	const uint chrBanks1 = MAX<uint>(1, _chr.size() / 0x400);
	if (_mapper == 1) {
		const int mode = (_mmc1Ctrl >> 2) & 3;
		const uint outer = _prg.size() > 0x40000 ? (_mmc1Chr0 & 0x10) * 2 : 0; // 512K boards: 256K halves
		const uint b = (_mmc1Prg & 0x0F) * 2 + outer, last = (outer + (MIN<uint>(prgBanks8, 32) - 2));
		uint p[4];
		if (mode < 2) {
			const uint b32 = ((_mmc1Prg & 0x0E) * 2) + outer;
			p[0] = b32; p[1] = b32 + 1; p[2] = b32 + 2; p[3] = b32 + 3;
		} else if (mode == 2) {
			p[0] = outer; p[1] = outer + 1; p[2] = b; p[3] = b + 1;
		} else {
			p[0] = b; p[1] = b + 1; p[2] = last; p[3] = last + 1;
		}
		for (int i = 0; i < 4; i++)
			_prgMap[i] = (p[i] % prgBanks8) * 0x2000;
		if (_mmc1Ctrl & 0x10) {
			for (int i = 0; i < 4; i++) {
				_chrMap[i] = ((_mmc1Chr0 * 4 + i) % chrBanks1) * 0x400;
				_chrMap[4 + i] = ((_mmc1Chr1 * 4 + i) % chrBanks1) * 0x400;
			}
		} else {
			for (int i = 0; i < 8; i++)
				_chrMap[i] = (((_mmc1Chr0 & 0x1E) * 4 + i) % chrBanks1) * 0x400;
		}
		static const Mirroring kMirror[4] = { kSingle0, kSingle1, kVertical, kHorizontal };
		_mirror = kMirror[_mmc1Ctrl & 3];
	} else if (_mapper == 4) {
		const uint second = prgBanks8 - 2, last = prgBanks8 - 1;
		const uint r6 = _mmc3Regs[6] % prgBanks8, r7 = _mmc3Regs[7] % prgBanks8;
		if (_mmc3Select & 0x40) {
			_prgMap[0] = second * 0x2000; _prgMap[2] = r6 * 0x2000;
		} else {
			_prgMap[0] = r6 * 0x2000; _prgMap[2] = second * 0x2000;
		}
		_prgMap[1] = r7 * 0x2000;
		_prgMap[3] = last * 0x2000;
		uint c[8];
		c[0] = _mmc3Regs[0] & 0xFE; c[1] = c[0] + 1;
		c[2] = _mmc3Regs[1] & 0xFE; c[3] = c[2] + 1;
		c[4] = _mmc3Regs[2]; c[5] = _mmc3Regs[3]; c[6] = _mmc3Regs[4]; c[7] = _mmc3Regs[5];
		for (int i = 0; i < 8; i++) {
			const int slot = (_mmc3Select & 0x80) ? (i ^ 4) : i;
			_chrMap[slot] = (c[i] % chrBanks1) * 0x400;
		}
	} else {
		for (int i = 0; i < 4; i++)
			_prgMap[i] = (i % prgBanks8) * 0x2000;
		for (int i = 0; i < 8; i++)
			_chrMap[i] = (i % chrBanks1) * 0x400;
	}
}

void Nes::mapperWrite(uint16 addr, uint8 v) {
	if (_mapper == 1) {
		// A write on the very next cycle (a read-modify-write's second write) is ignored
		if (_cycles == _mmc1LastWrite + 1) {
			_mmc1LastWrite = _cycles;
			return;
		}
		_mmc1LastWrite = _cycles;
		if (v & 0x80) {
			_mmc1Shift = 0;
			_mmc1Count = 0;
			_mmc1Ctrl |= 0x0C;
			updateMaps();
			return;
		}
		_mmc1Shift |= (v & 1) << _mmc1Count;
		if (++_mmc1Count < 5)
			return;
		switch ((addr >> 13) & 3) {
		case 0: _mmc1Ctrl = _mmc1Shift; break;
		case 1: _mmc1Chr0 = _mmc1Shift; break;
		case 2: _mmc1Chr1 = _mmc1Shift; break;
		default: _mmc1Prg = _mmc1Shift; break;
		}
		_mmc1Shift = 0;
		_mmc1Count = 0;
		updateMaps();
	} else if (_mapper == 4) {
		const bool odd = addr & 1;
		switch (addr & 0xE000) {
		case 0x8000:
			if (odd)
				_mmc3Regs[_mmc3Select & 7] = v;
			else
				_mmc3Select = v;
			updateMaps();
			break;
		case 0xA000:
			if (!odd)
				_mirror = (v & 1) ? kHorizontal : kVertical;
			break;
		case 0xC000:
			if (odd) {
				_mmc3Counter = 0;
				_mmc3Reload = true;
			} else {
				_mmc3Latch = v;
			}
			break;
		default:
			_mmc3IrqOn = odd;
			if (!odd)
				_mmc3Irq = false;
			break;
		}
	}
}

void Nes::mmc3Clock() {
	if (_mapper != 4)
		return;
	if (_mmc3Counter == 0 || _mmc3Reload) {
		_mmc3Counter = _mmc3Latch;
		_mmc3Reload = false;
	} else {
		_mmc3Counter--;
	}
	if (_mmc3Counter == 0 && _mmc3IrqOn)
		_mmc3Irq = true;
}

// CPU side

uint8 Nes::read(uint16 addr) {
	if (addr < 0x2000)
		return _ram[addr & 0x7FF];
	if (addr < 0x4000) {
		switch (addr & 7) {
		case 2: {
			uint8 r = (_status & 0xE0) | (_readBuf & 0x1F);
			_status &= ~0x80;
			_w = false;
			return r;
		}
		case 4:
			return _oam[_oamAddr];
		case 7: {
			uint8 r;
			const uint16 a = _v & 0x3FFF;
			if (a < 0x3F00) {
				r = _readBuf;
				_readBuf = ppuRead(a);
			} else {
				r = ppuRead(a);
				_readBuf = ppuRead(a - 0x1000);
			}
			_v += (_ctrl & 4) ? 32 : 1;
			return r;
		}
		default:
			return _readBuf;
		}
	}
	if (addr == 0x4015)
		return apuStatus();
	if (addr == 0x4016) {
		uint8 r = _padStrobe ? (buttons & 1) : (_padShift & 1);
		if (!_padStrobe)
			_padShift = (_padShift >> 1) | 0x80;
		return 0x40 | r;
	}
	if (addr < 0x6000)
		return 0x40;
	if (addr < 0x8000)
		return _prgRam[addr - 0x6000];
	return _prg[_prgMap[(addr >> 13) & 3] + (addr & 0x1FFF)];
}

void Nes::write(uint16 addr, uint8 v) {
	if (addr < 0x2000) {
		_ram[addr & 0x7FF] = v;
		return;
	}
	if (addr < 0x4000) {
		switch (addr & 7) {
		case 0: {
			const bool wasOff = !(_ctrl & 0x80);
			_ctrl = v;
			_t = (_t & 0xF3FF) | ((v & 3) << 10);
			if (wasOff && (v & 0x80) && (_status & 0x80))
				_nmiPending = true;
			break;
		}
		case 1:
			_mask = v;
			break;
		case 3:
			_oamAddr = v;
			break;
		case 4:
			_oam[_oamAddr++] = v;
			break;
		case 5:
			if (!_w) {
				_t = (_t & 0xFFE0) | (v >> 3);
				_fineX = v & 7;
			} else {
				_t = (_t & 0x8C1F) | ((v & 7) << 12) | ((v & 0xF8) << 2);
			}
			_w = !_w;
			break;
		case 6:
			if (!_w) {
				_t = (_t & 0x00FF) | ((v & 0x3F) << 8);
			} else {
				_t = (_t & 0xFF00) | v;
				_v = _t;
			}
			_w = !_w;
			break;
		case 7:
			ppuWrite(_v & 0x3FFF, v);
			_v += (_ctrl & 4) ? 32 : 1;
			break;
		default:
			break;
		}
		return;
	}
	if (addr == 0x4014) {
		for (int i = 0; i < 256; i++)
			_oam[(_oamAddr + i) & 0xFF] = read((v << 8) | i);
		_stall += 513 + (_cycles & 1);
		return;
	}
	if (addr == 0x4016) {
		_padStrobe = v & 1;
		if (_padStrobe)
			_padShift = buttons;
		else
			_padShift = buttons;
		return;
	}
	if (addr < 0x4018) {
		apuWrite(addr, v);
		return;
	}
	if (addr < 0x6000)
		return;
	if (addr < 0x8000) {
		if (_prgRam[addr - 0x6000] != v)
			batteryDirty = true;
		_prgRam[addr - 0x6000] = v;
		return;
	}
	mapperWrite(addr, v);
}

// PPU

uint16 Nes::ntAddr(uint16 addr) const {
	const uint16 a = (addr - 0x2000) & 0x0FFF;
	const int table = a >> 10;
	int phys;
	switch (_mirror) {
	case kVertical: phys = table & 1; break;
	case kHorizontal: phys = table >> 1; break;
	case kSingle0: phys = 0; break;
	default: phys = 1; break;
	}
	return phys * 0x400 + (a & 0x3FF);
}

uint8 Nes::ppuRead(uint16 addr) {
	addr &= 0x3FFF;
	if (addr < 0x2000)
		return chrRead(addr);
	if (addr < 0x3F00)
		return _vram[ntAddr(addr)];
	uint8 i = addr & 0x1F;
	if ((i & 0x13) == 0x10)
		i &= 0x0F;
	return _palRam[i];
}

void Nes::ppuWrite(uint16 addr, uint8 v) {
	addr &= 0x3FFF;
	if (addr < 0x2000) {
		if (_chrRam)
			_chr[_chrMap[(addr >> 10) & 7] + (addr & 0x3FF)] = v;
		return;
	}
	if (addr < 0x3F00) {
		_vram[ntAddr(addr)] = v;
		return;
	}
	uint8 i = addr & 0x1F;
	if ((i & 0x13) == 0x10)
		i &= 0x0F;
	_palRam[i] = v & 0x3F;
}

void Nes::incrementY() {
	if ((_v & 0x7000) != 0x7000) {
		_v += 0x1000;
		return;
	}
	_v &= ~0x7000;
	int y = (_v & 0x03E0) >> 5;
	if (y == 29) {
		y = 0;
		_v ^= 0x0800;
	} else if (y == 31) {
		y = 0;
	} else {
		y++;
	}
	_v = (_v & ~0x03E0) | (y << 5);
}

void Nes::renderLine(int y) {
	byte bg[kScreenW + 16];
	memset(bg, 0, sizeof(bg));
	byte *out = &_screen[y * kScreenW];
	const bool showBg = _mask & 0x08, showSpr = _mask & 0x10;

	// Background: 33 tiles from v, shifted by fine X
	if (showBg) {
		uint16 v = _v;
		const uint16 base = (_ctrl & 0x10) ? 0x1000 : 0;
		const int fineY = (v >> 12) & 7;
		for (int tile = 0; tile < 33; tile++) {
			const uint8 nt = _vram[ntAddr(0x2000 | (v & 0x0FFF))];
			const uint8 at = _vram[ntAddr(0x23C0 | (v & 0x0C00) | ((v >> 4) & 0x38) | ((v >> 2) & 7))];
			const int shift = ((v >> 4) & 4) | (v & 2);
			const uint8 pal = (at >> shift) & 3;
			const uint8 lo = chrRead(base + nt * 16 + fineY), hi = chrRead(base + nt * 16 + fineY + 8);
			for (int px = 0; px < 8; px++) {
				const uint8 c = ((lo >> (7 - px)) & 1) | (((hi >> (7 - px)) & 1) << 1);
				const int x = tile * 8 + px - _fineX;
				if (x >= 0 && x < kScreenW)
					bg[x] = c ? (pal << 2) | c : 0;
			}
			// coarse X, wrapping into the next name table
			if ((v & 0x1F) == 31) {
				v &= ~0x1F;
				v ^= 0x0400;
			} else {
				v++;
			}
		}
		if (!(_mask & 0x02))
			memset(bg, 0, 8);
	}

	// Sprites on this line: the first 8 in OAM order
	byte spr[kScreenW];
	bool sprBehind[kScreenW], sprZero[kScreenW];
	memset(spr, 0, sizeof(spr));
	memset(sprBehind, 0, sizeof(sprBehind));
	memset(sprZero, 0, sizeof(sprZero));
	if (showSpr) {
		const int h = (_ctrl & 0x20) ? 16 : 8;
		int found = 0;
		for (int i = 0; i < 64; i++) {
			const int sy = _oam[i * 4] + 1;
			if (y < sy || y >= sy + h)
				continue;
			if (++found > 8) {
				_status |= 0x20;
				break;
			}
			const uint8 tileNo = _oam[i * 4 + 1], attr = _oam[i * 4 + 2];
			const int sx = _oam[i * 4 + 3];
			int row = y - sy;
			if (attr & 0x80)
				row = h - 1 - row;
			uint16 addr;
			if (h == 16)
				addr = ((tileNo & 1) ? 0x1000 : 0) + (tileNo & 0xFE) * 16 + (row >= 8 ? 16 : 0) + (row & 7);
			else
				addr = ((_ctrl & 0x08) ? 0x1000 : 0) + tileNo * 16 + row;
			const uint8 lo = chrRead(addr), hi = chrRead(addr + 8);
			for (int px = 0; px < 8; px++) {
				const int bit = (attr & 0x40) ? px : 7 - px;
				const uint8 c = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
				const int x = sx + px;
				if (!c || x >= kScreenW || spr[x])
					continue;
				if (x < 8 && !(_mask & 0x04))
					continue;
				spr[x] = 0x10 | ((attr & 3) << 2) | c;
				sprBehind[x] = attr & 0x20;
				sprZero[x] = i == 0;
			}
		}
	}

	const uint8 gray = (_mask & 0x01) ? 0x30 : 0x3F;
	for (int x = 0; x < kScreenW; x++) {
		uint8 c = bg[x];
		if (spr[x]) {
			if (sprZero[x] && bg[x] && x != 255)
				_status |= 0x40;
			if (!bg[x] || !sprBehind[x])
				c = spr[x];
		}
		out[x] = _palRam[(c & 3) ? c : 0] & gray;
	}
}

void Nes::runFrame() {
	audio.clear();
	// 262 lines of 341 dots; the CPU runs one cycle per three dots
	int dotDebt = 0;
	for (_line = 0; _line < 262; _line++) {
		const bool rendering = _mask & 0x18;
		if (_line < kScreenH) {
			if (rendering)
				renderLine(_line);
			else
				memset(&_screen[_line * kScreenW], _palRam[0], kScreenW);
		}
		if (_line == 241) {
			_status |= 0x80;
			if (_ctrl & 0x80)
				_nmiPending = true;
		}
		if (_line == 261)
			_status &= ~0xE0;

		dotDebt += 341;
		while (dotDebt > 0) {
			if (_nmiPending) {
				_nmiPending = false;
				cpu.nmi();
				_cycles += 7;
				apuRun(7);
				dotDebt -= 21;
				continue;
			}
			if ((_mmc3Irq || apuIrq()) && !(cpu.p & Cpu6502::kFlagI)) {
				cpu.irq();
				_cycles += 7;
				apuRun(7);
				dotDebt -= 21;
				continue;
			}
			int c;
			if (_stall) {
				c = _stall;
				_stall = 0;
			} else {
				c = cpu.step();
			}
			_cycles += c;
			apuRun(c);
			dotDebt -= c * 3;
		}

		if (rendering && (_line < kScreenH || _line == 261)) {
			if (_line < kScreenH) {
				incrementY();
				_v = (_v & ~0x041F) | (_t & 0x041F); // horizontal from t
			} else {
				_v = (_v & 0x041F) | (_t & 0x7BE0); // vertical from t
			}
			mmc3Clock();
		}
	}
}

} // End of namespace NesVenture
