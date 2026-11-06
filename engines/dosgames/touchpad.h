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

#ifndef DOSGAMES_TOUCHPAD_H
#define DOSGAMES_TOUCHPAD_H

#include "common/array.h"
#include "common/hash-str.h"
#include "common/hashmap.h"
#include "common/rect.h"
#include "common/str.h"
#include "graphics/font.h"
#include "graphics/managed_surface.h"

namespace DosGames {

/**
 * Near-Grog's phone layout for The Oregon Trail, as agreed in the mock-ups:
 * the game's screen (4:3, unstretched) and beside it in landscape, or
 * under it in portrait, the keys the game asks for - Save, Load, Menu, the
 * digits, Yes and No, Delete, Keyboard, Enter and Continue (the space bar).
 * While hunting the keys give way to a hunting pad: Fire, Walk / Stop and
 * Done, with the rifle pointed by turning it (the game's arrow keys) or,
 * the tray's other choice, straight in one of eight directions (its keypad).
 */
class TouchPad {
public:
	enum Action { kNone, kSaveDialog, kLoadDialog, kQuickSave, kQuickLoad, kTray, kKeyboard };
	enum Aim { kAimTap, kAimStick, kAimTurn, kAimPoint };

	/** A key for the PC: down or up. */
	struct Key {
		uint8 scan, ascii;
		bool down;
		uint32 at; // (the frame it's due)
	};

	TouchPad();
	~TouchPad();

	static bool portraitScreen();
	static Aim configAim();
	static const char *aimName(Aim a);
	static void setConfigAim(Aim a);

	void setup(int w, int h, bool portrait, const Graphics::PixelFormat &format);
	void setHunting(bool hunting);
	/** After a change in the tray (the hunting aim). */
	void refresh();
	void setPalette(const byte *rgb);
	/** While hunting: where the hunter stands (the game's 640 x 400), if he's on the screen. */
	void setHunter(Common::Point p, bool seen);
	/** Draws what changed (the game's screen: 640 x 400 palette indexes); the part to show, empty if none. */
	Common::Rect draw(const byte *screen);
	const Graphics::ManagedSurface &output() const { return _out; }

	Action press(Common::Point p);
	void move(Common::Point p);
	Action release(Common::Point p);
	/** Each frame: a held Save / Load, a held turn (the keyboard's repeat). */
	Action tick();
	/** The keys the touches pressed and let go, oldest first. */
	bool takeKey(Key &k);
	/** A key typed some other way (a keyboard): what Continue gives next. */
	void noteKey(uint8 ascii);

	/** The game's screen on the phone, for tests. */
	const Common::Rect &gameBox() const { return _gameBox; }
	Common::Point buttonCentre(const char *label) const;

private:
	enum Style { kPlain, kBlue, kRed, kSmall };
	enum Glyph { kNoGlyph, kArrow };
	struct Button {
		Common::Rect box;
		Common::String label;
		Style style;
		int action;   // an Action, or -1 for a key
		uint8 scan, ascii;
		bool repeat;  // held: pressed again and again
		Glyph glyph;
		float angle;  // the arrow's way (radians, 0 = right, clockwise)
		bool round;
	};

	int _w, _h;
	bool _portrait;
	bool _hunting;
	Graphics::PixelFormat _format;
	Graphics::ManagedSurface _out;
	Common::HashMap<Common::String, Graphics::Font *> _fonts;
	uint32 _pal[256];
	Common::Rect _gameBox, _padBox;
	Common::Array<Button> _buttons;
	Common::Array<int> _mapX, _mapY;
	Common::Array<byte> _last;
	bool _gameDirty, _padDirty, _allDirty;

	int _pressed;        // the button under the finger, or -1
	uint32 _pressTime;
	bool _holdFired;
	bool _keyHeld;       // its key is down on the PC
	int _heldFrames;
	uint32 _frame, _downFrame;
	Common::Array<Key> _keys;

	void layout();
	void keypad(const Common::Rect &r);
	void huntPad(const Common::Rect &r);
	void addButton(const Common::Rect &box, const char *label, Style style, int action, uint8 scan = 0, uint8 ascii = 0);
	void addArrow(const Common::Rect &box, float angle, uint8 scan, bool repeat);
	int buttonAt(Common::Point p) const;
	void keyDown(const Button &b);
	void keyUp(const Button &b);
	void releaseKey();
	void tapKey(uint8 scan, uint8 ascii);
	void pushKey(uint8 scan, uint8 ascii, bool down);
	uint32 _lastEmit;
	int _typed;       // keys typed since the last Enter or space
	uint8 _heldScan;

	// Tap and Stick: the hunter steered (8 ways), walking while wanted
	Common::Point _hunter;
	bool _hunterSeen;
	uint32 _movedFrame, _walkWait;
	bool _wantWalk;
	int _wantDir, _sentDir;  // 0 = right, then clockwise (-1: none)
	void steer();
	int dirTo(Common::Point phone) const;
	Common::Point toPhone(Common::Point game) const;
	// the stick
	Common::Point _stickCentre;
	int _stickR;
	bool _stickDown, _stickMoved;
	Common::Point _knob;
	uint32 _stickTime;
	void stickTo(Common::Point p);
	// the field (Tap)
	bool _fieldDown, _fieldWalk;
	Common::Point _fieldPoint;
	uint32 _fieldTime;
	void fire(int dir);
	void drawStick();

	uint32 rgb(byte r, byte g, byte b) const { return _format.RGBToColor(r, g, b); }
	const Graphics::Font *font(int size);
	void roundRect(const Common::Rect &r, int radius, uint32 fill, uint32 edge);
	void polygon(const float *vx, const float *vy, int n, uint32 col);
	void arrow(float cx, float cy, float size, float angle, uint32 col);
	void drawHoldRing(const Common::Rect &r, int radius, float progress);
	void drawPad();
	void drawGame(const byte *screen);
};

} // End of namespace DosGames

#endif
