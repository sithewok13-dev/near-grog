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

#include "dosgames/cpu8086.h"

namespace DosGames {

enum { kAdd, kOr, kAdc, kSbb, kAnd, kSub, kXor, kCmp };

static bool parity(uint8 v) {
	v ^= v >> 4;
	v ^= v >> 2;
	v ^= v >> 1;
	return !(v & 1);
}

Cpu8086::Cpu8086(Bus8086 *bus) : ip(0), flags(0x0002), halted(false), _bus(bus), _segOverride(-1),
		_rep(0), _modrm(0), _modIsReg(false), _eaSeg(0), _eaOff(0) {
	for (int i = 0; i < 8; i++)
		r[i] = 0;
	for (int i = 0; i < 4; i++)
		seg[i] = 0;
}

void Cpu8086::push(uint16 v) {
	r[kSP] -= 2;
	setMem16(seg[kSS], r[kSP], v);
}

uint16 Cpu8086::pop() {
	uint16 v = mem16(seg[kSS], r[kSP]);
	r[kSP] += 2;
	return v;
}

void Cpu8086::intCall(uint8 n) {
	push(flags);
	push(seg[kCS]);
	push(ip);
	flags &= ~(kIF | kTF);
	ip = mem16(0, n * 4);
	seg[kCS] = mem16(0, n * 4 + 2);
}

void Cpu8086::iret() {
	ip = pop();
	seg[kCS] = pop();
	flags = (pop() & 0x0FD5) | 0xF002;
}

bool Cpu8086::irq(uint8 n) {
	if (!(flags & kIF))
		return false;
	halted = false;
	intCall(n);
	return true;
}

void Cpu8086::decodeModrm() {
	_modrm = fetch8();
	int mod = _modrm >> 6, rm = _modrm & 7;
	if (mod == 3) {
		_modIsReg = true;
		return;
	}
	_modIsReg = false;
	uint16 off = 0;
	int defSeg = kDS;
	switch (rm) {
	case 0: off = r[kBX] + r[kSI]; break;
	case 1: off = r[kBX] + r[kDI]; break;
	case 2: off = r[kBP] + r[kSI]; defSeg = kSS; break;
	case 3: off = r[kBP] + r[kDI]; defSeg = kSS; break;
	case 4: off = r[kSI]; break;
	case 5: off = r[kDI]; break;
	case 6:
		if (mod == 0)
			off = fetch16();
		else {
			off = r[kBP];
			defSeg = kSS;
		}
		break;
	case 7: off = r[kBX]; break;
	}
	if (mod == 1)
		off += (int8)fetch8();
	else if (mod == 2)
		off += fetch16();
	_eaOff = off;
	_eaSeg = _segOverride >= 0 ? seg[_segOverride] : seg[defSeg];
}

uint8 Cpu8086::getRM8() {
	return _modIsReg ? rb(_modrm & 7) : mem8(_eaSeg, _eaOff);
}

uint16 Cpu8086::getRM16() {
	return _modIsReg ? r[_modrm & 7] : mem16(_eaSeg, _eaOff);
}

void Cpu8086::setRM8(uint8 v) {
	if (_modIsReg)
		wb(_modrm & 7, v);
	else
		setMem8(_eaSeg, _eaOff, v);
}

void Cpu8086::setRM16(uint16 v) {
	if (_modIsReg)
		r[_modrm & 7] = v;
	else
		setMem16(_eaSeg, _eaOff, v);
}

void Cpu8086::setSZP8(uint8 v) {
	setFlag(kZF, v == 0);
	setFlag(kSF, v & 0x80);
	setFlag(kPF, parity(v));
}

void Cpu8086::setSZP16(uint16 v) {
	setFlag(kZF, v == 0);
	setFlag(kSF, v & 0x8000);
	setFlag(kPF, parity(v & 0xFF));
}

uint32 Cpu8086::alu8(int op, uint8 a, uint8 b) {
	uint32 res;
	int c = flag(kCF) ? 1 : 0;
	switch (op) {
	case kAdd:
	case kAdc:
		res = a + b + (op == kAdc ? c : 0);
		setFlag(kCF, res > 0xFF);
		setFlag(kAF, (a ^ b ^ res) & 0x10);
		setFlag(kOF, (a ^ res) & (b ^ res) & 0x80);
		break;
	case kSub:
	case kSbb:
	case kCmp:
		res = a - b - (op == kSbb ? c : 0);
		setFlag(kCF, (uint32)a < (uint32)b + (op == kSbb ? c : 0));
		setFlag(kAF, (a ^ b ^ res) & 0x10);
		setFlag(kOF, (a ^ b) & (a ^ res) & 0x80);
		break;
	case kOr: res = a | b; flags &= ~(kCF | kOF | kAF); break;
	case kAnd: res = a & b; flags &= ~(kCF | kOF | kAF); break;
	default: res = a ^ b; flags &= ~(kCF | kOF | kAF); break;
	}
	setSZP8(res & 0xFF);
	return res & 0xFF;
}

uint32 Cpu8086::alu16(int op, uint16 a, uint16 b) {
	uint32 res;
	int c = flag(kCF) ? 1 : 0;
	switch (op) {
	case kAdd:
	case kAdc:
		res = a + b + (op == kAdc ? c : 0);
		setFlag(kCF, res > 0xFFFF);
		setFlag(kAF, (a ^ b ^ res) & 0x10);
		setFlag(kOF, (a ^ res) & (b ^ res) & 0x8000);
		break;
	case kSub:
	case kSbb:
	case kCmp:
		res = a - b - (op == kSbb ? c : 0);
		setFlag(kCF, (uint32)a < (uint32)b + (op == kSbb ? c : 0));
		setFlag(kAF, (a ^ b ^ res) & 0x10);
		setFlag(kOF, (a ^ b) & (a ^ res) & 0x8000);
		break;
	case kOr: res = a | b; flags &= ~(kCF | kOF | kAF); break;
	case kAnd: res = a & b; flags &= ~(kCF | kOF | kAF); break;
	default: res = a ^ b; flags &= ~(kCF | kOF | kAF); break;
	}
	setSZP16(res & 0xFFFF);
	return res & 0xFFFF;
}

uint8 Cpu8086::shift8(int op, uint8 v, int count) {
	count &= 31;
	if (!count)
		return v;
	uint8 res = v;
	for (int i = 0; i < count; i++) {
		bool cf = flag(kCF);
		switch (op) {
		case 0: setFlag(kCF, res & 0x80); res = (res << 1) | (res >> 7); break;
		case 1: setFlag(kCF, res & 1); res = (res >> 1) | (res << 7); break;
		case 2: setFlag(kCF, res & 0x80); res = (res << 1) | (cf ? 1 : 0); break;
		case 3: setFlag(kCF, res & 1); res = (res >> 1) | (cf ? 0x80 : 0); break;
		case 4: case 6: setFlag(kCF, res & 0x80); res <<= 1; break;
		case 5: setFlag(kCF, res & 1); res >>= 1; break;
		case 7: setFlag(kCF, res & 1); res = (res >> 1) | (res & 0x80); break;
		}
	}
	switch (op) {
	case 0: case 2: setFlag(kOF, ((res >> 7) & 1) ^ (flag(kCF) ? 1 : 0)); break;
	case 1: case 3: setFlag(kOF, ((res >> 7) ^ (res >> 6)) & 1); break;
	case 4: case 6: setFlag(kOF, ((res >> 7) & 1) ^ (flag(kCF) ? 1 : 0)); setSZP8(res); break;
	case 5: setFlag(kOF, v & 0x80); setSZP8(res); break;
	case 7: setFlag(kOF, false); setSZP8(res); break;
	}
	return res;
}

uint16 Cpu8086::shift16(int op, uint16 v, int count) {
	count &= 31;
	if (!count)
		return v;
	uint16 res = v;
	for (int i = 0; i < count; i++) {
		bool cf = flag(kCF);
		switch (op) {
		case 0: setFlag(kCF, res & 0x8000); res = (res << 1) | (res >> 15); break;
		case 1: setFlag(kCF, res & 1); res = (res >> 1) | (res << 15); break;
		case 2: setFlag(kCF, res & 0x8000); res = (res << 1) | (cf ? 1 : 0); break;
		case 3: setFlag(kCF, res & 1); res = (res >> 1) | (cf ? 0x8000 : 0); break;
		case 4: case 6: setFlag(kCF, res & 0x8000); res <<= 1; break;
		case 5: setFlag(kCF, res & 1); res >>= 1; break;
		case 7: setFlag(kCF, res & 1); res = (res >> 1) | (res & 0x8000); break;
		}
	}
	switch (op) {
	case 0: case 2: setFlag(kOF, ((res >> 15) & 1) ^ (flag(kCF) ? 1 : 0)); break;
	case 1: case 3: setFlag(kOF, ((res >> 15) ^ (res >> 14)) & 1); break;
	case 4: case 6: setFlag(kOF, ((res >> 15) & 1) ^ (flag(kCF) ? 1 : 0)); setSZP16(res); break;
	case 5: setFlag(kOF, v & 0x8000); setSZP16(res); break;
	case 7: setFlag(kOF, false); setSZP16(res); break;
	}
	return res;
}

void Cpu8086::divide(uint8 n) {
	(void)n;
	intCall(0);
}

void Cpu8086::group3_8() {
	uint8 v = getRM8();
	switch (regField()) {
	case 0: case 1: alu8(kAnd, v, fetch8()); break;
	case 2: setRM8(~v); break;
	case 3: {
		uint8 res = alu8(kSub, 0, v);
		setFlag(kCF, v != 0);
		setRM8(res);
		break;
	}
	case 4: {
		uint16 res = (r[kAX] & 0xFF) * v;
		r[kAX] = res;
		setFlag(kCF, res >> 8);
		setFlag(kOF, res >> 8);
		break;
	}
	case 5: {
		int16 res = (int16)(int8)(r[kAX] & 0xFF) * (int8)v;
		r[kAX] = res;
		bool ext = res != (int8)res;
		setFlag(kCF, ext);
		setFlag(kOF, ext);
		break;
	}
	case 6: {
		if (!v) { divide(0); return; }
		uint16 q = r[kAX] / v, rem = r[kAX] % v;
		if (q > 0xFF) { divide(0); return; }
		r[kAX] = (rem << 8) | q;
		break;
	}
	case 7: {
		if (!v) { divide(0); return; }
		int16 a = r[kAX];
		int q = a / (int8)v, rem = a % (int8)v;
		if (q > 127 || q < -128) { divide(0); return; }
		r[kAX] = ((rem & 0xFF) << 8) | (q & 0xFF);
		break;
	}
	}
}

void Cpu8086::group3_16() {
	uint16 v = getRM16();
	switch (regField()) {
	case 0: case 1: alu16(kAnd, v, fetch16()); break;
	case 2: setRM16(~v); break;
	case 3: {
		uint16 res = alu16(kSub, 0, v);
		setFlag(kCF, v != 0);
		setRM16(res);
		break;
	}
	case 4: {
		uint32 res = (uint32)r[kAX] * v;
		r[kAX] = res & 0xFFFF;
		r[kDX] = res >> 16;
		setFlag(kCF, r[kDX]);
		setFlag(kOF, r[kDX]);
		break;
	}
	case 5: {
		int32 res = (int32)(int16)r[kAX] * (int16)v;
		r[kAX] = res & 0xFFFF;
		r[kDX] = (uint32)res >> 16;
		bool ext = res != (int16)res;
		setFlag(kCF, ext);
		setFlag(kOF, ext);
		break;
	}
	case 6: {
		if (!v) { divide(0); return; }
		uint32 a = ((uint32)r[kDX] << 16) | r[kAX];
		uint32 q = a / v, rem = a % v;
		if (q > 0xFFFF) { divide(0); return; }
		r[kAX] = q;
		r[kDX] = rem;
		break;
	}
	case 7: {
		if (!v) { divide(0); return; }
		int32 a = (int32)(((uint32)r[kDX] << 16) | r[kAX]);
		int32 q = a / (int16)v, rem = a % (int16)v;
		if (q > 32767 || q < -32768) { divide(0); return; }
		r[kAX] = q & 0xFFFF;
		r[kDX] = rem & 0xFFFF;
		break;
	}
	}
}

bool Cpu8086::cond(int cc) const {
	bool res;
	switch (cc >> 1) {
	case 0: res = flag(kOF); break;
	case 1: res = flag(kCF); break;
	case 2: res = flag(kZF); break;
	case 3: res = flag(kCF) || flag(kZF); break;
	case 4: res = flag(kSF); break;
	case 5: res = flag(kPF); break;
	case 6: res = flag(kSF) != flag(kOF); break;
	default: res = flag(kZF) || (flag(kSF) != flag(kOF)); break;
	}
	return (cc & 1) ? !res : res;
}

void Cpu8086::stringOp(uint8 op) {
	int delta = flag(kDF) ? -1 : 1;
	bool word = op & 1;
	int size = word ? 2 : 1;
	uint16 src = dataSeg();
	for (;;) {
		if (_rep && r[kCX] == 0)
			break;
		switch (op) {
		case 0x6C: case 0x6D: // INS
			if (word) setMem16(seg[kES], r[kDI], _bus->in8(r[kDX]) | (_bus->in8(r[kDX] + 1) << 8));
			else setMem8(seg[kES], r[kDI], _bus->in8(r[kDX]));
			r[kDI] += delta * size;
			break;
		case 0x6E: case 0x6F: // OUTS
			_bus->out8(r[kDX], mem8(src, r[kSI]));
			if (word) _bus->out8(r[kDX] + 1, mem8(src, r[kSI] + 1));
			r[kSI] += delta * size;
			break;
		case 0xA4: setMem8(seg[kES], r[kDI], mem8(src, r[kSI])); r[kSI] += delta; r[kDI] += delta; break;
		case 0xA5: setMem16(seg[kES], r[kDI], mem16(src, r[kSI])); r[kSI] += delta * 2; r[kDI] += delta * 2; break;
		case 0xA6: alu8(kCmp, mem8(src, r[kSI]), mem8(seg[kES], r[kDI])); r[kSI] += delta; r[kDI] += delta; break;
		case 0xA7: alu16(kCmp, mem16(src, r[kSI]), mem16(seg[kES], r[kDI])); r[kSI] += delta * 2; r[kDI] += delta * 2; break;
		case 0xAA: setMem8(seg[kES], r[kDI], r[kAX] & 0xFF); r[kDI] += delta; break;
		case 0xAB: setMem16(seg[kES], r[kDI], r[kAX]); r[kDI] += delta * 2; break;
		case 0xAC: wb(0, mem8(src, r[kSI])); r[kSI] += delta; break;
		case 0xAD: r[kAX] = mem16(src, r[kSI]); r[kSI] += delta * 2; break;
		case 0xAE: alu8(kCmp, r[kAX] & 0xFF, mem8(seg[kES], r[kDI])); r[kDI] += delta; break;
		case 0xAF: alu16(kCmp, r[kAX], mem16(seg[kES], r[kDI])); r[kDI] += delta * 2; break;
		}
		if (!_rep)
			break;
		r[kCX]--;
		bool compares = op == 0xA6 || op == 0xA7 || op == 0xAE || op == 0xAF;
		if (compares && ((_rep == 0xF3 && !flag(kZF)) || (_rep == 0xF2 && flag(kZF))))
			break;
	}
}

int Cpu8086::step() {
	if (halted)
		return 4;
	_segOverride = -1;
	_rep = 0;
	uint16 startIp = ip;
	uint8 op;
	// Prefixes
	for (;;) {
		op = fetch8();
		if (op == 0x26) _segOverride = kES;
		else if (op == 0x2E) _segOverride = kCS;
		else if (op == 0x36) _segOverride = kSS;
		else if (op == 0x3E) _segOverride = kDS;
		else if (op == 0xF2 || op == 0xF3) _rep = op;
		else if (op == 0xF0) {}
		else break;
	}

	if (op < 0x40 && (op & 7) < 6) {
		int aop = op >> 3;
		switch (op & 7) {
		case 0: { decodeModrm(); uint8 v = alu8(aop, getRM8(), rb(regField())); if (aop != kCmp) setRM8(v); break; }
		case 1: { decodeModrm(); uint16 v = alu16(aop, getRM16(), r[regField()]); if (aop != kCmp) setRM16(v); break; }
		case 2: { decodeModrm(); uint8 v = alu8(aop, rb(regField()), getRM8()); if (aop != kCmp) wb(regField(), v); break; }
		case 3: { decodeModrm(); uint16 v = alu16(aop, r[regField()], getRM16()); if (aop != kCmp) r[regField()] = v; break; }
		case 4: { uint8 v = alu8(aop, r[kAX] & 0xFF, fetch8()); if (aop != kCmp) wb(0, v); break; }
		case 5: { uint16 v = alu16(aop, r[kAX], fetch16()); if (aop != kCmp) r[kAX] = v; break; }
		}
		return 4;
	}

	switch (op) {
	case 0x06: push(seg[kES]); break;
	case 0x07: seg[kES] = pop(); break;
	case 0x0E: push(seg[kCS]); break;
	case 0x0F: seg[kCS] = pop(); break;
	case 0x16: push(seg[kSS]); break;
	case 0x17: seg[kSS] = pop(); break;
	case 0x1E: push(seg[kDS]); break;
	case 0x1F: seg[kDS] = pop(); break;
	case 0x27: { // DAA
		uint8 al = r[kAX] & 0xFF, old = al;
		bool cf = flag(kCF);
		if ((al & 0x0F) > 9 || flag(kAF)) { al += 6; setFlag(kAF, true); } else setFlag(kAF, false);
		if (old > 0x99 || cf) { al += 0x60; setFlag(kCF, true); } else setFlag(kCF, false);
		wb(0, al);
		setSZP8(al);
		break;
	}
	case 0x2F: { // DAS
		uint8 al = r[kAX] & 0xFF, old = al;
		bool cf = flag(kCF);
		if ((al & 0x0F) > 9 || flag(kAF)) { al -= 6; setFlag(kAF, true); } else setFlag(kAF, false);
		if (old > 0x99 || cf) { al -= 0x60; setFlag(kCF, true); } else setFlag(kCF, false);
		wb(0, al);
		setSZP8(al);
		break;
	}
	case 0x37: // AAA
		if ((r[kAX] & 0x0F) > 9 || flag(kAF)) { r[kAX] += 0x106; setFlag(kAF, true); setFlag(kCF, true); }
		else { setFlag(kAF, false); setFlag(kCF, false); }
		r[kAX] &= 0xFF0F;
		break;
	case 0x3F: // AAS
		if ((r[kAX] & 0x0F) > 9 || flag(kAF)) { r[kAX] -= 6; wb(4, rb(4) - 1); setFlag(kAF, true); setFlag(kCF, true); }
		else { setFlag(kAF, false); setFlag(kCF, false); }
		r[kAX] &= 0xFF0F;
		break;
	case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47: {
		bool cf = flag(kCF);
		r[op & 7] = alu16(kAdd, r[op & 7], 1);
		setFlag(kCF, cf);
		break;
	}
	case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F: {
		bool cf = flag(kCF);
		r[op & 7] = alu16(kSub, r[op & 7], 1);
		setFlag(kCF, cf);
		break;
	}
	case 0x50: case 0x51: case 0x52: case 0x53: case 0x55: case 0x56: case 0x57: push(r[op & 7]); break;
	case 0x54: { uint16 sp = r[kSP] - 2; push(sp); break; }
	case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F: r[op & 7] = pop(); break;
	case 0x60: { // PUSHA
		uint16 sp = r[kSP];
		for (int i = 0; i < 8; i++)
			push(i == kSP ? sp : r[i]);
		break;
	}
	case 0x61: // POPA
		for (int i = 7; i >= 0; i--) {
			uint16 v = pop();
			if (i != kSP)
				r[i] = v;
		}
		break;
	case 0x62: { // BOUND
		decodeModrm();
		int16 v = r[regField()], lo = mem16(_eaSeg, _eaOff), hi = mem16(_eaSeg, _eaOff + 2);
		if (v < lo || v > hi) {
			ip = startIp;
			intCall(5);
		}
		break;
	}
	case 0x68: push(fetch16()); break;
	case 0x69: case 0x6B: {
		decodeModrm();
		int16 a = getRM16();
		int16 b = op == 0x69 ? (int16)fetch16() : (int16)(int8)fetch8();
		int32 res = (int32)a * b;
		r[regField()] = res & 0xFFFF;
		bool ext = res != (int16)res;
		setFlag(kCF, ext);
		setFlag(kOF, ext);
		break;
	}
	case 0x6A: push((int16)(int8)fetch8()); break;
	case 0x6C: case 0x6D: case 0x6E: case 0x6F: stringOp(op); break;
	case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
	case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F: {
		int8 d = fetch8();
		if (cond(op & 15))
			ip += d;
		break;
	}
	case 0x80: case 0x82: { decodeModrm(); uint8 v = alu8(regField(), getRM8(), fetch8()); if (regField() != kCmp) setRM8(v); break; }
	case 0x81: { decodeModrm(); uint16 v = alu16(regField(), getRM16(), fetch16()); if (regField() != kCmp) setRM16(v); break; }
	case 0x83: { decodeModrm(); uint16 v = alu16(regField(), getRM16(), (int16)(int8)fetch8()); if (regField() != kCmp) setRM16(v); break; }
	case 0x84: decodeModrm(); alu8(kAnd, getRM8(), rb(regField())); break;
	case 0x85: decodeModrm(); alu16(kAnd, getRM16(), r[regField()]); break;
	case 0x86: { decodeModrm(); uint8 v = getRM8(); setRM8(rb(regField())); wb(regField(), v); break; }
	case 0x87: { decodeModrm(); uint16 v = getRM16(); setRM16(r[regField()]); r[regField()] = v; break; }
	case 0x88: decodeModrm(); setRM8(rb(regField())); break;
	case 0x89: decodeModrm(); setRM16(r[regField()]); break;
	case 0x8A: decodeModrm(); wb(regField(), getRM8()); break;
	case 0x8B: decodeModrm(); r[regField()] = getRM16(); break;
	case 0x8C: decodeModrm(); setRM16(seg[regField() & 3]); break;
	case 0x8D: decodeModrm(); r[regField()] = _eaOff; break;
	case 0x8E: decodeModrm(); seg[regField() & 3] = getRM16(); break;
	case 0x8F: decodeModrm(); setRM16(pop()); break;
	case 0x90: break;
	case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97: {
		uint16 v = r[op & 7];
		r[op & 7] = r[kAX];
		r[kAX] = v;
		break;
	}
	case 0x98: r[kAX] = (int16)(int8)(r[kAX] & 0xFF); break;
	case 0x99: r[kDX] = (r[kAX] & 0x8000) ? 0xFFFF : 0; break;
	case 0x9A: {
		uint16 o = fetch16(), s = fetch16();
		push(seg[kCS]);
		push(ip);
		ip = o;
		seg[kCS] = s;
		break;
	}
	case 0x9B: break;
	case 0x9C: push(flags); break;
	case 0x9D: flags = (pop() & 0x0FD5) | 0xF002; break;
	case 0x9E: flags = (flags & 0xFF00) | (rb(4) & 0xD5) | 2; break;
	case 0x9F: wb(4, flags & 0xFF); break;
	case 0xA0: wb(0, mem8(dataSeg(), fetch16())); break;
	case 0xA1: r[kAX] = mem16(dataSeg(), fetch16()); break;
	case 0xA2: setMem8(dataSeg(), fetch16(), r[kAX] & 0xFF); break;
	case 0xA3: setMem16(dataSeg(), fetch16(), r[kAX]); break;
	case 0xA4: case 0xA5: case 0xA6: case 0xA7: case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF:
		stringOp(op);
		break;
	case 0xA8: alu8(kAnd, r[kAX] & 0xFF, fetch8()); break;
	case 0xA9: alu16(kAnd, r[kAX], fetch16()); break;
	case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7: wb(op & 7, fetch8()); break;
	case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF: r[op & 7] = fetch16(); break;
	case 0xC0: { decodeModrm(); uint8 v = getRM8(); setRM8(shift8(regField(), v, fetch8())); break; }
	case 0xC1: { decodeModrm(); uint16 v = getRM16(); setRM16(shift16(regField(), v, fetch8())); break; }
	case 0xC2: { uint16 n = fetch16(); ip = pop(); r[kSP] += n; break; }
	case 0xC3: ip = pop(); break;
	case 0xC4: case 0xC5: {
		decodeModrm();
		r[regField()] = mem16(_eaSeg, _eaOff);
		seg[op == 0xC4 ? kES : kDS] = mem16(_eaSeg, _eaOff + 2);
		break;
	}
	case 0xC6: decodeModrm(); setRM8(fetch8()); break;
	case 0xC7: decodeModrm(); setRM16(fetch16()); break;
	case 0xC8: { // ENTER
		uint16 size = fetch16();
		uint8 level = fetch8() & 31;
		push(r[kBP]);
		uint16 frame = r[kSP];
		for (int i = 1; i < level; i++) {
			r[kBP] -= 2;
			push(mem16(seg[kSS], r[kBP]));
		}
		if (level)
			push(frame);
		r[kBP] = frame;
		r[kSP] -= size;
		break;
	}
	case 0xC9: r[kSP] = r[kBP]; r[kBP] = pop(); break;
	case 0xCA: { uint16 n = fetch16(); ip = pop(); seg[kCS] = pop(); r[kSP] += n; break; }
	case 0xCB: ip = pop(); seg[kCS] = pop(); break;
	case 0xCC: intCall(3); break;
	case 0xCD: intCall(fetch8()); break;
	case 0xCE: if (flag(kOF)) intCall(4); break;
	case 0xCF: iret(); break;
	case 0xD0: { decodeModrm(); uint8 v = getRM8(); setRM8(shift8(regField(), v, 1)); break; }
	case 0xD1: { decodeModrm(); uint16 v = getRM16(); setRM16(shift16(regField(), v, 1)); break; }
	case 0xD2: { decodeModrm(); uint8 v = getRM8(); setRM8(shift8(regField(), v, r[kCX] & 0xFF)); break; }
	case 0xD3: { decodeModrm(); uint16 v = getRM16(); setRM16(shift16(regField(), v, r[kCX] & 0xFF)); break; }
	case 0xD4: { // AAM
		uint8 base = fetch8();
		if (!base) { intCall(0); break; }
		uint8 al = r[kAX] & 0xFF;
		r[kAX] = ((al / base) << 8) | (al % base);
		setSZP8(r[kAX] & 0xFF);
		break;
	}
	case 0xD5: { // AAD
		uint8 base = fetch8();
		uint8 al = (rb(0) + rb(4) * base) & 0xFF;
		r[kAX] = al;
		setSZP8(al);
		break;
	}
	case 0xD6: wb(0, flag(kCF) ? 0xFF : 0); break;
	case 0xD7: wb(0, mem8(dataSeg(), r[kBX] + (r[kAX] & 0xFF))); break;
	case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF:
		decodeModrm(); // no FPU
		break;
	case 0xE0: case 0xE1: case 0xE2: {
		int8 d = fetch8();
		r[kCX]--;
		bool go = r[kCX] != 0;
		if (op == 0xE0) go = go && !flag(kZF);
		if (op == 0xE1) go = go && flag(kZF);
		if (go)
			ip += d;
		break;
	}
	case 0xE3: { int8 d = fetch8(); if (!r[kCX]) ip += d; break; }
	case 0xE4: wb(0, _bus->in8(fetch8())); break;
	case 0xE5: { uint8 p = fetch8(); r[kAX] = _bus->in8(p) | (_bus->in8(p + 1) << 8); break; }
	case 0xE6: _bus->out8(fetch8(), r[kAX] & 0xFF); break;
	case 0xE7: { uint8 p = fetch8(); _bus->out8(p, r[kAX] & 0xFF); _bus->out8(p + 1, r[kAX] >> 8); break; }
	case 0xE8: { int16 d = fetch16(); push(ip); ip += d; break; }
	case 0xE9: { int16 d = fetch16(); ip += d; break; }
	case 0xEA: { uint16 o = fetch16(), s = fetch16(); ip = o; seg[kCS] = s; break; }
	case 0xEB: { int8 d = fetch8(); ip += d; break; }
	case 0xEC: wb(0, _bus->in8(r[kDX])); break;
	case 0xED: r[kAX] = _bus->in8(r[kDX]) | (_bus->in8(r[kDX] + 1) << 8); break;
	case 0xEE: _bus->out8(r[kDX], r[kAX] & 0xFF); break;
	case 0xEF: _bus->out8(r[kDX], r[kAX] & 0xFF); _bus->out8(r[kDX] + 1, r[kAX] >> 8); break;
	case 0xF1: // Machine service trap: F1 nn, then the stub's own IRET
		_bus->interrupt(fetch8());
		break;
	case 0xF4: halted = true; break;
	case 0xF5: flags ^= kCF; break;
	case 0xF6: decodeModrm(); group3_8(); break;
	case 0xF7: decodeModrm(); group3_16(); break;
	case 0xF8: flags &= ~kCF; break;
	case 0xF9: flags |= kCF; break;
	case 0xFA: flags &= ~kIF; break;
	case 0xFB: flags |= kIF; break;
	case 0xFC: flags &= ~kDF; break;
	case 0xFD: flags |= kDF; break;
	case 0xFE: {
		decodeModrm();
		bool cf = flag(kCF);
		uint8 v = getRM8();
		if (regField() == 0) setRM8(alu8(kAdd, v, 1));
		else if (regField() == 1) setRM8(alu8(kSub, v, 1));
		setFlag(kCF, cf);
		break;
	}
	case 0xFF: {
		decodeModrm();
		switch (regField()) {
		case 0: case 1: {
			bool cf = flag(kCF);
			uint16 v = getRM16();
			setRM16(alu16(regField() == 0 ? kAdd : kSub, v, 1));
			setFlag(kCF, cf);
			break;
		}
		case 2: { uint16 t = getRM16(); push(ip); ip = t; break; }
		case 3: {
			uint16 o = mem16(_eaSeg, _eaOff), s = mem16(_eaSeg, _eaOff + 2);
			push(seg[kCS]);
			push(ip);
			ip = o;
			seg[kCS] = s;
			break;
		}
		case 4: ip = getRM16(); break;
		case 5: ip = mem16(_eaSeg, _eaOff); seg[kCS] = mem16(_eaSeg, _eaOff + 2); break;
		case 6: push(getRM16()); break;
		default: break;
		}
		break;
	}
	default:
		// Unknown opcode: invalid opcode exception, as on a 186
		ip = startIp;
		intCall(6);
		break;
	}
	return 4;
}

} // End of namespace DosGames
