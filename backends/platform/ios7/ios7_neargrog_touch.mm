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

// Near-Grog: touch controls for the Grim engine's games. See the header.

// Disable symbol overrides so that we can use system headers.
#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include "backends/platform/ios7/ios7_neargrog_touch.h"
#include "backends/platform/ios7/ios7_video.h"
#include "backends/platform/ios7/ios7_app_delegate.h"
#include "common/events.h"
#include "common/util.h"
#include <math.h>
#include <string.h>

#if TARGET_OS_IOS

#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>

// The house style's numbers (Serious Sam's ios/IOSTouch.m, Jedi Knight's run marker)
#define NG_STICK_ZONE 0.42
#define NG_STICK_RADIUS 60.0
#define NG_STICK_DEADZONE 0.30    // JK's
// Jedi Knight's run marker (iosTouch.m IOSTOUCH_RUN_*): a small circle with
// two chevrons, this far above the stick, joined to its ring by a line; a
// thumb pushed past the ring towards it runs
#define NG_RUN_MARKER_UP 100.0
#define NG_RUN_MARKER_MIN_UP 76.0
#define NG_RUN_MARKER_RADIUS 15.0
#define NG_RUN_ON 70.0
#define NG_RUN_OFF 60.0
#define NG_RUN_CONE 30.0
#define NG_RUN_CONE_OFF 40.0
#define NG_STICK_RUN 0.9         // pushed this far out (of the radius), any way: running
#define NG_IDLE_ALPHA 0.65
#define NG_TOUCH_SLOP 6.0
#define NG_CLUSTER_GAP 30.0
#define NG_QUICK_HOLD 0.3
#define NG_MENU_HOLD 0.45
#define NG_TAP_SLIDE 10.0         // a touch that slid further isn't a tap
#define NG_TAP_TIME 0.3

enum NGKind {
	kNGHold,     // holds its joystick button while touched
	kNGStep,     // a tap: one step (a trigger pulled and let go) as it lifts
	kNGQuickSave,
	kNGQuickLoad,
	kNGMenu      // a tap: the game's menu; held: the Near-Grog tray
};

enum {
	kBtnUse, kBtnLook, kBtnGet, kBtnBag, kBtnRun,
	kBtnNext, kBtnPrev,
	kBtnQuickSave, kBtnQuickLoad, kBtnMenu,
	kBtnCount
};

struct NGButton {
	const char *label;
	NGKind kind;
	int joy;          // kNGHold: Common::JOYSTICK_BUTTON_*; kNGStep: Common::JOYSTICK_AXIS_*
	CGFloat radius;
	bool monkeyOnly;
	CGFloat x, y;
	int touches;      // fingers on it
	double tDown;     // holds: when the finger landed
	bool fired;       // holds: went off already
	bool cancelled;   // holds: slid off, so never
	CAShapeLayer *disc;
	CATextLayer *text;
	CAShapeLayer *ring;
};

static NGButton _buttons[kBtnCount] = {
	{ "USE",          kNGHold,      Common::JOYSTICK_BUTTON_A,              42, false },
	{ "LOOK",         kNGHold,      Common::JOYSTICK_BUTTON_X,              29, false },
	{ "GET",          kNGHold,      Common::JOYSTICK_BUTTON_B,              29, false },
	{ "BAG",          kNGHold,      Common::JOYSTICK_BUTTON_Y,              31, false },
	{ "RUN",          kNGHold,      Common::JOYSTICK_BUTTON_RIGHT_SHOULDER, 28, false },
	{ "NEXT",         kNGStep,      Common::JOYSTICK_AXIS_RIGHT_TRIGGER,    22, true },
	{ "PREV",         kNGStep,      Common::JOYSTICK_AXIS_LEFT_TRIGGER,     22, true },
	{ "QUICK\nSAVE",  kNGQuickSave, 0,                                      22, false },
	{ "QUICK\nLOAD",  kNGQuickLoad, 0,                                      22, false },
	{ "MENU",         kNGMenu,      Common::JOYSTICK_BUTTON_GUIDE,          22, false },
};

enum { kRoleNone = -1, kRoleStick = 100 };

struct NGTouch {
	UITouch *touch;   // not retained: only compared
	int role;         // a button, kRoleStick or kRoleNone
	CGPoint start;
	double tStart;
	bool slid;
};

static int _mode = kNearGrogTouchHidden;
static bool _guiShown = false;

