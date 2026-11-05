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

#ifndef DARKLORD_C64UI_H
#define DARKLORD_C64UI_H

#include "common/array.h"
#include "common/hash-str.h"
#include "common/hashmap.h"
#include "common/rect.h"
#include "common/str.h"
#include "graphics/font.h"
#include "graphics/managed_surface.h"

namespace DarkLord {

class Machine;

/**
 * Near-Grog's phone layouts for the C64 Uninvited, as agreed in the
 * mock-ups: the game's screen taken apart (picture, verbs, inventory,
 * exits, text) and laid out again - C2, E1, E2 or the game's own screen
 * (Original) in landscape, one layout in portrait - in the C64's own look
 * or Dark. Each piece is the live C64 screen scaled, or drawn from what it
 * shows (the verbs, the text, read with the game's own font); a touch on a
 * piece points the game's pointer at the matching spot on the C64 screen.
 */
class C64UI {
public:
	enum Look { kLookC64, kLookDark };
	enum Layout { kLayoutC2, kLayoutE1, kLayoutE2, kLayoutOriginal };
	enum Action { kNone, kSaveDialog, kLoadDialog, kQuickSave, kQuickLoad, kTray };

	explicit C64UI(Machine *machine);
	~C64UI();

	static bool portraitScreen();
	static Look configLook();
	static Layout configLayout();
	static const char *layoutName(Layout l);

	void setup(int w, int h, bool portrait, Look look, Layout layout, const Graphics::PixelFormat &format);
	/** After each C64 frame: reads the text window. */
	void frame(const byte *screen);
	/** While the game starts (the crack's intro, the disk swap): a loading screen instead. */
	void setLoading(bool loading) { _loading = loading; }
	/** The game's title picture (320 x 200 C64 colour indexes) for the loading screen. */
	void setTitle(const byte *pixels) { _title.resize(320 * 200); memcpy(_title.data(), pixels, 320 * 200); _drawn = false; }
	// For the cheats
	bool takeMove() { bool m = _move; _move = false; return m; } ///< a touch on the game (a move may follow)
	uint historySize() const { return _history.size(); }
	void truncateHistory(uint n) { while (_history.size() > n) _history.pop_back(); _lastPage.clear(); _pending.clear(); }
	bool textHas(const char *upper) const; ///< in the last lines read (capitals)
	bool waitingToContinue() const { return continueShown(); }
	/** Forgets the text read so far (the start-up's messages). */
	void clearText();
	/** Draws everything; false if nothing changed since last time. */
	bool draw(const byte *screen);
	const Graphics::ManagedSurface &output() const { return _out; }

	Action press(Common::Point p);
	void move(Common::Point p);
	Action release(Common::Point p);
	Action tick();
	void wheel(int lines);
	Common::Point toScreen(Common::Point c64) const;

	/** Where the game's pointer should be (C64 screen pixels) and whether its button is down. */
	bool pointer(Common::Point &c64, bool &button) const;

private:
	struct Region {
		Common::Rect box;  // on the phone
		Common::Rect src;  // on the C64 screen
	};
	struct Cell {
		Common::Rect box;
		int verb;          // index into the verb table
	};

	Machine *_machine;
	int _w, _h;
	bool _portrait;
	Look _look;
	Layout _layout;
	Graphics::PixelFormat _format;
	Graphics::ManagedSurface _out;
	uint32 _pal[16];
	Common::HashMap<Common::String, Graphics::Font *> _fonts;

	// The layout
	Common::Rect _buttons[3];  // SAVE, LOAD, MENU
	Common::Rect _titleBox, _picBox, _invBox, _exitBox, _textBox, _verbBox, _whole;
	Common::Array<Cell> _cells;
	Common::Array<Region> _regions;
	void layout();
	void placeVerbs(const Common::Rect &r);
	Common::Rect fit(const Common::Rect &into, const Common::Rect &src, bool center = true) const;

	// What the game shows
	const byte *_screen;
	Common::Array<Common::String> _history;  // the text read so far
	Common::Array<Common::String> _lastPage, _pending;
	int _pendingFrames;
	int _scroll;
	bool continueShown() const;
	bool verbSelected(int verb) const;
	void readText();
	char glyphAt(int x, int y, byte ink) const;

	// Touch
	bool _down, _button;
	Common::Point _target;     // the C64 pixel being pointed at
	int _pressButton;          // a SAVE / LOAD / MENU touch
	uint32 _pressTime;
	bool _holdFired;
	int _tapFrames;            // frames left to hold the button after a quick tap
	bool _scrolling;
	Common::Point _pressPoint;
	int _scrollStart;
	bool _mapped;              // the touch began on a piece of the C64 screen
	bool mapPoint(Common::Point p, Common::Point &c64) const;

	// Drawing
	uint32 rgb(byte r, byte g, byte b) const { return _format.RGBToColor(r, g, b); }
	const Graphics::Font *font(int size, bool bold, bool serif);
	void text(const Common::String &s, int x, int y, int w, uint32 col, const Graphics::Font *f, bool center);
	void roundRect(const Common::Rect &r, int radius, uint32 fill, uint32 edge);
	void blit(const Common::Rect &src, const Common::Rect &to);
	void blitInk(const Common::Rect &src, const Common::Rect &to, byte ink, uint32 col);
	void glyph(char c, float x, int y, float scale, uint32 col);
	void drawButtons();
	void drawHoldRing(const Common::Rect &r, int radius, float progress);
	void drawVerbs();
	void drawText();
	void drawPieces();
	bool _loading;
	Common::Array<byte> _title;
	bool _move;
	uint32 _lastSig;
	bool _drawn;
	uint32 signature() const;
};

} // End of namespace DarkLord

#endif
