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

#ifndef NESVENTURE_NESUI_H
#define NESVENTURE_NESUI_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/rect.h"
#include "common/str.h"
#include "graphics/managed_surface.h"
#include "nesventure/detection.h"

namespace Graphics {
class Font;
}

namespace NesVenture {

class Nes;

/** Where things are on a game's NES screen (NES pixels, tile positions). */
struct NesItem {
	const char *name; // as the layouts label it
	int col, row;     // the item's marker tile in name table 0
};

struct NesGameMap {
	uint16 pointerX, pointerY; // RAM: the pointer sprite's position
	const NesItem *verbs;
	int verbCount;
	Common::Rect picture;  // the room picture with its frame
	Common::Rect moveBox;  // the exits
	const char *goodsTitle;
	int rowOffset;   // the panels' rows are drawn at row * 8 + rowOffset
	byte blankTile;  // the tile the panels are filled with
};

/**
 * Near-Grog's phone layouts for the NES MacVentures (as agreed for every
 * MacVenture port): the picture full height on the left in landscape (the
 * NES pictures are square), buttons, verbs, the text and the inventory with
 * the exits beside it; one portrait layout. Two looks: the game's own (built
 * from its live screen) and Dark. Taps steer the game's own pointer with the
 * d-pad and press A.
 */
class NesUI {
public:
	enum Look { kLookNes, kLookDark };
	enum Action { kNone, kSaveDialog, kLoadDialog, kQuickSave, kQuickLoad, kTray };

	NesUI(Nes *nes, GameId game);
	~NesUI();

	static bool portraitScreen();
	static Look configLook();
	void setup(int w, int h, bool portrait, Look look, const Graphics::PixelFormat &format);

	/** After each NES frame: follows the screen, records text, steers. */
	void update();
	/** The controller buttons the layout holds for the next frame. */
	uint8 buttons() const { return _pad; }
	/** True while steering: the engine runs the NES faster meanwhile. */
	bool steering() const { return _goal.active; }

	/** Draws the layout; false if nothing has changed since last time. */
	bool draw();
	const Graphics::ManagedSurface &output() const { return _out; }

	/** A touch (screen coordinates); returns what the engine should do. */
	Action press(Common::Point p);
	void move(Common::Point p);
	Action release(Common::Point p);
	Action tick(); // holds that come due
	void scroll(int lines);

private:
	Nes *_nes;
	GameId _game;
	const NesGameMap *_map;
	int _w, _h;
	bool _portrait;
	Look _look;
	Graphics::PixelFormat _format;
	Graphics::ManagedSurface _out;
	uint32 _pal[64];
	Common::HashMap<Common::String, Graphics::Font *> _fonts;

	// The game's screen
	bool _textMode;
	int _commandFrames; // frames the command panel has been up
	byte _snapshot[256 * 240]; // the last command screen
	bool _haveSnapshot;
	uint8 _textFg, _textBg;    // NES colours of the text panel
	bool _haveTextColours;
	uint8 _verbPaper;          // the command panel's paper

	// Text: rows of tiles - what's been read since the last command, and before
	Common::Array<Common::Array<byte> > _page;
	Common::Array<Common::Array<byte> > _history;
	int _scroll; // lines up from the newest

	// Steering
	struct Goal {
		bool active;
		Common::Point target;
		int still, frames, aFrames;
		uint8 lead;  // a press to make first
		Common::Point last;
	} _goal;
	// Places to go next, in turn, once there (MOVE, then the exit)
	struct Step {
		Common::Point target;
		uint8 lead;        // a press to make first
	};
	Common::Array<Step> _steps;
	void then(Common::Point target, uint8 lead = 0) { Step s; s.target = target; s.lead = lead; _steps.push_back(s); }
	// A tap on the picture waits a moment to see if it's a double tap
	bool _tapPending;
	Common::Point _tapPoint;   // NES pixels
	uint32 _tapTime;
	// A double tap: go there (MOVE); a shut door is opened first
	int _smart;                // 0 none, 1 moving (watching the reply), 2 to open, 3 opening and going
	Common::Point _smartTarget;
	int _smartIdle;            // frames quiet since the last step
	int verbIndex(const char *name) const;
	bool _textDrag, _dragged;  // a swipe in the text
	int _dragScroll;
	bool _dropPage;            // the page being read is left out of the history
	int _flashVerb;            // the verb just tapped, lit for a moment
	uint32 _flashUntil;
	int selectedVerb() const;
	Common::Rect moveMapRect() const;
	Common::Point moveTarget(Common::Point nes) const;  // the game's chosen verb (its marker filled in), or -1
	uint8 _pad;
	int _continueFrames;

	// Title and menu screens (not the play screen): the whole NES screen and
	// an NES pad
	bool _fullMode;
	Common::Rect _fullBox, _fullMenu, _fullBack, _fullPad, _padRects[8];
	uint8 _padHeld, _padLast;
	int _padTouch, _padLinger;
	bool _padShown;            // the pad, after a long press
	Common::Rect _fullBig;     // the screen without the pad
	Common::Point _fullStart;  // where a touch began
	uint32 _fullStartTime;
	bool _fullTouching, _fullConsumed;
	int _tapWait;              // frames left to see whether A did anything
	uint32 _tapSig;
	uint32 tableSignature();
	void padPress(uint8 bits);
	bool playScreen();
	void drawFull();

	// Layout (screen pixels)
	Common::Rect _picBox, _buttons[3], _verbBox, _textBox, _goodsBox, _moveBox;
	Common::Array<Common::Rect> _verbCells;
	Common::Rect _goodsSrc; // the NES goods rows shown
	int _pressButton;
	uint32 _pressTime;
	bool _holdFired;
	Common::Point _pressPoint;
	uint32 _lastSignature;
	bool _drawn;
	uint32 signature() const;

	void layout();
	int rowY(int r) const;
	void steerTo(Common::Point nes);
	void steer();
	void readText();
	void commitPage();
	Common::String decode(const Common::Array<byte> &row) const;
	Common::Rect itemRect(const NesItem &it) const; // NES pixels
	Common::Point itemPoint(const NesItem &it) const;

	uint32 rgb(int r, int g, int b) const { return _format.RGBToColor(r, g, b); }
	void blitNes(const byte *src, const Common::Rect &from, const Common::Rect &to);
	void drawGlyph(byte tile, int x, int y, int scale, uint32 fg, uint32 bg);
	void nesText(const char *s, int x, int y, int scale, uint32 col);
	void drawGlyphF(byte tile, float x, int y, float scale, uint32 fg);
	const Graphics::Font *font(int size, bool bold, bool serif);
	void text(const Common::String &s, int x, int y, int w, uint32 col, const Graphics::Font *f, bool center = false);
	void roundRect(const Common::Rect &r, int radius, uint32 fill, uint32 edge);
	void drawButtons();
	void drawVerbs();
	void drawText();
	void drawShelf();
	void drawHoldRing(const Common::Rect &r, int radius, float progress);
	Common::Point toNes(const Common::Rect &box, const Common::Rect &src, Common::Point p) const;
};

} // End of namespace NesVenture

#endif