@interface NearGrogTouchOverlay : UIView
- (void)refresh;
@end

static NearGrogTouchOverlay *_overlay = nil;

static double nowSeconds() {
	return CACurrentMediaTime();
}

static void sendEvent(const InternalEvent &e) {
	[[iOS7AppDelegate iPhoneView] addEvent:e];
}

@implementation NearGrogTouchOverlay {
	NGTouch _touches[10];
	bool _stickOn;
	CGPoint _stickOrigin;
	int _stickX, _stickY;      // last set, JOYAXIS units
	bool _padDown[4];          // D-pad up, down, left, right held by the stick
	bool _runFromStick;
	CAShapeLayer *_stickBase, *_stickKnob, *_runMarker, *_runChevrons, *_runLink;
	bool _hasMarker;          // there was room for the run marker this time
	CGPoint _markerPos;
	double _markerDeg;        // its direction from the stick (90: straight up)
	bool _markerRunning;      // the thumb is on it
	int _runLit;              // what it shows (-1: not yet)
	CADisplayLink *_tick;
	bool _visible;
}

- (instancetype)initWithFrame:(CGRect)frame {
	self = [super initWithFrame:frame];
	if (!self)
		return nil;
	self.multipleTouchEnabled = YES;
	self.backgroundColor = [UIColor clearColor];
	self.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
	for (int i = 0; i < 10; i++)
		_touches[i].touch = nil;
	const CGFloat scale = [UIScreen mainScreen].scale;

	for (int i = 0; i < kBtnCount; i++) {
		NGButton &b = _buttons[i];
		b.disc = [CAShapeLayer layer];
		b.disc.fillColor = [UIColor colorWithWhite:0 alpha:0.35].CGColor;
		b.disc.strokeColor = [UIColor colorWithWhite:1 alpha:0.85].CGColor;
		b.disc.lineWidth = 2;
		[self.layer addSublayer:b.disc];

		b.ring = [CAShapeLayer layer];
		b.ring.fillColor = nil;
		b.ring.strokeColor = [UIColor colorWithRed:1 green:0.85 blue:0.3 alpha:1].CGColor;
		b.ring.lineWidth = 4;
		b.ring.strokeEnd = 0;
		[self.layer addSublayer:b.ring];

		b.text = [CATextLayer layer];
		b.text.string = [NSString stringWithUTF8String:b.label];
		b.text.alignmentMode = kCAAlignmentCenter;
		b.text.foregroundColor = [UIColor whiteColor].CGColor;
		b.text.contentsScale = scale;
		b.text.wrapped = YES;
		UIFont *font = [UIFont boldSystemFontOfSize:(b.radius >= 40 ? 15 : (strchr(b.label, '\n') ? 8.5 : 11))];
		b.text.font = (__bridge CFTypeRef)font.fontName;
		b.text.fontSize = font.pointSize;
		[self.layer addSublayer:b.text];
	}

	_stickBase = [CAShapeLayer layer];
	_stickBase.fillColor = [UIColor colorWithWhite:0 alpha:0.25].CGColor;
	_stickBase.strokeColor = [UIColor colorWithWhite:1 alpha:0.6].CGColor;
	_stickBase.lineWidth = 2;
	_stickKnob = [CAShapeLayer layer];
	_stickKnob.fillColor = [UIColor colorWithWhite:1 alpha:0.5].CGColor;
	_runLink = [CAShapeLayer layer];
	_runLink.lineWidth = 1.5;
	_runLink.lineCap = kCALineCapRound;
	_runLink.fillColor = [UIColor clearColor].CGColor;
	_runMarker = [CAShapeLayer layer];
	_runMarker.lineWidth = 1.5;
	_runChevrons = [CAShapeLayer layer];
	_runChevrons.lineWidth = 2.0;
	_runChevrons.lineCap = kCALineCapRound;
	_runChevrons.lineJoin = kCALineJoinRound;
	_runChevrons.fillColor = [UIColor clearColor].CGColor;
	_runLit = -1;
	for (CALayer *l in @[_stickBase, _runLink, _runMarker, _runChevrons, _stickKnob]) {
		l.hidden = YES;
		[self.layer addSublayer:l];
	}
	self.hidden = YES;
	return self;
}

// The iOS keyboard is up: only MENU shows, and a tap on it closes the keyboard
- (bool)keyboardUp {
	return [[iOS7AppDelegate iPhoneView] isKeyboardShown];
}

