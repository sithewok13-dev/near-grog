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

#include "common/config-manager.h"
#include "engines/neargrog.h"
#include "common/system.h"
#include "graphics/fontman.h"
#include "graphics/fonts/ttf.h"

#include "darklord/c64ui.h"
#include "darklord/machine.h"

namespace DarkLord {

namespace {

// The "Pepto" palette, as the engine uses
const byte kPalette[16 * 3] = {
	0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x68, 0x37, 0x2B, 0x70, 0xA4, 0xB2,
	0x6F, 0x3D, 0x86, 0x58, 0x8D, 0x43, 0x35, 0x28, 0x79, 0xB8, 0xC7, 0x6F,
	0x6F, 0x4F, 0x25, 0x43, 0x39, 0x00, 0x9A, 0x67, 0x59, 0x44, 0x44, 0x44,
	0x6C, 0x6C, 0x6C, 0x9A, 0xD2, 0x84, 0x6C, 0x5E, 0xB5, 0x95, 0x95, 0x95
};

enum {
	kWhite = 1, kBlue = 6, kBrown = 9, kGrey = 12,
	kW = 320, kH = 200
};

// The game's verb bar: two rows of words, SELF in its oval to the right
struct Verb {
	const char *name;
	int x0, x1, row; // row -1: SELF
};
const Verb kVerbs[] = {
	{ "Examine", 6, 54, 0 }, { "Open", 55, 95, 0 }, { "Close", 96, 143, 0 }, { "Speak", 144, 200, 0 },
	{ "Operate", 6, 61, 1 }, { "Go", 62, 93, 1 }, { "Hit", 94, 134, 1 }, { "Consume", 135, 200, 1 },
	{ "Self", 243, 287, -1 }
};
// The grid, as agreed: SELF bottom left
const int kGrid[9] = { 0, 1, 6, 2, 3, 7, 8, 5, 4 };

Common::Point verbPoint(int v) {
	const Verb &d = kVerbs[v];
	return d.row < 0 ? Common::Point(265, 11) : Common::Point((d.x0 + d.x1) / 2, d.row * 8 + 4);
}

// Pieces of the C64 screen
const Common::Rect kPicture(8, 24, 200, 144);
const Common::Rect kTitle(8, 16, 200, 24);
const Common::Rect kInventory(200, 24, 312, 143);
const Common::Rect kExits(250, 144, 320, 200);
const Common::Rect kLoadWord(201, 0, 241, 8), kSaveWord(201, 8, 241, 16), kMenuBox(291, 0, 320, 23);
const Common::Rect kSelfOval(244, 1, 287, 22);
const Common::Rect kContinueBar(8, 0, 200, 16);
const Common::Point kContinuePoint(100, 4);

// The text window: 6 rows of 29 letters, white on blue
const int kTextTop = 149, kTextRows = 6, kTextCols = 29;
// The game's font: 96 letters from the space, 8x8
const uint16 kFont = 0x5F92;

} // End of anonymous namespace

C64UI::C64UI(Machine *machine) : _machine(machine), _w(0), _h(0), _portrait(false), _look(kLookC64), _layout(kLayoutC2),
		_screen(nullptr), _pendingFrames(0), _scroll(0), _down(false), _button(false), _pressButton(-1), _pressTime(0), _holdFired(false), _tapFrames(0),
		_scrolling(false), _scrollStart(0), _mapped(false), _loading(false), _move(false), _lastSig(0), _drawn(false) {
}

C64UI::~C64UI() {
	_out.free();
	for (Common::HashMap<Common::String, Graphics::Font *>::iterator it = _fonts.begin(); it != _fonts.end(); ++it)
		delete it->_value;
}

bool C64UI::portraitScreen() {
	Common::String o = ConfMan.get("neargrog_orientation"); // for testing
	if (o == "portrait")
		return true;
	if (o == "landscape")
		return false;
	return g_system->getOverlayHeight() > g_system->getOverlayWidth();
}

C64UI::Look C64UI::configLook() {
	return ConfMan.get("neargrog_look") == "dark" ? kLookDark : kLookC64;
}

C64UI::Layout C64UI::configLayout() {
	const Common::String l = ConfMan.get("neargrog_layout");
	return l == "e1" ? kLayoutE1 : l == "e2" ? kLayoutE2 : l == "original" ? kLayoutOriginal : kLayoutC2;
}

const char *C64UI::layoutName(Layout l) {
	return l == kLayoutE1 ? "E1" : l == kLayoutE2 ? "E2" : l == kLayoutOriginal ? "Original" : "C2";
}

void C64UI::setup(int w, int h, bool portrait, Look look, Layout layout, const Graphics::PixelFormat &format) {
	_w = w;
	_h = h;
	_portrait = portrait;
	_look = look;
	_layout = layout;
	_format = format;
	_out.create(w, h, format);
	for (int i = 0; i < 16; i++)
		_pal[i] = rgb(kPalette[i * 3], kPalette[i * 3 + 1], kPalette[i * 3 + 2]);
	_drawn = false;
	this->layout();
}

// The biggest copy of src that fits, keeping its shape
Common::Rect C64UI::fit(const Common::Rect &into, const Common::Rect &src, bool center) const {
	const float s = MIN((float)into.width() / src.width(), (float)into.height() / src.height());
	const int w = (int)(src.width() * s), h = (int)(src.height() * s);
	const int x = into.left + (center ? (into.width() - w) / 2 : 0), y = into.top + (into.height() - h) / 2;
	return Common::Rect(x, y, x + w, y + h);
}

void C64UI::placeVerbs(const Common::Rect &r) {
	_verbBox = r;
	_cells.clear();
	const int gap = _look == kLookDark ? 8 : 4;
	const int cw = (r.width() + gap) / 3, ch = (r.height() + gap) / 3;
	for (int i = 0; i < 9; i++) {
		Cell c;
		const int col = i % 3, row = i / 3;
		c.box = Common::Rect(r.left + col * cw, r.top + row * ch, r.left + col * cw + cw - gap, r.top + row * ch + ch - gap);
		c.verb = kGrid[i];
		_cells.push_back(c);
	}
}

void C64UI::layout() {
	_regions.clear();
	_cells.clear();
	_titleBox = _picBox = _invBox = _exitBox = _textBox = _verbBox = _whole = Common::Rect();
	const bool dark = _look == kLookDark;
	auto region = [&](const Common::Rect &box, const Common::Rect &src) {
		Region r;
		r.box = box;
		r.src = src;
		_regions.push_back(r);
	};
	// The picture with its title bar above (dark: the name over the picture)
	auto picture = [&](const Common::Rect &area) {
		const Common::Rect both(kTitle.left, kTitle.top, kPicture.right, kPicture.bottom);
		// (the game's own title bar in both looks: its lettering is not
		// the text window's font)
		const Common::Rect b = fit(area, both);
		const int th = b.height() * kTitle.height() / both.height();
		_titleBox = Common::Rect(b.left, b.top, b.right, b.top + th);
		_picBox = Common::Rect(b.left, b.top + th, b.right, b.bottom);
		region(_titleBox, kTitle);
		region(_picBox, kPicture);
	};
	// The inventory and exits side by side in a strip
	auto shelf = [&](const Common::Rect &area) {
		const int pad = dark ? 10 : 4;
		const Common::Rect in(area.left + pad, area.top + pad, area.right - pad, area.bottom - pad);
		const Common::Rect inv = fit(Common::Rect(in.left, in.top, in.left + in.width() * 6 / 10, in.bottom), kInventory, false);
		const Common::Rect ex = fit(Common::Rect(inv.right + pad * 2, in.top, in.right, in.bottom), kExits, false);
		_invBox = inv;
		_exitBox = ex;
		region(_invBox, kInventory);
		region(_exitBox, kExits);
	};

	// The whole screen is ours (the backend is asked not to keep the safe
	// area): only what matters is kept clear - in portrait the Dynamic
	// Island at the top and the home bar at the bottom, in landscape the
	// island's side (either side, the phone can turn both ways) and the
	// home bar
	if (_portrait) {
		const int L = 8, R = _w - 8, T = NearGrog::kPortraitTop, B = _h - NearGrog::kHomeBar;
		const int bw = (R - L - 16) / 3;
		for (int i = 0; i < 3; i++)
			_buttons[i] = Common::Rect(L + i * (bw + 8), T, L + i * (bw + 8) + bw, T + 54);
		int y = T + 62;
		if (_layout == kLayoutOriginal) {
			_whole = fit(Common::Rect(L, y, R, B), Common::Rect(kW, kH));
			region(_whole, Common::Rect(kW, kH));
			return;
		}
		picture(Common::Rect(L, y, R, y + (R - L) * 128 / 192));
		const int shelfH = 210, verbH = 3 * 68;
		shelf(Common::Rect(L, B - shelfH, R, B));
		placeVerbs(Common::Rect(L, B - shelfH - 8 - verbH, R, B - shelfH - 8));
		_textBox = Common::Rect(L, _picBox.bottom + 8, R, _verbBox.top - 8);
		return;
	}

	const int L = NearGrog::kLandscapeSide, R = _w - NearGrog::kLandscapeSide, T = 10, B = _h - 16;
	if (_layout == kLayoutOriginal) {
		// The game's own screen, our three buttons beside it
		_whole = fit(Common::Rect(L, T, R - 200, B), Common::Rect(kW, kH), false);
		region(_whole, Common::Rect(kW, kH));
		const int bx = _whole.right + 24;
		for (int i = 0; i < 3; i++)
			_buttons[i] = Common::Rect(bx, T + i * 76, R, T + i * 76 + 64);
		return;
	}
	int rx;
	if (_layout == kLayoutC2) {
		// C2: the picture top left, inventory and exits under it; buttons,
		// verbs and text on the right
		const int shelfH = 180;
		const int ph = B - T - shelfH - 8;
		picture(Common::Rect(L, T, L + ph * 192 / 128, T + ph));
		shelf(Common::Rect(L, _picBox.bottom + 8, _picBox.right, B));
		rx = _picBox.right + 16;
		const int bw = (R - rx - 16) / 3;
		for (int i = 0; i < 3; i++)
			_buttons[i] = Common::Rect(rx + i * (bw + 8), T, rx + i * (bw + 8) + bw, T + 56);
		placeVerbs(Common::Rect(rx, T + 64, R, T + 64 + 3 * 64));
		_textBox = Common::Rect(rx, _verbBox.bottom + 8, R, B);
		return;
	}
	// E1 / E2: the picture full height on the left, everything else beside it
	picture(Common::Rect(L, T, L + (B - T) * 192 / 128, B));
	rx = _picBox.right + 16;
	const int bw = (R - rx - 16) / 3;
	for (int i = 0; i < 3; i++)
		_buttons[i] = Common::Rect(rx + i * (bw + 8), T, rx + i * (bw + 8) + bw, T + 52);
	const int vh = _layout == kLayoutE2 ? 3 * 66 : 3 * 54;
	placeVerbs(Common::Rect(rx, T + 60, R, T + 60 + vh));
	const int shelfTop = B - 150;
	_textBox = Common::Rect(rx, _verbBox.bottom + 6, R, shelfTop - 6);
	shelf(Common::Rect(rx, shelfTop, R, B));
}

// Reading the screen

bool C64UI::continueShown() const {
	return _screen && _screen[3 * kW + 10] == kBrown;
}

bool C64UI::verbSelected(int v) const {
	const Verb &d = kVerbs[v];
	if (!_screen || d.row < 0 || continueShown())
		return false;
	int blue = 0, all = 0;
	for (int y = d.row * 8 + 1; y < d.row * 8 + 7; y++)
		for (int x = d.x0 + 2; x < d.x1 - 2; x++, all++)
			blue += _screen[y * kW + x] == kBlue;
	return blue * 10 > all * 6;
}

// One letter of the game's font at a screen position, by its ink colour
char C64UI::glyphAt(int x, int y, byte ink) const {
	byte rows[8];
	bool any = false;
	for (int r = 0; r < 8; r++) {
		byte b = 0;
		for (int k = 0; k < 8; k++)
			if (_screen[(y + r) * kW + x + k] == ink)
				b |= 0x80 >> k;
		rows[r] = b;
		any |= b != 0;
	}
	if (!any)
		return ' ';
	for (int c = 1; c < 96; c++) {
		int r = 0;
		while (r < 8 && _machine->peek(kFont + c * 8 + r) == rows[r])
			r++;
		if (r == 8)
			return (char)(32 + c);
	}
	return 0; // not a letter (the window scrolling, or a picture)
}

void C64UI::readText() {
	Common::Array<Common::String> page;
	for (int row = 0; row < kTextRows; row++) {
		Common::String line;
		for (int col = 1; col <= kTextCols; col++) {
			const char c = glyphAt(col * 8, kTextTop + row * 8, kWhite);
			if (!c)
				return; // mid-scroll: wait for a clean page
			line += c;
		}
		while (!line.empty() && line.lastChar() == ' ')
			line.deleteLastChar();
		page.push_back(line);
	}
	while (!page.empty() && page.back().empty())
		page.pop_back();
	while (!page.empty() && page.front().empty())
		page.remove_at(0);
	// Only a page that stays put: the window redraws by copying rows, and a
	// half-copied page reads as letters too
	if (page != _pending) {
		_pending = page;
		_pendingFrames = 0;
		return;
	}
	if (++_pendingFrames != 4)
		return;
	if (page == _lastPage)
		return;
	_lastPage = page;
	if (page.empty())
		return;
	// Join with what's been read: the longest run of the newest lines the
	// page starts with (its last one may have grown since)
	const int n = _history.size();
	bool joined = false;
	for (int k = MIN<int>(n, page.size()); k >= 1; k--) {
		bool same = true;
		for (int i = 0; i < k && same; i++) {
			const Common::String &h = _history[n - k + i];
			same = i == k - 1 ? page[i].hasPrefix(h) && !h.empty() : h == page[i];
		}
		if (same) {
			_history.resize(n - k);
			joined = true;
			break;
		}
	}
	// a page of its own: a new paragraph
	if (!joined && !_history.empty() && !_history.back().empty())
		_history.push_back("");
	for (uint i = 0; i < page.size(); i++)
		_history.push_back(page[i]);
	while (_history.size() > 400)
		_history.remove_at(0);
	_scroll = 0;
}

bool C64UI::textHas(const char *upper) const {
	for (int i = MAX<int>(0, (int)_history.size() - 8); i < (int)_history.size(); i++) {
		Common::String l = _history[i];
		l.toUppercase();
		if (l.contains(upper))
			return true;
	}
	return false;
}

void C64UI::clearText() {
	_history.clear();
	_lastPage.clear();
	_pending.clear();
	_scroll = 0;
}

void C64UI::frame(const byte *screen) {
	_screen = screen;
	if (_tapFrames > 0)
		_tapFrames--;
	readText(); // (in Original too: the cheats watch it)
}

// Touch

bool C64UI::mapPoint(Common::Point p, Common::Point &c64) const {
	for (uint i = 0; i < _regions.size(); i++) {
		const Region &r = _regions[i];
		if (!r.box.contains(p))
			continue;
		c64.x = r.src.left + (p.x - r.box.left) * r.src.width() / MAX<int>(1, r.box.width());
		c64.y = r.src.top + (p.y - r.box.top) * r.src.height() / MAX<int>(1, r.box.height());
		return true;
	}
	return false;
}

// Where on the phone a C64 screen pixel is drawn (for tests)
Common::Point C64UI::toScreen(Common::Point c64) const {
	for (uint i = 0; i < _regions.size(); i++) {
		const Region &r = _regions[i];
		if (!r.src.contains(c64))
			continue;
		return Common::Point(r.box.left + (c64.x - r.src.left) * r.box.width() / r.src.width() + r.box.width() / r.src.width() / 2,
			r.box.top + (c64.y - r.src.top) * r.box.height() / r.src.height() + r.box.height() / r.src.height() / 2);
	}
	for (uint i = 0; i < _cells.size(); i++)
		if (verbPoint(_cells[i].verb) == c64)
			return Common::Point((_cells[i].box.left + _cells[i].box.right) / 2, (_cells[i].box.top + _cells[i].box.bottom) / 2);
	return Common::Point(-1, -1);
}

C64UI::Action C64UI::press(Common::Point p) {
	_pressPoint = p;
	_pressTime = g_system->getMillis();
	_holdFired = false;
	_pressButton = -1;
	_scrolling = false;
	_mapped = false;
	for (int i = 0; i < 3; i++)
		if (_buttons[i].contains(p)) {
			_pressButton = i;
			return kNone;
		}
	_down = true;
	// "Click to continue": a tap anywhere does
	if (continueShown() && _layout != kLayoutOriginal) {
		_target = kContinuePoint;
		_button = true;
		_tapFrames = 4;
		return kNone;
	}
	for (uint i = 0; i < _cells.size(); i++)
		if (_cells[i].box.contains(p)) {
			_target = verbPoint(_cells[i].verb);
			_button = true;
			_tapFrames = 4;
			return kNone;
		}
	if (_textBox.contains(p)) {
		_scrolling = true;
		_scrollStart = _scroll;
		_button = false;
		return kNone;
	}
	Common::Point c;
	if (mapPoint(p, c)) {
		_mapped = true;
		_move = true;
		_target = c;
		_button = true;
		_tapFrames = 4;
	}
	return kNone;
}

void C64UI::move(Common::Point p) {
	if (!_down)
		return;
	if (_scrolling) {
		const Graphics::Font *f = font(20, false, true);
		const int lh = _look == kLookDark ? (f ? f->getFontHeight() + 4 : 24) : 24;
		_scroll = MAX(0, _scrollStart + (p.y - _pressPoint.y) / MAX(1, lh));
		return;
	}
	// A drag carries on across the pieces (an object to the inventory)
	Common::Point c;
	if (_mapped && mapPoint(p, c))
		_target = c;
}

C64UI::Action C64UI::release(Common::Point p) {
	const int button = _pressButton;
	_pressButton = -1;
	if (button >= 0) {
		if (_holdFired || !_buttons[button].contains(p))
			return kNone;
		return button == 0 ? kSaveDialog : button == 1 ? kLoadDialog : kTray;
	}
	move(p);
	_down = false;
	_button = false;
	_scrolling = false;
	return kNone;
}

C64UI::Action C64UI::tick() {
	if (_pressButton < 0 || _pressButton > 1 || _holdFired || g_system->getMillis() - _pressTime < 500)
		return kNone;
	_holdFired = true;
	return _pressButton == 0 ? kQuickSave : kQuickLoad;
}

void C64UI::wheel(int lines) {
	_scroll = MAX(0, _scroll + lines);
}

bool C64UI::pointer(Common::Point &c64, bool &button) const {
	// (a quick tap holds the button a few frames, so the game sees it)
	if ((!_down || _scrolling) && _tapFrames <= 0)
		return false;
	c64 = _target;
	button = _button || _tapFrames > 0;
	return true;
}

// Drawing

const Graphics::Font *C64UI::font(int size, bool bold, bool serif) {
	Common::String name = Common::String::format("Liberation%s-%s.ttf", serif ? "Serif" : "Sans", bold ? "Bold" : "Regular");
	Common::String key = Common::String::format("%s/%d", name.c_str(), size);
	if (_fonts.contains(key) && _fonts[key])
		return _fonts[key];
	Graphics::Font *f = nullptr;
#ifdef USE_FREETYPE2
	f = Graphics::loadTTFFontFromArchive(name, size, Graphics::kTTFSizeModeCharacter, 0, 0, Graphics::kTTFRenderModeLight);
#endif
	_fonts[key] = f;
	if (!f)
		return FontMan.getFontByUsage(Graphics::FontManager::kBigGUIFont);
	return f;
}

void C64UI::text(const Common::String &s, int x, int y, int w, uint32 col, const Graphics::Font *f, bool center) {
	if (f)
		f->drawString(&_out, s, x, y, w, col, center ? Graphics::kTextAlignCenter : Graphics::kTextAlignLeft);
}

// A rounded box, smooth at the edges (a 1.5 pixel outline over the fill)
void C64UI::roundRect(const Common::Rect &r, int radius, uint32 fill, uint32 edge) {
	radius = MIN<int>(radius, MIN(r.width(), r.height()) / 2);
	uint8 fr, fg, fb, er, eg, eb;
	_format.colorToRGB(fill, fr, fg, fb);
	_format.colorToRGB(edge, er, eg, eb);
	const float lw = 1.5f;
	for (int y = MAX<int>(0, r.top); y < MIN<int>(_h, r.bottom); y++) {
		for (int x = MAX<int>(0, r.left); x < MIN<int>(_w, r.right); x++) {
			const float px = x + 0.5f, py = y + 0.5f;
			const float cx = CLIP<float>(px, r.left + radius, r.right - radius), cy = CLIP<float>(py, r.top + radius, r.bottom - radius);
			float inside;
			if (px != cx || py != cy) {
				const float dx = px - cx, dy = py - cy;
				inside = radius - sqrtf(dx * dx + dy * dy);
			} else {
				inside = MIN(MIN(px - r.left, r.right - px), MIN(py - r.top, r.bottom - py));
			}
			const float cover = CLIP(inside + 0.5f, 0.0f, 1.0f);
			if (cover <= 0)
				continue;
			const float edgeA = CLIP(lw - inside + 0.5f, 0.0f, 1.0f);
			const float rr = fr + (er - fr) * edgeA, gg = fg + (eg - fg) * edgeA, bb = fb + (eb - fb) * edgeA;
			if (cover >= 1) {
				_out.setPixel(x, y, _format.RGBToColor((uint8)rr, (uint8)gg, (uint8)bb));
			} else {
				uint8 br, bgc, bbc;
				_format.colorToRGB(_out.getPixel(x, y), br, bgc, bbc);
				_out.setPixel(x, y, _format.RGBToColor((uint8)(br + (rr - br) * cover), (uint8)(bgc + (gg - bgc) * cover), (uint8)(bbc + (bb - bbc) * cover)));
			}
		}
	}
}

// A piece of the C64 screen, scaled (each pixel to the nearest)
void C64UI::blit(const Common::Rect &src, const Common::Rect &to) {
	if (!_screen || to.isEmpty())
		return;
	const int x0 = MAX<int>(0, to.left), x1 = MIN<int>(_w, to.right);
	for (int y = MAX<int>(0, to.top); y < MIN<int>(_h, to.bottom); y++) {
		const int sy = CLIP<int>(src.top + (y - to.top) * src.height() / to.height(), 0, kH - 1);
		const byte *row = _screen + sy * kW;
		for (int x = x0; x < x1; x++) {
			const int sx = CLIP<int>(src.left + (x - to.left) * src.width() / to.width(), 0, kW - 1);
			_out.setPixel(x, y, _pal[row[sx] & 15]);
		}
	}
}

// Only the pixels of one colour from a piece of the screen (lettering), in another
void C64UI::blitInk(const Common::Rect &src, const Common::Rect &to, byte ink, uint32 col) {
	if (!_screen || to.isEmpty())
		return;
	for (int y = MAX<int>(0, to.top); y < MIN<int>(_h, to.bottom); y++) {
		const int sy = CLIP<int>(src.top + (y - to.top) * src.height() / to.height(), 0, kH - 1);
		for (int x = MAX<int>(0, to.left); x < MIN<int>(_w, to.right); x++) {
			const int sx = CLIP<int>(src.left + (x - to.left) * src.width() / to.width(), 0, kW - 1);
			if ((_screen[sy * kW + sx] & 15) == ink)
				_out.setPixel(x, y, col);
		}
	}
}

// A letter of the game's own font
void C64UI::glyph(char c, float x, int y, float scale, uint32 col) {
	if (c <= 32 || c >= 127)
		return;
	for (int r = 0; r < 8; r++) {
		const byte b = _machine->peek(kFont + (c - 32) * 8 + r);
		for (int k = 0; k < 8; k++)
			if (b & (0x80 >> k))
				_out.fillRect(Common::Rect((int)(x + k * scale), (int)(y + r * scale), (int)(x + (k + 1) * scale), (int)(y + (r + 1) * scale)), col);
	}
}

void C64UI::drawHoldRing(const Common::Rect &r, int radius, float progress) {
	const uint32 col = _look == kLookDark ? rgb(60, 120, 200) : _pal[kWhite];
	uint8 cr, cg, cb;
	_format.colorToRGB(col, cr, cg, cb);
	const float g = 4, t = 3;
	const float l = r.left - g, tp = r.top - g, rt = r.right + g, bt = r.bottom + g;
	const float R = MIN<float>(radius + g, MIN(rt - l, bt - tp) / 2);
	const float cx = (l + rt) / 2, hw = (rt - l) / 2, hh = (bt - tp) / 2;
	const float L1 = hw - R, A = (float)M_PI * R / 2, V = 2 * (hh - R), H = 2 * (hw - R);
	const float total = 2 * H + 2 * V + 4 * A, len = progress * total;
	for (int y = MAX<int>(0, (int)tp - 1); y < MIN<int>(_h, (int)bt + 1); y++)
		for (int x = MAX<int>(0, (int)l - 1); x < MIN<int>(_w, (int)rt + 1); x++) {
			const float px = x + 0.5f, py = y + 0.5f;
			const float qx = CLIP<float>(px, l + R, rt - R), qy = CLIP<float>(py, tp + R, bt - R);
			const float dx = px - qx, dy = py - qy;
			if (dx == 0 && dy == 0)
				continue;
			const float od = sqrtf(dx * dx + dy * dy) - R;
			float cover = CLIP(0.5f - od, 0.0f, 1.0f) * CLIP(od + t + 0.5f, 0.0f, 1.0f);
			if (cover <= 0)
				continue;
			float sp;
			float ang = atan2f(dx, -dy);
			if (ang < 0)
				ang += 2 * (float)M_PI;
			if (dx == 0 && dy < 0)
				sp = qx >= cx ? qx - cx : total - (cx - qx);
			else if (dy == 0 && dx > 0)
				sp = L1 + A + (qy - (tp + R));
			else if (dx == 0)
				sp = L1 + 2 * A + V + ((rt - R) - qx);
			else if (dy == 0)
				sp = L1 + 3 * A + V + H + ((bt - R) - qy);
			else if (dx > 0 && dy < 0)
				sp = L1 + R * ang;
			else if (dx > 0)
				sp = L1 + A + V + R * (ang - (float)M_PI / 2);
			else if (dy > 0)
				sp = L1 + 2 * A + V + H + R * (ang - (float)M_PI);
			else
				sp = L1 + 3 * A + 2 * V + H + R * (ang - 3 * (float)M_PI / 2);
			cover *= CLIP(len - sp + 0.5f, 0.0f, 1.0f);
			if (cover <= 0)
				continue;
			uint8 br, bgc, bbc;
			_format.colorToRGB(_out.getPixel(x, y), br, bgc, bbc);
			_out.setPixel(x, y, _format.RGBToColor((uint8)(br + (cr - br) * cover), (uint8)(bgc + (cg - bgc) * cover), (uint8)(bbc + (cb - bbc) * cover)));
		}
}

void C64UI::drawButtons() {
	static const char *const kLabels[3] = { "Save", "Load", "Menu" }; // (Dark: in the verbs' case)
	const bool dark = _look == kLookDark;
	for (int i = 0; i < 3; i++) {
		const Common::Rect &b = _buttons[i];
		if (dark) {
			roundRect(b, 24, rgb(40, 40, 48), rgb(200, 200, 210));
			const Graphics::Font *f = font(16, true, false);
			text(kLabels[i], b.left, b.top + (b.height() - (f ? f->getFontHeight() : 16)) / 2, b.width(), rgb(255, 255, 255), f, true);
		} else {
			// The game's own: white, a blue edge, its lettering
			_out.fillRect(b, _pal[kBlue]);
			_out.fillRect(Common::Rect(b.left + 3, b.top + 3, b.right - 3, b.bottom - 3), _pal[kWhite]);
			const Common::Rect &src = i == 0 ? kSaveWord : i == 1 ? kLoadWord : kMenuBox;
			Common::Rect in(b.left + 8, b.top + 6, b.right - 8, b.bottom - 6);
			if (i < 2)
				blitInk(src, fit(in, src), kBlue, _pal[kBlue]);
			else
				blit(Common::Rect(src.left + 1, src.top + 1, src.right - 1, src.bottom - 1), fit(in, src));
		}
		if (_pressButton == i && !_holdFired && i < 2)
			drawHoldRing(b, dark ? 24 : 0, MIN(1.0f, (g_system->getMillis() - _pressTime) / 500.0f));
	}
}

void C64UI::drawVerbs() {
	const bool dark = _look == kLookDark;
	for (uint i = 0; i < _cells.size(); i++) {
		const Cell &c = _cells[i];
		const Verb &v = kVerbs[c.verb];
		const bool chosen = verbSelected(c.verb);
		if (dark) {
			roundRect(c.box, 16, chosen ? rgb(60, 120, 200) : rgb(40, 40, 48), rgb(200, 200, 210));
			const Graphics::Font *f = font(_portrait ? 20 : 18, true, false);
			text(v.name, c.box.left, c.box.top + (c.box.height() - (f ? f->getFontHeight() : 16)) / 2, c.box.width(), rgb(255, 255, 255), f, true);
			continue;
		}
		const uint32 paper = chosen ? _pal[kBlue] : _pal[kWhite], ink = chosen ? _pal[kWhite] : _pal[kBlue];
		_out.fillRect(c.box, _pal[kBlue]);
		if (v.row < 0) {
			// SELF: white, a blue bar down each side, its oval
			_out.fillRect(Common::Rect(c.box.left + 8, c.box.top + 3, c.box.right - 8, c.box.bottom - 3), _pal[kWhite]);
			const Common::Rect in(c.box.left + 14, c.box.top + 6, c.box.right - 14, c.box.bottom - 6);
			blit(kSelfOval, fit(in, kSelfOval));
			continue;
		}
		_out.fillRect(Common::Rect(c.box.left + 3, c.box.top + 3, c.box.right - 3, c.box.bottom - 3), paper);
		// the word in the game's own font (always there: the bar itself is
		// covered while the game waits for a click)
		const Common::Rect in(c.box.left + 8, c.box.top + 6, c.box.right - 8, c.box.bottom - 6);
		const int len = strlen(v.name);
		const float sc = MIN(3.0f, MIN((float)in.height() / 8, (float)in.width() / (len * 8)));
		const float x = in.left + (in.width() - len * 8 * sc) / 2;
		const int y = in.top + (int)((in.height() - 8 * sc) / 2);
		for (int k = 0; k < len; k++)
			glyph(v.name[k], x + k * 8 * sc, y, sc, ink);
	}

}

void C64UI::drawText() {
	const bool dark = _look == kLookDark;
	Common::Rect b = _textBox;
	if (dark)
		roundRect(b, 14, rgb(30, 28, 24), rgb(110, 100, 80));
	else
		_out.fillRect(b, _pal[kBlue]);
	if (continueShown()) {
		// The game waits for a click: said at the foot of the text, where
		// the reading stops (a tap anywhere continues)
		if (dark) {
			const Common::Rect bar(b.left + 10, b.bottom - 56, b.right - 10, b.bottom - 10);
			roundRect(bar, 23, rgb(60, 120, 200), rgb(200, 200, 210));
			const Graphics::Font *f = font(20, true, false);
			text("Tap to continue", bar.left, bar.top + (bar.height() - (f ? f->getFontHeight() : 16)) / 2, bar.width(), rgb(255, 255, 255), f, true);
			b.bottom = bar.top - 4;
		} else {
			// The game's bar, stretched across the box as the game has it
			// across its verbs: brown, a double yellow frame, its own
			// lettering in the middle (the letters keep their shape)
			const int bh = 44;
			const float k = bh / 16.0f;
			const Common::Rect bar(b.left, b.bottom - bh, b.right, b.bottom);
			_out.fillRect(bar, _pal[kBrown]);
			const int fx = (int)(5 * k), fy = (int)(2 * k), fw = (int)(2 * k);
			const Common::Rect fo(bar.left + fx, bar.top + fy, bar.right - fx, bar.bottom - fy);
			_out.fillRect(fo, _pal[7]);
			_out.fillRect(Common::Rect(fo.left + fw, fo.top + fw, fo.right - fw, fo.bottom - fw), _pal[kBrown]);
			const Common::Rect letters(40, 5, 160, 11);
			const int lw = (int)(letters.width() * k), lh = (int)(letters.height() * k);
			const int lx = bar.left + (bar.width() - lw) / 2, ly = bar.top + (int)(5 * k);
			blitInk(letters, Common::Rect(lx, ly, lx + lw, ly + lh), 7, _pal[7]);
			b.bottom = bar.top - 4;
		}
	}
	// The paragraphs read so far, flowed to the width
	Common::Array<Common::String> paras;
	Common::String cur;
	for (uint i = 0; i < _history.size(); i++) {
		if (_history[i].empty()) {
			if (!cur.empty())
				paras.push_back(cur);
			cur.clear();
			continue;
		}
		if (!cur.empty())
			cur += ' ';
		Common::String l = _history[i];
		l.trim();
		cur += l;
	}
	if (!cur.empty())
		paras.push_back(cur);
	Common::Array<Common::String> lines;
	if (dark) {
		const Graphics::Font *f = font(_portrait ? 22 : 19, false, true);
		if (!f)
			return;
		for (uint i = 0; i < paras.size(); i++) {
			Common::Array<Common::String> w;
			f->wordWrapText(paras[i], b.width() - 32, w);
			for (uint k = 0; k < w.size(); k++)
				lines.push_back(w[k]);
			if (i + 1 < paras.size())
				lines.push_back("");
		}
		const int lh = f->getFontHeight() + 4;
		const int n = (b.height() - 20) / lh;
		const int last = MAX<int>(0, (int)lines.size() - _scroll);
		const int first = MAX<int>(0, last - n);
		int y = b.top + 10;
		for (int i = first; i < last; i++, y += lh)
			text(lines[i], b.left + 14, y, b.width() - 28, rgb(240, 232, 210), f, false);
		return;
	}
	// The game's own letters, about 28 a line
	const float sc = MIN(3.0f, (b.width() - 20) / (28.0f * 8));
	const int cols = MAX(10, (int)((b.width() - 20) / (8 * sc)));
	for (uint i = 0; i < paras.size(); i++) {
		Common::String line;
		const Common::String &p = paras[i];
		uint k = 0;
		while (k < p.size()) {
			while (k < p.size() && p[k] == ' ')
				k++;
			Common::String word;
			while (k < p.size() && p[k] != ' ')
				word += p[k++];
			if (word.empty())
				break;
			if (!line.empty() && (int)(line.size() + 1 + word.size()) > cols) {
				lines.push_back(line);
				line.clear();
			}
			if (!line.empty())
				line += ' ';
			line += word;
		}
		if (!line.empty())
			lines.push_back(line);
		if (i + 1 < paras.size())
			lines.push_back("");
	}
	const int lh = (int)(8 * sc) + 2;
	const int n = (b.height() - 12) / lh;
	const int last = MAX<int>(0, (int)lines.size() - _scroll);
	const int first = MAX<int>(0, last - n);
	int y = b.top + 6;
	for (int i = first; i < last; i++, y += lh)
		for (uint k = 0; k < lines[i].size(); k++)
			glyph(lines[i][k], b.left + 10 + k * 8 * sc, y, sc, _pal[kWhite]);
}

void C64UI::drawPieces() {
	const bool dark = _look == kLookDark;
	if (!_titleBox.isEmpty())
		blit(kTitle, _titleBox);
	blit(kPicture, _picBox);
	if (dark) {
		Common::Rect shelf(_invBox.left - 10, MIN(_invBox.top, _exitBox.top) - 10, _exitBox.right + 10, MAX(_invBox.bottom, _exitBox.bottom) + 10);
		roundRect(shelf, 18, rgb(34, 34, 40), rgb(150, 150, 160));
	}
	blit(kInventory, _invBox);
	blit(kExits, _exitBox);
}

uint32 C64UI::signature() const {
	uint32 h = 2166136261u;
	if (_screen)
		for (int i = 0; i < kW * kH; i += 4)
			h = (h ^ (_screen[i] | _screen[i + 1] << 8 | _screen[i + 2] << 16 | _screen[i + 3] << 24)) * 16777619u;
	h ^= _loading * 65537;
	h ^= _history.size() * 7919 + _scroll * 131 + (_pressButton + 1) * 977 + _holdFired * 3 + _look * 5 + _layout * 17;
	if (_pressButton >= 0 && !_holdFired)
		h ^= g_system->getMillis();
	return h;
}

bool C64UI::draw(const byte *screen) {
	_screen = screen;
	const uint32 sig = signature();
	if (sig == _lastSig && _drawn)
		return false;
	_lastSig = sig;
	_drawn = true;
	const bool dark = _look == kLookDark;
	_out.fillRect(Common::Rect(_w, _h), dark ? rgb(12, 12, 16) : _pal[0]);
	if (_loading) {
		// (not the crack's intro and the disk prompt: just a moment's wait)
		const Graphics::Font *f = font(26, true, false);
		text("Uninvited", 0, _h / 2 - 40, _w, rgb(255, 255, 255), f, true);
		text("Loading...", 0, _h / 2 + 4, _w, rgb(150, 150, 160), font(20, false, false), true);
		return true;
	}
	if (_layout == kLayoutOriginal) {
		blit(Common::Rect(kW, kH), _whole);
		drawButtons();
		return true;
	}
	drawPieces();
	drawButtons();
	drawVerbs();
	drawText();
	return true;
}

} // End of namespace DarkLord
