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

// Near-Grog: phone-friendly layouts, see modernui.h

#include "common/config-manager.h"
#include "common/system.h"
#include "common/translation.h"
#include "graphics/font.h"
#include "graphics/fonts/ttf.h"
#include "graphics/macgui/macfontmanager.h"
#include "gui/chooser.h"

#include "macventure/gui.h"
#include "macventure/macventure.h"
#include "macventure/modernui.h"

namespace MacVenture {

namespace {

// The verbs, row by row; SELF is the Thyself picture
const int kSelfCell = 6;
const struct {
	const char *label;
	ControlAction action;
} kVerbs[9] = {
	{ "Examine", kExamine }, { "Open", kOpen }, { "Hit", kHit },
	{ "Close", kClose }, { "Speak", kSpeak }, { "Consume", kConsume },
	{ "SELF", kNoCommand }, { "Go", kGo }, { "Operate", kOperate }
};

const char *const kButtonLabels[3] = { "QUICK\nSAVE", "QUICK\nLOAD", "MENU" };
const char *const kTitleButtonLabels[3] = { "SAVE", "LOAD", "MENU" };

const uint32 kSaveHold = 300;
const uint32 kMenuHold = 450;
const int kQuickSlot = 99;
const int kDragThreshold = 8;

// Colours
const byte kTeal[3] = { 0, 128, 128 };
const byte kNavy[3] = { 0, 0, 128 };
const byte kGray[3] = { 192, 192, 192 };
const byte kDarkGray[3] = { 128, 128, 128 };
const byte kDarkBg[3] = { 12, 12, 16 };
const byte kPanel[3] = { 34, 34, 40 };
const byte kEdge[3] = { 150, 150, 160 };
const byte kWarm[3] = { 240, 232, 210 };
const byte kBlue[3] = { 60, 120, 200 };

} // End of anonymous namespace

ModernUI::ModernUI(Gui *gui, MacVentureEngine *engine) :
	_gui(gui), _engine(engine), _w(0), _h(0), _portrait(false), _layout(kLayoutC2), _look(kLookWin31), _opacity(100),
	_verbH(0), _verbGap(0), _verbFont(18), _textFont(17), _titleFont(16), _roomTitleFont(22), _roundButtons(true),
	_shelfRows(1), _shelfScroll(0), _wrapWidth(0), _wrapFont(nullptr), _scroll(0), _userScrolled(false), _maximized(false),
	_velocity(0), _lastMomentum(0), _pressInside(false), _pressTime(0), _holdFired(false), _pressScroll(0), _lastMoveTime(0),
	_dragging(false), _engineCapture(false), _lastTitleTap(0), _lastExitTap(0), _lastExitIndex(-1),
	_forcedObj(0), _lastForcedObj(0), _forcedRef(kNoWindow), _lastForcedRef(kNoWindow), _forced(false), _dialogOpen(false) {
	memset(_lookup, 0, sizeof(_lookup));
}

ModernUI::~ModernUI() {
	for (Common::HashMap<Common::String, Graphics::Font *>::iterator it = _fonts.begin(); it != _fonts.end(); ++it)
		delete it->_value;
}

// Settings

ModernLayout ModernUI::configLayout() {
	Common::String s = ConfMan.get("neargrog_layout");
	if (s == "e")
		return kLayoutE;
	if (s == "original")
		return kLayoutOriginal;
	return kLayoutC2;
}

ModernLook ModernUI::configLook() {
	return ConfMan.get("neargrog_look") == "dark" ? kLookDark : kLookWin31;
}

int ModernUI::configOpacity() {
	int v = ConfMan.hasKey("neargrog_button_opacity") ? ConfMan.getInt("neargrog_button_opacity") : 100;
	return (v == 75 || v == 50 || v == 25) ? v : 100;
}

void ModernUI::setConfig(ModernLayout layout, ModernLook look, int opacity) {
	static const char *const kLayouts[] = { "c2", "e", "original" };
	ConfMan.set("neargrog_layout", kLayouts[layout]);
	ConfMan.set("neargrog_look", look == kLookDark ? "dark" : "win31");
	ConfMan.setInt("neargrog_button_opacity", opacity);
	ConfMan.flushToDisk();
}

const char *ModernUI::layoutName(ModernLayout layout) {
	switch (layout) {
	case kLayoutE:
		return "E";
	case kLayoutOriginal:
		return "Original";
	default:
		return "C2";
	}
}

const char *ModernUI::lookName(ModernLook look) {
	return look == kLookDark ? "Dark" : "Windows 3.1";
}

bool ModernUI::portraitScreen() {
	// "neargrog_orientation" (portrait / landscape) overrides, for testing
	Common::String o = ConfMan.get("neargrog_orientation");
	if (o == "portrait")
		return true;
	if (o == "landscape")
		return false;
	return g_system->getOverlayHeight() > g_system->getOverlayWidth();
}

void ModernUI::screenSize(bool portrait, int &w, int &h) {
	// An iPhone's shape (about 2.17:1); other screens get borders
	w = portrait ? 646 : 1400;
	h = portrait ? 1400 : 646;
}

// Layout

void ModernUI::setup(int w, int h, bool portrait, ModernLayout layout, ModernLook look, int opacity, const Graphics::PixelFormat &format) {
	_w = w;
	_h = h;
	_portrait = portrait;
	_layout = layout;
	_look = look;
	_opacity = opacity;
	_format = format;
	_out.create(w, h, format);
	_wrapWidth = 0;
	_shelfScroll = 0;
	_press = Hit();
	doLayout();
	rewrap();
	if (!_userScrolled)
		_scroll = maxScroll();
	clampScroll();
}

void ModernUI::doLayout() {
	const bool dark = _look == kLookDark;
	if (_portrait) {
		// Top to bottom: the picture (SAVE / LOAD / MENU on its title bar), the
		// text, the verbs, the inventory with the exits
		const int L = 24, R = _w - 24;
		int y = 70;
		const int rw = R - L;
		const int rh = (rw - 10) * 10 / 16;
		_roomBox = Common::Rect(L, y, R, y + rh + 42);
		_roomPic = dark ? _roomBox : Common::Rect(L + 5, y + 37, R - 5, y + rh + 42 - 5);
		_roomTitleFont = dark ? 24 : 22;
		_roundButtons = false;
		int bx = R - 8;
		const Graphics::Font *f = font(14, true);
		for (int i = 2; i >= 0; i--) {
			int tw = (f ? f->getStringWidth(kTitleButtonLabels[i]) : 40) + (dark ? 22 : 20);
			_buttons[i] = dark ? Common::Rect(bx - tw, y + 8, bx, y + 36) : Common::Rect(bx - tw, y + 6, bx, y + 32);
			bx -= tw + 6;
		}
		y += rh + 50;
		const int th = 430;
		_textNormal = Common::Rect(L, y, R, y + th);
		_textFont = dark ? 19 : 18;
		_titleFont = 16;
		y += th + 12;
		_verbH = 66;
		_verbGap = 10;
		_verbFont = 21;
		_verbArea = Common::Rect(L, y, R, y + 3 * _verbH + 2 * _verbGap);
		y = _verbArea.bottom + 12;
		_shelfBox = Common::Rect(L, y, R, _h - 40);
		_textMax = Common::Rect(L, _textNormal.top, R, _h - 40);
		_itemSize = Common::Point(34, 60);
	} else if (_layout == kLayoutE) {
		// The picture full height on the left; buttons, verbs, text and
		// inventory in a column on the right
		const int right = _w - 40;
		const int rh = _h - 14 - 12 - 42;
		const int rw = rh * 16 / 10;
		_roomBox = Common::Rect(40, 14, 40 + rw + 10, 14 + rh + 42);
		_roomPic = dark ? _roomBox : Common::Rect(_roomBox.left + 5, _roomBox.top + 37, _roomBox.right - 5, _roomBox.bottom - 5);
		_roomTitleFont = dark ? 26 : 22;
		const int vx = 40 + rw + 30;
		_roundButtons = true;
		for (int i = 0; i < 3; i++) {
			int cx = right - 34 - (2 - i) * 92;
			_buttons[i] = Common::Rect(cx - 34, 44 - 34, cx + 34, 44 + 34);
		}
		_verbH = 46;
		_verbGap = 7;
		_verbFont = 15;
		_verbArea = Common::Rect(vx, 88, right, 88 + 3 * _verbH + 2 * _verbGap);
		const int y = _verbArea.bottom;
		_textNormal = Common::Rect(vx, y + 10, right, y + 10 + 220);
		_textMax = Common::Rect(vx, 88, right, y + 10 + 220);
		_textFont = 15;
		_titleFont = 13;
		_shelfBox = Common::Rect(vx, y + 238, right, _h - 12);
		_itemSize = Common::Point(26, 50);
	} else {
		// C2: the picture with the inventory shelf under it on the left;
		// buttons, verbs and the text on the right
		const int right = _w - 40;
		const int rh = 400;
		const int rw = rh * 16 / 10;
		_roomBox = Common::Rect(40, 14, 40 + rw + 10, 14 + rh + 42);
		_roomPic = dark ? _roomBox : Common::Rect(_roomBox.left + 5, _roomBox.top + 37, _roomBox.right - 5, _roomBox.bottom - 5);
		_roomTitleFont = dark ? 26 : 22;
		_shelfBox = Common::Rect(40, 14 + rh + 50, 40 + rw + 10, _h - 12);
		_roundButtons = true;
		for (int i = 0; i < 3; i++) {
			int cx = right - 34 - (2 - i) * 100;
			_buttons[i] = Common::Rect(cx - 34, 50 - 34, cx + 34, 50 + 34);
		}
		const int vx = 40 + rw + 40;
		_verbH = 60;
		_verbGap = 10;
		_verbFont = 18;
		_verbArea = Common::Rect(vx, 100, right, 100 + 3 * _verbH + 2 * _verbGap);
		_textNormal = Common::Rect(vx, _verbArea.bottom + 12, right, _h - 12);
		_textMax = Common::Rect(vx, 96, right, _h - 12);
		_textFont = 17;
		_titleFont = 16;
		_itemSize = Common::Point(34, 62);
	}

	// The shelf: the items on the left, the exits at the right end
	Common::Rect inner = dark ? Common::Rect(_shelfBox.left + 4, _shelfBox.top + 30, _shelfBox.right - 4, _shelfBox.bottom - 4)
		: Common::Rect(_shelfBox.left + 5, _shelfBox.top + 37, _shelfBox.right - 5, _shelfBox.bottom - 5);
	const int exitsW = _portrait ? 128 : (_layout == kLayoutE ? 112 : 128);
	_shelfItems = Common::Rect(inner.left, inner.top, inner.right - exitsW, inner.bottom);
	_shelfExits = Common::Rect(inner.right - exitsW + 8, inner.top, inner.right, inner.bottom);
	const int gy = 10;
	_shelfRows = _portrait ? CLIP<int>((_shelfItems.height() - 10) / (_itemSize.y + gy), 1, 2) : 1;
}

Common::Rect ModernUI::textBox() const {
	return _maximized ? _textMax : _textNormal;
}

Common::Rect ModernUI::textArea() const {
	Common::Rect t = textBox();
	if (_look == kLookDark)
		return Common::Rect(t.left + 14, t.top + 12, t.right - 36, t.bottom - 10);
	// title bar, then the text left of the scroll bar
	return Common::Rect(t.left + 5 + 12, t.top + 37 + 8, t.right - 5 - 24 - 8, t.bottom - 5 - 4);
}

Common::Rect ModernUI::verbRect(int i) const {
	const int vw = (_verbArea.width() - 2 * _verbGap) / 3;
	const int c = i % 3, r = i / 3;
	const int x = _verbArea.left + c * (vw + _verbGap), y = _verbArea.top + r * (_verbH + _verbGap);
	return Common::Rect(x, y, x + vw, y + _verbH);
}

Common::Rect ModernUI::verbsBounds() const {
	return _verbArea;
}

bool ModernUI::verbsShown() const {
	return !_maximized;
}

bool ModernUI::shelfShown() const {
	return !(_maximized && _portrait);
}

// Mapping touches to the hidden screen

Common::Point ModernUI::roomToHidden(Common::Point p) const {
	Common::Rect inner = _gui->_mainGameWindow->getInnerDimensions();
	int x = inner.left + (p.x - _roomPic.left) * inner.width() / MAX<int>(1, _roomPic.width());
	int y = inner.top + (p.y - _roomPic.top) * inner.height() / MAX<int>(1, _roomPic.height());
	return Common::Point(x, y);
}

Common::Point ModernUI::windowCenter(WindowReference ref) const {
	Graphics::MacWindow *win = _gui->findWindow(ref);
	if (!win)
		return Common::Point(-1000, -1000);
	Common::Rect inner = win->getInnerDimensions();
	return Common::Point((inner.left + inner.right) / 2, (inner.top + inner.bottom) / 2);
}

// Other windows (bags, the diploma) and dialogs: the hidden screen scaled to fit
Common::Rect ModernUI::hiddenToScreen(const Common::Rect &r) const {
	const int sw = _gui->_screen.w, sh = _gui->_screen.h;
	const int k1000 = MIN(_w * 1000 / sw, _h * 1000 / sh);
	const int ox = (_w - sw * k1000 / 1000) / 2, oy = (_h - sh * k1000 / 1000) / 2;
	return Common::Rect(ox + r.left * k1000 / 1000, oy + r.top * k1000 / 1000, ox + r.right * k1000 / 1000, oy + r.bottom * k1000 / 1000);
}

Common::Point ModernUI::floatingToHidden(Common::Point p) const {
	const int sw = _gui->_screen.w, sh = _gui->_screen.h;
	const int k1000 = MIN(_w * 1000 / sw, _h * 1000 / sh);
	const int ox = (_w - sw * k1000 / 1000) / 2, oy = (_h - sh * k1000 / 1000) / 2;
	return Common::Point((p.x - ox) * 1000 / k1000, (p.y - oy) * 1000 / k1000);
}

WindowReference ModernUI::playerInventory() const {
	for (uint i = 0; i < _gui->_inventoryWindows.size(); i++) {
		WindowReference ref = _gui->_inventoryWindows[i].ref;
		if (_gui->findWindowData(ref).objRef == 1)
			return ref;
	}
	return kNoWindow;
}

// Where a dragged object would land, on the hidden screen
Common::Point ModernUI::dropPoint(Common::Point p) {
	Hit h = hitTest(p);
	switch (h.region) {
	case kRegionRoom:
		return roomToHidden(p);
	case kRegionSelf:
		return windowCenter(kSelfWindow);
	case kRegionShelf:
		return windowCenter(playerInventory());
	case kRegionFloating:
		return floatingToHidden(p);
	default:
		return Common::Point(-1000, -1000);
	}
}

ModernUI::Hit ModernUI::hitTest(Common::Point p) {
	if (_dialogOpen)
		return Hit(kRegionDialog);

	// Other windows of the game lie on top
	for (int i = (int)_gui->_inventoryWindows.size() - 1; i >= 0; i--) {
		WindowReference ref = _gui->_inventoryWindows[i].ref;
		if (ref == playerInventory())
			continue;
		if (hiddenToScreen(_gui->_inventoryWindows[i].win->getDimensions()).contains(p))
			return Hit(kRegionFloating, i);
	}
	if (_gui->_diplomaWindow && _engine->getGameState() == kGameStateWinning)
		return Hit(kRegionFloating, -1);

	for (int i = 0; i < 3; i++) {
		Common::Rect b = _buttons[i];
		if (_roundButtons)
			b.grow(6);
		if (b.contains(p))
			return Hit(kRegionButton, i);
	}

	Common::Rect t = textBox();
	if (t.contains(p)) {
		if (_look == kLookDark) {
			if (Common::Rect(t.right - 40, t.top, t.right, t.top + 40).contains(p))
				return Hit(kRegionMaximize);
			return Hit(kRegionText);
		}
		if (p.y < t.top + 37) {
			if (p.x >= t.right - 40)
				return Hit(kRegionMaximize);
			return Hit(kRegionTextTitle);
		}
		const int sb0 = t.right - 5 - 24;
		if (p.x >= sb0) {
			Common::Rect inner(sb0, t.top + 37, t.right - 5, t.bottom - 5);
			if (p.y < inner.top + 24)
				return Hit(kRegionScrollUp);
			if (p.y >= inner.bottom - 24)
				return Hit(kRegionScrollDown);
			const int span = inner.height() - 48 - 30;
			const int ms = maxScroll();
			const int ty = inner.top + 24 + (ms ? span * _scroll / ms : 0);
			if (p.y >= ty && p.y < ty + 30)
				return Hit(kRegionScrollThumb);
			return Hit(kRegionScrollTrack, p.y < ty ? -1 : 1);
		}
		return Hit(kRegionText);
	}

	if (verbsShown() && _verbArea.contains(p)) {
		if (_engine->needsClickToContinue())
			return Hit(kRegionContinue);
		for (int i = 0; i < 9; i++) {
			if (verbRect(i).contains(p))
				return Hit(i == kSelfCell ? kRegionSelf : kRegionVerb, i);
		}
		return Hit();
	}

	if (_roomPic.contains(p))
		return Hit(kRegionRoom);
	if (_roomBox.contains(p))
		return Hit();

	if (shelfShown() && _shelfBox.contains(p)) {
		if (_shelfExits.contains(p))
			return Hit(kRegionExits, -1);
		for (uint i = 0; i < _slots.size(); i++) {
			if (_slots[i].rect.contains(p))
				return Hit(kRegionShelf, i);
		}
		return Hit(kRegionShelf, -1);
	}
	return Hit();
}

bool ModernUI::takeForcedPick(ObjID &obj, WindowReference &ref) {
	if (!_forced)
		return false;
	_forced = false;
	obj = _forcedObj;
	ref = _forcedRef;
	return true;
}

bool ModernUI::lastForcedPick(ObjID &obj, WindowReference &ref) {
	if (!_lastForcedObj)
		return false;
	obj = _lastForcedObj;
	ref = _lastForcedRef;
	return true;
}

void ModernUI::setDialog(const Common::Rect *bounds) {
	// The engine's dialogs ask for typed text (what to say): the phone's keyboard
	if ((bounds != nullptr) != _dialogOpen && g_system->hasFeature(OSystem::kFeatureVirtualKeyboard))
		g_system->setFeatureState(OSystem::kFeatureVirtualKeyboard, bounds != nullptr);
	_dialogOpen = bounds != nullptr;
	if (bounds)
		_dialogBounds = *bounds;
}

// Events

bool ModernUI::processEvent(Common::Event &event) {
	const uint32 now = g_system->getMillis();
	switch (event.type) {
	case Common::EVENT_LBUTTONDOWN: {
		Common::Point p = event.mouse;
		_pointer = p;
		_press = hitTest(p);
		_pressInside = true;
		_pressTime = now;
		_holdFired = false;
		_pressPoint = _lastPoint = p;
		_lastMoveTime = now;
		_pressScroll = _scroll;
		_dragging = false;
		_velocity = 0;
		_engineCapture = false;
		_forced = false;
		_lastForcedObj = 0;
		switch (_press.region) {
		case kRegionDialog:
			event.mouse = floatingToHidden(p);
			_engineCapture = true;
			return false;
		case kRegionFloating:
			event.mouse = floatingToHidden(p);
			_engineCapture = true;
			return false;
		case kRegionRoom:
			event.mouse = roomToHidden(p);
			_engineCapture = true;
			return false;
		case kRegionSelf: {
			const WindowData &data = _gui->findWindowData(kSelfWindow);
			if (data.children.empty() || _engine->needsClickToContinue())
				return true;
			_forced = true;
			_forcedObj = _lastForcedObj = data.children[0].obj;
			_forcedRef = _lastForcedRef = kSelfWindow;
			event.mouse = windowCenter(kSelfWindow);
			_engineCapture = true;
			return false;
		}
		case kRegionShelf:
			if (_press.index >= 0 && !_engine->needsClickToContinue()) {
				WindowReference inv = playerInventory();
				_forced = true;
				_forcedObj = _lastForcedObj = _slots[_press.index].obj;
				_forcedRef = _lastForcedRef = inv;
				event.mouse = windowCenter(inv);
				_engineCapture = true;
				return false;
			}
			return true;
		default:
			_gui->markRedraw();
			return true;
		}
	}

	case Common::EVENT_MOUSEMOVE: {
		Common::Point p = event.mouse;
		_pointer = p;
		if (_engineCapture) {
			if (!_gui->_draggedObjects.empty())
				event.mouse = dropPoint(p);
			else if (_press.region == kRegionRoom)
				event.mouse = roomToHidden(p);
			else if (_press.region == kRegionFloating || _press.region == kRegionDialog)
				event.mouse = floatingToHidden(p);
			else
				event.mouse = _press.region == kRegionSelf ? windowCenter(kSelfWindow) : windowCenter(playerInventory());
			return false;
		}
		if (_press.region == kRegionNone)
			return true;
		if (ABS(p.x - _pressPoint.x) > kDragThreshold || ABS(p.y - _pressPoint.y) > kDragThreshold)
			_dragging = true;
		if (_press.region == kRegionText && _dragging) {
			const uint32 dt = MAX<uint32>(1, now - _lastMoveTime);
			_velocity = 0.7f * _velocity + 0.3f * (float)(_lastPoint.y - p.y) / dt;
			_scroll = _pressScroll - (p.y - _pressPoint.y);
			_userScrolled = true;
			clampScroll();
		} else if (_press.region == kRegionScrollThumb) {
			Common::Rect t = textBox();
			const int span = MAX(1, (t.bottom - 5) - (t.top + 37) - 48 - 30);
			_scroll = _pressScroll + (p.y - _pressPoint.y) * maxScroll() / span;
			_userScrolled = true;
			clampScroll();
		} else if (_press.region == kRegionShelf && _press.index < 0 && _dragging) {
			_shelfScroll = MAX(0, _shelfScroll - (p.x - _lastPoint.x));
		} else {
			Hit h = hitTest(p);
			_pressInside = h.region == _press.region && h.index == _press.index;
		}
		_lastPoint = p;
		_lastMoveTime = now;
		return true;
	}

	case Common::EVENT_LBUTTONUP: {
		Common::Point p = event.mouse;
		_pointer = p;
		Hit press = _press;
		_press = Hit();
		_gui->markRedraw();
		if (_engineCapture) {
			_engineCapture = false;
			if (!_gui->_draggedObjects.empty())
				event.mouse = dropPoint(p);
			else if (press.region == kRegionRoom)
				event.mouse = roomToHidden(p);
			else if (press.region == kRegionFloating || press.region == kRegionDialog)
				event.mouse = floatingToHidden(p);
			else
				event.mouse = press.region == kRegionSelf ? windowCenter(kSelfWindow) : windowCenter(playerInventory());
			return false;
		}
		Hit h = hitTest(p);
		const bool inside = h.region == press.region && h.index == press.index;
		switch (press.region) {
		case kRegionVerb:
			if (inside)
				pressVerb(press.index);
			break;
		case kRegionContinue:
			if (inside)
				continueText();
			break;
		case kRegionButton:
			if (inside && !_holdFired && press.index == 2 && now - _pressTime < kMenuHold)
				buttonFired(2, false);
			break;
		case kRegionMaximize:
			if (inside) {
				_maximized = !_maximized;
				rewrap();
				clampScroll();
			}
			break;
		case kRegionTextTitle:
			if (now - _lastTitleTap < 400) {
				_maximized = !_maximized;
				rewrap();
				clampScroll();
				_lastTitleTap = 0;
			} else {
				_lastTitleTap = now;
			}
			break;
		case kRegionText:
			if (!_dragging) {
				if (_engine->needsClickToContinue())
					continueText();
			} else {
				_lastMomentum = now;
			}
			break;
		case kRegionScrollUp:
			_scroll -= lineStep();
			_userScrolled = true;
			clampScroll();
			break;
		case kRegionScrollDown:
			_scroll += lineStep();
			clampScroll();
			break;
		case kRegionScrollTrack:
			_scroll += press.index * (textArea().height() - lineStep());
			_userScrolled = true;
			clampScroll();
			break;
		case kRegionExits:
			if (!_dragging) {
				for (uint i = 0; i < _exitRects.size(); i++) {
					if (_exitRects[i].rect.contains(p) && !_engine->needsClickToContinue()) {
						const bool dbl = (int)i == _lastExitIndex && now - _lastExitTap < 400;
						_engine->handleObjectSelect(_exitRects[i].obj, kExitsWindow, false, dbl);
						_lastExitTap = now;
						_lastExitIndex = i;
						break;
					}
				}
			}
			break;
		default:
			break;
		}
		return true;
	}

	case Common::EVENT_WHEELUP:
	case Common::EVENT_WHEELDOWN:
		if (textBox().contains(event.mouse)) {
			_scroll += (event.type == Common::EVENT_WHEELUP ? -1 : 1) * lineStep() * 2;
			if (event.type == Common::EVENT_WHEELUP)
				_userScrolled = true;
			clampScroll();
		}
		return true;

	case Common::EVENT_RBUTTONDOWN:
	case Common::EVENT_RBUTTONUP:
		return true;

	default:
		return false;
	}
}

void ModernUI::pressVerb(int i) {
	_gui->bringToFront(kOutConsoleWindow);
	if (_engine->needsClickToContinue()) {
		continueText();
		return;
	}
	ControlAction action = kVerbs[i].action;
	Common::Array<CommandButton>::iterator it = _gui->_controlData->begin();
	for (; it != _gui->_controlData->end(); ++it) {
		if (it->getData().refcon == action)
			it->select();
		else
			it->unselect();
	}
	_engine->selectControl(action);
	_engine->refreshReady();
	_engine->preparedToRun();
}

void ModernUI::continueText() {
	if (!_engine->needsClickToContinue())
		return;
	_engine->selectControl(kClickToContinue);
	_gui->markRedraw();
}

void ModernUI::buttonFired(int i, bool hold) {
	switch (i) {
	case 0: {
		Common::Error err = _engine->saveGameState(kQuickSlot, "Quick save");
		g_system->displayMessageOnOSD(Common::U32String(err.getCode() == Common::kNoError ? "Saved" : "Could not save"));
		break;
	}
	case 1: {
		Common::String name = _engine->getSaveStateName(kQuickSlot);
		if (!g_system->getSavefileManager()->exists(name)) {
			g_system->displayMessageOnOSD(Common::U32String("No quick save yet"));
			break;
		}
		Common::Error err = _engine->loadGameState(kQuickSlot);
		g_system->displayMessageOnOSD(Common::U32String(err.getCode() == Common::kNoError ? "Loaded" : "Could not load"));
		break;
	}
	case 2:
		if (hold)
			_engine->openMainMenuDialog();
		else
			openGameMenu();
		break;
	default:
		break;
	}
	_gui->markRedraw();
}

// The game's own menus, as a list
void ModernUI::openGameMenu() {
	static const struct {
		const char *label;
		MenuAction action;
	} kItems[] = {
		{ "New game", kMenuActionNew },
		{ "Open a saved game", kMenuActionOpen },
		{ "Save", kMenuActionSave },
		{ "Clean up (tidy the inventory)", kMenuActionCleanUp },
		{ "Mess up", kMenuActionMessUp },
		{ "Quit", kMenuActionQuit }
	};
	Common::U32StringArray names;
	for (int i = 0; i < ARRAYSIZE(kItems); i++)
		names.push_back(Common::U32String(kItems[i].label));
	int picked;
	{
		PauseToken pause = _engine->pauseEngine();
		GUI::ChooserDialog chooser(Common::U32String("Menu"));
		chooser.setList(names);
		picked = chooser.runModal();
	}
	if (picked < 0 || picked >= ARRAYSIZE(kItems))
		return;
	if (kItems[picked].action == kMenuActionCleanUp || kItems[picked].action == kMenuActionMessUp) {
		WindowReference inv = playerInventory();
		if (inv != kNoWindow)
			_gui->bringToFront(inv);
	}
	_gui->handleMenuAction(kItems[picked].action);
}

void ModernUI::updateHolds() {
	if (_press.region != kRegionButton || _holdFired || !_pressInside)
		return;
	const uint32 held = g_system->getMillis() - _pressTime;
	const int i = _press.index;
	if ((i < 2 && held >= kSaveHold) || (i == 2 && held >= kMenuHold)) {
		_holdFired = true;
		buttonFired(i, true);
	}
}

void ModernUI::updateMomentum() {
	const uint32 now = g_system->getMillis();
	if (_press.region != kRegionNone || ABS(_velocity) < 0.02f) {
		_lastMomentum = now;
		if (_press.region == kRegionNone)
			_velocity = 0;
		return;
	}
	const uint32 dt = MIN<uint32>(100, now - _lastMomentum);
	_lastMomentum = now;
	_scroll += (int)(_velocity * dt);
	for (uint32 i = 0; i < dt; i += 16)
		_velocity *= 0.95f;
	clampScroll();
	if (_scroll <= 0 || _scroll >= maxScroll())
		_velocity = 0;
}

// Text

void ModernUI::addText(const Common::String &text) {
	Common::String t = text;
	while (!t.empty() && t.lastChar() == '\n')
		t.deleteLastChar();
	// One paragraph a line of the game's text
	uint start = 0;
	for (uint i = 0; i <= t.size(); i++) {
		if (i == t.size() || t[i] == '\n') {
			_paras.push_back(Common::String(t.c_str() + start, i - start));
			if (_wrapFont && _wrapWidth > 0) {
				Common::Array<Common::String> lines;
				_wrapFont->wordWrapText(_paras.back(), _wrapWidth, lines);
				if (lines.empty())
					lines.push_back("");
				for (uint l = 0; l < lines.size(); l++)
					_lines.push_back(lines[l]);
			}
			start = i + 1;
		}
	}
	if (_paras.size() > 2000) {
		_paras = Common::Array<Common::String>(&_paras[500], _paras.size() - 500);
		_wrapWidth = 0;
	}
	if (!_wrapFont || _wrapWidth <= 0)
		rewrap();
	// The newest text, unless the player has scrolled up to read
	if (!_userScrolled)
		_scroll = maxScroll();
	_gui->markRedraw();
}

void ModernUI::setText(const Common::String &text) {
	_paras.clear();
	_lines.clear();
	_userScrolled = false;
	addText(text);
}

void ModernUI::rewrap() {
	if (!_w)
		return;
	const Graphics::Font *f = font(_textFont, false, _look == kLookDark);
	const int width = textArea().width();
	_wrapFont = f;
	_wrapWidth = width;
	_lines.clear();
	if (!f)
		return;
	for (uint i = 0; i < _paras.size(); i++) {
		Common::Array<Common::String> lines;
		f->wordWrapText(_paras[i], width, lines);
		if (lines.empty())
			lines.push_back("");
		for (uint l = 0; l < lines.size(); l++)
			_lines.push_back(lines[l]);
	}
}

int ModernUI::lineStep() const {
	return _textFont * 135 / 100;
}

int ModernUI::maxScroll() {
	// Whole lines only, so the last one is never cut off
	const int visible = MAX(1, textArea().height() / lineStep());
	return MAX<int>(0, ((int)_lines.size() - visible) * lineStep());
}

void ModernUI::clampScroll() {
	const int m = maxScroll();
	_scroll = CLIP(_scroll, 0, m);
	if (_scroll >= m)
		_userScrolled = false;
}

uint ModernUI::rowCount() {
	if (!_wrapFont || _wrapWidth != textArea().width())
		rewrap();
	return _lines.size();
}

uint ModernUI::visibleRows() {
	// The normal (not maximized) text window
	bool m = _maximized;
	_maximized = false;
	int rows = MAX(1, textArea().height() / lineStep());
	_maximized = m;
	return rows;
}

void ModernUI::scrollToRow(uint row) {
	_scroll = row * lineStep();
	clampScroll();
	_gui->markRedraw();
}

// Drawing helpers

const Graphics::Font *ModernUI::font(int size, bool bold, bool serif) {
	Common::String name = Common::String::format("Liberation%s-%s.ttf", serif ? "Serif" : "Sans", bold ? "Bold" : "Regular");
	Common::String key = Common::String::format("%s/%d", name.c_str(), size);
	if (_fonts.contains(key))
		return _fonts[key];
	Graphics::Font *f = nullptr;
#ifdef USE_FREETYPE2
	f = Graphics::loadTTFFontFromArchive(name, size, Graphics::kTTFSizeModeCharacter, 0, 0, Graphics::kTTFRenderModeLight);
#endif
	_fonts[key] = f;
	if (!f)
		return _gui->_wm->_fontMan->getFont(Graphics::MacFont(Graphics::kMacFontSystem, 12));
	return f;
}

void ModernUI::buildLookup() {
	const byte *pal = _gui->_wm->getPalette();
	const uint size = _gui->_wm->getPaletteSize();
	for (uint i = 0; i < 256; i++) {
		if (i < size)
			_lookup[i] = rgb(pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2]);
		else
			_lookup[i] = rgb(0, 0, 0);
	}
}

