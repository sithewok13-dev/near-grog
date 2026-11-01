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

#include <Foundation/NSArray.h>
#include <Foundation/NSURL.h>
#include <Foundation/NSFileManager.h>
#include <Foundation/NSBundle.h>
#include <Foundation/NSPathUtilities.h>

#include "backends/platform/ios7/ios7_common.h"
#include "common/str.h"
#include "common/config-manager.h"

Common::String iOS7_getDocumentsDir() {
#if TARGET_OS_IOS
	NSURL *url = [[[NSFileManager defaultManager] URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask] firstObject];
#else
	NSURL *url = [[[NSFileManager defaultManager] URLsForDirectory:NSCachesDirectory inDomains:NSUserDomainMask] firstObject];
#endif
	return Common::String([url fileSystemRepresentation]);
}

// Near-Grog: Grim Fandango and Escape from Monkey Island have their own touch
// overlay (ios7_neargrog_touch.mm), so Apple's gamepad stays off there unless
// the swipe that shows and hides it turns it on (a setting of their own)
static bool nearGrogGamepadGame() {
	return ConfMan.getActiveDomain() != nullptr && ConfMan.get("engineid") == "grim";
}

const char *iOS7_gamepadSetting() {
	return nearGrogGamepadGame() ? "neargrog_grim_gamepad" : "gamepad_controller";
}

bool iOS7_gamepadWanted() {
	if (nearGrogGamepadGame())
		return ConfMan.hasKey("neargrog_grim_gamepad") && ConfMan.getBool("neargrog_grim_gamepad");
	return ConfMan.getBool("gamepad_controller");
}

// Near-Grog: the build, as the house style's version readout shows it:
// "v<CFBundleShortVersionString>", plus " (<CFBundleVersion>)" when different
Common::String iOS7_getBuildVersion() {
	NSDictionary *info = [[NSBundle mainBundle] infoDictionary];
	NSString *shortVersion = info[@"CFBundleShortVersionString"];
	NSString *build = info[@"CFBundleVersion"];
	Common::String v = Common::String::format("v%s", shortVersion ? [shortVersion UTF8String] : "?");
	if (build && ![build isEqualToString:shortVersion])
		v += Common::String::format(" (%s)", [build UTF8String]);
	return v;
}

Common::String iOS7_getAppBundleDir() {
	NSString *bundlePath = [[NSBundle mainBundle] resourcePath];
	if (bundlePath == nil)
		return Common::String();
	return Common::String([bundlePath fileSystemRepresentation]);
}
