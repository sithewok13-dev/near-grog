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

#ifndef NESVENTURE_NES_H
#define NESVENTURE_NES_H

#include "common/array.h"
#include "common/stream.h"
#include "nesventure/cpu6502.h"

namespace NesVenture {

/**
 * Near-Grog's NES, enough for Kemco-Seika's MacVenture ports (Shadowgate,
 * Deja Vu, Uninvited): the 2A03 (the 6502 without decimal mode), the 2C02
 * picture chip drawn a scanline at a time, the MMC1 and MMC3 cartridge
 * chips with battery RAM, one controller and the sound chip (nes_apu.cpp).
 */
class Nes : public Bus {
public:
	enum {
		kScreenW = 256,
		kScreenH = 240,
		// Buttons, as the controller sends them
		kA = 0x01, kB = 0x02, kSelect = 0x04, kStart = 0x08,
		kUp = 0x10, kDown = 0x20, kLeft = 0x40, kRight = 0x80
	};

	Nes();
	~Nes() override;

	/** An iNES file (16-byte header); false if the cartridge chip isn't supported. */
	bool load(const Common::Array<byte> &ines);
	void reset();
	/** Runs one frame (to the next vertical blank). */
	void runFrame();

	/** The picture: 256x240 NES colour indices (0-63). */
	const byte *screen() const { return _screen; }
	/** The 2C02's colours, RGB, 64 entries. */
	static const byte kPalette[64 * 3];

	uint8 buttons; // held now (k* bits)

	/** The cartridge's battery RAM ($6000-$7FFF), for save files. */
	byte *batteryRam() { return _prgRam; }
	bool hasBattery() const { return _battery; }
	bool batteryDirty;

	/** Machine memory, for the layouts to read the game's state. */
	uint8 peek(uint16 addr) { return addr < 0x2000 ? _ram[addr & 0x7FF] : addr >= 0x6000 && addr < 0x8000 ? _prgRam[addr - 0x6000] : 0; }
	void poke(uint16 addr, uint8 v) { if (addr < 0x2000) _ram[addr & 0x7FF] = v; }
	/** The name tables, patterns and palette as the PPU sees them. */
	uint8 ppuPeek(uint16 addr) { return ppuRead(addr); }
	/** Which name table (0-3) line y of the last frame was drawn from. */
	int lineTable(int y) const { return _lineNt[y]; }
	/** The background's pattern table (0x0000 or 0x1000). */
	uint16 bgPatterns() const { return (_ctrl & 0x10) ? 0x1000 : 0; }

	/** The whole machine, for save states. */
	void saveState(Common::WriteStream &out);
	bool loadState(Common::SeekableReadStream &in);

	// Bus
	uint8 read(uint16 addr) override;
	void write(uint16 addr, uint8 v) override;

	// Sound: signed 16-bit mono at kSampleRate, appended each frame
	enum { kSampleRate = 44100 };
	Common::Array<int16> audio;

	Cpu6502 cpu;

private:
	// Cartridge
	Common::Array<byte> _prg, _chr;
	bool _chrRam, _battery;
	int _mapper;
	byte _prgRam[0x2000];
	uint32 _prgMap[4];  // offsets of the four 8K PRG windows
	uint32 _chrMap[8];  // offsets of the eight 1K CHR windows
	enum Mirroring { kHorizontal, kVertical, kSingle0, kSingle1 } _mirror;
	// MMC1
	uint8 _mmc1Shift, _mmc1Count, _mmc1Ctrl, _mmc1Chr0, _mmc1Chr1, _mmc1Prg;
	uint64 _mmc1LastWrite;
	// MMC3
	uint8 _mmc3Select, _mmc3Regs[8], _mmc3Latch, _mmc3Counter;
	bool _mmc3Reload, _mmc3IrqOn, _mmc3Irq;
	void updateMaps();
	void mapperWrite(uint16 addr, uint8 v);
	void mmc3Clock();

	// CPU side
	byte _ram[0x800];
	uint64 _cycles;
	int _stall;
	uint8 _padShift, _padStrobe;

	// PPU
	byte _vram[0x800], _palRam[32], _oam[256];
	uint8 _ctrl, _mask, _status, _oamAddr, _readBuf, _fineX;
	uint16 _v, _t;
	bool _w, _nmiPending;
	int _line;
	byte _screen[kScreenW * kScreenH];
	byte _lineNt[kScreenH];
	uint8 ppuRead(uint16 addr);
	void ppuWrite(uint16 addr, uint8 v);
	uint16 ntAddr(uint16 addr) const;
	uint8 chrRead(uint16 addr) const { return _chr[_chrMap[(addr >> 10) & 7] + (addr & 0x3FF)]; }
	void renderLine(int y);
	void incrementY();

	// APU (nes_apu.cpp)
	struct Apu;
	Apu *_apu;
	void apuInit();
	void apuFree();
	void apuWrite(uint16 addr, uint8 v);
	uint8 apuStatus();
	void apuRun(int cycles);
	bool apuIrq();
};

} // End of namespace NesVenture

#endif
