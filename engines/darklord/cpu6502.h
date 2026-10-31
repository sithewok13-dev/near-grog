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

#ifndef DARKLORD_CPU6502_H
#define DARKLORD_CPU6502_H

#include "common/scummsys.h"

namespace DarkLord {

/** Whatever the CPU reads and writes: a C64 or an Apple II. */
class Bus {
public:
	virtual ~Bus() {}
	virtual uint8 read(uint16 addr) = 0;
	virtual void write(uint16 addr, uint8 v) = 0;
};

/**
 * A plain NMOS 6502 core (documented opcodes, decimal mode, IRQ/NMI).
 * Undefined opcodes are treated as one-byte NOPs.
 */
class Cpu6502 {
public:
	enum {
		kFlagC = 0x01, kFlagZ = 0x02, kFlagI = 0x04, kFlagD = 0x08,
		kFlagB = 0x10, kFlagU = 0x20, kFlagV = 0x40, kFlagN = 0x80
	};

	explicit Cpu6502(Bus *m);

	void reset(uint16 pc);
	/** Executes one instruction and returns the cycles it took. */
	int step();
	/** Takes an IRQ if interrupts are enabled; returns true if taken. */
	bool irq();
	void nmi();

	uint16 pc;
	uint8 a, x, y, sp, p;

private:
	Bus *_m;

	uint8 rd(uint16 addr);
	void wr(uint16 addr, uint8 v);
	uint16 rd16(uint16 addr);
	uint16 rd16zp(uint8 addr);
	uint16 rd16bug(uint16 addr);
	void push(uint8 v);
	uint8 pull();
	void setNZ(uint8 v) { p = (p & ~(kFlagN | kFlagZ)) | (v & kFlagN) | (v ? 0 : kFlagZ); }

	void adc(uint8 v);
	void sbc(uint8 v);
	void cmp(uint8 r, uint8 v);
	uint8 asl(uint8 v);
	uint8 lsr(uint8 v);
	uint8 rol(uint8 v);
	uint8 ror(uint8 v);
	int branch(bool cond);
};

} // End of namespace DarkLord

#endif