- (bool)shows:(int)i {
	if ([self keyboardUp])
		return i == kBtnMenu;
	// No RUN button: the stick's run marker does that (owner, 2026-11-01)
	if (i == kBtnRun)
		return false;
	return !_buttons[i].monkeyOnly || _mode == kNearGrogTouchMonkey;
}

// Placed as in Serious Sam's IOSTouch_LayoutButtons
- (void)layoutSubviews {
	[super layoutSubviews];
	const CGFloat W = self.bounds.size.width, H = self.bounds.size.height;
	UIEdgeInsets in = self.safeAreaInsets;
	const CGFloat left = MAX(in.left, 8.0), right = W - MAX(in.right, 8.0);
	const CGFloat top = MAX(in.top, 8.0), bottom = H - MAX(in.bottom, 8.0);

	// USE in FIRE's corner; LOOK / GET / BAG on the arc around it
	NGButton &f = _buttons[kBtnUse];
	f.x = right - (in.right < 20.0 ? 100 : 76);
	f.y = bottom - 58;
	const int arcBtn[3] = { kBtnLook, kBtnGet, kBtnBag };
	const double arcDeg[3] = { 180.0, 135.0, 90.0 };
	for (int i = 0; i < 3; i++) {
		const double rad = arcDeg[i] * M_PI / 180.0;
		_buttons[arcBtn[i]].x = f.x + 115.0 * cos(rad);
		_buttons[arcBtn[i]].y = f.y - 115.0 * sin(rad);
	}

	// RUN in the secondary slot: up and right of USE, a cluster gap from it,
	// as low as fits on screen and clear of BAG
	{
		NGButton &a = _buttons[kBtnRun];
		const NGButton &j = _buttons[kBtnBag];
		const double D = f.radius + a.radius + NG_CLUSTER_GAP;
		a.x = f.x + D * cos(M_PI * 35.0 / 180.0);
		a.y = f.y - D * sin(M_PI * 35.0 / 180.0);
		for (int deg = 10; deg <= 85; deg++) {
			const double rad = deg * M_PI / 180.0;
			const double px = f.x + D * cos(rad), py = f.y - D * sin(rad);
			if (px + a.radius > W - 4 || py - a.radius < top + 60 || py + a.radius > bottom)
				continue;
			if (hypot(px - j.x, py - j.y) < a.radius + j.radius + 8)
				continue;
			a.x = px;
			a.y = py;
			break;
		}
	}

	// Top left: NEXT | PREV (the house style's wheel-button places)
	_buttons[kBtnNext].x = left + 24;
	_buttons[kBtnPrev].x = left + 84;
	_buttons[kBtnNext].y = _buttons[kBtnPrev].y = top + 26;
	// Top right: QUICK SAVE, QUICK LOAD, MENU, spaced well apart
	_buttons[kBtnQuickSave].x = right - 156;
	_buttons[kBtnQuickLoad].x = right - 90;
	_buttons[kBtnMenu].x = right - 24;
	_buttons[kBtnQuickSave].y = _buttons[kBtnQuickLoad].y = _buttons[kBtnMenu].y = top + 26;

	[CATransaction begin];
	[CATransaction setDisableActions:YES];
	for (int i = 0; i < kBtnCount; i++) {
		NGButton &b = _buttons[i];
		const CGRect r = CGRectMake(b.x - b.radius, b.y - b.radius, b.radius * 2, b.radius * 2);
		b.disc.path = [UIBezierPath bezierPathWithOvalInRect:r].CGPath;
		const CGFloat rr = b.radius + 4;
		b.ring.path = [UIBezierPath bezierPathWithArcCenter:CGPointMake(b.x, b.y) radius:rr
			startAngle:-M_PI_2 endAngle:-M_PI_2 + 2 * M_PI clockwise:YES].CGPath;
		const bool twoLines = strchr(b.label, '\n') != nullptr;
		const CGFloat th = b.text.fontSize * (twoLines ? 2.4 : 1.25);
		b.text.frame = CGRectMake(b.x - b.radius, b.y - th / 2, b.radius * 2, th);
	}
	[CATransaction commit];
	[self refreshLooks];
}

