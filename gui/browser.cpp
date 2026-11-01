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

#include "gui/browser.h"
#include "gui/helpdialog.h"
#include "gui/gui-manager.h"
#include "gui/widgets/edittext.h"
#include "gui/widgets/list.h"

#include "common/config-manager.h"
#include "common/system.h"
#include "common/algorithm.h"
#if defined(USE_SYSDIALOGS)
#include "common/dialogs.h"
#endif

#include "common/translation.h"
#include "common/archive.h"
#include "common/compression/unzip.h"
#include "common/ptr.h"
#include "common/stream.h"
#include "engines/metaengine.h"
#include "gui/message.h"

namespace GUI {

enum {
	kChooseCmd = 'Chos',
	kGoUpCmd = 'GoUp',
	kHiddenCmd = 'Hidd',
	kPathEditedCmd = 'Path',
	kHelpCmd = 'Help',
};

/* We want to use this as a general directory selector at some point... possible uses
 * - to select the data dir for a game
 * - to select the place where save games are stored
 * - others???
 */

BrowserDialog::BrowserDialog(const Common::U32String &title, bool dirBrowser)
	: Dialog("Browser") {

	_title = title;
	_isDirBrowser = dirBrowser;
	_fileList = nullptr;
	_currentPath = nullptr;
	_showHidden = false;

	// Headline - TODO: should be customizable during creation time
	new StaticTextWidget(this, "Browser.Headline", title);
	new ButtonWidget(this, "Browser.Help", _("Help"), Common::U32String(), kHelpCmd);

	// Current path - TODO: handle long paths ?
	_currentPath = new EditTextWidget(this, "Browser.Path", Common::U32String(), Common::U32String(), 0, kPathEditedCmd);

	// Add file list
	_fileList = new ListWidget(this, "Browser.List");
	_fileList->setNumberingMode(kListNumberingOff);
	_fileList->setEditable(false);

	_backgroundType = GUI::ThemeEngine::kDialogBackgroundPlain;

	// Checkbox for the "show hidden files" state.
	_showHiddenWidget = new CheckboxWidget(this, "Browser.Hidden", _("Show hidden files"), _("Show files marked with the hidden attribute"), kHiddenCmd);

	// Buttons
	if (!g_gui.useLowResGUI())
		new ButtonWidget(this, "Browser.Up", _("Go up"), _("Go to previous directory level"), kGoUpCmd);
	else
		new ButtonWidget(this, "Browser.Up", _c("Go up", "lowres"), _("Go to previous directory level"), kGoUpCmd);
	new ButtonWidget(this, "Browser.Cancel", _("Cancel"), Common::U32String(), kCloseCmd);
	new ButtonWidget(this, "Browser.Choose", _("Choose"), Common::U32String(), kChooseCmd);
}

int BrowserDialog::runModal() {
#if defined(USE_SYSDIALOGS)
	// Try to use the backend browser
	Common::DialogManager *dialogManager = g_system->getDialogManager();
	if (dialogManager) {
		if (ConfMan.getBool("gui_browser_native", Common::ConfigManager::kApplicationDomain)) {
			Common::DialogManager::DialogResult result = dialogManager->showFileBrowser(_title, _choice, _isDirBrowser);
			if (result != Common::DialogManager::kDialogError) {
				return result;
			}
		}
	}
#endif
	// If all else fails, use the GUI browser
	return Dialog::runModal();
}

void BrowserDialog::open() {
	// Call super implementation
	Dialog::open();

	if (ConfMan.hasKey("browser_lastpath"))
		_node = Common::FSNode(ConfMan.getPath("browser_lastpath"));

	if (!_node.isDirectory())
		_node = Common::FSNode(".");

	_showHidden = ConfMan.getBool("gui_browser_show_hidden", Common::ConfigManager::kApplicationDomain);
	_showHiddenWidget->setState(_showHidden);

	// At this point the file list has already been refreshed by the kHiddenCmd handler
}

void BrowserDialog::handleCommand(CommandSender *sender, uint32 cmd, uint32 data) {
	switch (cmd) {
	//Search for typed-in directory
	case kPathEditedCmd:
		_node = Common::FSNode(Common::Path(Common::convertFromU32String(_currentPath->getEditString()), Common::Path::kNativeSeparator));
		updateListing();
		break;
	case kHelpCmd: {
		GUI::HelpDialog dlg;
		dlg.runModal();
		}
		break;
	//Search by text input
	case kChooseCmd:
		if (_isDirBrowser) {
			// If nothing is selected in the list widget, choose the current dir.
			// Else, choose the dir that is selected.
			int selection = _fileList->getSelected();
			if (selection >= 0 && isZip(_nodeContent[selection])) {
				if (chooseZip(_nodeContent[selection])) {
					setResult(1);
					close();
				}
				break;
			}
			if (selection >= 0)
				_choice = _nodeContent[selection];
			else
				_choice = _node;
			setResult(1);
			close();
		} else {
			int selection = _fileList->getSelected();
			if (selection < 0)
				break;
			if (_nodeContent[selection].isDirectory()) {
				_node = _nodeContent[selection];
				updateListing();
			} else {
				_choice = _nodeContent[selection];
				setResult(1);
				close();
			}
		}
		break;
	case kGoUpCmd:
		_node = _node.getParent();
		updateListing();
		break;
	case kListItemActivatedCmd:
	case kListItemDoubleClickedCmd:
		if (_nodeContent[data].isDirectory()) {
			_node = _nodeContent[data];
			updateListing();
		} else if (isZip(_nodeContent[data])) {
			if (chooseZip(_nodeContent[data])) {
				setResult(1);
				close();
			}
		} else if (!_isDirBrowser) {
			_choice = _nodeContent[data];
			setResult(1);
			close();
		}
		break;
	case kListSelectionChangedCmd:
		// We do not allow selecting directories in directory
		// browser mode, thus we will invalidate the selection
		// when the user selects a directory over here.
		if (data != (uint32)-1 && _isDirBrowser && !_nodeContent[data].isDirectory() && !isZip(_nodeContent[data]))
			_fileList->setSelected(-1);
		break;
	case kHiddenCmd:
		// Update whether the user wants hidden files to be shown
		_showHidden = _showHiddenWidget->getState();
		// We save the state in the application domain to avoid cluttering and
		// to prevent odd behavior.
		ConfMan.setBool("gui_browser_show_hidden", _showHidden, Common::ConfigManager::kApplicationDomain);
		// Update the file listing
		updateListing();
		break;
	default:
		Dialog::handleCommand(sender, cmd, data);
	}
}

void BrowserDialog::updateListing() {
	// Update the path display
	_currentPath->setEditString(_node.getPath().toString(Common::Path::kNativeSeparator));

	// We memorize the last visited path.
	// Don't memorize a path that is not a directory
	if (_node.isDirectory()) {
		ConfMan.setPath("browser_lastpath", _node.getPath());
	}

	// Read in the data from the file system
	if (!_node.getChildren(_nodeContent, Common::FSNode::kListAll, _showHidden))
		_nodeContent.clear();
	else
		Common::sort(_nodeContent.begin(), _nodeContent.end());

	// Populate the ListWidget
	Common::U32StringArray list;
	Common::U32String color = ListWidget::getThemeColor(ThemeEngine::kFontColorNormal);
	for (auto &node : _nodeContent) {
		if (_isDirBrowser) {
			if (node.isDirectory() || isZip(node))
				color = ListWidget::getThemeColor(ThemeEngine::kFontColorNormal);
			else
				color = ListWidget::getThemeColor(ThemeEngine::kFontColorAlternate);
		}

		if (node.isDirectory())
			list.push_back(color + ListWidget::escapeString(Common::U32String(node.getName()) + "/"));
		else
			list.push_back(color + ListWidget::escapeString(Common::U32String(node.getName())));
	}

	_fileList->setList(list);
	_fileList->scrollTo(0);

	// Finally, redraw
	g_gui.scheduleTopDialogRedraw();
}

bool BrowserDialog::isZip(const Common::FSNode &node) const {
	return _unzipGames && _isDirBrowser && !node.isDirectory() && node.getName().hasSuffixIgnoreCase(".zip");
}

namespace {

// Unzips every file of zip into dest, keeping the zip's folders
bool unzipTo(Common::Archive &zip, const Common::FSNode &dest) {
	Common::ArchiveMemberList members;
	zip.listMembers(members);
	Common::Array<byte> buffer(64 * 1024);
	for (const Common::ArchiveMemberPtr &m : members) {
		if (m->isDirectory())
			continue;
		Common::StringArray parts = m->getPathInArchive().splitComponents();
		Common::FSNode dir = dest;
		for (uint i = 0; i + 1 < parts.size(); i++) {
			if (parts[i].empty())
				continue;
			Common::FSNode sub = dir.getChild(parts[i]);
			if (!sub.exists() && !sub.createDirectory())
				return false;
			dir = sub;
		}
		if (parts.empty() || parts.back().empty())
			continue;
		Common::ScopedPtr<Common::SeekableReadStream> in(m->createReadStream());
		Common::ScopedPtr<Common::SeekableWriteStream> out(dir.getChild(parts.back()).createWriteStream(false));
		if (!in || !out)
			return false;
		while (!in->eos() && !in->err()) {
			uint32 n = in->read(buffer.data(), buffer.size());
			if (n == 0)
				break;
			if (out->write(buffer.data(), n) != n)
				return false;
		}
		out->finalize();
	}
	return true;
}

bool hasGame(const Common::FSNode &dir) {
	Common::FSList files;
	if (!dir.getChildren(files, Common::FSNode::kListAll))
		return false;
	DetectionResults results = EngineMan.detectGames(files);
	return !results.listRecognizedGames().empty();
}

} // End of anonymous namespace

bool BrowserDialog::chooseZip(const Common::FSNode &zip) {
	g_system->displayMessageOnOSD(Common::U32String::format("Unzipping %s...", zip.getName().c_str()));
	g_system->updateScreen();
	if (!unzipGame(zip, _choice)) {
		MessageDialog error(Common::U32String::format("Couldn't unzip %s.", zip.getName().c_str()));
		error.runModal();
		return false;
	}
	return true;
}

bool BrowserDialog::unzipGame(const Common::FSNode &zip, Common::FSNode &result) {
	Common::String name = zip.getName();
	name = name.substr(0, name.size() - 4);
	Common::FSNode dest = zip.getParent().getChild(name);
	Common::FSNode marker = dest.getChild(".neargrog-unzipped");

	// Unzip once; choosing the same zip again reuses the folder
	if (!marker.exists()) {
		Common::ScopedPtr<Common::Archive> archive(Common::makeZipArchive(zip));
		if (!archive || !(dest.exists() || dest.createDirectory()) || !unzipTo(*archive, dest))
			return false;
		Common::ScopedPtr<Common::SeekableWriteStream> m(marker.createWriteStream(false));
		if (m)
			m->finalize();
	}

	// The game may sit a few folders down (installers' folders, drivers):
	// pick the first folder, nearest first, that holds a game ScummVM knows
	Common::FSList queue;
	queue.push_back(dest);
	for (uint i = 0; i < queue.size() && i < 300; i++) {
		if (hasGame(queue[i])) {
			result = queue[i];
			return true;
		}
		Common::FSList subs;
		if (queue[i].getChildren(subs, Common::FSNode::kListDirectoriesOnly)) {
			Common::sort(subs.begin(), subs.end());
			for (const Common::FSNode &sub : subs)
				queue.push_back(sub);
		}
	}
	// Nothing recognised: hand over the folder, and the usual
	// "couldn't find any game" message follows
	result = dest;
	return true;
}

} // End of namespace GUI
