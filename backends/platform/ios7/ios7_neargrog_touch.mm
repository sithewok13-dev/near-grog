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
#define NG_RUN_MARKER_UP 100.0    // JK: the run marker, this far above the stick
#define NG_RUN_MARKER_RADIUS 18.0
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
	int _stickX, _stickY;      // last sent, JOYAXIS units
	bool _runFromStick;
	CAShapeLayer *_stickBase, *_stickKnob, *_runMarker;
	CATextLayer *_runText;
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
	_runMarker = [CAShapeLayer layer];
	_runMarker.fillColor = [UIColor colorWithWhite:0 alpha:0.3].CGColor;
	_runMarker.strokeColor = [UIColor colorWithWhite:1 alpha:0.6].CGColor;
	_runMarker.lineWidth = 1.5;
	_runText = [CATextLayer layer];
	_runText.string = @"RUN";
	_runText.alignmentMode = kCAAlignmentCenter;
	_runText.foregroundColor = [UIColor whiteColor].CGColor;
	_runText.contentsScale = scale;
	_runText.fontSize = 9;
	for (CALayer *l in @[_stickBase, _stickKnob, _runMarker, _runText]) {
		l.hidden = YES;
		[self.layer addSublayer:l];
	}
	self.hidden = YES;
	return self;
}

- (bool)shows:(int)i {
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
	if ([self buttonAt:p] >= 0 || p.x < self.bounds.size.width * NG_STICK_ZONE)
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
	if (x != _stickX) {
		sendEvent(InternalEvent(kInputJoystickAxisMotion, Common::JOYSTICK_AXIS_LEFT_STICK_X, x));
		_stickX = x;
	}
	if (y != _stickY) {
		sendEvent(InternalEvent(kInputJoystickAxisMotion, Common::JOYSTICK_AXIS_LEFT_STICK_Y, y));
		_stickY = y;
	}
	if (run != _runFromStick) {
		// RUN held by the button as well keeps it held
		if (_buttons[kBtnRun].touches == 0)
			sendEvent(InternalEvent(run ? kInputJoystickButtonDown : kInputJoystickButtonUp, Common::JOYSTICK_BUTTON_RIGHT_SHOULDER, 0));
		_runFromStick = run;
	}
}

- (void)moveStickTo:(CGPoint)p {
	const CGPoint marker = CGPointMake(_stickOrigin.x, _stickOrigin.y - NG_RUN_MARKER_UP);
	const bool onMarker = hypot(p.x - marker.x, p.y - marker.y) <= NG_RUN_MARKER_RADIUS + NG_TOUCH_SLOP;
	double dx = p.x - _stickOrigin.x, dy = p.y - _stickOrigin.y;
	const double mag = hypot(dx, dy) / NG_STICK_RADIUS;
	int x = 0, y = 0;
	if (onMarker) {
		y = -Common::JOYAXIS_MAX; // straight ahead, running
	} else if (mag >= NG_STICK_DEADZONE) {
		// Grim takes a direction once an axis is past half way: past the
		// dead zone is half way, the edge of the stick is all the way
		const double full = NG_STICK_RADIUS * 0.6;
		x = (int)(CLIP(dx / full, -1.0, 1.0) * Common::JOYAXIS_MAX);
		y = (int)(CLIP(dy / full, -1.0, 1.0) * Common::JOYAXIS_MAX);
	}
	[self setStickX:x y:y run:onMarker];

	// The knob follows the thumb, up to the edge
	if (mag > 1.0) {
		dx /= mag;
		dy /= mag;
	}
	[CATransaction begin];
	[CATransaction setDisableActions:YES];
	_stickKnob.path = [UIBezierPath bezierPathWithOvalInRect:CGRectMake(_stickOrigin.x + dx - 24, _stickOrigin.y + dy - 24, 48, 48)].CGPath;
	_runMarker.fillColor = [UIColor colorWithWhite:(onMarker ? 1 : 0) alpha:0.35].CGColor;
	[CATransaction commit];
}

- (void)showStick:(bool)show {
	[CATransaction begin];
	[CATransaction setDisableActions:YES];
	if (show) {
		const CGPoint o = _stickOrigin;
		_stickBase.path = [UIBezierPath bezierPathWithOvalInRect:CGRectMake(o.x - NG_STICK_RADIUS, o.y - NG_STICK_RADIUS, NG_STICK_RADIUS * 2, NG_STICK_RADIUS * 2)].CGPath;
		const CGFloat my = o.y - NG_RUN_MARKER_UP;
		_runMarker.path = [UIBezierPath bezierPathWithOvalInRect:CGRectMake(o.x - NG_RUN_MARKER_RADIUS, my - NG_RUN_MARKER_RADIUS, NG_RUN_MARKER_RADIUS * 2, NG_RUN_MARKER_RADIUS * 2)].CGPath;
		_runText.frame = CGRectMake(o.x - NG_RUN_MARKER_RADIUS, my - 6, NG_RUN_MARKER_RADIUS * 2, 12);
	}
	for (CALayer *l in @[_stickBase, _stickKnob, _runMarker, _runText])
		l.hidden = !show;
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
						sendEvent(InternalEvent(kInputJoystickButtonDown, b.joy, 0));
						sendEvent(InternalEvent(kInputJoystickButtonUp, b.joy, 0));
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