- (void)refreshLooks {
	[CATransaction begin];
	[CATransaction setDisableActions:YES];
	for (int i = 0; i < kBtnCount; i++) {
		NGButton &b = _buttons[i];
		const bool show = [self shows:i];
		const float alpha = b.touches > 0 ? 1.0f : NG_IDLE_ALPHA;
		b.disc.hidden = b.text.hidden = !show;
		b.ring.hidden = !show || b.kind < kNGQuickSave;
		b.disc.opacity = b.text.opacity = alpha;
		b.disc.fillColor = [UIColor colorWithWhite:(b.touches > 0 ? 1 : 0) alpha:0.35].CGColor;
	}
	[CATransaction commit];
}

// The button under a point: within its radius plus the slop, the nearest
- (int)buttonAt:(CGPoint)p {
	int best = -1;
	double bestD = 1e9;
	for (int i = 0; i < kBtnCount; i++) {
		if (![self shows:i])
			continue;
		const NGButton &b = _buttons[i];
		const double d = hypot(p.x - b.x, p.y - b.y) - b.radius;
		if (d <= NG_TOUCH_SLOP && d < bestD) {
			bestD = d;
			best = i;
		}
	}
	return best;
}

- (bool)isOnButton:(int)i at:(CGPoint)p {
	const NGButton &b = _buttons[i];
	return hypot(p.x - b.x, p.y - b.y) <= b.radius + NG_TOUCH_SLOP;
}

// Touches the overlay doesn't use go on to the game view underneath
- (UIView *)hitTest:(CGPoint)p withEvent:(UIEvent *)event {
	if (self.hidden)
		return nil;
	if ([self buttonAt:p] >= 0 || (p.x < self.bounds.size.width * NG_STICK_ZONE && ![self keyboardUp]))
		return self;
	return nil;
}

- (NGTouch *)slotFor:(UITouch *)t {
	for (int i = 0; i < 10; i++)
		if (_touches[i].touch == t)
			return &_touches[i];
	return nullptr;
}

- (void)press:(int)i down:(bool)down {
	const NGButton &b = _buttons[i];
	if (b.kind == kNGHold)
		sendEvent(InternalEvent(down ? kInputJoystickButtonDown : kInputJoystickButtonUp, b.joy, 0));
}

- (void)setStickX:(int)x y:(int)y run:(bool)run {
	// The stick presses the D-pad, which Grim's keymap turns into the arrow
	// keys: both games always walk with those. (Monkey Island ignores the
	// analog stick outside its gamepad modes, so Guybrush stood still.) Past
	// half way on an axis is that direction, as Grim reads a stick.
	_stickX = x;
	_stickY = y;
	const bool want[4] = { y < -Common::JOYAXIS_MAX / 2, y > Common::JOYAXIS_MAX / 2,
		x < -Common::JOYAXIS_MAX / 2, x > Common::JOYAXIS_MAX / 2 };
	const int pad[4] = { Common::JOYSTICK_BUTTON_DPAD_UP, Common::JOYSTICK_BUTTON_DPAD_DOWN,
		Common::JOYSTICK_BUTTON_DPAD_LEFT, Common::JOYSTICK_BUTTON_DPAD_RIGHT };
	for (int i = 0; i < 4; i++) {
		if (want[i] != _padDown[i]) {
			sendEvent(InternalEvent(want[i] ? kInputJoystickButtonDown : kInputJoystickButtonUp, pad[i], 0));
			_padDown[i] = want[i];
		}
	}
	if (run != _runFromStick) {
		// RUN held by the button as well keeps it held
		if (_buttons[kBtnRun].touches == 0)
			sendEvent(InternalEvent(run ? kInputJoystickButtonDown : kInputJoystickButtonUp, Common::JOYSTICK_BUTTON_RIGHT_SHOULDER, 0));
		_runFromStick = run;
	}
}

// Jedi Knight's run marker look: idle (dark, "^^") or lit blue while running
- (void)setRunLit:(bool)lit {
	if (_runLit == (int)lit)
		return;
	_runLit = lit;
	_runMarker.fillColor = lit ? [UIColor colorWithRed:0.47 green:0.78 blue:1.0 alpha:0.55].CGColor : [UIColor colorWithWhite:0 alpha:0.35].CGColor;
	_runMarker.strokeColor = [UIColor colorWithWhite:1 alpha:(lit ? 0.85 : 0.45)].CGColor;
	_runMarker.opacity = _runChevrons.opacity = lit ? 1.0 : NG_IDLE_ALPHA;
	_runChevrons.strokeColor = [UIColor colorWithWhite:1 alpha:(lit ? 1.0 : 0.85)].CGColor;
	_runLink.strokeColor = lit ? [UIColor colorWithRed:0.47 green:0.78 blue:1.0 alpha:0.95].CGColor : [UIColor colorWithWhite:1 alpha:0.45].CGColor;
	_runLink.opacity = lit ? 1.0 : NG_IDLE_ALPHA;
	_stickKnob.fillColor = [UIColor colorWithWhite:1 alpha:(lit ? 0.9 : 0.5)].CGColor;
}

