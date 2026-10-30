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

#include "darklord/cpu6502.h"
#include "darklord/machine.h"

namespace DarkLord {

static const uint8 kCycles[256] = {
	7, 6, 2, 2, 2, 3, 5, 2, 3, 2, 2, 2, 2, 4, 6, 2,
	2, 5, 2, 2, 2, 4, 6, 2, 2, 4, 2, 2, 2, 4, 7, 2,
	6, 6, 2, 2, 3, 3, 5, 2, 4, 2, 2, 2, 4, 4, 6, 2,
	2, 5, 2, 2, 2, 4, 6, 2, 2, 4, 2, 2, 2, 4, 7, 2,
	6, 6, 2, 2, 2, 3, 5, 2, 3, 2, 2, 2, 3, 4, 6, 2,
	2, 5, 2, 2, 2, 4, 6, 2, 2, 4, 2, 2, 2, 4, 7, 2,
	6, 6, 2, 2, 2, 3, 5, 2, 4, 2, 2, 2, 5, 4, 6, 2,
	2, 5, 2, 2, 2, 4, 6, 2, 2, 4, 2, 2, 2, 4, 7, 2,
	2, 6, 2, 2, 3, 3, 3, 2, 2, 2, 2, 2, 4, 4, 4, 2,
	2, 6, 2, 2, 4, 4, 4, 2, 2, 5, 2, 2, 2, 5, 2, 2,
	2, 6, 2, 2, 3, 3, 3, 2, 2, 2, 2, 2, 4, 4, 4, 2,
	2, 5, 2, 2, 4, 4, 4, 2, 2, 4, 2, 2, 4, 4, 4, 2,
	2, 6, 2, 2, 3, 3, 5, 2, 2, 2, 2, 2, 4, 4, 6, 2,
	2, 5, 2, 2, 2, 4, 6, 2, 2, 4, 2, 2, 2, 4, 7, 2,
	2, 6, 2, 2, 3, 3, 5, 2, 2, 2, 2, 2, 4, 4, 6, 2,
	2, 5, 2, 2, 2, 4, 6, 2, 2, 4, 2, 2, 2, 4, 7, 2
};

Cpu6502::Cpu6502(Machine *m) : pc(0), a(0), x(0), y(0), sp(0xFF), p(kFlagU | kFlagI), _m(m) {
}

void Cpu6502::reset(uint16 newPc) {
	pc = newPc;
	a = x = y = 0;
	sp = 0xFF;
	p = kFlagU | kFlagI;
}

uint8 Cpu6502::rd(uint16 addr) { return _m->read(addr); }
void Cpu6502::wr(uint16 addr, uint8 v) { _m->write(addr, v); }
uint16 Cpu6502::rd16(uint16 addr) { return rd(addr) | (rd((uint16)(addr + 1)) << 8); }
uint16 Cpu6502::rd16zp(uint8 addr) { return rd(addr) | (rd((uint8)(addr + 1)) << 8); }
// JMP ($xxFF) wraps within the page on the NMOS 6502
uint16 Cpu6502::rd16bug(uint16 addr) {
	return rd(addr) | (rd((addr & 0xFF00) | ((addr + 1) & 0xFF)) << 8);
}
void Cpu6502::push(uint8 v) { wr(0x100 | sp, v); sp--; }
uint8 Cpu6502::pull() { sp++; return rd(0x100 | sp); }

bool Cpu6502::irq() {
	if (p & kFlagI)
		return false;
	push(pc >> 8);
	push(pc & 0xFF);
	push((p & ~kFlagB) | kFlagU);
	p |= kFlagI;
	pc = rd16(0xFFFE);
	return true;
}

void Cpu6502::nmi() {
	push(pc >> 8);
	push(pc & 0xFF);
	push((p & ~kFlagB) | kFlagU);
	p |= kFlagI;
	pc = rd16(0xFFFA);
}

void Cpu6502::adc(uint8 v) {
	uint8 c = p & kFlagC;
	if (p & kFlagD) {
		int lo = (a & 0x0F) + (v & 0x0F) + c;
		int adj0 = 0, half = 0;
		if (lo > 9) { adj0 = 6; half = 1; }
		int hi = (a >> 4) + (v >> 4) + half;
		int adj1 = 0, carry = 0;
		if (hi > 9) { adj1 = 6; carry = 1; }
		uint8 alu = ((hi & 0x0F) << 4) | (lo & 0x0F);
		p &= ~(kFlagC | kFlagV | kFlagN | kFlagZ);
		if (alu == 0)
			p |= kFlagZ;
		else
			p |= alu & kFlagN;
		if (carry)
			p |= kFlagC;
		if (~(a ^ v) & (a ^ alu) & 0x80)
			p |= kFlagV;
		a = (((hi + adj1) & 0x0F) << 4) | ((lo + adj0) & 0x0F);
	} else {
		int r = a + v + c;
		p &= ~(kFlagC | kFlagV);
		if (r > 0xFF)
			p |= kFlagC;
		if (~(a ^ v) & (a ^ r) & 0x80)
			p |= kFlagV;
		a = r & 0xFF;
		setNZ(a);
	}
}

void Cpu6502::sbc(uint8 v) {
	if (p & kFlagD) {
		int c = (p & kFlagC) ? 0 : 1;
		int lo = (a & 0x0F) - (v & 0x0F) - c;
		int hi = (a >> 4) - (v >> 4);
		if (lo < 0) { lo -= 6; hi--; }
		int r = a - v - c;
		p &= ~(kFlagC | kFlagV | kFlagN | kFlagZ);
		if (r >= 0)
			p |= kFlagC;
		if ((a ^ v) & (a ^ r) & 0x80)
			p |= kFlagV;
		if ((r & 0xFF) == 0)
			p |= kFlagZ;
		p |= r & kFlagN;
		if (hi < 0)
			hi -= 6;
		a = ((hi & 0x0F) << 4) | (lo & 0x0F);
	} else {
		adc(v ^ 0xFF);
	}
}

void Cpu6502::cmp(uint8 r, uint8 v) {
	int d = r - v;
	p = (p & ~kFlagC) | (d >= 0 ? kFlagC : 0);
	setNZ(d & 0xFF);
}

uint8 Cpu6502::asl(uint8 v) { p = (p & ~kFlagC) | (v >> 7); v <<= 1; setNZ(v); return v; }
uint8 Cpu6502::lsr(uint8 v) { p = (p & ~kFlagC) | (v & 1); v >>= 1; setNZ(v); return v; }
uint8 Cpu6502::rol(uint8 v) { uint8 c = p & kFlagC; p = (p & ~kFlagC) | (v >> 7); v = (v << 1) | c; setNZ(v); return v; }
uint8 Cpu6502::ror(uint8 v) { uint8 c = p & kFlagC; p = (p & ~kFlagC) | (v & 1); v = (v >> 1) | (c << 7); setNZ(v); return v; }

int Cpu6502::branch(bool cond) {
	int8 off = (int8)rd(pc++);
	if (!cond)
		return 0;
	uint16 dest = pc + off;
	int extra = ((dest ^ pc) & 0xFF00) ? 2 : 1;
	pc = dest;
	return extra;
}

int Cpu6502::step() {
	uint8 op = rd(pc++);
	int cycles = kCycles[op];
	uint16 ea = 0;

	// Effective address for each addressing mode
#define IMM()  (ea = pc++)
#define ZP()   (ea = rd(pc++))
#define ZPX()  (ea = (uint8)(rd(pc++) + x))
#define ZPY()  (ea = (uint8)(rd(pc++) + y))
#define ABS()  (ea = rd16(pc), pc += 2)
#define ABX()  (ea = rd16(pc) + x, pc += 2)
#define ABY()  (ea = rd16(pc) + y, pc += 2)
#define INX()  (ea = rd16zp((uint8)(rd(pc++) + x)))
#define INY()  (ea = rd16zp(rd(pc++)) + y)

	switch (op) {
	// Loads
	case 0xA9: IMM(); a = rd(ea); setNZ(a); break;
	case 0xA5: ZP(); a = rd(ea); setNZ(a); break;
	case 0xB5: ZPX(); a = rd(ea); setNZ(a); break;
	case 0xAD: ABS(); a = rd(ea); setNZ(a); break;
	case 0xBD: ABX(); a = rd(ea); setNZ(a); break;
	case 0xB9: ABY(); a = rd(ea); setNZ(a); break;
	case 0xA1: INX(); a = rd(ea); setNZ(a); break;
	case 0xB1: INY(); a = rd(ea); setNZ(a); break;
	case 0xA2: IMM(); x = rd(ea); setNZ(x); break;
	case 0xA6: ZP(); x = rd(ea); setNZ(x); break;
	case 0xB6: ZPY(); x = rd(ea); setNZ(x); break;
	case 0xAE: ABS(); x = rd(ea); setNZ(x); break;
	case 0xBE: ABY(); x = rd(ea); setNZ(x); break;
	case 0xA0: IMM(); y = rd(ea); setNZ(y); break;
	case 0xA4: ZP(); y = rd(ea); setNZ(y); break;
	case 0xB4: ZPX(); y = rd(ea); setNZ(y); break;
	case 0xAC: ABS(); y = rd(ea); setNZ(y); break;
	case 0xBC: ABX(); y = rd(ea); setNZ(y); break;
	// Stores
	case 0x85: ZP(); wr(ea, a); break;
	case 0x95: ZPX(); wr(ea, a); break;
	case 0x8D: ABS(); wr(ea, a); break;
	case 0x9D: ABX(); wr(ea, a); break;
	case 0x99: ABY(); wr(ea, a); break;
	case 0x81: INX(); wr(ea, a); break;
	case 0x91: INY(); wr(ea, a); break;
	case 0x86: ZP(); wr(ea, x); break;
	case 0x96: ZPY(); wr(ea, x); break;
	case 0x8E: ABS(); wr(ea, x); break;
	case 0x84: ZP(); wr(ea, y); break;
	case 0x94: ZPX(); wr(ea, y); break;
	case 0x8C: ABS(); wr(ea, y); break;
	// Transfers
	case 0xAA: x = a; setNZ(x); break;
	case 0xA8: y = a; setNZ(y); break;
	case 0x8A: a = x; setNZ(a); break;
	case 0x98: a = y; setNZ(a); break;
	case 0xBA: x = sp; setNZ(x); break;
	case 0x9A: sp = x; break;
	// Stack
	case 0x48: push(a); break;
	case 0x68: a = pull(); setNZ(a); break;
	case 0x08: push(p | kFlagB | kFlagU); break;
	case 0x28: p = (pull() & ~kFlagB) | kFlagU; break;
	// Logic
#define LOGIC(OPC, MODE, EXPR) case OPC: MODE(); a EXPR rd(ea); setNZ(a); break;
	LOGIC(0x29, IMM, &=) LOGIC(0x25, ZP, &=) LOGIC(0x35, ZPX, &=) LOGIC(0x2D, ABS, &=)
	LOGIC(0x3D, ABX, &=) LOGIC(0x39, ABY, &=) LOGIC(0x21, INX, &=) LOGIC(0x31, INY, &=)
	LOGIC(0x09, IMM, |=) LOGIC(0x05, ZP, |=) LOGIC(0x15, ZPX, |=) LOGIC(0x0D, ABS, |=)
	LOGIC(0x1D, ABX, |=) LOGIC(0x19, ABY, |=) LOGIC(0x01, INX, |=) LOGIC(0x11, INY, |=)
	LOGIC(0x49, IMM, ^=) LOGIC(0x45, ZP, ^=) LOGIC(0x55, ZPX, ^=) LOGIC(0x4D, ABS, ^=)
	LOGIC(0x5D, ABX, ^=) LOGIC(0x59, ABY, ^=) LOGIC(0x41, INX, ^=) LOGIC(0x51, INY, ^=)
#undef LOGIC
	case 0x24: case 0x2C: {
		if (op == 0x24) ZP(); else ABS();
		uint8 v = rd(ea);
		p = (p & ~(kFlagN | kFlagV | kFlagZ)) | (v & (kFlagN | kFlagV)) | ((a & v) ? 0 : kFlagZ);
		break;
	}
	// Arithmetic
#define ARITH(OPC, MODE, FN) case OPC: MODE(); FN(rd(ea)); break;
	ARITH(0x69, IMM, adc) ARITH(0x65, ZP, adc) ARITH(0x75, ZPX, adc) ARITH(0x6D, ABS, adc)
	ARITH(0x7D, ABX, adc) ARITH(0x79, ABY, adc) ARITH(0x61, INX, adc) ARITH(0x71, INY, adc)
	ARITH(0xE9, IMM, sbc) ARITH(0xE5, ZP, sbc) ARITH(0xF5, ZPX, sbc) ARITH(0xED, ABS, sbc)
	ARITH(0xFD, ABX, sbc) ARITH(0xF9, ABY, sbc) ARITH(0xE1, INX, sbc) ARITH(0xF1, INY, sbc)
	case 0xEB: IMM(); sbc(rd(ea)); break;
#undef ARITH
#define COMPARE(OPC, MODE, REG) case OPC: MODE(); cmp(REG, rd(ea)); break;
	COMPARE(0xC9, IMM, a) COMPARE(0xC5, ZP, a) COMPARE(0xD5, ZPX, a) COMPARE(0xCD, ABS, a)
	COMPARE(0xDD, ABX, a) COMPARE(0xD9, ABY, a) COMPARE(0xC1, INX, a) COMPARE(0xD1, INY, a)
	COMPARE(0xE0, IMM, x) COMPARE(0xE4, ZP, x) COMPARE(0xEC, ABS, x)
	COMPARE(0xC0, IMM, y) COMPARE(0xC4, ZP, y) COMPARE(0xCC, ABS, y)
#undef COMPARE
	// Increments and decrements
#define RMW(OPC, MODE, EXPR) case OPC: { MODE(); uint8 v = rd(ea); v = EXPR; wr(ea, v); break; }
	RMW(0xE6, ZP, (uint8)(v + 1); setNZ(v)) RMW(0xF6, ZPX, (uint8)(v + 1); setNZ(v))
	RMW(0xEE, ABS, (uint8)(v + 1); setNZ(v)) RMW(0xFE, ABX, (uint8)(v + 1); setNZ(v))
	RMW(0xC6, ZP, (uint8)(v - 1); setNZ(v)) RMW(0xD6, ZPX, (uint8)(v - 1); setNZ(v))
	RMW(0xCE, ABS, (uint8)(v - 1); setNZ(v)) RMW(0xDE, ABX, (uint8)(v - 1); setNZ(v))
	RMW(0x06, ZP, asl(v)) RMW(0x16, ZPX, asl(v)) RMW(0x0E, ABS, asl(v)) RMW(0x1E, ABX, asl(v))
	RMW(0x46, ZP, lsr(v)) RMW(0x56, ZPX, lsr(v)) RMW(0x4E, ABS, lsr(v)) RMW(0x5E, ABX, lsr(v))
	RMW(0x26, ZP, rol(v)) RMW(0x36, ZPX, rol(v)) RMW(0x2E, ABS, rol(v)) RMW(0x3E, ABX, rol(v))
	RMW(0x66, ZP, ror(v)) RMW(0x76, ZPX, ror(v)) RMW(0x6E, ABS, ror(v)) RMW(0x7E, ABX, ror(v))
#undef RMW
	case 0xE8: x++; setNZ(x); break;
	case 0xC8: y++; setNZ(y); break;
	case 0xCA: x--; setNZ(x); break;
	case 0x88: y--; setNZ(y); break;
	case 0x0A: a = asl(a); break;
	case 0x4A: a = lsr(a); break;
	case 0x2A: a = rol(a); break;
	case 0x6A: a = ror(a); break;
	// Jumps and calls
	case 0x4C: pc = rd16(pc); break;
	case 0x6C: pc = rd16bug(rd16(pc)); break;
	case 0x20: {
		uint16 dest = rd16(pc);
		pc++;
		push(pc >> 8);
		push(pc & 0xFF);
		pc = dest;
		break;
	}
	case 0x60: { uint16 lo = pull(); uint16 hi = pull(); pc = ((hi << 8) | lo) + 1; break; }
	case 0x40: {
		p = (pull() & ~kFlagB) | kFlagU;
		uint16 lo = pull();
		uint16 hi = pull();
		pc = (hi << 8) | lo;
		break;
	}
	case 0x00: {
		pc++;
		push(pc >> 8);
		push(pc & 0xFF);
		push(p | kFlagB | kFlagU);
		p |= kFlagI;
		pc = rd16(0xFFFE);
		break;
	}
	// Branches
	case 0x10: cycles += branch(!(p & kFlagN)); break;
	case 0x30: cycles += branch(p & kFlagN); break;
	case 0x50: cycles += branch(!(p & kFlagV)); break;
	case 0x70: cycles += branch(p & kFlagV); break;
	case 0x90: cycles += branch(!(p & kFlagC)); break;
	case 0xB0: cycles += branch(p & kFlagC); break;
	case 0xD0: cycles += branch(!(p & kFlagZ)); break;
	case 0xF0: cycles += branch(p & kFlagZ); break;
	// Flags
	case 0x18: p &= ~kFlagC; break;
	case 0x38: p |= kFlagC; break;
	case 0x58: p &= ~kFlagI; break;
	case 0x78: p |= kFlagI; break;
	case 0xB8: p &= ~kFlagV; break;
	case 0xD8: p &= ~kFlagD; break;
	case 0xF8: p |= kFlagD; break;
	case 0xEA: break;
	default:
		// Undefined opcode: treated as a one-byte NOP
		break;
	}
#undef IMM
#undef ZP
#undef ZPX
#undef ZPY
#undef ABS
#undef ABX
#undef ABY
#undef INX
#undef INY
	return cycles;
}

} // End of namespace DarkLord