void ModernUI::blitScaled(const Graphics::ManagedSurface &src, const Common::Rect &srcRect, const Common::Rect &dst, int key) {
	if (srcRect.isEmpty() || dst.isEmpty())
		return;
	Common::Rect clip = dst.findIntersectingRect(Common::Rect(_w, _h));
	for (int y = clip.top; y < clip.bottom; y++) {
		const int sy = srcRect.top + (y - dst.top) * srcRect.height() / dst.height();
		if (sy < 0 || sy >= src.h)
			continue;
		const byte *row = (const byte *)src.getBasePtr(0, sy);
		for (int x = clip.left; x < clip.right; x++) {
			const int sx = srcRect.left + (x - dst.left) * srcRect.width() / dst.width();
			if (sx < 0 || sx >= src.w)
				continue;
			const byte c = row[sx];
			if ((int)c == key)
				continue;
			_out.setPixel(x, y, _lookup[c]);
		}
	}
}

void ModernUI::fillRoundRect(const Common::Rect &r, int radius, uint32 color) {
	radius = MIN<int>(radius, MIN(r.width(), r.height()) / 2);
	for (int y = r.top; y < r.bottom; y++) {
		int inset = 0;
		int dy = -1;
		if (y < r.top + radius)
			dy = r.top + radius - y;
		else if (y >= r.bottom - radius)
			dy = y - (r.bottom - radius - 1);
		if (dy > 0) {
			float d = (float)radius - sqrtf((float)(radius * radius - (dy - 0.5f) * (dy - 0.5f)));
			inset = (int)(d + 0.5f);
		}
		const int x0 = MAX<int>(0, r.left + inset), x1 = MIN<int>(_w, r.right - inset);
		if (y >= 0 && y < _h && x1 > x0)
			_out.hLine(x0, y, x1 - 1, color);
	}
}

