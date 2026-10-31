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

#ifndef DARKLORD_APPLE2_H
#define DARKLORD_APPLE2_H

#include "common/array.h"
#include "common/str.h"
#include "darklord/cpu6502.h"

namespace Common {
class ReadStream;
class WriteStream;
}

namespace DarkLord {

/**
 * Receives what the Apple II produces besides the picture: speaker sound
 * and the sectors the game writes to disk (saved games). Implemented by the
 * engine.
 */
class Apple2Host {
public:
	virtual ~Apple2Host() {}
	/** One frame of speaker sound, signed 16-bit mono at kSampleRate. */
	virtual void appleSound(const int16 *samples, int count) = 0;
	virtual void diskSave(const Common::String &name, const Common::Array<byte> &data) = 0;
	virtual bool diskLoad(const Common::String &name, Common::Array<byte> &data) = 0;
};

/**
 * Just enough of a 64K Apple II+ to run Dark Lord's original code: the
 * 6502, the language card, the text and hi-res screens, keyboard, speaker
 * and a Disk II drive in slot 6. It contains no Apple ROM code: the few
 * Monitor routines the game calls are answered directly, and the disk boot
 * ROM's sector reader is replaced the same way. The drive works at the
 * level of the bytes on the disk surface, so the game's own disk code runs
 * unchanged.
 */
class Apple2 : public Bus {
public:
	enum {
		kScreenW = 560,
		kScreenH = 384,
		kCyclesPerFrame = 20460, // 1.023 MHz, 20 ms
		kSampleRate = 22050,
		kSamplesPerFrame = kSampleRate / 50
	};

	explicit Apple2(Apple2Host *host);

	/** Adds a 140K disk side (DOS 3.3 sector order). The first is booted. */
	bool addDisk(const Common::Array<byte> &dsk);
	bool boot();
	void runFrame();

	uint8 read(uint16 addr) override;
	void write(uint16 addr, uint8 v) override;

	/** Draws the screen as palette indexes 0-15 (the lo-res colours). */
	void render(byte *dst, int pitch) const;
	static const byte *palette();
	void setFont(const byte *glyphs); // 128 glyphs of 8 bytes, ASCII order

	void typeChar(int ch);
	bool keyQueueEmpty() const { return _keys.empty() && !(_key & 0x80); }

	void saveState(Common::WriteStream &s);
	bool loadState(Common::ReadStream &s);
	uint32 frameCount() const { return _frame; }

	/** Swaps to the other disk side, as the player would. */
	void flipDisk();
	int currentSide() const { return _side; }

private:
	Apple2Host *_host;
	Cpu6502 _cpu;
	byte _ram[0x10000];
	byte _lcBank[2][0x1000];
	byte _lcHigh[0x2000];
	byte _rom[0x3000]; // stand-in $D000-$FFFF
	byte _font[128 * 8];

	bool _lcRead, _lcWrite, _lcBank2;
	int _lcPrewrite;

	bool _text, _mixed, _page2, _hires;
	uint8 _key;
	Common::Array<uint8> _keys;
	int _keyDelay;

	// Speaker: cycle times of the clicks in the current frame
	Common::Array<int> _clicks;
	bool _speaker;
	int32 _level;
	int32 _dc;

	uint32 _frame;
	int _frameCycles;

	// Disk II
	Common::Array<Common::Array<byte> > _sides; // 143360 bytes each
	int _side;
	int _halfTrack;
	uint8 _phases;
	bool _motor, _q6, _q7;
	int _spinDown;     // frames the disk keeps turning after motor off
	bool _byteReady;   // the latch holds a whole disk byte
	uint8 _latch;
	Common::Array<byte> _nibbles; // the current track's surface
	int _nibbleTrack;
	uint _pos;
	bool _trackDirty;

	uint8 io(uint16 addr, bool writing, uint8 v);
	uint8 disk(int reg, bool writing, uint8 v);
	void loadTrack();
	void storeTrack();
	static int physicalToLogical(int p);
	void readSector(int side, int track, int logical, byte *out);
	void writeSector(int side, int track, int logical, const byte *in);

	bool _promptShown;
	bool screenShows(const char *text) const;
	void answerDiskPrompt();
	bool trap();
	void rts();
	void cout(uint8 c);
	void hposn(int x, int y);
	void hplot(int x, int y);
	void hlin(int x, int y);
	void bootSector();
	void mixSound();
};

} // End of namespace DarkLord

#endif
