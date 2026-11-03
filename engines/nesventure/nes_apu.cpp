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

#include "nesventure/nes.h"

namespace NesVenture {

// The 2A03's sound: two pulse channels, triangle, noise and the sample
// (DMC) channel, mixed with the chip's own non-linear mix, sampled at
// Nes::kSampleRate.

namespace {

const uint8 kLength[32] = {
	10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14,
	12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30
};
const uint8 kDuty[4][8] = {
	{ 0, 1, 0, 0, 0, 0, 0, 0 }, { 0, 1, 1, 0, 0, 0, 0, 0 },
	{ 0, 1, 1, 1, 1, 0, 0, 0 }, { 1, 0, 0, 1, 1, 1, 1, 1 }
};
const uint8 kTriangle[32] = {
	15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
};
const uint16 kNoise[16] = { 4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068 };
const uint16 kDmcRate[16] = { 428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54 };

struct Envelope {
	bool start, loop, constant;
	uint8 period, divider, decay, volume;
	void tick() {
		if (start) {
			start = false;
			decay = 15;
			divider = period;
		} else if (divider == 0) {
			divider = period;
			if (decay)
				decay--;
			else if (loop)
				decay = 15;
		} else {
			divider--;
		}
	}
	uint8 out() const { return constant ? volume : decay; }
};

struct Pulse {
	bool enabled, second;
	uint8 duty, step, length;
	uint16 period, timer;
	Envelope env;
	bool sweepOn, sweepNeg, sweepReload;
	uint8 sweepPeriod, sweepShift, sweepDivider;
	uint16 target() const {
		const uint16 change = period >> sweepShift;
		if (sweepNeg)
			return period - change - (second ? 0 : 1);
		return period + change;
	}
	bool muted() const { return period < 8 || target() > 0x7FF; }
	void clockTimer() {
		if (timer == 0) {
			timer = period;
			step = (step + 1) & 7;
		} else {
			timer--;
		}
	}
	void clockSweep() {
		if (sweepDivider == 0 && sweepOn && sweepShift && !muted())
			period = target();
		if (sweepDivider == 0 || sweepReload) {
			sweepDivider = sweepPeriod;
			sweepReload = false;
		} else {
			sweepDivider--;
		}
	}
	uint8 out() const {
		if (!enabled || !length || muted() || !kDuty[duty][step])
			return 0;
		return env.out();
	}
};

struct Triangle {
	bool enabled, control, linearReload;
	uint8 length, linear, linearPeriod, step;
	uint16 period, timer;
	void clockTimer() {
		if (timer == 0) {
			timer = period;
			if (length && linear && period > 1)
				step = (step + 1) & 31;
		} else {
			timer--;
		}
	}
	void clockLinear() {
		if (linearReload)
			linear = linearPeriod;
		else if (linear)
			linear--;
		if (!control)
			linearReload = false;
	}
	uint8 out() const { return enabled ? kTriangle[step] : 0; }
};

struct Noise {
	bool enabled, mode;
	uint8 length;
	uint16 period, timer, shift;
	Envelope env;
	void clockTimer() {
		if (timer == 0) {
			timer = period;
			const uint16 fb = (shift & 1) ^ ((shift >> (mode ? 6 : 1)) & 1);
			shift = (shift >> 1) | (fb << 14);
		} else {
			timer--;
		}
	}
	uint8 out() const { return (!enabled || !length || (shift & 1)) ? 0 : env.out(); }
};

struct Dmc {
	bool enabled, irqOn, loop, irq;
	uint16 rate, timer, startAddr, startLen, addr, remaining;
	uint8 level, shift, bits, sample;
	bool hasSample, silent;
};

} // End of anonymous namespace

struct Nes::Apu {
	Pulse pulse[2];
	Triangle tri;
	Noise noise;
	Dmc dmc;
	bool fiveStep, irqInhibit, frameIrq;
	uint32 frameCycle;
	bool odd;
	double sampleAcc, sampleSum;
	int sampleCount;
};

void Nes::apuInit() {
	_apu = new Apu();
	memset((void *)_apu, 0, sizeof(Apu));
	_apu->pulse[1].second = true;
	_apu->noise.shift = 1;
	_apu->noise.period = kNoise[0];
	_apu->dmc.rate = kDmcRate[0];
	_apu->dmc.silent = true;
	_apu->dmc.bits = 8;
}

void Nes::apuFree() {
	delete _apu;
	_apu = nullptr;
}

void Nes::apuWrite(uint16 addr, uint8 v) {
	Apu &a = *_apu;
	if (addr < 0x4008) {
		Pulse &p = a.pulse[(addr >> 2) & 1];
		switch (addr & 3) {
		case 0:
			p.duty = v >> 6;
			p.env.loop = v & 0x20;
			p.env.constant = v & 0x10;
			p.env.period = p.env.volume = v & 0x0F;
			break;
		case 1:
			p.sweepOn = v & 0x80;
			p.sweepPeriod = (v >> 4) & 7;
			p.sweepNeg = v & 8;
			p.sweepShift = v & 7;
			p.sweepReload = true;
			break;
		case 2:
			p.period = (p.period & 0x700) | v;
			break;
		default:
			p.period = (p.period & 0xFF) | ((v & 7) << 8);
			if (p.enabled)
				p.length = kLength[v >> 3];
			p.step = 0;
			p.env.start = true;
			break;
		}
		return;
	}
	switch (addr) {
	case 0x4008:
		a.tri.control = v & 0x80;
		a.tri.linearPeriod = v & 0x7F;
		break;
	case 0x400A:
		a.tri.period = (a.tri.period & 0x700) | v;
		break;
	case 0x400B:
		a.tri.period = (a.tri.period & 0xFF) | ((v & 7) << 8);
		if (a.tri.enabled)
			a.tri.length = kLength[v >> 3];
		a.tri.linearReload = true;
		break;
	case 0x400C:
		a.noise.env.loop = v & 0x20;
		a.noise.env.constant = v & 0x10;
		a.noise.env.period = a.noise.env.volume = v & 0x0F;
		break;
	case 0x400E:
		a.noise.mode = v & 0x80;
		a.noise.period = kNoise[v & 0x0F];
		break;
	case 0x400F:
		if (a.noise.enabled)
			a.noise.length = kLength[v >> 3];
		a.noise.env.start = true;
		break;
	case 0x4010:
		a.dmc.irqOn = v & 0x80;
		a.dmc.loop = v & 0x40;
		a.dmc.rate = kDmcRate[v & 0x0F];
		if (!a.dmc.irqOn)
			a.dmc.irq = false;
		break;
	case 0x4011:
		a.dmc.level = v & 0x7F;
		break;
	case 0x4012:
		a.dmc.startAddr = 0xC000 + v * 64;
		break;
	case 0x4013:
		a.dmc.startLen = v * 16 + 1;
		break;
	case 0x4015:
		a.pulse[0].enabled = v & 1;
		a.pulse[1].enabled = v & 2;
		a.tri.enabled = v & 4;
		a.noise.enabled = v & 8;
		if (!a.pulse[0].enabled) a.pulse[0].length = 0;
		if (!a.pulse[1].enabled) a.pulse[1].length = 0;
		if (!a.tri.enabled) a.tri.length = 0;
		if (!a.noise.enabled) a.noise.length = 0;
		a.dmc.irq = false;
		if (v & 0x10) {
			if (!a.dmc.remaining) {
				a.dmc.addr = a.dmc.startAddr;
				a.dmc.remaining = a.dmc.startLen;
			}
		} else {
			a.dmc.remaining = 0;
		}
		break;
	case 0x4017:
		a.fiveStep = v & 0x80;
		a.irqInhibit = v & 0x40;
		if (a.irqInhibit)
			a.frameIrq = false;
		a.frameCycle = 0;
		break;
	default:
		break;
	}
}

uint8 Nes::apuStatus() {
	Apu &a = *_apu;
	uint8 r = (a.pulse[0].length ? 1 : 0) | (a.pulse[1].length ? 2 : 0) | (a.tri.length ? 4 : 0) |
		(a.noise.length ? 8 : 0) | (a.dmc.remaining ? 0x10 : 0) | (a.frameIrq ? 0x40 : 0) | (a.dmc.irq ? 0x80 : 0);
	a.frameIrq = false;
	return r;
}

bool Nes::apuIrq() {
	return _apu->frameIrq || _apu->dmc.irq;
}

void Nes::apuRun(int cycles) {
	Apu &a = *_apu;
	const double cyclesPerSample = 1789773.0 / kSampleRate;
	for (int c = 0; c < cycles; c++) {
		// Frame sequencer (in CPU cycles)
		a.frameCycle++;
		bool quarter = false, half = false;
		switch (a.frameCycle) {
		case 7457: quarter = true; break;
		case 14913: quarter = half = true; break;
		case 22371: quarter = true; break;
		case 29829:
			if (!a.fiveStep) {
				quarter = half = true;
				if (!a.irqInhibit)
					a.frameIrq = true;
				a.frameCycle = 0;
			}
			break;
		case 37281:
			quarter = half = true;
			a.frameCycle = 0;
			break;
		default:
			break;
		}
		if (quarter) {
			a.pulse[0].env.tick();
			a.pulse[1].env.tick();
			a.noise.env.tick();
			a.tri.clockLinear();
		}
		if (half) {
			for (int i = 0; i < 2; i++) {
				if (a.pulse[i].length && !a.pulse[i].env.loop)
					a.pulse[i].length--;
				a.pulse[i].clockSweep();
			}
			if (a.tri.length && !a.tri.control)
				a.tri.length--;
			if (a.noise.length && !a.noise.env.loop)
				a.noise.length--;
		}

		// Timers: the triangle every cycle, the rest every other
		a.tri.clockTimer();
		a.odd = !a.odd;
		if (a.odd) {
			a.pulse[0].clockTimer();
			a.pulse[1].clockTimer();
			a.noise.clockTimer();
		}

		// The sample channel
		Dmc &d = a.dmc;
		if (d.timer == 0) {
			d.timer = d.rate;
			if (!d.silent) {
				if (d.shift & 1) {
					if (d.level <= 125)
						d.level += 2;
				} else if (d.level >= 2) {
					d.level -= 2;
				}
			}
			d.shift >>= 1;
			if (--d.bits == 0) {
				d.bits = 8;
				if (d.hasSample) {
					d.silent = false;
					d.shift = d.sample;
					d.hasSample = false;
				} else {
					d.silent = true;
				}
			}
		} else {
			d.timer--;
		}
		if (!d.hasSample && d.remaining) {
			d.sample = read(d.addr);
			_stall += 4;
			d.hasSample = true;
			d.addr = d.addr == 0xFFFF ? 0x8000 : d.addr + 1;
			if (--d.remaining == 0) {
				if (d.loop) {
					d.addr = d.startAddr;
					d.remaining = d.startLen;
				} else if (d.irqOn) {
					d.irq = true;
				}
			}
		}

		// Mix
		const int p = a.pulse[0].out() + a.pulse[1].out();
		const double pulseOut = p ? 95.88 / (8128.0 / p + 100) : 0;
		const double tnd = a.tri.out() / 8227.0 + a.noise.out() / 12241.0 + d.level / 22638.0;
		const double tndOut = tnd > 0 ? 159.79 / (1 / tnd + 100) : 0;
		a.sampleSum += pulseOut + tndOut;
		a.sampleCount++;
		a.sampleAcc += 1;
		if (a.sampleAcc >= cyclesPerSample) {
			a.sampleAcc -= cyclesPerSample;
			const double s = a.sampleSum / a.sampleCount;
			a.sampleSum = 0;
			a.sampleCount = 0;
			audio.push_back((int16)CLIP<int>((int)((s - 0.25) * 52000), -32768, 32767));
		}
	}
}

} // End of namespace NesVenture
