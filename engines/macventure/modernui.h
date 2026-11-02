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

/*
 * Near-Grog: phone-friendly layouts for the MacVenture games.
 *
 * The engine keeps running its own window layout (the original one, on a
 * hidden 640x480 or 512x342 screen): the windows, their contents and the
 * window manager's event handling are unchanged. A modern layout draws its
 * own screen (a wide or a tall one, in true colour) from the windows'
 * contents, and turns touches on it into events on the hidden windows, or
 * into calls the original windows would make.
 */

#ifndef MACVENTURE_MODERNUI_H
#define MACVENTURE_MODERNUI_H

#include "common/array.h"
#include "common/events.h"
#include "common/hashmap.h"
#include "common/rect.h"
#include "common/str.h"
#include "graphics/managed_surface.h"
#include "graphics/pixelformat.h"

#include "macventure/windows.h"

namespace Graphics {
class Font;
}

namespace MacVenture {

class Gui;
class MacVentureEngine;
typedef uint32 ObjID;

enum ModernLayout {
	kLayoutC2 = 0,
	kLayoutE = 1,
	kLayoutOriginal = 2
};

enum ModernLook {
	kLookWin31 = 0,
	kLookDark = 1
};

class ModernUI {
public:
	ModernUI(Gui *gui, MacVentureEngine *engine);
	~ModernUI();

	// Settings, kept per game
	static ModernLayout configLayout();
	static ModernLook configLook();
	static int configOpacity(); // percent: 100, 75, 50 or 25
	static void setConfig(ModernLayout layout, ModernLook look, int opacity);
	static const char *layoutName(ModernLayout layout);
	static const char *lookName(ModernLook look);
	/** Is the device held upright? (the overlay's shape, or a config override) */
	static bool portraitScreen();
	/** The virtual screen of a modern layout */
	static void screenSize(bool portrait, int &w, int &h);

	/** Lays out for a screen of this size and format. */
	void setup(int w, int h, bool portrait, ModernLayout layout, ModernLook look, int opacity, const Graphics::PixelFormat &format);
	bool isPortrait() const { return _portrait; }

	/** Draws the screen and shows it. */
	void render();
	/** Shows a picture (the title screen) scaled to fit, on black. */
	void showPicture(const Graphics::ManagedSurface &picture);
	/** A touch or a click: true if it was used here, else event.mouse is now on the hidden screen. */
	bool processEvent(Common::Event &event);

	// The text window
	void addText(const Common::String &text);
	void setText(const Common::String &text);
	uint rowCount();
	uint visibleRows();
	void scrollToRow(uint row);

	/** An object picked on the shelf or SELF, for Gui::select(). */
	bool takeForcedPick(ObjID &obj, WindowReference &ref);
	/** The last one, for a double click. */
	bool lastForcedPick(ObjID &obj, WindowReference &ref);

	/** An engine dialog (drawn on the hidden screen at bounds), shown over the layout; nullptr when none. */
	void setDialog(const Common::Rect *bounds);

	/** For the test harness: the shown screen. */
	const Graphics::ManagedSurface &output() const { return _out; }

private:
	enum Region {
		kRegionNone,
		kRegionRoom,
		kRegionSelf,
		kRegionShelf,
		kRegionExits,
		kRegionVerb,
		kRegionContinue,
		kRegionButton,  // quick save, quick load, menu
		kRegionMaximize,
		kRegionTextTitle,
		kRegionText,
		kRegionScrollUp,
		kRegionScrollDown,
		kRegionScrollThumb,
		kRegionScrollTrack,
		kRegionFloating, // another window of the game (a bag, the diploma)
		kRegionDialog
	};

	struct Hit {
		Region region;
		int index;
		Hit() : region(kRegionNone), index(0) {}
		Hit(Region r, int i = 0) : region(r), index(i) {}
	};

	struct Slot {
		ObjID obj;
		Common::Rect rect;
	};

	// Layout
	void doLayout();
	Common::Rect textBox() const;
	Common::Rect textArea() const;
	Common::Rect verbRect(int i) const;
	Common::Rect verbsBounds() const;
	bool verbsShown() const;
	bool shelfShown() const;
	Hit hitTest(Common::Point p);