// JK's iosTouch_StickOnRunMarker: pushed past the ring towards the marker
- (bool)onRunMarker:(CGPoint)p {
	if (!_hasMarker)
		return false;
	const double dx = p.x - _stickOrigin.x, dy = _stickOrigin.y - p.y;
	const double d = hypot(dx, dy);
	if (d < (_markerRunning ? NG_RUN_OFF : NG_RUN_ON))
		return false;
	double off = fabs(atan2(dy, dx) * 180.0 / M_PI - _markerDeg);
	if (off > 180.0)
		off = 360.0 - off;
	return off <= (_markerRunning ? NG_RUN_CONE_OFF : NG_RUN_CONE);
}

- (void)moveStickTo:(CGPoint)p {
	_markerRunning = [self onRunMarker:p];
	double dx = p.x - _stickOrigin.x, dy = p.y - _stickOrigin.y;
	const double mag = hypot(dx, dy) / NG_STICK_RADIUS;
	int x = 0, y = 0;
	if (mag >= NG_STICK_DEADZONE) {
		// Grim takes a direction once an axis is past half way: past the
		// dead zone is half way, the edge of the stick is all the way
		const double full = NG_STICK_RADIUS * 0.6;
		x = (int)(CLIP(dx / full, -1.0, 1.0) * Common::JOYAXIS_MAX);
		y = (int)(CLIP(dy / full, -1.0, 1.0) * Common::JOYAXIS_MAX);
	}
	// Running: on the marker, or the stick pushed right out in any direction
	const bool run = _markerRunning || mag >= NG_STICK_RUN;
	[self setStickX:x y:y run:run];

	// The knob follows the thumb, up to the ring -- or sits on the marker
	if (mag > 1.0) {
		dx /= mag;
		dy /= mag;
	}
	const CGPoint k = _markerRunning ? _markerPos : CGPointMake(_stickOrigin.x + dx, _stickOrigin.y + dy);
	[CATransaction begin];
	[CATransaction setDisableActions:YES];
	_stickKnob.path = [UIBezierPath bezierPathWithOvalInRect:CGRectMake(k.x - 24, k.y - 24, 48, 48)].CGPath;
	[self setRunLit:run];
	[CATransaction commit];
}

// JK's placeRunMarker: 100 pt straight above the stick or, where the top of
// the screen or a button is in the way, as near that as fits (no nearer
// than 76 pt, else no marker this time)
- (void)placeRunMarker {
	const CGPoint o = _stickOrigin;
	const CGFloat R = NG_RUN_MARKER_RADIUS;
	const CGFloat minY = MAX(self.safeAreaInsets.top, 0.0) + 4.0 + R;
	_hasMarker = false;
	_markerRunning = false;
	for (CGFloat h = NG_RUN_MARKER_UP; h >= NG_RUN_MARKER_MIN_UP; h -= 2.0) {
		const CGPoint c = CGPointMake(o.x, o.y - h);
		if (c.y < minY)
			continue;
		bool clear = true;
		for (int i = 0; i < kBtnCount && clear; i++)
			if ([self shows:i])
				clear = hypot(c.x - _buttons[i].x, c.y - _buttons[i].y) >= _buttons[i].radius + R + 4.0;
		if (!clear)
			continue;
		_hasMarker = true;
		_markerPos = c;
		_markerDeg = 90.0;
		break;
	}
	if (!_hasMarker)
		return;
	const CGPoint c = _markerPos;
	_runMarker.path = [UIBezierPath bezierPathWithOvalInRect:CGRectMake(c.x - R, c.y - R, R * 2, R * 2)].CGPath;
	UIBezierPath *chev = [UIBezierPath bezierPath];
	for (int i = 0; i < 2; i++) {
		const CGFloat tip = c.y - 5.5 + 5.5 * i; // two chevrons, 12 pt wide, 5.5 pt apart
		[chev moveToPoint:CGPointMake(c.x - 6.0, tip + 5.0)];
		[chev addLineToPoint:CGPointMake(c.x, tip)];
		[chev addLineToPoint:CGPointMake(c.x + 6.0, tip + 5.0)];
	}
	_runChevrons.path = chev.CGPath;
	UIBezierPath *link = [UIBezierPath bezierPath];
	[link moveToPoint:CGPointMake(o.x, o.y - NG_STICK_RADIUS)];
	[link addLineToPoint:CGPointMake(o.x, c.y + R)];
	_runLink.path = link.CGPath;
}