void ModernUI::roundRect(const Common::Rect &r, int radius, uint32 fill, uint32 outline, int width) {
	fillRoundRect(r, radius, outline);
	Common::Rect in(r.left + width, r.top + width, r.right - width, r.bottom - width);
	fillRoundRect(in, MAX(0, radius - width), fill);
}

void ModernUI::fillCircle(int cx, int cy, int r, uint32 color) {
	fillRoundRect(Common::Rect(cx - r, cy - r, cx + r, cy + r), r, color);
}

void ModernUI::bevel(const Common::Rect &r, bool pressed, int width) {
	const uint32 gray = rgb(kGray[0], kGray[1], kGray[2]);
	const uint32 light = rgb(255, 255, 255), dark = rgb(kDarkGray[0], kDarkGray[1], kDarkGray[2]);
	_out.fillRect(r, gray);
	_out.frameRect(r, rgb(0, 0, 0));
	const uint32 tl = pressed ? dark : light, br = pressed ? light : dark;
	for (int i = 1; i <= width; i++) {
		_out.hLine(r.left + i, r.top + i, r.right - 1 - i, tl);
		_out.vLine(r.left + i, r.top + i, r.bottom - 1 - i, tl);
		_out.hLine(r.left + i, r.bottom - 1 - i, r.right - 1 - i, br);
		_out.vLine(r.right - 1 - i, r.top + i, r.bottom - 1 - i, br);
	}
}

