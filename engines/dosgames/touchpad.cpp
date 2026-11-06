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
#include "common/system.h"
#include "engines/neargrog.h"
#include "graphics/fontman.h"
#include "graphics/fonts/ttf.h"
#include "dosgames/dospc.h"
#include "dosgames/touchpad.h"

namespace DosGames {

namespace {

const int kGap = 10;
const uint32 kHoldMillis = 500;

// PC scan codes
enum {
	kScanEsc = 0x01, kScanBackspace = 0x0E, kScanY = 0x15, kScanEnter = 0x1C,
	kScanN = 0x31, kScanSpace = 0x39, kScanLeft = 0x4B, kScanRight = 0x4D
};

} // End of anonymous namespace

TouchPad::TouchPad() : _w(0), _h(0), _portrait(false), _hunting(false), _gameDirty(true), _padDirty(true), _allDirty(true),
		_pressed(-1), _pressTime(0), _holdFired(false), _keyHeld(false), _heldFrames(0), _frame(0), _downFrame(0) {
	for (int i = 0; i < 256; i++)
		_pal[i] = 0;
}

TouchPad::~TouchPad() {
	for (Common::HashMap<Common::String, Graphics::Font *>::iterator it = _fonts.begin(); it != _fonts.end(); ++it)
		delete it->_value;
	_out.free();
}

bool TouchPad::portraitScreen() {
	const Common::String o = ConfMan.get("neargrog_orientation"); // (tests)
	if (o == "portrait")
		return true;
	if (o == "landscape")
		return false;
	return g_system->getOverlayHeight() > g_system->getOverlayWidth();
}

TouchPad::Aim TouchPad::configAim() {
	return ConfMan.get("neargrog_hunt_aim") == "point" ? kAimPoint : kAimTurn;
}

void TouchPad::setup(int w, int h, bool portrait, const Graphics::PixelFormat &format) {
	releaseKey();
	_w = w;
	_h = h;
	_portrait = portrait;
	_format = format;
	_out.create(w, h, format);
	_out.fillRect(Common::Rect(w, h), rgb(10, 10, 14));
	layout();
	_gameDirty = _padDirty = _allDirty = true;
}

void TouchPad::setHunting(bool hunting) {
	if (hunting == _hunting)
		return;
	releaseKey();
	_hunting = hunting;
	layout();
	_padDirty = true;
}

void TouchPad::refresh() {
	releaseKey();
	layout();
	_padDirty = true;
}

void TouchPad::setPalette(const byte *rgbs) {
	for (int i = 0; i < 256; i++)
		_pal[i] = rgb(rgbs[i * 3], rgbs[i * 3 + 1], rgbs[i * 3 + 2]);
	_gameDirty = true;
}

// --- Layout ---

void TouchPad::layout() {
	if (_portrait) {
		// The game across the top (under the island), the keys below
		const int gh = _w * 3 / 4;
		_gameBox = Common::Rect(0, NearGrog::kPortraitTop, _w, NearGrog::kPortraitTop + gh);
		_padBox = Common::Rect(16, _gameBox.bottom + 16, _w - 16, _h - NearGrog::kHomeBar - 12);
	} else {
		// The game at the left, the full height; the keys in the rest
		const int side = NearGrog::kLandscapeSide;
		int gh = _h, gw = gh * 4 / 3;
		if (2 * side + gw + 16 + 340 > _w) {
			gw = _w - 2 * side - 16 - 340;
			gh = gw * 3 / 4;
		}
		_gameBox = Common::Rect(side, (_h - gh) / 2, side + gw, (_h - gh) / 2 + gh);
		_padBox = Common::Rect(_gameBox.right + 16, 12, _w - side, _h - NearGrog::kHomeBar);
	}
	_mapX.resize(_gameBox.width());
	for (int x = 0; x < _gameBox.width(); x++)
		_mapX[x] = x * DosPC::kScreenW / _gameBox.width();
	_mapY.resize(_gameBox.height());
	for (int y = 0; y < _gameBox.height(); y++)
		_mapY[y] = y * DosPC::kScreenH / _gameBox.height();

	_buttons.clear();
	_pressed = -1;
	// Save, Load, Menu along the top
	const Common::Rect &r = _padBox;
	const int top = _portrait ? 56 : 48;
	const int cw = (r.width() - 2 * kGap) / 3;
	static const char *const kTop[3] = { "Save", "Load", "Menu" };
	static const Action kTopAction[3] = { kSaveDialog, kLoadDialog, kTray };
	for (int i = 0; i < 3; i++)
		addButton(Common::Rect(r.left + i * (cw + kGap), r.top, r.left + i * (cw + kGap) + cw, r.top + top), kTop[i], kSmall, kTopAction[i]);
	const Common::Rect rest(r.left, r.top + top + kGap, r.right, r.bottom);
	if (_hunting)
		huntPad(rest);
	else
		keypad(rest);
}

// The digits, Yes and No, Delete and Keyboard, Enter and Continue
void TouchPad::keypad(const Common::Rect &r) {
	const int rh = (r.height() - 5 * kGap) / 6;
	const int cw = (r.width() - 2 * kGap) / 3, hw = (r.width() - kGap) / 2;
	for (int row = 0; row < 4; row++) {
		const int y = r.top + row * (rh + kGap);
		for (int col = 0; col < 3; col++) {
			const Common::Rect box(r.left + col * (cw + kGap), y, r.left + col * (cw + kGap) + cw, y + rh);
			if (row < 3) {
				const int n = row * 3 + col; // 1 to 9
				const char label[2] = { (char)('1' + n), 0 };
				addButton(box, label, kPlain, -1, 0x02 + n, '1' + n);
			} else if (col == 0) {
				addButton(box, "Yes", kPlain, -1, kScanY, 'Y');
			} else if (col == 1) {
				addButton(box, "0", kPlain, -1, 0x0B, '0');
			} else {
				addButton(box, "No", kPlain, -1, kScanN, 'N');
			}
		}
	}
	int y = r.top + 4 * (rh + kGap);
	addButton(Common::Rect(r.left, y, r.left + hw, y + rh), "Delete", kPlain, -1, kScanBackspace, 8);
	addButton(Common::Rect(r.right - hw, y, r.right, y + rh), "Keyboard", kPlain, kKeyboard);
	y += rh + kGap;
	addButton(Common::Rect(r.left, y, r.left + hw, y + rh), "Enter", kPlain, -1, kScanEnter, 13);
	addButton(Common::Rect(r.right - hw, y, r.right, y + rh), "Continue", kBlue, -1, kScanSpace, ' ');
}

// Hunting: the rifle's pad and Fire, Walk / Stop and Done
void TouchPad::huntPad(const Common::Rect &r) {
	const int bh = _portrait ? 84 : 64;
	const int hw = (r.width() - kGap) / 2;
	addButton(Common::Rect(r.left, r.bottom - bh, r.left + hw, r.bottom), "Walk / Stop", kPlain, -1, kScanEnter, 13);
	addButton(Common::Rect(r.right - hw, r.bottom - bh, r.right, r.bottom), "Done", kBlue, -1, kScanEsc, 27);
	const Common::Rect m(r.left, r.top, r.right, r.bottom - bh - kGap);
	if (configAim() == kAimTurn) {
		// The game's arrows turn the rifle a step at a time (held: on round)
		const int s = MIN<int>((m.width() - 2 * kGap) / 3, m.height());
		const int x = m.left + (m.width() - 3 * s - 2 * kGap) / 2, y = m.top + (m.height() - s) / 2;
		addArrow(Common::Rect(x, y, x + s, y + s), (float)M_PI, kScanLeft, true);
		addButton(Common::Rect(x + s + kGap, y, x + 2 * s + kGap, y + s), "Fire", kRed, -1, kScanSpace, ' ');
		_buttons.back().round = true;
		addArrow(Common::Rect(x + 2 * (s + kGap), y, x + 3 * s + 2 * kGap, y + s), 0, kScanRight, true);
	} else {
		// The game's keypad points it straight: 7 8 9 / 4 6 / 1 2 3, Fire between
		static const uint8 kScan[9] = { 0x47, 0x48, 0x49, 0x4B, 0, 0x4D, 0x4F, 0x50, 0x51 };
		static const float kAngle[9] = { -0.75f, -0.5f, -0.25f, 1.0f, 0, 0, 0.75f, 0.5f, 0.25f }; // (times pi)
		const int s = MIN((m.width() - 2 * kGap) / 3, (m.height() - 2 * kGap) / 3);
		const int x = m.left + (m.width() - 3 * s - 2 * kGap) / 2, y = m.top + (m.height() - 3 * s - 2 * kGap) / 2;
		for (int i = 0; i < 9; i++) {
			const Common::Rect box(x + (i % 3) * (s + kGap), y + (i / 3) * (s + kGap), x + (i % 3) * (s + kGap) + s, y + (i / 3) * (s + kGap) + s);
			if (i == 4) {
				addButton(box, "Fire", kRed, -1, kScanSpace, ' ');
				_buttons.back().round = true;
			} else {
				addArrow(box, kAngle[i] * (float)M_PI, kScan[i], false);
			}
		}
	}
}

void TouchPad::addButton(const Common::Rect &box, const char *label, Style style, int action, uint8 scan, uint8 ascii) {
	Button b;
	b.box = box;
	b.label = label;
	b.style = style;
	b.action = action;
	b.scan = scan;
	b.ascii = ascii;
	b.repeat = false;
	b.glyph = kNoGlyph;
	b.angle = 0;
	b.round = false;
	_buttons.push_back(b);
}

void TouchPad::addArrow(const Common::Rect &box, float angle, uint8 scan, bool repeat) {
	addButton(box, Common::String::format("Arrow%02X", scan).c_str(), kPlain, -1, scan, 0); // (the name isn't shown)
	_buttons.back().glyph = kArrow;
	_buttons.back().angle = angle;
	_buttons.back().repeat = repeat;
}

int TouchPad::buttonAt(Common::Point p) const {
	for (uint i = 0; i < _buttons.size(); i++) {
		Common::Rect b = _buttons[i].box;
		b.grow(kGap / 2); // (between two: the nearer)
		if (b.contains(p))
			return i;
	}
	return -1;
}

Common::Point TouchPad::buttonCentre(const char *label) const {
	for (uint i = 0; i < _buttons.size(); i++)
		if (_buttons[i].label == label)
			return Common::Point((_buttons[i].box.left + _buttons[i].box.right) / 2, (_buttons[i].box.top + _buttons[i].box.bottom) / 2);
	return Common::Point(-1, -1);
}

// --- Touch ---

void TouchPad::keyDown(const Button &b) {
	Key k;
	k.scan = b.scan;
	k.ascii = b.ascii;
	k.down = true;
	k.at = _frame;
	_keys.push_back(k);
	_keyHeld = true;
	_downFrame = _frame;
}

void TouchPad::keyUp(const Button &b) {
	Key k;
	k.scan = b.scan;
	k.ascii = 0;
	k.down = false;
	k.at = MAX(_frame, _downFrame + 3); // (held long enough to count)
	_keys.push_back(k);
	_keyHeld = false;
}

void TouchPad::releaseKey() {
	if (_pressed >= 0 && _keyHeld && _buttons[_pressed].action < 0)
		keyUp(_buttons[_pressed]);
	_keyHeld = false;
	_pressed = -1;
}

bool TouchPad::takeKey(Key &k) {
	if (_keys.empty() || _keys[0].at > _frame)
		return false;
	k = _keys[0];
	_keys.remove_at(0);
	return true;
}

TouchPad::Action TouchPad::press(Common::Point p) {
	releaseKey();
	const int i = buttonAt(p);
	if (i < 0)
		return kNone;
	_pressed = i;
	_pressTime = g_system->getMillis();
	_holdFired = false;
	_heldFrames = 0;
	_padDirty = true;
	if (_buttons[i].action < 0)
		keyDown(_buttons[i]);
	return kNone;
}

void TouchPad::move(Common::Point p) {
}

TouchPad::Action TouchPad::release(Common::Point p) {
	if (_pressed < 0)
		return kNone;
	const Button b = _buttons[_pressed];
	_padDirty = true;
	if (b.action < 0) {
		releaseKey();
		return kNone;
	}
	_pressed = -1;
	Common::Rect box = b.box;
	box.grow(kGap);
	if (_holdFired || !box.contains(p))
		return kNone;
	return (Action)b.action;
}

TouchPad::Action TouchPad::tick() {
	_frame++;
	if (_pressed < 0)
		return kNone;
	const Button &b = _buttons[_pressed];
	_heldFrames++;
	if (b.action < 0 && b.repeat && _heldFrames >= 15 && (_heldFrames - 15) % 5 == 0) {
		// the keyboard's own repeat: the key pressed again, still down
		Key k;
		k.scan = b.scan;
		k.ascii = b.ascii;
		k.down = true;
		k.at = _frame;
		_keys.push_back(k);
	}
	if ((b.action == kSaveDialog || b.action == kLoadDialog) && !_holdFired) {
		_padDirty = true; // (the ring goes round)
		if (g_system->getMillis() - _pressTime >= kHoldMillis) {
			_holdFired = true;
			return b.action == kSaveDialog ? kQuickSave : kQuickLoad;
		}
	}
	return kNone;
}

// --- Drawing ---

const Graphics::Font *TouchPad::font(int size) {
	const Common::String key = Common::String::format("%d", size);
	if (_fonts.contains(key) && _fonts[key])
		return _fonts[key];
	Graphics::Font *f = nullptr;
#ifdef USE_FREETYPE2
	f = Graphics::loadTTFFontFromArchive("LiberationSans-Bold.ttf", size, Graphics::kTTFSizeModeCharacter, 0, 0, Graphics::kTTFRenderModeLight);
#endif
	_fonts[key] = f;
	if (!f)
		return FontMan.getFontByUsage(Graphics::FontManager::kBigGUIFont);
	return f;
}

// A rounded box, smooth at the edges (a 1.5 pixel outline over the fill)
void TouchPad::roundRect(const Common::Rect &r, int radius, uint32 fill, uint32 edge) {
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

// A convex shape, smooth at the edges
void TouchPad::polygon(const float *vx, const float *vy, int n, uint32 col) {
	float cx = 0, cy = 0, x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
	for (int i = 0; i < n; i++) {
		cx += vx[i] / n;
		cy += vy[i] / n;
		x0 = MIN(x0, vx[i]);
		x1 = MAX(x1, vx[i]);
		y0 = MIN(y0, vy[i]);
		y1 = MAX(y1, vy[i]);
	}
	// each edge's inward normal
	float nx[8], ny[8], c[8];
	for (int i = 0; i < n; i++) {
		const int j = (i + 1) % n;
		const float ex = vx[j] - vx[i], ey = vy[j] - vy[i];
		const float len = sqrtf(ex * ex + ey * ey);
		nx[i] = -ey / len;
		ny[i] = ex / len;
		if (nx[i] * (cx - vx[i]) + ny[i] * (cy - vy[i]) < 0) {
			nx[i] = -nx[i];
			ny[i] = -ny[i];
		}
		c[i] = nx[i] * vx[i] + ny[i] * vy[i];
	}
	uint8 cr, cg, cb;
	_format.colorToRGB(col, cr, cg, cb);
	for (int y = MAX<int>(0, (int)y0 - 1); y < MIN<int>(_h, (int)y1 + 2); y++)
		for (int x = MAX<int>(0, (int)x0 - 1); x < MIN<int>(_w, (int)x1 + 2); x++) {
			const float px = x + 0.5f, py = y + 0.5f;
			float d = 1e9f;
			for (int i = 0; i < n; i++)
				d = MIN(d, nx[i] * px + ny[i] * py - c[i]);
			const float cover = CLIP(d + 0.5f, 0.0f, 1.0f);
			if (cover <= 0)
				continue;
			uint8 br, bgc, bbc;
			_format.colorToRGB(_out.getPixel(x, y), br, bgc, bbc);
			_out.setPixel(x, y, _format.RGBToColor((uint8)(br + (cr - br) * cover), (uint8)(bgc + (cg - bgc) * cover), (uint8)(bbc + (cb - bbc) * cover)));
		}
}

// An arrow (a head and a shaft) pointing the given way
void TouchPad::arrow(float cx, float cy, float size, float angle, uint32 col) {
	const float ux = cosf(angle), uy = sinf(angle), px = -uy, py = ux;
	// along the arrow / across it, in units of size
	static const float kHead[3][2] = { { 1.0f, 0 }, { 0.15f, 0.6f }, { 0.15f, -0.6f } };
	static const float kShaft[4][2] = { { 0.25f, 0.2f }, { 0.25f, -0.2f }, { -0.9f, -0.2f }, { -0.9f, 0.2f } };
	float vx[4], vy[4];
	for (int i = 0; i < 3; i++) {
		vx[i] = cx + size * (kHead[i][0] * ux + kHead[i][1] * px);
		vy[i] = cy + size * (kHead[i][0] * uy + kHead[i][1] * py);
	}
	polygon(vx, vy, 3, col);
	for (int i = 0; i < 4; i++) {
		vx[i] = cx + size * (kShaft[i][0] * ux + kShaft[i][1] * px);
		vy[i] = cy + size * (kShaft[i][0] * uy + kShaft[i][1] * py);
	}
	polygon(vx, vy, 4, col);
}

// Save / Load held: a ring round the button fills up, then the quick save
void TouchPad::drawHoldRing(const Common::Rect &r, int radius, float progress) {
	const uint32 col = rgb(60, 120, 200);
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

void TouchPad::drawPad() {
	Common::Rect bg = _padBox;
	bg.grow(8);
	bg.clip(Common::Rect(_w, _h));
	_out.fillRect(bg, rgb(10, 10, 14));
	const int radius = _portrait ? 18 : 14;
	for (uint i = 0; i < _buttons.size(); i++) {
		const Button &b = _buttons[i];
		const bool down = (int)i == _pressed;
		uint32 fill;
		switch (b.style) {
		case kBlue: fill = down ? rgb(110, 165, 235) : rgb(60, 120, 200); break;
		case kRed: fill = down ? rgb(225, 85, 70) : rgb(175, 45, 40); break;
		default: fill = down ? rgb(60, 120, 200) : rgb(40, 40, 48); break;
		}
		roundRect(b.box, b.round ? MIN(b.box.width(), b.box.height()) / 2 : radius, fill, rgb(200, 200, 210));
		const int cx = (b.box.left + b.box.right) / 2, cy = (b.box.top + b.box.bottom) / 2;
		if (b.glyph == kArrow) {
			arrow(cx, cy, MIN(b.box.width(), b.box.height()) * 0.32f, b.angle, rgb(255, 255, 255));
		} else if (!b.label.empty()) {
			const bool digit = b.label.size() == 1;
			const int size = b.style == kSmall ? (_portrait ? 20 : 16) : digit ? (_portrait ? 32 : 24) : b.round ? (_portrait ? 28 : 22) : (_portrait ? 24 : 19);
			const Graphics::Font *f = font(size);
			if (f)
				f->drawString(&_out, b.label, b.box.left, cy - f->getFontHeight() / 2, b.box.width(), rgb(255, 255, 255), Graphics::kTextAlignCenter);
		}
		if (down && !_holdFired && (b.action == kSaveDialog || b.action == kLoadDialog))
			drawHoldRing(b.box, radius, MIN(1.0f, (g_system->getMillis() - _pressTime) / (float)kHoldMillis));
	}
}

void TouchPad::drawGame(const byte *screen) {
	const int bpp = _format.bytesPerPixel;
	for (int y = 0; y < _gameBox.height(); y++) {
		const int oy = _gameBox.top + y;
		if (oy < 0 || oy >= _h)
			continue;
		const byte *row = screen + _mapY[y] * DosPC::kScreenW;
		byte *dst = (byte *)_out.getBasePtr(_gameBox.left, oy);
		if (bpp == 4) {
			uint32 *d = (uint32 *)dst;
			for (int x = 0; x < _gameBox.width(); x++)
				d[x] = _pal[row[_mapX[x]]];
		} else {
			uint16 *d = (uint16 *)dst;
			for (int x = 0; x < _gameBox.width(); x++)
				d[x] = (uint16)_pal[row[_mapX[x]]];
		}
	}
}

Common::Rect TouchPad::draw(const byte *screen) {
	Common::Rect dirty;
	bool any = false;
	if (_padDirty) {
		_padDirty = false;
		drawPad();
		dirty = _padBox;
		dirty.grow(8);
		any = true;
	}
	const uint size = DosPC::kScreenW * DosPC::kScreenH;
	if (_gameDirty || _last.size() != size || memcmp(_last.data(), screen, size)) {
		_gameDirty = false;
		_last.resize(size);
		memcpy(_last.data(), screen, size);
		drawGame(screen);
		if (any)
			dirty.extend(_gameBox);
		else
			dirty = _gameBox;
		any = true;
	}
	if (_allDirty) {
		_allDirty = false;
		return Common::Rect(_w, _h);
	}
	if (!any)
		return Common::Rect();
	dirty.clip(Common::Rect(_w, _h));
	return dirty;
}

} // End of namespace DosGames
