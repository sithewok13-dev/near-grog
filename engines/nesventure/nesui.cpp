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
#include "common/hash-str.h"
#include "common/system.h"
#include "graphics/font.h"
#include "graphics/fontman.h"
#include "graphics/fonts/ttf.h"

#include "nesventure/nes.h"
#include "nesventure/nesui.h"

namespace NesVenture {

// Each game's screen: the verbs' marker tiles (name table 0; the first nine
// make the 3x3 grid, SELF bottom-left; the rest a row under it), the picture,
// the exits box and the inventory's title
namespace {

const NesItem kUninvitedVerbs[] = {
	{ "EXAM.", 11, 21 }, { "TAKE", 17, 21 }, { "OPEN", 11, 23 },
	{ "CLOSE", 17, 23 }, { "USE", 11, 25 }, { "HIT", 17, 25 },
	{ "SELF", 24, 25 }, { "LEAVE", 11, 27 }, { "SPEAK", 17, 27 },
	{ "SAVE", 24, 27 }, { "MEMO <", 24, 23 }, { "MEMO >", 28, 23 }
};
const NesItem kShadowgateVerbs[] = {
	{ "LOOK", 12, 21 }, { "TAKE", 18, 21 }, { "OPEN", 12, 23 },
	{ "CLOSE", 18, 23 }, { "USE", 12, 25 }, { "HIT", 18, 25 },
	{ "SELF", 24, 25 }, { "LEAVE", 12, 27 }, { "SPEAK", 18, 27 },
	{ "SAVE", 24, 27 }, { "CARD up", 24, 21 }, { "CARD down", 24, 23 }
};
const NesItem kDejaVuVerbs[] = {
	{ "EXAM.", 3, 21 }, { "OPEN", 10, 21 }, { "TAKE", 10, 23 },
	{ "CLOSE", 3, 27 }, { "USE", 10, 27 }, { "HIT", 3, 25 },
	{ "SELF", 24, 25 }, { "LEAVE", 10, 25 }, { "SPEAK", 3, 23 },
	{ "MOVE", 17, 21 }, { "SAVE", 24, 27 }, { "NOTE <", 24, 21 }, { "NOTE >", 28, 21 }
};

const NesGameMap kMaps[] = {
	{ 0x76, 0x77, kShadowgateVerbs, ARRAYSIZE(kShadowgateVerbs), Common::Rect(8, 24, 137, 152), Common::Rect(24, 168, 80, 224), "GOODS", 0, 0x7F },
	{ 0xA5, 0xA6, kDejaVuVerbs, ARRAYSIZE(kDejaVuVerbs), Common::Rect(8, 24, 136, 152), Common::Rect(136, 184, 176, 224), "GOODS", 0, 0x7E },
	{ 0x76, 0x77, kUninvitedVerbs, ARRAYSIZE(kUninvitedVerbs), Common::Rect(8, 24, 137, 152), Common::Rect(24, 167, 80, 223), "PROPERTY", -1, 0x7F }
};


const int kGoodsRows = 7;      // name table rows 5, 7 ... 17
const int kGoodsMarkerCol = 19;
const int kGoodsCols = 11;     // the marker and the name

// Blank, or the ruled lines between text rows (seen while Deja Vu scrolls)
bool isBlank(byte t) { return t == 0x7E || t == 0x7F || t == 0xDE || t == 0xDF; }

} // End of anonymous namespace

NesUI::NesUI(Nes *nes, GameId game) : _nes(nes), _game(game), _map(&kMaps[game]), _w(0), _h(0), _portrait(false),
		_look(kLookNes), _textMode(false), _haveSnapshot(false), _textFg(0x0F), _textBg(0x30), _haveTextColours(false), _verbPaper(0x30), _commandFrames(0),
		_scroll(0), _pad(0), _continueFrames(0), _pressButton(-1), _pressTime(0), _holdFired(false), _lastSignature(0), _drawn(false) {
	memset(_snapshot, 0, sizeof(_snapshot));
	memset(_pal, 0, sizeof(_pal));
	_goal.active = false;
}

NesUI::~NesUI() {
	for (Common::HashMap<Common::String, Graphics::Font *>::iterator it = _fonts.begin(); it != _fonts.end(); ++it)
		delete it->_value;
}

bool NesUI::portraitScreen() {
	Common::String o = ConfMan.get("neargrog_orientation"); // for testing
	if (o == "portrait")
		return true;
	if (o == "landscape")
		return false;
	return g_system->getOverlayHeight() > g_system->getOverlayWidth();
}

NesUI::Look NesUI::configLook() {
	return ConfMan.get("neargrog_look") == "dark" ? kLookDark : kLookNes;
}

void NesUI::setup(int w, int h, bool portrait, Look look, const Graphics::PixelFormat &format) {
	_w = w;
	_h = h;
	_portrait = portrait;
	_look = look;
	_format = format;
	_out.create(w, h, format);
	_drawn = false;
	for (int i = 0; i < 64; i++)
		_pal[i] = rgb(Nes::kPalette[i * 3], Nes::kPalette[i * 3 + 1], Nes::kPalette[i * 3 + 2]);
	layout();
}

// Layout: landscape has the square picture full height on the left;
// portrait stacks everything under it

// Rows on screen: name table row r is drawn at y = r * 8 + the game's offset
int NesUI::rowY(int r) const {
	return r * 8 + _map->rowOffset;
}

void NesUI::layout() {
	const int rows = 3 + (_map->verbCount > 9 ? 1 : 0);
	if (!_portrait) {
		const int L = 54, R = _w - 54, T = 14, B = _h - 14;
		const int side = B - T;
		_picBox = Common::Rect(L, T, L + side, B);
		const int x0 = L + side + 16;
		const int bw = (R - x0 - 16) / 3;
		for (int i = 0; i < 3; i++)
			_buttons[i] = Common::Rect(x0 + i * (bw + 8), T, x0 + i * (bw + 8) + bw, T + 54);
		_verbBox = Common::Rect(x0, T + 64, R, T + 64 + rows * 46);
		_goodsBox = Common::Rect(x0, B - 150, R - 160, B);
		_moveBox = Common::Rect(R - 150, B - 150, R, B);
		_textBox = Common::Rect(x0, _verbBox.bottom + 10, R, _goodsBox.top - 10);
	} else {
		const int L = 24, R = _w - 24;
		int y = 60;
		const int bw = (R - L - 16) / 3;
		for (int i = 0; i < 3; i++)
			_buttons[i] = Common::Rect(L + i * (bw + 8), y, L + i * (bw + 8) + bw, y + 50);
		y += 60;
		_picBox = Common::Rect(L, y, R, y + (R - L));
		const int B = _h - 40;
		_goodsBox = Common::Rect(L, B - 170, R - 170, B);
		_moveBox = Common::Rect(R - 160, B - 170, R, B);
		_verbBox = Common::Rect(L, _goodsBox.top - 10 - rows * 56, R, _goodsBox.top - 10);
		_textBox = Common::Rect(L, _picBox.bottom + 10, R, _verbBox.top - 10);
	}
	// The verb cells: three across, the extras in a row of their own
	_verbCells.clear();
	const int rowsH = _verbBox.height() / rows;
	for (int i = 0; i < _map->verbCount; i++) {
		int r, c, cols;
		if (i < 9) {
			r = i / 3; c = i % 3; cols = 3;
		} else {
			r = 3; c = i - 9; cols = _map->verbCount - 9;
		}
		const int cw = _verbBox.width() / cols;
		_verbCells.push_back(Common::Rect(_verbBox.left + c * cw, _verbBox.top + r * rowsH,
			_verbBox.left + (c + 1) * cw - 4, _verbBox.top + (r + 1) * rowsH - 4));
	}
}

// Following the game

Common::Point NesUI::itemPoint(const NesItem &it) const {
	return Common::Point(it.col * 8 + 4, rowY(it.row) + 4);
}

Common::Rect NesUI::itemRect(const NesItem &it) const {
	const Common::String n(it.name);
	// The arrows: the marker and its arrow ("MEMO >" has the arrow before the marker)
	if (n.hasSuffix(">"))
		return Common::Rect((it.col - 1) * 8, rowY(it.row), (it.col + 1) * 8, rowY(it.row) + 8);
	if (n.hasSuffix("<") || n.hasSuffix(" up") || n.hasSuffix(" down"))
		return Common::Rect(it.col * 8, rowY(it.row), (it.col + 2) * 8, rowY(it.row) + 8);
	// The marker and the word
	const int tiles = 1 + (n.contains(' ') ? (int)n.find(' ') : (int)n.size());
	return Common::Rect(it.col * 8, rowY(it.row), (it.col + tiles) * 8, rowY(it.row) + 8);
}

void NesUI::update() {
	const bool wasText = _textMode;
	// Text or commands: which name table the bottom panel comes from. The
	// games flicker back to the commands for a frame now and then while
	// writing, so the commands count once they've stayed up a moment.
	if (_nes->lineTable(200) & 1) {
		_textMode = true;
		_commandFrames = 0;
	} else if (++_commandFrames > 8) {
		_textMode = false;
	}
	if (!_textMode) {
		memcpy(_snapshot, _nes->screen(), sizeof(_snapshot));
		_haveSnapshot = true;
		// The command panel's paper: its commonest colour
		int count[64] = {};
		for (int y = 165; y < 230; y += 2)
			for (int x = 16; x < 240; x += 2)
				count[_snapshot[y * 256 + x] & 63]++;
		for (int i = 0; i < 64; i++)
			if (count[i] > count[_verbPaper])
				_verbPaper = i;
	}
	if (_textMode) {
		readText();
	} else if (wasText) {
		commitPage();
		if (!_history.empty() && !_history.back().empty())
			_history.push_back(Common::Array<byte>());
	}
	_pad = 0;
	if (_continueFrames) {
		_continueFrames--;
		_pad = _continueFrames >= 3 ? Nes::kA : 0;
		return;
	}
	if (_goal.active)
		steer();
}

void NesUI::steerTo(Common::Point nes) {
	_goal.active = true;
	_goal.target = nes;
	_goal.still = 0;
	_goal.frames = 0;
	_goal.aFrames = 0;
	_goal.last = Common::Point(-1, -1);
}

// One direction at a time (the games ignore diagonals); A once there - or
// once the pointer has settled on the nearest item in a panel
void NesUI::steer() {
	if (_textMode) {
		_goal.active = false;
		return;
	}
	const Common::Point p(_nes->peek(_map->pointerX), _nes->peek(_map->pointerY) + 1);
	const int dx = _goal.target.x - p.x, dy = _goal.target.y - p.y;
	if (_goal.aFrames) {
		_pad = Nes::kA;
		if (--_goal.aFrames == 0)
			_goal.active = false;
		return;
	}
	if (ABS(dx) <= 2 && ABS(dy) <= 2) {
		_goal.aFrames = 3;
		return;
	}
	_goal.still = p == _goal.last ? _goal.still + 1 : 0;
	_goal.last = p;
	if ((_goal.still > 20 && ABS(dx) <= 12 && ABS(dy) <= 12) || ++_goal.frames > 900) {
		_goal.aFrames = _goal.frames > 900 ? 0 : 3;
		if (!_goal.aFrames)
			_goal.active = false;
		return;
	}
	if (ABS(dx) > 2 && (ABS(dx) >= ABS(dy) || ABS(dy) <= 2))
		_pad = dx > 0 ? Nes::kRight : Nes::kLeft;
	else
		_pad = dy > 0 ? Nes::kDown : Nes::kUp;
	if (ABS(dx) < 10 && ABS(dy) < 10 && (_goal.frames & 1))
		_pad = 0; // close: a step at a time
}

// Text: name table 1, rows 21-27 (every other), columns 3-28. Uninvited and
// Shadowgate turn pages, Deja Vu scrolls a line at a time, and all write a
// letter at a time: each screenful is matched against the end of what's
// been read so far and only what's new is added.
namespace {
// The same line, or the same line part written (one starts the other)
bool sameLine(const Common::Array<byte> &a, const Common::Array<byte> &b) {
	const uint n = MIN(a.size(), b.size());
	if (n == 0)
		return a.empty() && b.empty();
	for (uint k = 0; k < n; k++)
		if (a[k] != b[k])
			return false;
	return true;
}
} // End of anonymous namespace

void NesUI::readText() {
	Common::Array<Common::Array<byte> > rows;
	for (int r = 21; r <= 27; r += 2) {
		Common::Array<byte> row;
		for (int c = 3; c <= 28; c++)
			row.push_back(_nes->ppuPeek(0x2400 + r * 32 + c));
		// Without trailing blanks or anything not a letter or sign (the
		// writing cursor)
		while (!row.empty() && (isBlank(row.back()) || row.back() == _map->blankTile || decode(Common::Array<byte>(&row.back(), 1)).empty()))
			row.pop_back();
		rows.push_back(row);
	}
	// The lines on show, without the empty ones at either end
	while (!rows.empty() && rows.front().empty())
		rows.remove_at(0);
	while (!rows.empty() && rows.back().empty())
		rows.pop_back();
	if (!rows.empty()) {
		// Where these lines fit what's been read (among the last dozen
		// lines): every line the same, except the line being written - the
		// last one read so far, or the last on show - which may have more
		// letters (or fewer, redrawn from the top). Of the places that fit,
		// the one with the most lines exactly the same; none means new lines.
		int at = (int)_page.size(), best = -1;
		for (int j = (int)_page.size() - 1; j >= MAX<int>(0, (int)_page.size() - 12); j--) {
			bool fits = true;
			int same = 0;
			for (uint i = 0; fits && i < rows.size() && j + i < _page.size(); i++) {
				const Common::Array<byte> &a = _page[j + i], &b = rows[i];
				if (a == b)
					same++;
				else
					fits = (j + i == _page.size() - 1 || i == rows.size() - 1) && sameLine(a, b);
			}
			if (fits && same > best) {
				best = same;
				at = j;
			}
		}
		for (uint i = 0; i < rows.size(); i++) {
			if (at + i < _page.size()) {
				if (rows[i].size() >= _page[at + i].size())
					_page[at + i] = rows[i];
			} else {
				_page.push_back(rows[i]);
			}
		}
	}
	bool letters = false;
	for (uint i = 0; i < rows.size(); i++)
		letters = letters || rows[i].size() > 3;
	if (!_haveTextColours && letters && (_nes->lineTable(200) & 1)) {
		// The panel's colours: the commonest pixel, and the darkest other
		int count[64] = {};
		const byte *s = _nes->screen();
		for (int y = 165; y < 226; y++)
			for (int x = 24; x < 232; x++)
				count[s[y * 256 + x] & 63]++;
		int bg = 0, fg = -1;
		for (int i = 0; i < 64; i++)
			if (count[i] > count[bg])
				bg = i;
		for (int i = 0; i < 64; i++) {
			if (i == bg || count[i] < 40)
				continue;
			const int lum = Nes::kPalette[i * 3] + Nes::kPalette[i * 3 + 1] + Nes::kPalette[i * 3 + 2];
			if (fg < 0 || lum < Nes::kPalette[fg * 3] + Nes::kPalette[fg * 3 + 1] + Nes::kPalette[fg * 3 + 2])
				fg = i;
		}
		if (fg >= 0) {
			_textBg = bg;
			_textFg = fg;
			_haveTextColours = true;
		}
	}
}

// The text read since the last command goes into the history
void NesUI::commitPage() {
	for (uint i = 0; i < _page.size(); i++)
		_history.push_back(_page[i]);
	_page.clear();
	while (_history.size() > 400)
		_history.remove_at(0);
	_scroll = 0;
}

// The games' letters: A-Z from tile $B1; a few signs
Common::String NesUI::decode(const Common::Array<byte> &row) const {
	Common::String s;
	for (uint i = 0; i < row.size(); i++) {
		const byte t = row[i];
		if (t >= 0xB1 && t < 0xB1 + 26)
			s += (char)('A' + t - 0xB1);
		else if (isBlank(t) || t == 0xB0)
			s += ' ';
		else if (t == 0xA1)
			s += '.';
		else if (t == 0xD8)
			s += ',';
		else if (t == 0xCD)
			s += '\'';
		else if (t == 0x8A)
			s += '!';
		else if (t >= 0xF0 && t <= 0xF9)
			s += (char)('0' + t - 0xF0);
	}
	return s;
}

// Touch

Common::Point NesUI::toNes(const Common::Rect &box, const Common::Rect &src, Common::Point p) const {
	return Common::Point(src.left + (p.x - box.left) * src.width() / MAX<int>(1, box.width()),
		src.top + (p.y - box.top) * src.height() / MAX<int>(1, box.height()));
}

NesUI::Action NesUI::press(Common::Point p) {
	_pressPoint = p;
	_pressTime = g_system->getMillis();
	_holdFired = false;
	_pressButton = -1;
	for (int i = 0; i < 3; i++)
		if (_buttons[i].contains(p))
			_pressButton = i;
	return kNone;
}

NesUI::Action NesUI::tick() {
	if (_pressButton < 0 || _holdFired || g_system->getMillis() - _pressTime < 500)
		return kNone;
	_holdFired = true;
	return _pressButton == 0 ? kQuickSave : _pressButton == 1 ? kQuickLoad : kNone;
}

NesUI::Action NesUI::release(Common::Point p) {
	const int button = _pressButton;
	_pressButton = -1;
	if (button >= 0) {
		if (_holdFired || !_buttons[button].contains(p))
			return kNone;
		return button == 0 ? kSaveDialog : button == 1 ? kLoadDialog : kTray;
	}
	// While a page of text shows, a tap turns the page
	if (_textMode) {
		_continueFrames = 6;
		return kNone;
	}
	if (_picBox.contains(p)) {
		// (taps map to the picture as drawn: with or without its frame)
		const Common::String frame = ConfMan.get("neargrog_nes_frame");
		Common::Rect src = _map->picture, box = _picBox;
		if (_look == kLookDark || frame.empty() || frame == "none") {
			src = Common::Rect(src.left + 8, src.top + 8, src.right - 9, src.bottom - 8);
		} else if (frame == "thin") {
			src = Common::Rect(src.left + 8, src.top + 8, src.right - 9, src.bottom - 8);
			box.grow(-_picBox.width() / 50);
		}
		steerTo(toNes(box, src, CLIP<int16>(p.x, box.left, box.right - 1) == p.x && CLIP<int16>(p.y, box.top, box.bottom - 1) == p.y ? p : Common::Point(CLIP<int16>(p.x, box.left, box.right - 1), CLIP<int16>(p.y, box.top, box.bottom - 1))));
		return kNone;
	}
	for (uint i = 0; i < _verbCells.size(); i++)
		if (_verbCells[i].contains(p)) {
			steerTo(itemPoint(_map->verbs[i]));
			return kNone;
		}
	if (_goodsBox.contains(p)) {
		// Two columns of rows: 4 and 3
		const Common::Rect in(_goodsBox.left, _goodsBox.top + 30, _goodsBox.right, _goodsBox.bottom - 6);
		if (in.contains(p)) {
			const int c = (p.x - in.left) * 2 / MAX<int>(1, in.width());
			const int r = (p.y - in.top) * 4 / MAX<int>(1, in.height());
			const int idx = c * 4 + r;
			if (idx < kGoodsRows)
				steerTo(Common::Point(kGoodsMarkerCol * 8 + 4, rowY(5 + idx * 2) + 4));
		}
		return kNone;
	}
	if (_moveBox.contains(p)) {
		const Common::Rect in(_moveBox.left + 6, _moveBox.top + 30, _moveBox.right - 6, _moveBox.bottom - 6);
		if (in.contains(p))
			steerTo(toNes(in, _map->moveBox, p));
		return kNone;
	}
	if (_textBox.contains(p))
		return kNone;
	return kNone;
}

void NesUI::scroll(int lines) {
	_scroll = CLIP<int>(_scroll + lines, 0, MAX<int>(0, (int)_history.size() - 1));
}

// Drawing

const Graphics::Font *NesUI::font(int size, bool bold, bool serif) {
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

void NesUI::text(const Common::String &s, int x, int y, int w, uint32 col, const Graphics::Font *f, bool center) {
	if (f)
		f->drawString(&_out, s, x, y, w, col, center ? Graphics::kTextAlignCenter : Graphics::kTextAlignLeft);
}

void NesUI::roundRect(const Common::Rect &r, int radius, uint32 fill, uint32 edge) {
	for (int y = r.top; y < r.bottom; y++) {
		int inset = 0;
		const int dy = y < r.top + radius ? r.top + radius - y : (y >= r.bottom - radius ? y - (r.bottom - radius - 1) : 0);
		if (dy > 0)
			while (inset < radius && (radius - inset) * (radius - inset) + dy * dy > radius * radius)
				inset++;
		_out.hLine(r.left + inset, y, r.right - 1 - inset, fill);
		_out.setPixel(r.left + inset, y, edge);
		_out.setPixel(r.right - 1 - inset, y, edge);
	}
	_out.hLine(r.left + radius, r.top, r.right - 1 - radius, edge);
	_out.hLine(r.left + radius, r.bottom - 1, r.right - 1 - radius, edge);
}

void NesUI::blitNes(const byte *src, const Common::Rect &from, const Common::Rect &to) {
	const int x0 = MAX<int>(0, to.left), x1 = MIN<int>(_w, to.right);
	if (x1 <= x0)
		return;
	int sxs[1600];
	for (int x = x0; x < x1 && x - x0 < 1600; x++)
		sxs[x - x0] = CLIP<int>(from.left + (x - to.left) * from.width() / MAX<int>(1, to.width()), 0, 255);
	for (int y = MAX<int>(0, to.top); y < MIN<int>(_h, to.bottom); y++) {
		const int sy = CLIP<int>(from.top + (y - to.top) * from.height() / MAX<int>(1, to.height()), 0, 239);
		const byte *row = src + sy * 256;
		if (_format.bytesPerPixel == 4) {
			uint32 *d = (uint32 *)_out.getBasePtr(x0, y);
			for (int x = x0; x < x1; x++)
				*d++ = _pal[row[sxs[x - x0]] & 63];
		} else {
			uint16 *d = (uint16 *)_out.getBasePtr(x0, y);
			for (int x = x0; x < x1; x++)
				*d++ = (uint16)_pal[row[sxs[x - x0]] & 63];
		}
	}
}

// A letter in the game's own shape: its ink is whatever isn't the paper
// (the colour the blank tile is filled with)
void NesUI::drawGlyph(byte tile, int x, int y, int scale, uint32 fg, uint32 bg) {
	const uint16 table = _nes->bgPatterns();
	const uint16 blank = table + _map->blankTile * 16;
	const uint8 paper = ((_nes->ppuPeek(blank) >> 7) & 1) | (((_nes->ppuPeek(blank + 8) >> 7) & 1) << 1);
	const uint16 base = table + tile * 16;
	for (int r = 0; r < 8; r++) {
		const uint8 lo = _nes->ppuPeek(base + r), hi = _nes->ppuPeek(base + r + 8);
		for (int c = 0; c < 8; c++) {
			const uint8 v = ((lo >> (7 - c)) & 1) | (((hi >> (7 - c)) & 1) << 1);
			if (v != paper)
				_out.fillRect(Common::Rect(x + c * scale, y + r * scale, x + (c + 1) * scale, y + (r + 1) * scale), fg);
		}
	}
	(void)bg;
}

// Words in the game's own letters (capitals, digits and a few signs)
void NesUI::nesText(const char *s, int x, int y, int scale, uint32 col) {
	for (int k = 0; s[k]; k++, x += 8 * scale) {
		const char ch = s[k];
		if (ch >= 'A' && ch <= 'Z')
			drawGlyph(0xB1 + ch - 'A', x, y, scale, col, 0);
		else if (ch == '.')
			drawGlyph(0xA1, x, y, scale, col, 0);
	}
}

void NesUI::drawHoldRing(const Common::Rect &r, float progress) {
	const uint32 col = _look == kLookDark ? rgb(60, 120, 200) : _pal[_textFg];
	const int len = (int)(progress * r.width());
	_out.fillRect(Common::Rect(r.left, r.bottom - 6, r.left + len, r.bottom), col);
}

void NesUI::drawButtons() {
	static const char *const kLabels[3] = { "SAVE", "LOAD", "MENU" };
	const bool dark = _look == kLookDark;
	const uint32 panel = dark ? rgb(40, 40, 48) : _pal[_textBg], ink = dark ? rgb(255, 255, 255) : _pal[_textFg];
	for (int i = 0; i < 3; i++) {
		const Common::Rect &b = _buttons[i];
		roundRect(b, dark ? 24 : 6, panel, dark ? rgb(200, 200, 210) : ink);
		if (dark) {
			const Graphics::Font *f = font(16, true, false);
			text(kLabels[i], b.left, b.top + (b.height() - (f ? f->getFontHeight() : 16)) / 2, b.width(), ink, f, true);
		} else {
			const int sc = 3, w = 4 * 8 * sc;
			nesText(kLabels[i], b.left + (b.width() - w) / 2, b.top + (b.height() - 8 * sc) / 2, sc, ink);
		}
		if (_pressButton == i && !_holdFired && i < 2)
			drawHoldRing(b, MIN(1.0f, (g_system->getMillis() - _pressTime) / 500.0f));
	}
}

void NesUI::drawVerbs() {
	const bool dark = _look == kLookDark;
	const byte *src = _textMode ? _snapshot : _nes->screen();
	for (uint i = 0; i < _verbCells.size(); i++) {
		const Common::Rect &c = _verbCells[i];
		const NesItem &it = _map->verbs[i];
		if (dark) {
			// "EXAM." -> "Exam.", "MEMO <" -> "Memo <"
			Common::String label(it.name);
			for (uint k = 1; k < label.size(); k++)
				if (label[k] >= 'A' && label[k] <= 'Z' && label[k - 1] != ' ')
					label.setChar(label[k] - 'A' + 'a', k);
			roundRect(c, 16, rgb(40, 40, 48), rgb(200, 200, 210));
			const Graphics::Font *f = font(_portrait ? 20 : 18, true, false);
			text(label, c.left, c.top + (c.height() - (f ? f->getFontHeight() : 16)) / 2, c.width(), rgb(255, 255, 255), f, true);
			continue;
		}
		// The game's own label, from its screen, on its panel colour
		const Common::Rect from = itemRect(it);
		roundRect(c, 6, _pal[_verbPaper], _pal[_textFg]);
		int sc = MIN<int>(3, (c.height() - 8) / 8);
		while (sc > 1 && from.width() * sc > c.width() - 8)
			sc--;
		const Common::Rect to(c.left + (c.width() - from.width() * sc) / 2, c.top + (c.height() - from.height() * sc) / 2,
			c.left + (c.width() - from.width() * sc) / 2 + from.width() * sc, c.top + (c.height() - from.height() * sc) / 2 + from.height() * sc);
		blitNes(src, from, to);
	}
}

void NesUI::drawText() {
	const bool dark = _look == kLookDark;
	const Common::Rect &b = _textBox;
	if (dark)
		roundRect(b, 14, rgb(30, 28, 24), rgb(110, 100, 80));
	else
		roundRect(b, 6, _pal[_textBg], _pal[_textFg]);
	// What to show: the history and the page being written
	Common::Array<Common::Array<byte> > lines = _history;
	for (uint i = 0; i < _page.size(); i++)
		if (!_page[i].empty())
			lines.push_back(_page[i]);
	if (dark) {
		// Re-wrapped in the serif font
		const Graphics::Font *f = font(_portrait ? 20 : 18, false, true);
		const int lh = f ? f->getFontHeight() + 6 : 24;
		Common::Array<Common::String> out;
		Common::String para;
		for (uint i = 0; i <= lines.size(); i++) {
			const bool end = i == lines.size() || lines[i].empty();
			if (!end) {
				if (!para.empty())
					para += ' ';
				para += decode(lines[i]);
				continue;
			}
			if (!para.empty() && f) {
				// sentence case
				Common::String s;
				bool cap = true;
				for (uint k = 0; k < para.size(); k++) {
					char ch = para[k];
					if (ch >= 'A' && ch <= 'Z') {
						if (!cap)
							ch = ch - 'A' + 'a';
						cap = false;
					}
					if (ch == '.' || ch == '!' || ch == '?')
						cap = true;
					s += ch;
				}
				// "I" stays a capital
				for (uint k = 1; k + 1 < s.size(); k++)
					if (s[k] == 'i' && s[k - 1] == ' ' && (s[k + 1] == ' ' || s[k + 1] == '\''))
						s.setChar('I', k);
				Common::Array<Common::String> wrapped;
				f->wordWrapText(s, b.width() - 40, wrapped);
				for (uint k = 0; k < wrapped.size(); k++)
					out.push_back(wrapped[k]);
				out.push_back("");
			}
			para.clear();
		}
		while (!out.empty() && out.back().empty())
			out.pop_back();
		const int n = (b.height() - 20) / lh;
		const int last = MAX<int>(0, (int)out.size() - _scroll);
		const int first = MAX<int>(0, last - n);
		int y = b.top + 12;
		for (int i = first; i < last; i++, y += lh)
			text(out[i], b.left + 14, y, b.width() - 40, rgb(240, 232, 210), f);
		return;
	}
	// The game's own letters
	int sc = MIN(3, (b.width() - 24) / (26 * 8));
	sc = MAX(sc, 2);
	const int lh = 8 * sc + 8 * sc / 3;
	const int n = (b.height() - 16) / lh;
	const int last = MAX<int>(0, (int)lines.size() - _scroll);
	const int first = MAX<int>(0, last - n);
	int y = b.top + 10;
	for (int i = first; i < last; i++, y += lh) {
		for (uint k = 0; k < lines[i].size(); k++)
			if (!isBlank(lines[i][k]))
				drawGlyph(lines[i][k], b.left + 12 + k * 8 * sc, y, sc, _pal[_textFg], _pal[_textBg]);
		_out.hLine(b.left + 8, y + 8 * sc + 2, b.right - 9, _pal[_textFg]);
	}
	if (_textMode) // the game waits: a tap turns the page
		nesText("TAP", b.right - 8 * 2 * 3 - 12, b.bottom - 8 * 2 - 8, 2, _pal[_textFg]);
}

void NesUI::drawShelf() {
	const bool dark = _look == kLookDark;
	const byte *src = _textMode ? _snapshot : _nes->screen();
	const Common::Rect &g = _goodsBox;
	if (dark)
		roundRect(g, 18, rgb(34, 34, 40), rgb(150, 150, 160));
	else
		roundRect(g, 6, _pal[_textBg], _pal[_textFg]);
	if (dark)
		text("INVENTORY", g.left + 14, g.top + 8, g.width() - 20, rgb(200, 200, 210), font(15, true, false));
	else
		nesText(_map->goodsTitle, g.left + 12, g.top + 8, 2, _pal[_textFg]);
	const Common::Rect in(g.left, g.top + 30, g.right, g.bottom - 6);
	const int cw = in.width() / 2, rh = in.height() / 4;
	for (int idx = 0; idx < kGoodsRows; idx++) {
		const int c = idx / 4, r = idx % 4;
		const Common::Rect cell(in.left + c * cw + 8, in.top + r * rh, in.left + (c + 1) * cw - 8, in.top + (r + 1) * rh);
		const int nr = 5 + idx * 2;
		if (dark) {
			Common::Array<byte> row;
			for (int k = 1; k < kGoodsCols; k++)
				row.push_back(_nes->ppuPeek(0x2000 + nr * 32 + kGoodsMarkerCol + k));
			Common::String name = decode(row);
			name.trim();
			name.toLowercase();
			_out.hLine(cell.left, cell.bottom - 2, cell.right, rgb(70, 70, 80));
			text(name, cell.left + 4, cell.top + 4, cell.width() - 8, rgb(230, 230, 235), font(15, false, false));
			continue;
		}
		const Common::Rect from(kGoodsMarkerCol * 8, rowY(nr), (kGoodsMarkerCol + kGoodsCols) * 8, rowY(nr) + 8);
		int sc = 2;
		while (sc > 1 && from.width() * sc > cell.width())
			sc--;
		blitNes(src, from, Common::Rect(cell.left, cell.top + 2, cell.left + from.width() * sc, cell.top + 2 + 8 * sc));
	}
	// The exits: the game's own box, from its screen
	const Common::Rect &m = _moveBox;
	if (dark)
		roundRect(m, 18, rgb(34, 34, 40), rgb(150, 150, 160));
	else
		roundRect(m, 6, _pal[_textBg], _pal[_textFg]);
	if (dark)
		text("EXITS", m.left + 12, m.top + 8, m.width() - 20, rgb(200, 200, 210), font(15, true, false));
	else
		nesText("MOVE", m.left + 12, m.top + 8, 2, _pal[_textFg]);
	const Common::Rect min(m.left + 6, m.top + 30, m.right - 6, m.bottom - 6);
	const Common::Rect &mb = _map->moveBox;
	const int sc = MIN(min.width() / mb.width(), min.height() / mb.height());
	if (sc > 0) {
		const Common::Rect to(min.left + (min.width() - mb.width() * sc) / 2, min.top + (min.height() - mb.height() * sc) / 2,
			min.left + (min.width() - mb.width() * sc) / 2 + mb.width() * sc, min.top + (min.height() - mb.height() * sc) / 2 + mb.height() * sc);
		blitNes(src, mb, to);
	}
}

uint32 NesUI::signature() const {
	uint32 h = 2166136261u;
	const byte *scr = _nes->screen();
	for (int i = 0; i < 256 * 240; i += 4)
		h = (h ^ (scr[i] | scr[i + 1] << 8 | scr[i + 2] << 16 | scr[i + 3] << 24)) * 16777619u;
	uint32 pageLen = 0;
	for (uint i = 0; i < _page.size(); i++)
		pageLen += _page[i].size();
	h ^= Common::hashit(ConfMan.get("neargrog_nes_frame").c_str());
	h ^= _history.size() * 7919 + pageLen * 31 + _scroll * 131 + (_pressButton + 1) * 977 + _holdFired * 3 + _look * 5 + _textMode * 11;
	if (_pressButton >= 0 && !_holdFired)
		h ^= g_system->getMillis(); // the hold ring fills
	return h;
}

bool NesUI::draw() {
	const uint32 sig = signature();
	if (sig == _lastSignature && _drawn)
		return false;
	_lastSignature = sig;
	_drawn = true;
	const bool dark = _look == kLookDark;
	_out.fillRect(Common::Rect(_w, _h), dark ? rgb(12, 12, 16) : rgb(0, 0, 0));
	// The picture: alone (bigger) in Dark and by default in the game's look;
	// with a thin strip of the game's own frame, or the whole frame, if
	// chosen (neargrog_nes_frame: none / thin / full)
	const Common::Rect inner(_map->picture.left + 8, _map->picture.top + 8, _map->picture.right - 9, _map->picture.bottom - 8);
	const Common::String frame = ConfMan.get("neargrog_nes_frame");
	if (dark || frame.empty() || frame == "none") {
		blitNes(_nes->screen(), inner, _picBox);
	} else if (frame == "thin") {
		blitNes(_nes->screen(), _map->picture, _picBox);
		Common::Rect in = _picBox;
		in.grow(-_picBox.width() / 50);
		blitNes(_nes->screen(), inner, in);
	} else {
		blitNes(_nes->screen(), _map->picture, _picBox);
	}
	drawButtons();
	drawVerbs();
	drawText();
	drawShelf();
	return true;
}

} // End of namespace NesVenture