// A Windows 3.1 window: bevelled frame, navy title bar with the system menu box
Common::Rect ModernUI::titled(const Common::Rect &box, const Common::String &title, int fontSize) {
	bevel(box, false, 2);
	_out.fillRect(Common::Rect(box.left + 4, box.top + 4, box.right - 4, box.top + 35), rgb(kNavy[0], kNavy[1], kNavy[2]));
	Common::Rect sys(box.left + 7, box.top + 7, box.left + 32, box.top + 32);
	_out.fillRect(sys, rgb(kGray[0], kGray[1], kGray[2]));
	_out.frameRect(sys, rgb(0, 0, 0));
	_out.fillRect(Common::Rect(box.left + 12, box.top + 17, box.left + 27, box.top + 22), rgb(kDarkGray[0], kDarkGray[1], kDarkGray[2]));
	const Graphics::Font *f = font(fontSize, true);
	text(title, box.left + 42, box.top + 4 + (31 - f->getFontHeight()) / 2, box.width() - 50, rgb(255, 255, 255), f);
	return Common::Rect(box.left + 5, box.top + 37, box.right - 5, box.bottom - 5);
}

void ModernUI::text(const Common::String &s, int x, int y, int w, uint32 color, const Graphics::Font *f, int align) {
	if (!f)
		return;
	f->drawString(&_out, s, x, y, w, color, (Graphics::TextAlign)(align == 1 ? Graphics::kTextAlignCenter : (align == 2 ? Graphics::kTextAlignRight : Graphics::kTextAlignLeft)));
}