- (void)showStick:(bool)show {
	[CATransaction begin];
	[CATransaction setDisableActions:YES];
	if (show) {
		const CGPoint o = _stickOrigin;
		_stickBase.path = [UIBezierPath bezierPathWithOvalInRect:CGRectMake(o.x - NG_STICK_RADIUS, o.y - NG_STICK_RADIUS, NG_STICK_RADIUS * 2, NG_STICK_RADIUS * 2)].CGPath;
		[self placeRunMarker];
		_runLit = -1;
		[self setRunLit:false];
	}
	_stickBase.hidden = _stickKnob.hidden = !show;
	_runMarker.hidden = _runChevrons.hidden = !show || !_hasMarker;
	_runLink.hidden = !show || !_hasMarker || _markerPos.y + NG_RUN_MARKER_RADIUS >= _stickOrigin.y - NG_STICK_RADIUS - 1;
	[CATransaction commit];
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
	for (UITouch *t in touches) {
		NGTouch *s = [self slotFor:nil];
		if (!s)
			continue;
		const CGPoint p = [t locationInView:self];
		s->touch = t;
		s->start = p;
		s->tStart = nowSeconds();
		s->slid = false;
		const int i = [self buttonAt:p];
		if (i >= 0) {
			s->role = i;
			NGButton &b = _buttons[i];
			if (b.touches++ == 0) {
				[self press:i down:true];
				if (b.kind >= kNGQuickSave) {
					b.tDown = s->tStart;
					b.fired = b.cancelled = false;
					[self startTicking];
				}
			}
		} else if (!_stickOn) {
			s->role = kRoleStick;
			_stickOn = true;
			_stickOrigin = p;
			[self showStick:true];
			[self moveStickTo:p];
		} else {
			// A second finger on the left: the house style's look, which the
			// Grim games don't have
			s->role = kRoleNone;
		}
	}
	[self refreshLooks];
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
	for (UITouch *t in touches) {
		NGTouch *s = [self slotFor:t];
		if (!s)
			continue;
		const CGPoint p = [t locationInView:self];
		if (hypot(p.x - s->start.x, p.y - s->start.y) > NG_TAP_SLIDE)
			s->slid = true;
		if (s->role == kRoleStick) {
			[self moveStickTo:p];
		} else if (s->role >= 0 && s->role < kBtnCount) {
			NGButton &b = _buttons[s->role];
			// Holds: sliding off cancels for good
			if (b.kind >= kNGQuickSave && ![self isOnButton:s->role at:p])
				b.cancelled = true;
		}
	}
}

- (void)endTouches:(NSSet<UITouch *> *)touches cancelled:(bool)cancelled {
	for (UITouch *t in touches) {
		NGTouch *s = [self slotFor:t];
		if (!s)
			continue;
		const CGPoint p = [t locationInView:self];
		if (s->role == kRoleStick) {
			_stickOn = false;
			[self setStickX:0 y:0 run:false];
			[self showStick:false];
			// A quick tap in the stick's area is still a tap on the game
			if (!cancelled && !s->slid && nowSeconds() - s->tStart < NG_TAP_TIME)
				[self forwardTap:t];
		} else if (s->role >= 0 && s->role < kBtnCount) {
			NGButton &b = _buttons[s->role];
			const bool onIt = [self isOnButton:s->role at:p];
			if (--b.touches == 0) {
				[self press:s->role down:false];
				if (!cancelled && onIt) {
					if (b.kind == kNGStep) {
						sendEvent(InternalEvent(kInputJoystickAxisMotion, b.joy, Common::JOYAXIS_MAX));
						sendEvent(InternalEvent(kInputJoystickAxisMotion, b.joy, 0));
					} else if (b.kind == kNGMenu && !b.fired && !b.cancelled) {
						if ([self keyboardUp]) {
							[[iOS7AppDelegate iPhoneView] hideKeyboard];
						} else {
							sendEvent(InternalEvent(kInputJoystickButtonDown, b.joy, 0));
							sendEvent(InternalEvent(kInputJoystickButtonUp, b.joy, 0));
						}
					}
				}
				b.fired = b.cancelled = false;
				b.ring.strokeEnd = 0;
			}
		}
		s->touch = nil;
	}
	[self refreshLooks];
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
	[self endTouches:touches cancelled:false];
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
	[self endTouches:touches cancelled:true];
}

