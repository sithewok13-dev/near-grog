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

#ifndef DOSGAMES_CPU8086_H
#define DOSGAMES_CPU8086_H

#include "common/scummsys.h"

namespace DosGames {

/** Memory and I/O as the CPU sees them. */
class Bus8086 {
public:
	virtual ~Bus8086() {}
	virtual uint8 read8(uint32 addr) = 0;
	virtual void write8(uint32 addr, uint8 v) = 0;
	virtual uint8 in8(uint16 port) = 0;
	virtual void out8(uint16 port, uint8 v) = 0;
	/**
	 * Called for INT n. Returns true if the machine answered it itself
	 * (a BIOS or DOS service); false to go through the interrupt table.
	 */
	virtual bool interrupt(uint8 n) = 0;
};

/**
 * A real-mode 8086, with the 80186 additions programs of the late 1980s
 * use (PUSH immediate, PUSHA/POPA, ENTER/LEAVE, IMUL immediate, shifts by
 * an immediate count, string I/O, BOUND). No protected mode, no FPU.
 */
class Cpu8086 {
public:
	enum {
		kCF = 0x0001, kPF = 0x0004, kAF = 0x0010, kZF = 0x0040, kSF = 0x0080,
		kTF = 0x0100, kIF = 0x0200, kDF = 0x0400, kOF = 0x0800
	};
	enum { kES, kCS, kSS, kDS };
	enum { kAX, kCX, kDX, kBX, kSP, kBP, kSI, kDI };

	explicit Cpu8086(Bus8086 *bus);

	/** Executes one instruction; returns a rough cycle count. */
	int step();
	/** Takes a hardware interrupt if IF is set. */
	bool irq(uint8 n);
	/** Software interrupt through the table (used by the machine too). */
	void intCall(uint8 n);
	void iret();

	uint16 r[8];
	uint16 seg[4];
	uint16 ip;
	uint16 flags;
	bool halted;

	uint8 rb(int i) const { return i < 4 ? (r[i] & 0xFF) : (r[i - 4] >> 8); }
	void wb(int i, uint8 v) {
		if (i < 4)
			r[i] = (r[i] & 0xFF00) | v;
		else
			r[i - 4] = (r[i - 4] & 0x00FF) | (v << 8);
	}

	uint8 mem8(uint16 s, uint16 o) { return _bus->read8(((uint32)s << 4) + o); }
	uint16 mem16(uint16 s, uint16 o) { return mem8(s, o) | (mem8(s, o + 1) << 8); }
	void setMem8(uint16 s, uint16 o, uint8 v) { _bus->write8(((uint32)s << 4) + o, v); }
	void setMem16(uint16 s, uint16 o, uint16 v) { setMem8(s, o, v & 0xFF); setMem8(s, o + 1, v >> 8); }

	void push(uint16 v);
	uint16 pop();

	void setFlag(uint16 f, bool on) { flags = on ? (flags | f) : (flags & ~f); }
	bool flag(uint16 f) const { return flags & f; }

private:
	Bus8086 *_bus;

	// Decoding state for the current instruction
	int _segOverride;
	int _rep; // 0, 0xF2 or 0xF3
	uint8 _modrm;
	bool _modIsReg;
	uint16 _eaSeg, _eaOff;

	uint8 fetch8() { uint8 v = mem8(seg[kCS], ip); ip++; return v; }
	uint16 fetch16() { uint16 v = fetch8(); return v | (fetch8() << 8); }

	void decodeModrm();
	uint8 getRM8();
	uint16 getRM16();
	void setRM8(uint8 v);
	void setRM16(uint16 v);
	int regField() const { return (_modrm >> 3) & 7; }
	uint16 dataSeg() const { return _segOverride >= 0 ? seg[_segOverride] : seg[kDS]; }

	void setSZP8(uint8 v);
	void setSZP16(uint16 v);
	uint32 alu8(int op, uint8 a, uint8 b);
	uint32 alu16(int op, uint16 a, uint16 b);
	uint8 shift8(int op, uint8 v, int count);
	uint16 shift16(int op, uint16 v, int count);
	void group3_8();
	void group3_16();
	bool cond(int cc) const;
	void stringOp(uint8 op);
	void divide(uint8 n);
};

} // End of namespace DosGames

#endif