void ModernUI::textCentered(const Common::String &s, const Common::Rect &r, uint32 color, const Graphics::Font *f) {
	if (!f)
		return;
	// one or two lines (split at '\n')
	Common::Array<Common::String> lines;
	uint start = 0;
	for (uint i = 0; i <= s.size(); i++) {
		if (i == s.size() || s[i] == '\n') {
			lines.push_back(Common::String(s.c_str() + start, i - start));
			start = i + 1;
		}
	}
	const int lh = f->getFontHeight();
	int y = (r.top + r.bottom) / 2 - lh * (int)lines.size() / 2;
	for (uint i = 0; i < lines.size(); i++, y += lh)
		f->drawString(&_out, lines[i], r.left, y, r.width(), color, Graphics::kTextAlignCenter);
}

void ModernUI::saveArea(const Common::Rect &r) {
	_saved.create(r.width(), r.height(), _format);
	_saved.blitFrom(_out, r, Common::Point(0, 0));
}

void ModernUI::blendArea(const Common::Rect &r, int percent) {
	if (percent >= 100)
		return;
	for (int y = 0; y < r.height(); y++) {
		for (int x = 0; x < r.width(); x++) {
			const int sx = r.left + x, sy = r.top + y;
			if (sx < 0 || sy < 0 || sx >= _w || sy >= _h)
				continue;
			byte r1, g1, b1, r0, g0, b0;
			_format.colorToRGB(_out.getPixel(sx, sy), r1, g1, b1);
			_format.colorToRGB(_saved.getPixel(x, y), r0, g0, b0);
			_out.setPixel(sx, sy, rgb((r1 * percent + r0 * (100 - percent)) / 100, (g1 * percent + g0 * (100 - percent)) / 100,
				(b1 * percent + b0 * (100 - percent)) / 100));
		}
	}
}