	// Mapping to the hidden screen
	Common::Point roomToHidden(Common::Point p) const;
	Common::Point windowCenter(WindowReference ref) const;
	Common::Point floatingToHidden(Common::Point p) const;
	Common::Rect hiddenToScreen(const Common::Rect &r) const;
	Common::Point dropPoint(Common::Point p);
	WindowReference playerInventory() const;

	// Drawing
	uint32 rgb(byte r, byte g, byte b) const { return _format.RGBToColor(r, g, b); }
	void buildLookup();
	void blitScaled(const Graphics::ManagedSurface &src, const Common::Rect &srcRect, const Common::Rect &dst, int key = -1);
	void fillRoundRect(const Common::Rect &r, int radius, uint32 color);
	void roundRect(const Common::Rect &r, int radius, uint32 fill, uint32 outline, int width);
	void fillCircle(int cx, int cy, int r, uint32 color);
	void bevel(const Common::Rect &r, bool pressed, int width = 3);
	Common::Rect titled(const Common::Rect &box, const Common::String &title, int fontSize);
	void text(const Common::String &s, int x, int y, int w, uint32 color, const Graphics::Font *font, int align = 0);
	void textCentered(const Common::String &s, const Common::Rect &r, uint32 color, const Graphics::Font *font);
	const Graphics::Font *font(int size, bool bold, bool serif = false);
	void saveArea(const Common::Rect &r);
	void blendArea(const Common::Rect &r, int percent);

	void drawBackground();
	void drawRoom();
	void drawButtons();
	void drawHoldRing(const Common::Rect &r, bool round, float progress);
	void drawVerbs();
	void drawObjectImage(ObjID obj, const Common::Rect &dst, bool keepAspect);
	void drawSelf(const Common::Rect &r);
	void drawShelf();
	void drawExits(const Common::Rect &area);
	void drawTextWindow();
	void drawFloating();
	void drawDragged();
	Common::String roomTitle() const;

	// Text
	void rewrap();
	int lineStep() const;
	int maxScroll();
	void clampScroll();

	// Actions
	void pressVerb(int i);
	void continueText();
	void buttonFired(int i, bool hold);
	void openGameMenu();
	void updateHolds();
	void updateMomentum();

	Gui *_gui;
	MacVentureEngine *_engine;
	Graphics::ManagedSurface _out;
	Graphics::PixelFormat _format;
	uint32 _lookup[256];
	Graphics::ManagedSurface _saved;

	int _w, _h;
	bool _portrait;
	ModernLayout _layout;
	ModernLook _look;
	int _opacity;

	// Layout rectangles (screen pixels)
	Common::Rect _roomBox, _roomPic;
	Common::Rect _shelfBox, _shelfItems, _shelfExits;
	Common::Rect _verbArea;
	int _verbH, _verbGap, _verbFont;
	Common::Rect _textNormal, _textMax;
	int _textFont, _titleFont, _roomTitleFont;
	Common::Rect _buttons[3];
	bool _roundButtons;
	int _shelfRows;
	Common::Point _itemSize;
	Common::Array<Slot> _slots;
	Common::Array<Slot> _exitRects;
	int _shelfScroll;

	// Text window
	Common::Array<Common::String> _paras;
	Common::Array<Common::String> _lines;
	int _wrapWidth;
	const Graphics::Font *_wrapFont;
	int _scroll;
	bool _userScrolled;
	bool _maximized;
	float _velocity;
	uint32 _lastMomentum;

	// Touch state
	Hit _press;
	bool _pressInside;
	uint32 _pressTime;
	bool _holdFired;
	Common::Point _pressPoint, _lastPoint;
	int _pressScroll;
	uint32 _lastMoveTime;
	bool _dragging;
	bool _engineCapture;
	uint32 _lastTitleTap;
	uint32 _lastExitTap;
	int _lastExitIndex;
	Common::Point _pointer;

	ObjID _forcedObj, _lastForcedObj;
	WindowReference _forcedRef, _lastForcedRef;
	bool _forced;

	bool _dialogOpen;
	Common::Rect _dialogBounds;

	Common::HashMap<Common::String, Graphics::Font *> _fonts;
};

} // End of namespace MacVenture

#endif