- (void)forwardTap:(UITouch *)t {
	iPhoneView *view = [iOS7AppDelegate iPhoneView];
	CGPoint p = [t locationInView:view];
	const CGFloat scale = view.contentScaleFactor;
	sendEvent(InternalEvent(kInputTouchBegan, p.x * scale, p.y * scale));
	sendEvent(InternalEvent(kInputTap, kUIViewTapSingle, 1));
}

// The holds' rings, and going off once full with the finger still down
- (void)startTicking {
	if (_tick)
		return;
	_tick = [CADisplayLink displayLinkWithTarget:self selector:@selector(tickHolds)];
	[_tick addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
}

- (void)tickHolds {
	bool any = false;
	const double now = nowSeconds();
	[CATransaction begin];
	[CATransaction setDisableActions:YES];
	// QUICK SAVE wins a tie
	const int order[3] = { kBtnQuickSave, kBtnQuickLoad, kBtnMenu };
	for (int k = 0; k < 3; k++) {
		NGButton &b = _buttons[order[k]];
		if (b.touches == 0 || b.fired || b.cancelled) {
			b.ring.strokeEnd = 0;
			continue;
		}
		any = true;
		const double hold = b.kind == kNGMenu ? NG_MENU_HOLD : NG_QUICK_HOLD;
		const double f = (now - b.tDown) / hold;
		b.ring.strokeEnd = MIN(f, 1.0);
		if (f >= 1.0) {
			b.fired = true;
			if (b.kind == kNGMenu) {
				sendEvent(InternalEvent(kInputMainMenu, 0, 0));
			} else {
				sendEvent(InternalEvent(kInputNearGrogQuick, b.kind == kNGQuickLoad ? 1 : 0, 0));
				// Never a save and a load from one press
				NGButton &other = _buttons[b.kind == kNGQuickSave ? kBtnQuickLoad : kBtnQuickSave];
				other.cancelled = true;
			}
		}
	}
	[CATransaction commit];
	if (!any) {
		[_tick invalidate];
		_tick = nil;
	}
}

// Shown only while the game is being played; everything let go when hidden
- (void)refresh {
	const bool visible = _mode != kNearGrogTouchHidden && !_guiShown && [[iOS7AppDelegate iPhoneView] isInGame];
	if (visible == _visible) {
		if (visible)
			[self setNeedsLayout];
		return;
	}
	_visible = visible;
	if (!visible) {
		for (int i = 0; i < kBtnCount; i++) {
			if (_buttons[i].touches > 0)
				[self press:i down:false];
			_buttons[i].touches = 0;
			_buttons[i].ring.strokeEnd = 0;
		}
		for (int i = 0; i < 10; i++)
			_touches[i].touch = nil;
		if (_stickOn)
			[self showStick:false];
		_stickOn = false;
		[self setStickX:0 y:0 run:false];
	}
	self.hidden = !visible;
	[[iOS7AppDelegate iPhoneView] setNearGrogOverlayShown:visible];
	[self setNeedsLayout];
	[self refreshLooks];
}

@end

static void refreshOnMain() {
	dispatch_async(dispatch_get_main_queue(), ^{
		iPhoneView *view = [iOS7AppDelegate iPhoneView];
		if (!view)
			return;
		if (!_overlay) {
			if (_mode == kNearGrogTouchHidden)
				return;
			_overlay = [[NearGrogTouchOverlay alloc] initWithFrame:view.bounds];
			[view addSubview:_overlay];
		}
		[view bringSubviewToFront:_overlay];
		[_overlay refresh];
	});
}

void iOS7_setNearGrogTouchMode(int mode) {
	if (mode == _mode)
		return;
	_mode = mode;
	refreshOnMain();
}

void iOS7_setNearGrogGuiShown(bool shown) {
	if (shown == _guiShown)
		return;
	_guiShown = shown;
	refreshOnMain();
}

#else

void iOS7_setNearGrogTouchMode(int mode) {}
void iOS7_setNearGrogGuiShown(bool shown) {}

#endif