// Drawing

void ModernUI::render() {
	if (!_w)
		return;
	buildLookup();
	updateHolds();
	updateMomentum();
	if (!_wrapFont || _wrapWidth != textArea().width())
		rewrap();

	drawBackground();
	drawRoom();
	if (shelfShown())
		drawShelf();
	if (verbsShown())
		drawVerbs();
	drawTextWindow();
	drawButtons();
	drawFloating();
	drawDragged();

	g_system->copyRectToScreen(_out.getPixels(), _out.pitch, 0, 0, _w, _h);
}

void ModernUI::showPicture(const Graphics::ManagedSurface &picture) {
	if (!_w)
		return;
	buildLookup();
	_out.fillRect(Common::Rect(_w, _h), rgb(0, 0, 0));
	blitScaled(picture, Common::Rect(picture.w, picture.h), hiddenToScreen(Common::Rect(picture.w, picture.h)));
	g_system->copyRectToScreen(_out.getPixels(), _out.pitch, 0, 0, _w, _h);
}

void ModernUI::drawBackground() {
	if (_look == kLookDark)
		_out.fillRect(Common::Rect(_w, _h), rgb(kDarkBg[0], kDarkBg[1], kDarkBg[2]));
	else
		_out.fillRect(Common::Rect(_w, _h), rgb(kTeal[0], kTeal[1], kTeal[2]));
}

Common::String ModernUI::roomTitle() const {
	return _gui->findWindowData(kMainGameWindow).title;
}

void ModernUI::drawRoom() {
	if (_look == kLookDark) {
		_out.fillRect(_roomBox, rgb(0, 0, 0));
	} else {
		titled(_roomBox, roomTitle(), _roomTitleFont);
	}
	Graphics::ManagedSurface *room = _gui->_mainGameWindow->getWindowSurface();
	blitScaled(*room, Common::Rect(room->w, room->h), _roomPic);
	if (_look == kLookDark) {
		_out.frameRect(_roomBox, rgb(90, 90, 100));
		const Graphics::Font *f = font(_roomTitleFont, true, true);
		// the room's name over the picture, with a shadow
		text(roomTitle(), _roomBox.left + 13, _roomBox.top + 9, _roomBox.width() / 2, rgb(0, 0, 0), f);
		text(roomTitle(), _roomBox.left + 12, _roomBox.top + 8, _roomBox.width() / 2, rgb(255, 255, 255), f);
	}
}

void ModernUI::drawHoldRing(const Common::Rect &r, bool round, float progress) {
	const uint32 col = _look == kLookDark ? rgb(kBlue[0], kBlue[1], kBlue[2]) : rgb(kNavy[0], kNavy[1], kNavy[2]);
	if (round) {
		const int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2, rad = r.width() / 2 + 7;
		for (int y = -rad; y <= rad; y++) {
			for (int x = -rad; x <= rad; x++) {
				const int d2 = x * x + y * y;
				if (d2 > rad * rad || d2 < (rad - 5) * (rad - 5))
					continue;
				float a = atan2f((float)x, (float)-y);
				if (a < 0)
					a += 2 * 3.14159265f;
				if (a <= progress * 2 * 3.14159265f && cx + x >= 0 && cy + y >= 0 && cx + x < _w && cy + y < _h)
					_out.setPixel(cx + x, cy + y, col);
			}
		}
	} else {
		// around the button's edge, clockwise from the top left
		Common::Rect o(r.left - 4, r.top - 4, r.right + 4, r.bottom + 4);
		const int per = 2 * (o.width() + o.height());
		int n = (int)(per * progress);
		for (int i = 0; i < n; i++) {
			int x, y;
			if (i < o.width()) {
				x = o.left + i; y = o.top;
			} else if (i < o.width() + o.height()) {
				x = o.right - 1; y = o.top + i - o.width();
			} else if (i < 2 * o.width() + o.height()) {
				x = o.right - 1 - (i - o.width() - o.height()); y = o.bottom - 1;
			} else {
				x = o.left; y = o.bottom - 1 - (i - 2 * o.width() - o.height());
			}
			_out.fillRect(Common::Rect(x - 1, y - 1, x + 2, y + 2).findIntersectingRect(Common::Rect(_w, _h)), col);
		}
	}
}

void ModernUI::drawButtons() {
	const bool dark = _look == kLookDark;
	for (int i = 0; i < 3; i++) {
		const Common::Rect &r = _buttons[i];
		const bool held = _press.region == kRegionButton && _press.index == i && _pressInside;
		Common::Rect area(r.left - 8, r.top - 8, r.right + 8, r.bottom + 8);
		area.clip(Common::Rect(_w, _h));
		const int opacity = held ? 100 : _opacity;
		if (opacity < 100)
			saveArea(area);
		if (_roundButtons) {
			const int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2, rad = r.width() / 2;
			if (dark) {
				fillCircle(cx, cy, rad, rgb(220, 220, 220));
				fillCircle(cx, cy, rad - 3, rgb(25, 25, 30));
			} else {
				fillCircle(cx, cy, rad, rgb(0, 0, 0));
				fillCircle(cx, cy, rad - 2, rgb(kGray[0], kGray[1], kGray[2]));
				// the 3D edge: light top left, dark bottom right
				for (int y = -rad + 4; y <= rad - 4; y++) {
					for (int x = -rad + 4; x <= rad - 4; x++) {
						const int d2 = x * x + y * y;
						if (d2 > (rad - 4) * (rad - 4) || d2 < (rad - 7) * (rad - 7))
							continue;
						bool topLeft = (x + y) < 0;
						if (held)
							topLeft = !topLeft;
						_out.setPixel(cx + x, cy + y, topLeft ? rgb(255, 255, 255) : rgb(kDarkGray[0], kDarkGray[1], kDarkGray[2]));
					}
				}
			}
			textCentered(kButtonLabels[i], r, dark ? rgb(255, 255, 255) : rgb(0, 0, 0), font(13, true));
		} else {
			if (dark)
				roundRect(r, 10, rgb(25, 25, 30), rgb(220, 220, 220), 2);
			else
				bevel(r, held, 2);
			textCentered(kTitleButtonLabels[i], r, dark ? rgb(255, 255, 255) : rgb(0, 0, 0), font(14, true));
		}
		if (held && !_holdFired) {
			const float hold = i < 2 ? kSaveHold : kMenuHold;
			drawHoldRing(r, _roundButtons, MIN(1.0f, (g_system->getMillis() - _pressTime) / hold));
		}
		if (opacity < 100)
			blendArea(area, opacity);
	}
}

