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

#ifndef BROWSER_DIALOG_H
#define BROWSER_DIALOG_H

#include "gui/dialog.h"
#include "common/fs.h"
#include "common/str.h"

namespace GUI {

class ListWidget;
class EditTextWidget;
class CheckboxWidget;
class CommandSender;

class BrowserDialog : public Dialog {
public:
	BrowserDialog(const Common::U32String &title, bool dirBrowser);

	int runModal() override;
	void open() override;
	void handleCommand(CommandSender *sender, uint32 cmd, uint32 data) override;

	const Common::FSNode	&getResult() { return _choice; }

	/**
	 * Near-Grog: let a .zip be chosen as well. It is unzipped into a folder of
	 * the same name beside it, and the folder inside that holds a game
	 * ScummVM recognises becomes the choice.
	 */
	void setUnzipGames(bool unzip) { _unzipGames = unzip; }
	/** Unzips zip beside it (once) and finds the folder with the game. */
	static bool unzipGame(const Common::FSNode &zip, Common::FSNode &result);

protected:
	ListWidget		*_fileList;
	EditTextWidget	*_currentPath;
	Common::FSNode	_node;
	Common::FSList	_nodeContent;

	bool _showHidden;
	CheckboxWidget *_showHiddenWidget;

	Common::FSNode		_choice;
	Common::U32String	_title;
	bool				_isDirBrowser;
	bool				_unzipGames = false;

	bool isZip(const Common::FSNode &node) const;
	bool chooseZip(const Common::FSNode &zip);

	void updateListing();
};

} // End of namespace GUI

#endif
