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

#ifndef DARKLORD_MACHINE_H
#define DARKLORD_MACHINE_H

#include "common/array.h"
#include "common/hash-str.h"
#include "common/hashmap.h"
#include "common/str.h"
#include "darklord/cpu6502.h"

namespace Common {
class ReadStream;
class SeekableReadStream;
class WriteStream;
}

namespace DarkLord {

/**
 * Receives the game's disk saves and SID writes. Implemented by the engine.
 */
class MachineHost {
public:
	virtual ~MachineHost() {}
	virtual void sidWrite(int reg, int value) = 0;
	/** Stores a file the game saved to disk (load address + data). */
	virtual void diskSave(const Common::String &name, const Common::Array<byte> &data) = 0;
	/** Looks for a file the game saved earlier. */
	virtual bool diskLoad(const Common::String &name, Common::Array<byte> &data) = 0;
};

/**
 * Just enough of a Commodore 64 to run Dark Lord's original code:
 * the 6502, memory banking, a stand-in KERNAL that serves files from the
 * disk images, the VIC-II registers and raster interrupt, CIA timers and
 * keyboard, and SID register writes. It contains no Commodore ROM code.
 */
class Machine {
public:
	enum {
		kScreenW = 320,
		kScreenH = 200,
		kCyclesPerLine = 63,
		kLinesPerFrame = 312,
		kCyclesPerFrame = kCyclesPerLine * kLinesPerFrame,
		kFirstVisibleLine = 51
	};

	explicit Machine(MachineHost *host);

	/** Adds every file on a D64 disk image. */
	bool addDisk(Common::SeekableReadStream &d64);
	bool hasFile(const Common::String &name) const { return _files.contains(name); }
	/** Loads the boot program and points the CPU at it. */
	bool boot();
	/** Runs one PAL frame (20 ms) of emulated time. */
	void runFrame();

	uint8 read(uint16 addr);
	void write(uint16 addr, uint8 v);

	/** Draws the visible 320x200 screen as C64 colour indexes (0-15). */
	void render(byte *dst, int pitch) const;
	/** Supplies 4 KB of character shapes in C64 character ROM layout. */
	void setCharRom(const byte *data);

	/** Types a character: held on the keyboard matrix for a few frames. */
	void typeChar(int ch);
	bool keyQueueEmpty() const { return _typing.empty() && _keyHold == 0; }

	void saveState(Common::WriteStream &s) const;
	bool loadState(Common::ReadStream &s);

	uint32 frameCount() const { return _frame; }

private:
	struct TypedKey {
		uint8 pa, pb;
		bool shift;
	};
	struct LineRegs {
		uint8 d011, d016, d018, d021, dd00;
	};

	MachineHost *_host;
	Cpu6502 _cpu;
	byte _ram[65536];
	byte _kernal[8192];
	byte _charRom[4096];
	byte _color[1024];
	byte _vic[64];
	byte _cia1[16];
	byte _cia2[16];
	Common::HashMap<Common::String, Common::Array<byte> > _files;
	Common::Array<byte> _lastDisk;

	int _raster;
	int _lineCycles;
	int _rasterCmp;
	uint8 _vicIrq;
	int _stall;
	bool _denLatched;
	int32 _timerA;
	uint16 _timerALatch;
	uint8 _cia1IcrMask, _cia1Icr;
	uint32 _frame;
	LineRegs _lines[kLinesPerFrame];

	uint8 _matrix[8];
	Common::Array<TypedKey> _typing;
	int _keyHold, _keyGap;

	Common::String _status;
	uint _statusPos;

	enum MapType { kMapRam, kMapIo, kMapKernal, kMapRom };
	MapType mapOf(uint16 addr) const;
	uint8 ioRead(uint16 addr);
	void ioWrite(uint16 addr, uint8 v);
	uint8 keyboardRows(uint8 columns) const;
	void keyboardFrame();
	void installKernal();
	bool trap();
	void rts();
	Common::String zpFilename() const;
	void kernalLoad();
	void kernalSave();
	bool loaderTrap();
	int sectorOffset(int track, int sector) const;
	void advance(int cycles);
	byte vicRead(uint16 bankBase, uint16 offset) const;
};

} // End of namespace DarkLord

#endif