void ModernUI::drawVerbs() {
	const bool dark = _look == kLookDark;
	if (_engine->needsClickToContinue()) {
		// One big button over the verbs
		Common::Rect r = _verbArea;
		if (dark)
			roundRect(r, 20, rgb(kBlue[0], kBlue[1], kBlue[2]), rgb(200, 200, 210), 2);
		else
			bevel(r, _press.region == kRegionContinue, 3);
		textCentered("Click to continue", r, dark ? rgb(255, 255, 255) : rgb(0, 0, 0), font(_verbFont + 4, true));
		return;
	}
	ControlAction selected = kNoCommand;
	for (uint i = 0; i < _gui->_controlData->size(); i++) {
		if ((*_gui->_controlData)[i].isSelected())
			selected = (*_gui->_controlData)[i].getData().refcon;
	}
	for (int i = 0; i < 9; i++) {
		Common::Rect r = verbRect(i);
		if (i == kSelfCell) {
			drawSelf(r);
			continue;
		}
		const bool sel = kVerbs[i].action == selected;
		const bool down = _press.region == kRegionVerb && _press.index == i && _pressInside;
		const Graphics::Font *f = font(_verbFont, true);
		if (dark) {
			roundRect(r, MIN(20, r.height() / 3), (sel || down) ? rgb(kBlue[0], kBlue[1], kBlue[2]) : rgb(40, 40, 48), rgb(200, 200, 210), 2);
			textCentered(kVerbs[i].label, r, rgb(255, 255, 255), f);
		} else {
			bevel(r, sel || down);
			Common::Rect tr = r;
			if (sel || down)
				tr.translate(2, 2);
			textCentered(kVerbs[i].label, tr, rgb(0, 0, 0), f);
			if (sel)
				_out.frameRect(Common::Rect(r.left + 10, r.top + 9, r.right - 10, r.bottom - 9), rgb(0, 0, 0));
		}
	}
}

// An object's picture, as the engine draws it (selected ones inverted)
void ModernUI::drawObjectImage(ObjID obj, const Common::Rect &dst, bool keepAspect) {
	_gui->ensureAssetLoaded(obj);
	ImageAsset *asset = _gui->_assets[obj];
	const int iw = asset->getWidth(), ih = asset->getHeight();
	if (iw <= 0 || ih <= 0)
		return;
	Graphics::ManagedSurface tmp(iw, ih, Graphics::PixelFormat::createFormatCLUT8());
	tmp.clear(kColorGreen);
	asset->blitInto(&tmp, 0, 0, kBlitBIC);
	if (_engine->isObjSelected(obj))
		asset->blitInto(&tmp, 0, 0, kBlitOR);
	Common::Rect r = dst;
	if (keepAspect) {
		const int k = MIN(dst.width() * 1000 / iw, dst.height() * 1000 / ih);
		const int w = MAX(1, iw * k / 1000), h = MAX(1, ih * k / 1000);
		const int x = dst.left + (dst.width() - w) / 2, y = dst.top + (dst.height() - h) / 2;
		r = Common::Rect(x, y, x + w, y + h);
	}
	blitScaled(tmp, Common::Rect(iw, ih), r, kColorGreen);
}

void ModernUI::drawSelf(const Common::Rect &r) {
	const WindowData &data = _gui->findWindowData(kSelfWindow);
	if (data.children.empty())
		return;
	drawObjectImage(data.children[0].obj, r, false);
}

void ModernUI::drawShelf() {
	const bool dark = _look == kLookDark;
	const uint32 sep = dark ? rgb(90, 90, 100) : rgb(kDarkGray[0], kDarkGray[1], kDarkGray[2]);
	if (dark) {
		roundRect(_shelfBox, 18, rgb(kPanel[0], kPanel[1], kPanel[2]), rgb(kEdge[0], kEdge[1], kEdge[2]), 2);
		text("INVENTORY", _shelfBox.left + 16, _shelfBox.top + 9, 200, rgb(200, 200, 210), font(15, true));
	} else {
		Common::Rect in = titled(_shelfBox, "Inventory", 15);
		_out.fillRect(in, rgb(255, 255, 255));
	}

	// The player's things, in rows
	_slots.clear();
	WindowReference inv = playerInventory();
	Common::Array<ObjID> items;
	if (inv != kNoWindow) {
		const WindowData &data = _gui->findWindowData(inv);
		for (uint i = 0; i < data.children.size(); i++) {
			if (_engine->isObjVisible(data.children[i].obj))
				items.push_back(data.children[i].obj);
		}
	}
	const int gx = 14, gy = 10;
	const int sw = _itemSize.x, sh = _itemSize.y;
	const int cols = MAX(1, (_shelfItems.width() - 12) / (sw + gx));
	const int perPage = cols * _shelfRows;
	const int totalCols = MAX<int>(cols, ((int)items.size() + _shelfRows - 1) / _shelfRows);
	const int maxShift = MAX(0, (totalCols - cols) * (sw + gx));
	_shelfScroll = MIN(_shelfScroll, maxShift);
	const int n = MAX<int>(perPage, items.size());
	Common::Rect clip = _shelfItems;
	for (int k = 0; k < n; k++) {
		// filled column by column when there are several rows
		const int c = k / _shelfRows, rr = k % _shelfRows;
		const int x = _shelfItems.left + 12 + c * (sw + gx) - _shelfScroll, y = _shelfItems.top + 10 + rr * (sh + gy);
		Common::Rect cell(x, y, x + sw, y + sh);
		if (cell.right > clip.right || cell.left < clip.left)
			continue;
		if (k < (int)items.size()) {
			drawObjectImage(items[k], cell, true);
			Slot s;
			s.obj = items[k];
			s.rect = cell;
			_slots.push_back(s);
		} else {
			_out.frameRect(cell, sep);
		}
	}

	_out.vLine(_shelfExits.left - 8, _shelfExits.top + 4, _shelfExits.bottom - 4, sep);
	_out.vLine(_shelfExits.left - 7, _shelfExits.top + 4, _shelfExits.bottom - 4, sep);
	drawExits(_shelfExits);
}

void ModernUI::drawExits(const Common::Rect &area) {
	const bool dark = _look == kLookDark;
	const uint32 lc = dark ? rgb(230, 230, 235) : rgb(0, 0, 0);
	text("Exits", area.left, area.top + 4, area.width(), lc, font(15, true));
	// The exits window's buttons, where they are on it
	Common::Rect src = _gui->_exitsWindow->getInnerDimensions();
	const int sw = MAX<int>(1, src.width()), shh = MAX<int>(1, src.height());
	const int aw = area.width() - 48, ah = area.height() - 26 - 30;
	const int k = MIN(aw * 1000 / sw, ah * 1000 / shh);
	const int ox = area.left + 20 + (aw - sw * k / 1000) / 2, oy = area.top + 26 + 15 + (ah - shh * k / 1000) / 2;
	_exitRects.clear();
	for (uint i = 0; i < _gui->_exitsData->size(); i++) {
		const ControlData &d = (*_gui->_exitsData)[i].getData();
		const Common::Rect &er = d.bounds;
		const int ex = (er.left + er.right) / 2, ey = (er.top + er.bottom) / 2;
		const int cx = ox + ex * k / 1000, cy = oy + ey * k / 1000;
		Common::Rect b(cx - 20, cy - 13, cx + 20, cy + 13);
		Slot slot;
		slot.obj = (ObjID)d.refcon;
		slot.rect = b;
		_exitRects.push_back(slot);
		// A compass letter from where it is on the map
		Common::String lab;
		const int dx = ex - sw / 2, dy = ey - shh / 2;
		const int adx = ABS(dx), ady = ABS(dy);
		if (MAX(adx * 10 / sw, ady * 10 / shh) >= 1) {
			if (ady * 2 >= adx)
				lab += dy < 0 ? "N" : "S";
			if (adx * 2 >= ady)
				lab += dx < 0 ? "W" : "E";
		}
		if (lab.empty())
			lab = "*";
		if (dark)
			roundRect(b, 8, rgb(kPanel[0], kPanel[1], kPanel[2]), rgb(200, 200, 210), 2);
		else
			bevel(b, false, 2);
		textCentered(lab, b, lc, font(14, true));
	}
}

