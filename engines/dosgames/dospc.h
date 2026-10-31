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

#ifndef DOSGAMES_DOSPC_H
#define DOSGAMES_DOSPC_H

#include "common/array.h"
#include "common/hash-str.h"
#include "common/hashmap.h"
#include "common/str.h"
#include "common/str-array.h"
#include "dosgames/cpu8086.h"

namespace Common {
class ReadStream;
class WriteStream;
}

namespace DosGames {

/** What the PC needs from the engine: the game's files, and its sound. */
class DosHost {
public:
	virtual ~DosHost() {}
	/** Reads a file from the game folder (or one the game saved). */
	virtual bool readFile(const Common::String &name, Common::Array<byte> &data) = 0;
	/** Keeps a file the game wrote (saved games, settings). */
	virtual void writeFile(const Common::String &name, const Common::Array<byte> &data) = 0;
	/** Names of the files in the game folder and saved ones, upper case. */
	virtual Common::StringArray listFiles() = 0;
	virtual void oplWrite(int reg, int value) = 0;
};

/**
 * Just enough of a 1990 PC to run a DOS game's own program: an 8086 (with
 * 186 extras), 640K, the BIOS and DOS services such games call (answered
 * directly, no Microsoft or IBM code), VGA's 256-colour mode 13h and text
 * mode, keyboard, mouse driver, timer and an AdLib card.
 */
class DosPC : public Bus8086 {
public:
	enum {
		kScreenW = 640,
		kScreenH = 400,
		kInstructionsPerFrame = 120000 // about 6 million a second
	};

	explicit DosPC(DosHost *host);

	/** Programs to run one after the other, like a batch file. */
	void addProgram(const Common::String &name, const Common::String &args);
	/** Replaces a file's contents as the game sees it (not saved). */
	void overrideFile(const Common::String &name, const Common::Array<byte> &data);
	bool start();
	/** Runs 20 ms. Returns false once the last program has ended. */
	bool runFrame();

	uint8 read8(uint32 addr) override;
	void write8(uint32 addr, uint8 v) override;
	uint8 in8(uint16 port) override;
	void out8(uint16 port, uint8 v) override;
	bool interrupt(uint8 n) override;

	/** The 8x8 character shapes (256 x 8 bytes) for text mode. */
	void setFont(const byte *glyphs) { memcpy(&_mem[0xFA000], glyphs, 256 * 8); }
	/** Draws the screen as 8-bit palette indexes; palette() has the colours. */
	void render(byte *dst, int pitch);
	const byte *palette() const { return _rgb; }
	bool paletteChanged() { bool c = _paletteDirty; _paletteDirty = false; return c; }

	/** A key from the host: PC scan code and ASCII (0 for none). */
	void keyDown(uint8 scan, uint8 ascii);
	void keyUp(uint8 scan);
	/** Mouse in screen pixels (640x400). */
	void mouseMove(int x, int y);
	void mouseButton(int button, bool down);

	void saveState(Common::WriteStream &s);
	bool loadState(Common::ReadStream &s);

	Cpu8086 &cpu() { return _cpu; }
	byte *memory() { return _mem; }

private:
	struct File {
		Common::String name;
		Common::Array<byte> data;
		uint32 pos;
		bool open, dirty, device;
	};
	struct Process {
		uint16 psp;
		uint16 ss, sp; // the parent's stack at the EXEC call
	};
	struct Program {
		Common::String name, args;
	};

	DosHost *_host;
	Cpu8086 _cpu;
	byte *_mem;
	Common::Array<Program> _programs;
	uint _nextProgram;
	Common::HashMap<Common::String, Common::Array<byte>, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> _overrides;
	Common::Array<File> _files;
	Common::Array<Process> _processes;
	uint16 _currentPsp;
	uint16 _dta[2]; // offset, segment
	uint8 _returnCode;
	bool _finished;

	// Find first/next
	Common::StringArray _found;
	uint _foundPos;

	// Video
	uint8 _videoMode;
	byte _dac[256 * 3];
	byte _rgb[256 * 3];
	bool _paletteDirty;
	uint8 _dacWrite, _dacRead, _dacComp;
	uint32 _retrace;

	// Keyboard: BIOS buffer of (scan << 8 | ascii) and the port 60 latch
	Common::Array<uint16> _keys;
	Common::Array<uint8> _scanQueue;
	uint8 _port60;
	uint8 _picMask;
	uint8 _picInService; // IRQs being handled, until the handler's EOI

	// Mouse
	int _mouseX, _mouseY, _mouseButtons;
	int _mouseMinX, _mouseMaxX, _mouseMinY, _mouseMaxY;
	int _mousePresses[3], _mouseReleases[3];
	int _mouseCursor;

	// Timer
	uint32 _pitDivisor;
	uint8 _pitLatchLow;
	bool _pitLowNext;
	int32 _tickAccum;

	// AdLib
	uint8 _oplReg;
	uint8 _oplTimerCtl;
	bool _waitingForKey;

	uint32 _frame;

	void installBios();
	void setStackedFlag(uint16 flag, bool on);
	uint16 stub(uint8 n) const { return 0x1000 + n * 8; }

	void int10();
	void keyboardBios();
	void int21();
	void int33();
	void int1a();

	// DOS
	Common::String readString(uint16 s, uint16 o, int max = 128);
	Common::String dosName(const Common::String &path) const;
	bool loadFile(const Common::String &name, Common::Array<byte> &data);
	int openHandle(const Common::String &name, bool create);
	uint16 allocate(uint16 paras, uint16 owner, uint16 *largest);
	bool resize(uint16 segment, uint16 paras, uint16 *largest);
	void freeBlock(uint16 segment);
	void freeOwnedBy(uint16 psp);
	bool exec(const Common::String &name, const Common::String &args, bool fromProgram);
	void terminate(uint8 code, bool keep, uint16 keepParas);
	bool findNext();
	static bool wildcardMatch(const Common::String &pattern, const Common::String &name);

	bool raiseIrq(int irq);
	void scrollText(int top, int bottom, int lines, uint8 attr);
	void putChar(uint8 c, uint8 attr, bool advance);
};

} // End of namespace DosGames

#endif