void ModernUI::drawTextWindow() {
	const bool dark = _look == kLookDark;
	Common::Rect t = textBox();
	const Common::Rect a = textArea();
	const Graphics::Font *f = font(_textFont, false, dark);
	const int step = lineStep();
	uint32 tc;
	if (dark) {
		roundRect(t, 14, rgb(30, 28, 24), rgb(110, 100, 80), 2);
		tc = rgb(kWarm[0], kWarm[1], kWarm[2]);
	} else {
		Common::String title = _engine->getFilePath(kMCVID).toString();
		Common::Rect in = titled(t, title, _titleFont);
		_out.fillRect(in, rgb(255, 255, 255));
		tc = rgb(0, 0, 0);
	}

	// The lines (whole lines only)
	const int first = _scroll / step;
	const int visible = MAX(1, a.height() / step);
	const int off = _scroll % step;
	for (int i = 0; i <= visible; i++) {
		const int li = first + i;
		if (li < 0 || li >= (int)_lines.size())
			continue;
		const int y = a.top + i * step - off;
		if (y < a.top - 1 || y + step > a.bottom + step / 3)
			continue;
		text(_lines[li], a.left, y, a.width(), tc, f);
	}

	const int ms = maxScroll();
	if (dark) {
		// the maximize arrow and a thin scroll indicator
		textCentered(_maximized ? "v" : "^", Common::Rect(t.right - 36, t.top + 4, t.right - 8, t.top + 30), rgb(180, 170, 150), font(18, true));
		const int trackTop = t.top + 40, trackH = t.height() - 56;
		const int thumbH = MAX(20, ms ? trackH * a.height() / MAX(1, (int)_lines.size() * step) : trackH);
		const int ty = trackTop + (ms ? (trackH - thumbH) * _scroll / ms : 0);
		fillRoundRect(Common::Rect(t.right - 12, ty, t.right - 6, ty + thumbH), 3, rgb(120, 110, 90));
		return;
	}

	// The maximize button on the title bar
	Common::Rect mb(t.right - 4 - 3 - 26, t.top + 7, t.right - 4 - 3, t.top + 33);
	bevel(mb, _press.region == kRegionMaximize, 2);
	{
		const int cx = (mb.left + mb.right) / 2, cy = (mb.top + mb.bottom) / 2;
		for (int i = 0; i <= 10; i++) {
			const int half = i * 7 / 10;
			// up when it can grow, down when it is maximized
			const int y = _maximized ? cy + 5 - i : cy - 5 + i;
			_out.hLine(cx - half, y, cx + half, rgb(0, 0, 0));
		}
	}

	// The scroll bar
	const int sb0 = t.right - 5 - 24;
	Common::Rect track(sb0, t.top + 37, t.right - 5, t.bottom - 5);
	_out.fillRect(track, rgb(224, 224, 224));
	Common::Rect up(track.left, track.top, track.right, track.top + 24);
	Common::Rect down(track.left, track.bottom - 24, track.right, track.bottom);
	bevel(up, _press.region == kRegionScrollUp, 2);
	bevel(down, _press.region == kRegionScrollDown, 2);
	const int cx = (track.left + track.right) / 2;
	for (int i = 0; i < 7; i++) {
		_out.hLine(cx - i, up.top + 8 + i, cx + i, rgb(0, 0, 0));
		_out.hLine(cx - i, down.bottom - 9 - i, cx + i, rgb(0, 0, 0));
	}
	const int span = track.height() - 48 - 30;
	const int ty = track.top + 24 + (ms ? span * _scroll / ms : 0);
	bevel(Common::Rect(track.left, ty, track.right, ty + 30), false, 2);
}

// The game's other windows (bags, the diploma) and its dialogs, over the layout
void ModernUI::drawFloating() {
	const bool dark = _look == kLookDark;
	for (uint i = 0; i < _gui->_inventoryWindows.size(); i++) {
		WindowReference ref = _gui->_inventoryWindows[i].ref;
		if (ref == playerInventory())
			continue;
		Graphics::MacWindow *win = _gui->_inventoryWindows[i].win;
		Common::Rect outer = hiddenToScreen(win->getDimensions());
		Common::Rect inner = hiddenToScreen(win->getInnerDimensions());
		Common::Rect box(inner.left - 5, inner.top - 37, inner.right + 5, inner.bottom + 5);
		if (dark) {
			roundRect(box, 12, rgb(kPanel[0], kPanel[1], kPanel[2]), rgb(kEdge[0], kEdge[1], kEdge[2]), 2);
			text(_gui->findWindowData(ref).title, box.left + 12, box.top + 10, box.width() - 20, rgb(200, 200, 210), font(15, true));
		} else {
			titled(box, _gui->findWindowData(ref).title, 15);
		}
		(void)outer;
		Graphics::ManagedSurface *s = win->getWindowSurface();
		blitScaled(*s, Common::Rect(s->w, s->h), inner);
	}
	if (_gui->_diplomaWindow && _engine->getGameState() == kGameStateWinning) {
		Graphics::ManagedSurface *s = _gui->_diplomaWindow->getWindowSurface();
		blitScaled(*s, Common::Rect(s->w, s->h), hiddenToScreen(_gui->_diplomaWindow->getInnerDimensions()));
	}
	if (_dialogOpen) {
		Common::Rect r = hiddenToScreen(_dialogBounds);
		blitScaled(_gui->_screen, _dialogBounds, r);
		_out.frameRect(Common::Rect(r.left - 1, r.top - 1, r.right + 1, r.bottom + 1), rgb(0, 0, 0));
	}
}

// What the player is dragging, under the finger
void ModernUI::drawDragged() {
	for (uint i = 0; i < _gui->_draggedObjects.size(); i++) {
		const DraggedObj &d = _gui->_draggedObjects[i];
		if (!d.id || !d.hasMoved)
			continue;
		_gui->ensureAssetLoaded(d.id);
		ImageAsset *asset = _gui->_assets[d.id];
		const int k1000 = _roomPic.width() * 1000 / MAX<int>(1, _gui->_mainGameWindow->getInnerDimensions().width());
		const int w = asset->getWidth() * k1000 / 1000, h = asset->getHeight() * k1000 / 1000;
		Common::Rect r(_pointer.x - w / 2, _pointer.y - h - 10, _pointer.x - w / 2 + w, _pointer.y - 10);
		Graphics::ManagedSurface tmp(asset->getWidth(), asset->getHeight(), Graphics::PixelFormat::createFormatCLUT8());
		tmp.clear(kColorGreen);
		asset->blitInto(&tmp, 0, 0, kBlitBIC);
		blitScaled(tmp, Common::Rect(tmp.w, tmp.h), r, kColorGreen);
	}
}

} // End of namespace MacVenture
