// This file is part of NotePadLX, a Linux port of Notepad++.
// Copyright (C)2021 Don HO <don.h@free.fr>
// Modified for NotePadLX (Linux port).
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// at your option any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

// NPLX: the macro and the Run menu parts of upstream Notepad_plus (NppCommands.cpp macroPlayback and the
// recording switch at the end of command(), Notepad_plus.cpp addCurrentMacro/checkMacroState and the Macro/Run
// menus of init(), NppBigSwitch.cpp WM_MACRODLGRUNMACRO, the shortcuts.xml security validation),
// and recordedMacroStep::PlayBack (WinControls/shortcut/shortcut.cpp).

#include "Notepad_plus.h"

#include <algorithm>

#include <QAction>
#include <QMenu>
#include <QMenuBar>

#include "Buffer.h"
#include "FindReplaceDlg.h"
#include "KeyMap.h"
#include "Notepad_plus_msgs.h"
#include "Parameters.h"
#include "QtNppMenu.h"
#include "RunDlg.h"
#include "RunMacroDlg.h"
#include "ScintillaEditView.h"
#include "ShortcutDlg.h"
#include "hmac.h"
#include "menuCmdID.h"
#include "resource.h"
#include "shortcut.h"

using namespace std;

namespace
{
constexpr size_t macroPosBase = 6; // upstream Notepad_plus::init
constexpr int runPosBase = 4;

bool isDynamicMenuCmd(int id)
{
	return ((id >= ID_MACRO) && (id < ID_MACRO_LIMIT)) || ((id >= ID_USER_CMD) && (id < ID_USER_CMD_LIMIT));
}
}

QMenu* Notepad_plus::menuOfCommand(int cmdID) const
{
	const QAction* action = actionFor(cmdID);
	return action ? qobject_cast<QMenu*>(action->parent()) : nullptr;
}

NppMenu* Notepad_plus::nppMenuOfCommand(int cmdID) const
{
	// upstream ::GetSubMenu(_mainMenuHandle, MENUINDEX_MACRO / MENUINDEX_RUN)
	QMenu* menu = menuOfCommand(cmdID);
	if (!menu || !_mainMenuHandle)
		return nullptr;
	const QList<QAction*> topActions = menuBar()->actions();
	for (int i = 0; i < topActions.size(); ++i)
	{
		if (topActions.at(i)->menu() == menu)
			return _mainMenuHandle->subMenu(i);
	}
	return nullptr;
}

void Notepad_plus::initDynamicMenus()
{
	NppParameters& nppParam = NppParameters::getInstance();

	// Macro Menu
	NppMenu* hMacroMenu = nppMenuOfCommand(IDM_MACRO_STARTRECORDINGMACRO);
	DynamicMenu& macroMenuItems = nppParam.getMacroMenuItems();
	size_t nbMacroTopLevelItem = macroMenuItems.getTopLevelItemNumber();
	if (hMacroMenu && nbMacroTopLevelItem >= 1)
		hMacroMenu->insertSeparator(macroPosBase - 1);

	macroMenuItems.attach(hMacroMenu, macroPosBase, IDM_SETTING_SHORTCUT_MAPPER_MACRO, L"Modify Shortcut/Delete Macro...");


	// Run Menu
	NppMenu* hRunMenu = nppMenuOfCommand(IDM_EXECUTE);
	DynamicMenu& runMenuItems = nppParam.getRunMenuItems();
	size_t nbRunTopLevelItem = runMenuItems.getTopLevelItemNumber();
	if (hRunMenu && nbRunTopLevelItem >= 1)
		hRunMenu->insertSeparator(runPosBase - 1);

	runMenuItems.attach(hRunMenu, runPosBase, IDM_SETTING_SHORTCUT_MAPPER_RUN, L"Modify Shortcut/Delete Command...");

	// NPLX: the items inserted through NppMenu are plain QActions; route them to command() (upstream WM_COMMAND)
	for (QMenu* menu : { menuOfCommand(IDM_MACRO_STARTRECORDINGMACRO), menuOfCommand(IDM_EXECUTE) })
	{
		if (!menu)
			continue;
		connect(menu, &QMenu::triggered, this, [this](QAction* action) {
			bool ok = false;
			const int id = action->data().toInt(&ok);
			if (ok && (isDynamicMenuCmd(id) || id == IDM_SETTING_SHORTCUT_MAPPER_MACRO || id == IDM_SETTING_SHORTCUT_MAPPER_RUN))
				command(id);
		});
	}
	updateDynamicMenuShortcuts();
}

void Notepad_plus::updateDynamicMenuShortcuts()
{
	// NPLX: upstream _accelerator.updateShortcuts() (the accelerator table holds the macro and Run command keys)
	NppParameters& nppParam = NppParameters::getInstance();
	const vector<MacroShortcut>& macros = nppParam.getMacroList();
	const vector<UserCommand>& userCommands = nppParam.getUserCommandList();

	std::function<void(QMenu*)> update = [&](QMenu* menu) {
		for (QAction* action : menu->actions())
		{
			if (QMenu* sub = action->menu())
			{
				update(sub);
				continue;
			}
			bool ok = false;
			const int id = action->data().toInt(&ok);
			if (!ok)
				continue;

			const CommandShortcut* sc = nullptr;
			if ((id >= ID_MACRO) && (id < ID_MACRO_LIMIT) && static_cast<size_t>(id - ID_MACRO) < macros.size())
				sc = &macros[id - ID_MACRO];
			else if ((id >= ID_USER_CMD) && (id < ID_USER_CMD_LIMIT) && static_cast<size_t>(id - ID_USER_CMD) < userCommands.size())
				sc = &userCommands[id - ID_USER_CMD];

			if (sc)
			{
				// the key is shown from the QAction shortcut, not from a "\t" in the text
				QString text = action->text();
				const qsizetype tab = text.indexOf(QLatin1Char('\t'));
				if (tab >= 0)
					action->setText(text.left(tab));
				action->setShortcut(sc->isEnabled() ? nplx::keyComboToKeySequence(sc->getKeyCombo()) : QKeySequence());
				action->setShortcutContext(Qt::WindowShortcut);
				if (!actions().contains(action))
					addAction(action); // the key also works while the menu is closed
			}
			else if (!isCommandPorted(id))
			{
				action->setEnabled(false);
				action->setToolTip(tr("Not yet ported to NotePadLX"));
			}
		}
	};
	for (QMenu* menu : { menuOfCommand(IDM_MACRO_STARTRECORDINGMACRO), menuOfCommand(IDM_EXECUTE) })
	{
		if (menu)
			update(menu);
	}
}

bool Notepad_plus::findKeyConflicts(const KeyCombo& combo) const
{
	// NPLX_TODO(Phase 7): upstream ShortcutMapper::findKeyConflicts (NPPM_INTERNAL_FINDKEYCONFLICTS); until the
	// Shortcut Mapper is ported, the same lookup over the menu commands, macros, Run commands and Scintilla keys
	if (combo._key == 0)
		return false;

	auto same = [&combo](const KeyCombo& kc) {
		return kc._key == combo._key && kc._isCtrl == combo._isCtrl && kc._isAlt == combo._isAlt && kc._isShift == combo._isShift;
	};

	NppParameters& nppParam = NppParameters::getInstance();
	for (const CommandShortcut& sc : nppParam.getUserShortcuts())
		if (sc.isEnabled() && same(sc.getKeyCombo()))
			return true;
	for (const MacroShortcut& sc : nppParam.getMacroList())
		if (sc.isEnabled() && same(sc.getKeyCombo()))
			return true;
	for (const UserCommand& sc : nppParam.getUserCommandList())
		if (sc.isEnabled() && same(sc.getKeyCombo()))
			return true;
	for (const ScintillaKeyMap& skm : nppParam.getScintillaKeyList())
		for (size_t i = 0, n = skm.getSize(); i < n; ++i)
			if (same(skm.getKeyComboByIndex(i)))
				return true;
	return false;
}

void Notepad_plus::macroPlayback(Macro macro, std::vector<Document>* pDocs4EndUAIn)
{
	_playingBackMacro = true;

	std::vector<Document>* pDocs4EndUA = nullptr;
	if (pDocs4EndUAIn)
	{
		// continue with the passed param doc list
		pDocs4EndUA = pDocs4EndUAIn;
	}
	else
	{
		// use local doc list
		pDocs4EndUA = new std::vector<Document>;
		if (!pDocs4EndUA)
			return;
	}

	Document prevSciDoc = 0;
	for (Macro::iterator step = macro.begin(); step != macro.end(); ++step)
	{
		Document curSciDoc = _pEditView->getCurrentBuffer()->getDocument();
		if (curSciDoc != prevSciDoc)
		{
			// macro step is going to work with different Scintilla Document object
			// (for which the undo actions are bound)

			if (std::find(pDocs4EndUA->begin(), pDocs4EndUA->end(), curSciDoc) == pDocs4EndUA->end())
			{
				// not in the list of the macro affected docs so far
				_pEditView->execute(SCI_BEGINUNDOACTION); // the macro step will touch another doc, open another undo action
				pDocs4EndUA->push_back(curSciDoc); // store for possible ending undo action later
			}

			prevSciDoc = curSciDoc; // remember the doc switch
		}

		if (step->isScintillaMacro())
			playBackMacroStep(*step, _pEditView); // upstream step->PlayBack(_pPublicInterface, _pEditView)
		else
			_findReplaceDlg->execSavedCommand(step->_message, step->_lParameter, string2wstring(step->_sParameter, CP_UTF8));
	}

	if (!pDocs4EndUAIn)
	{
		// handle all the affected docs undo actions closing (for local-only doc list)

		Document invisSciDoc = _invisibleEditView->execute(SCI_GETDOCPOINTER); // store the view's original doc
		while (!pDocs4EndUA->empty())
		{
			Document doc = pDocs4EndUA->back();
			if (MainFileManager.getBufferFromDocument(doc) == BUFFER_INVALID)
			{
				// affected doc no longer exists (a macro step closed its associated Notepad++ tab/buffer),
				// the ending undo action is not needed (until Notepad++ supports tab/buffer closing undo)
			}
			else
			{
				// complete the open undo action for existing doc object
				_invisibleEditView->execute(SCI_SETDOCPOINTER, 0, doc);
				_invisibleEditView->execute(SCI_ENDUNDOACTION);
			}
			pDocs4EndUA->pop_back();
		}
		_invisibleEditView->execute(SCI_SETDOCPOINTER, 0, invisSciDoc); // restore

		delete pDocs4EndUA;
		pDocs4EndUA = nullptr;
	}

	_playingBackMacro = false;
}

void Notepad_plus::playBackMacroStep(const recordedMacroStep& step, ScintillaEditView* pEditView)
{
	// upstream recordedMacroStep::PlayBack (shortcut.cpp)
	if (step._macroType == recordedMacroStep::mtMenuCommand)
	{
		command(static_cast<int>(step._wParameter)); // ::SendMessage(pNotepad->getHSelf(), WM_COMMAND, _wParameter, 0)
	}
	else
	{
		// Ensure it's macroable message before send it
		if (!step.isMacroable())
			return;

		if (step._macroType == recordedMacroStep::mtUseSParameter)
		{
			pEditView->execute(step._message, step._wParameter, reinterpret_cast<sptr_t>(step._sParameter.c_str()));
		}
		else
		{
			pEditView->execute(step._message, step._wParameter, step._lParameter);
		}

		// If text content has been modified in Scintilla,
		// then notify Notepad++
		// NPLX: upstream sends a WM_NOTIFY SCN_CHARADDED here; Notepad_plus ignores SCN_CHARADDED while playing back a
		// macro, so only the plugins would see it. NPLX_TODO(Phase 9): relay it to the plugins.
	}
}

void Notepad_plus::recordMacroStep(const SCNotification* notification)
{
	// upstream NppNotification.cpp SCN_MACRORECORD
	// Normalize EOL by replacing macro step using SCI_REPLACESEL
	// with lParam representing string of one char with '\n' or '\r'
	// with step using SCI_NEWLINE which is document context aware and will insert correct EOL.
	if (notification->message == SCI_REPLACESEL)
	{
		const auto* ch = reinterpret_cast<char*>(notification->lParam);
		if (ch != nullptr
			&& ch[0] != '\0' // is not ""
			&& ch[1] == '\0' // is length == 1
			&& (ch[0] == '\n' || ch[0] == '\r')) // is EOL
		{
			// Current detected EOL is LF and document has CRLF,
			// previous detected step using SCI_REPLACESEL with CR was already replaced by SCI_NEWLINE.
			// To avoid double newlines, the previous macro step is removed.
			if (_pEditView->getCurrentBuffer()->getEolFormat() == EolType::windows
				&& ch[0] == '\n'
				&& !_macro.empty()
				&& _macro.back()._message == SCI_NEWLINE)
			{
				_macro.pop_back();
			}

			_macro.push_back(recordedMacroStep(SCI_NEWLINE, 0, 0, nullptr, 0));
			return;
		}
	}

	_macro.push_back(
		recordedMacroStep(
			notification->message,
			notification->wParam,
			notification->lParam
		)
	);
}

void Notepad_plus::recordMenuCommand(int id)
{
	// upstream: the end of Notepad_plus::command()
	if (_recordingMacro)
		switch (id)
		{
			case IDM_FILE_NEW :
			case IDM_FILE_CLOSE :
			case IDM_FILE_CLOSEALL :
			case IDM_FILE_CLOSEALL_BUT_CURRENT :
			case IDM_FILE_CLOSEALL_BUT_PINNED :
			case IDM_FILE_CLOSEALL_TOLEFT :
			case IDM_FILE_CLOSEALL_TORIGHT :
			case IDM_FILE_CLOSEALL_UNCHANGED:
			case IDM_FILE_SAVE :
			case IDM_FILE_SAVEALL :
			case IDM_FILE_RELOAD:
			case IDM_EDIT_UNDO:
			case IDM_EDIT_REDO:
			case IDM_EDIT_DELETE:
			case IDM_SEARCH_FINDNEXT :
			case IDM_SEARCH_FINDPREV :
            case IDM_SEARCH_SETANDFINDNEXT :
			case IDM_SEARCH_SETANDFINDPREV :
			case IDM_SEARCH_GOTOMATCHINGBRACE :
			case IDM_SEARCH_SELECTMATCHINGBRACES :
			case IDM_SEARCH_TOGGLE_BOOKMARK :
			case IDM_SEARCH_NEXT_BOOKMARK:
			case IDM_SEARCH_PREV_BOOKMARK:
			case IDM_SEARCH_CLEAR_BOOKMARKS:
			case IDM_SEARCH_INVERSEMARKS:
			case IDM_EDIT_SELECTALL:
			case IDM_EDIT_INS_TAB:
			case IDM_EDIT_RMV_TAB:
			case IDM_EDIT_DUP_LINE:
			case IDM_EDIT_REMOVE_CONSECUTIVE_DUP_LINES:
			case IDM_EDIT_REMOVE_ANY_DUP_LINES:
			case IDM_EDIT_TRANSPOSE_LINE:
			case IDM_EDIT_SPLIT_LINES:
			case IDM_EDIT_JOIN_LINES:
			case IDM_EDIT_REMOVEEMPTYLINES:
			case IDM_EDIT_REMOVEEMPTYLINESWITHBLANK:
			case IDM_EDIT_UPPERCASE:
			case IDM_EDIT_LOWERCASE:
			case IDM_EDIT_PROPERCASE_FORCE:
			case IDM_EDIT_PROPERCASE_BLEND:
			case IDM_EDIT_SENTENCECASE_FORCE:
			case IDM_EDIT_SENTENCECASE_BLEND:
			case IDM_EDIT_INVERTCASE:
			case IDM_EDIT_RANDOMCASE:
			case IDM_EDIT_BLOCK_COMMENT:
			case IDM_EDIT_BLOCK_COMMENT_SET:
			case IDM_EDIT_BLOCK_UNCOMMENT:
			case IDM_EDIT_STREAM_COMMENT:
			case IDM_EDIT_TRIMTRAILING:
			case IDM_EDIT_TRIMLINEHEAD:
			case IDM_EDIT_TRIM_BOTH:
			case IDM_EDIT_EOL2WS:
			case IDM_EDIT_TRIMALL:
			case IDM_EDIT_TAB2SW:
			case IDM_EDIT_SW2TAB_ALL:
			case IDM_EDIT_SW2TAB_LEADING:
			case IDM_EDIT_TOGGLEREADONLY :
			case IDM_EDIT_FULLPATHTOCLIP :
			case IDM_EDIT_FILENAMETOCLIP :
			case IDM_EDIT_CURRENTDIRTOCLIP :
			case IDM_EDIT_TOGGLESYSTEMREADONLY :
			case IDM_EDIT_RTL :
			case IDM_EDIT_LTR :
			case IDM_EDIT_BEGINENDSELECT:
			case IDM_EDIT_BEGINENDSELECT_COLUMNMODE:
			case IDM_EDIT_SORTLINES_LEXICOGRAPHIC_ASCENDING:
			case IDM_EDIT_SORTLINES_LEXICOGRAPHIC_DESCENDING:
			case IDM_EDIT_SORTLINES_LEXICO_CASE_INSENS_ASCENDING:
			case IDM_EDIT_SORTLINES_LEXICO_CASE_INSENS_DESCENDING:
			case IDM_EDIT_SORTLINES_INTEGER_ASCENDING:
			case IDM_EDIT_SORTLINES_INTEGER_DESCENDING:
			case IDM_EDIT_SORTLINES_DECIMALCOMMA_ASCENDING:
			case IDM_EDIT_SORTLINES_DECIMALCOMMA_DESCENDING:
			case IDM_EDIT_SORTLINES_DECIMALDOT_ASCENDING:
			case IDM_EDIT_SORTLINES_DECIMALDOT_DESCENDING:
			case IDM_EDIT_SORTLINES_REVERSE_ORDER:
			case IDM_EDIT_SORTLINES_RANDOMLY:
			case IDM_EDIT_BLANKLINEABOVECURRENT:
			case IDM_EDIT_BLANKLINEBELOWCURRENT:
			case IDM_VIEW_FULLSCREENTOGGLE :
			case IDM_VIEW_ALWAYSONTOP :
			case IDM_VIEW_WRAP :
			case IDM_VIEW_FOLD_CURRENT :
			case IDM_VIEW_UNFOLD_CURRENT :
			case IDM_VIEW_FOLDALL:
			case IDM_VIEW_UNFOLDALL:
			case IDM_VIEW_FOLD_1:
			case IDM_VIEW_FOLD_2:
			case IDM_VIEW_FOLD_3:
			case IDM_VIEW_FOLD_4:
			case IDM_VIEW_FOLD_5:
			case IDM_VIEW_FOLD_6:
			case IDM_VIEW_FOLD_7:
			case IDM_VIEW_FOLD_8:
			case IDM_VIEW_UNFOLD_1:
			case IDM_VIEW_UNFOLD_2:
			case IDM_VIEW_UNFOLD_3:
			case IDM_VIEW_UNFOLD_4:
			case IDM_VIEW_UNFOLD_5:
			case IDM_VIEW_UNFOLD_6:
			case IDM_VIEW_UNFOLD_7:
			case IDM_VIEW_UNFOLD_8:
			case IDM_VIEW_GOTO_START:
			case IDM_VIEW_GOTO_END:
			case IDM_VIEW_GOTO_ANOTHER_VIEW:
			case IDM_VIEW_CLONE_TO_ANOTHER_VIEW:
			case IDM_VIEW_GOTO_NEW_INSTANCE:
			case IDM_VIEW_LOAD_IN_NEW_INSTANCE:
			case IDM_VIEW_SYNSCROLLV:
			case IDM_VIEW_SYNSCROLLH:
			case IDM_VIEW_TAB1:
			case IDM_VIEW_TAB2:
			case IDM_VIEW_TAB3:
			case IDM_VIEW_TAB4:
			case IDM_VIEW_TAB5:
			case IDM_VIEW_TAB6:
			case IDM_VIEW_TAB7:
			case IDM_VIEW_TAB8:
			case IDM_VIEW_TAB9:
			case IDM_VIEW_TAB_NEXT:
			case IDM_VIEW_TAB_PREV:
			case IDM_VIEW_TAB_MOVEFORWARD:
			case IDM_VIEW_TAB_MOVEBACKWARD:
			case IDC_PREV_DOC :
			case IDC_NEXT_DOC :
			case IDM_SEARCH_GOPREVMARKER1   :
			case IDM_SEARCH_GOPREVMARKER2   :
			case IDM_SEARCH_GOPREVMARKER3   :
			case IDM_SEARCH_GOPREVMARKER4   :
			case IDM_SEARCH_GOPREVMARKER5   :
			case IDM_SEARCH_GOPREVMARKER_DEF:
			case IDM_SEARCH_GONEXTMARKER1   :
			case IDM_SEARCH_GONEXTMARKER2   :
			case IDM_SEARCH_GONEXTMARKER3   :
			case IDM_SEARCH_GONEXTMARKER4   :
			case IDM_SEARCH_GONEXTMARKER5   :
			case IDM_SEARCH_GONEXTMARKER_DEF:
			case IDM_SEARCH_STYLE1TOCLIP:
			case IDM_SEARCH_STYLE2TOCLIP:
			case IDM_SEARCH_STYLE3TOCLIP:
			case IDM_SEARCH_STYLE4TOCLIP:
			case IDM_SEARCH_STYLE5TOCLIP:
			case IDM_SEARCH_ALLSTYLESTOCLIP:
			case IDM_SEARCH_MARKEDTOCLIP:
			case IDM_SEARCH_VOLATILE_FINDNEXT:
			case IDM_SEARCH_VOLATILE_FINDPREV:
			case IDM_SEARCH_CUTMARKEDLINES   :
			case IDM_SEARCH_COPYMARKEDLINES     :
			case IDM_SEARCH_PASTEMARKEDLINES    :
			case IDM_SEARCH_DELETEMARKEDLINES   :
			case IDM_SEARCH_DELETEUNMARKEDLINES :
			case IDM_SEARCH_MARKALLEXT1      :
			case IDM_SEARCH_MARKALLEXT2      :
			case IDM_SEARCH_MARKALLEXT3      :
			case IDM_SEARCH_MARKALLEXT4      :
			case IDM_SEARCH_MARKALLEXT5      :
			case IDM_SEARCH_MARKONEEXT1      :
			case IDM_SEARCH_MARKONEEXT2      :
			case IDM_SEARCH_MARKONEEXT3      :
			case IDM_SEARCH_MARKONEEXT4      :
			case IDM_SEARCH_MARKONEEXT5      :
			case IDM_SEARCH_UNMARKALLEXT1    :
			case IDM_SEARCH_UNMARKALLEXT2    :
			case IDM_SEARCH_UNMARKALLEXT3    :
			case IDM_SEARCH_UNMARKALLEXT4    :
			case IDM_SEARCH_UNMARKALLEXT5    :
			case IDM_SEARCH_CLEARALLMARKS    :
			case IDM_FORMAT_TODOS  :
			case IDM_FORMAT_TOUNIX :
			case IDM_FORMAT_TOMAC  :
			case IDM_VIEW_IN_FIREFOX :
			case IDM_VIEW_IN_CHROME  :
			case IDM_VIEW_IN_EDGE    :
			case IDM_VIEW_IN_IE      :
			case IDM_EDIT_COPY_ALL_NAMES:
			case IDM_EDIT_COPY_ALL_PATHS:
			case IDM_EDIT_MULTISELECTALLWHOLEWORD:
			case IDM_EDIT_MULTISELECTALLMATCHCASEWHOLEWORD:
			case IDM_EDIT_MULTISELECTNEXT:
			case IDM_EDIT_MULTISELECTNEXTMATCHCASE:
			case IDM_EDIT_MULTISELECTNEXTWHOLEWORD:
			case IDM_EDIT_MULTISELECTNEXTMATCHCASEWHOLEWORD:
			case IDM_EDIT_MULTISELECTUNDO:
			case IDM_EDIT_MULTISELECTSSKIP:
			case IDM_EDIT_SORTLINES_LOCALE_ASCENDING:
			case IDM_EDIT_SORTLINES_LOCALE_DESCENDING:
			case IDM_EDIT_SETREADONLYFORALLDOCS:
			case IDM_EDIT_CLEARREADONLYFORALLDOCS:
			case IDM_EDIT_SORTLINES_LENGTH_ASCENDING:
			case IDM_EDIT_SORTLINES_LENGTH_DESCENDING:
			case IDM_EDIT_REDACT_SELECTION:
				_macro.push_back(recordedMacroStep(id));
				break;

			// No need to record the following commands: all they do is execute Scintilla commands, which are recorded instead.
			case IDM_EDIT_CUT:
			case IDM_EDIT_COPY:
			case IDM_EDIT_PASTE:
			case IDM_EDIT_LINE_UP:
			case IDM_EDIT_LINE_DOWN:
				break;

			// The following 3 commands will insert date time string during the recording:
			// SCI_REPLACESEL will be recorded firstly, then SCI_ADDTEXT (for adding date time string)
			// So we erase these 2 unwanted commanded for recording these 3 following commands.
			case IDM_EDIT_INSERT_DATETIME_SHORT:
			case IDM_EDIT_INSERT_DATETIME_LONG:
			case IDM_EDIT_INSERT_DATETIME_CUSTOMIZED:
			{
				size_t lastIndex = _macro.size();
				if (lastIndex >= 2)
				{
					--lastIndex;
					if (_macro[lastIndex]._message == SCI_ADDTEXT && _macro[lastIndex - 1]._message == SCI_REPLACESEL)
					{
						_macro.erase(_macro.begin() + lastIndex);
						--lastIndex;
						_macro.erase(_macro.begin() + lastIndex);
					}
				}
				_macro.push_back(recordedMacroStep(id));
			}
			break;
		}
}

void Notepad_plus::checkMacroState()
{
	enableCommand(IDM_MACRO_STARTRECORDINGMACRO, !_recordingMacro, MENU | TOOLBAR);
	enableCommand(IDM_MACRO_STOPRECORDINGMACRO, _recordingMacro, MENU | TOOLBAR);
	enableCommand(IDM_MACRO_PLAYBACKRECORDEDMACRO, !_macro.empty() && !_recordingMacro, MENU | TOOLBAR);
	enableCommand(IDM_MACRO_SAVECURRENTMACRO, !_macro.empty() && !_recordingMacro && !_recordingSaved, MENU | TOOLBAR);

	enableCommand(IDM_MACRO_RUNMULTIMACRODLG, (!_macro.empty() && !_recordingMacro) || !((NppParameters::getInstance()).getMacroList()).empty(), MENU | TOOLBAR);
}

intptr_t Notepad_plus::macroStatus() const
{
	// upstream NPPM_GETCURRENTMACROSTATUS
	if (_recordingMacro)
		return static_cast<intptr_t>(MacroStatus::RecordInProgress);
	if (_playingBackMacro)
		return static_cast<intptr_t>(MacroStatus::PlayingBack);
	return (_macro.empty()) ? static_cast<intptr_t>(MacroStatus::Idle) : static_cast<intptr_t>(MacroStatus::RecordingStopped);
}

bool Notepad_plus::addCurrentMacro()
{
	NppParameters& nppParams = NppParameters::getInstance();
	vector<MacroShortcut> & theMacros = nppParams.getMacroList();

	int nbMacro = static_cast<int32_t>(theMacros.size());

	DynamicMenu& macroMenu = nppParams.getMacroMenuItems();
	int nbTopLevelItem = macroMenu.getTopLevelItemNumber();
	int cmdID = ID_MACRO + nbMacro;
	MacroShortcut ms(Shortcut(), _macro, cmdID);

	ShortcutDlg shortcutDlg(ms, [this](const KeyCombo& kc) { return findKeyConflicts(kc); }, this);
	if (shortcutDlg.doDialog() != -1)
	{
		NppMenu* hMacroMenu = nppMenuOfCommand(IDM_MACRO_STARTRECORDINGMACRO);
		unsigned int posBase = macroMenu.getPosBase();
		if (hMacroMenu && nbTopLevelItem == 0)
		{
			hMacroMenu->insertSeparator(posBase - 1);	//no separator yet, add one

            // Insert the separator and modify/delete command
			hMacroMenu->insertSeparator(posBase + nbTopLevelItem + 1);

			wstring nativeLangShortcutMapperMacro = _nativeLangSpeaker.getNativeLangMenuString(IDM_SETTING_SHORTCUT_MAPPER_MACRO);
			if (nativeLangShortcutMapperMacro == L"")
				nativeLangShortcutMapperMacro = macroMenu.getLastCmdLabel();

			hMacroMenu->insertItem(posBase + nbTopLevelItem + 2, IDM_SETTING_SHORTCUT_MAPPER_MACRO, nativeLangShortcutMapperMacro);
        }
		theMacros.push_back(ms);
		macroMenu.push_back(MenuItemUnit(cmdID, string2wstring(ms.getName(), CP_UTF8)));
		if (hMacroMenu)
			hMacroMenu->insertItem(static_cast<int>(posBase + nbTopLevelItem), cmdID, string2wstring(ms.toMenuItemString(), CP_UTF8));
		updateDynamicMenuShortcuts(); // _accelerator.updateShortcuts()
		nppParams.setShortcutDirty();
		return true;
	}
	return false;
}

void Notepad_plus::addUserCommand(const UserCommand& uc)
{
	// upstream RunDlg IDC_BUTTON_SAVE, after the Shortcut dialog
	NppParameters& nppParams = NppParameters::getInstance();
	std::vector<UserCommand> & theUserCmds = nppParams.getUserCommandList();
	DynamicMenu& runMenu = nppParams.getRunMenuItems();
	int nbTopLevelItem = runMenu.getTopLevelItemNumber();
	const int cmdID = static_cast<int>(uc.getID());

	NppMenu* hRunMenu = nppMenuOfCommand(IDM_EXECUTE);
	int const posBase = runMenu.getPosBase();

	if (hRunMenu && nbTopLevelItem == 0)
		hRunMenu->insertSeparator(posBase - 1);

	theUserCmds.push_back(uc);
	runMenu.push_back(MenuItemUnit(cmdID, string2wstring(uc.getName(), CP_UTF8)));
	if (hRunMenu)
		hRunMenu->insertItem(posBase + nbTopLevelItem, cmdID, string2wstring(uc.toMenuItemString(), CP_UTF8));

	if (hRunMenu && nbTopLevelItem == 0)
	{
		// Insert the separator and modify/delete command
		hRunMenu->insertSeparator(posBase + nbTopLevelItem + 1);
		wstring nativeLangShortcutMapperMacro = _nativeLangSpeaker.getNativeLangMenuString(IDM_SETTING_SHORTCUT_MAPPER_MACRO);
		if (nativeLangShortcutMapperMacro == L"")
			nativeLangShortcutMapperMacro = runMenu.getLastCmdLabel();

		hRunMenu->insertItem(posBase + nbTopLevelItem + 2, IDM_SETTING_SHORTCUT_MAPPER_RUN, nativeLangShortcutMapperMacro);
	}
	updateDynamicMenuShortcuts(); // nppParams.getAccelerator()->updateShortcuts()
	nppParams.setShortcutDirty();
}

bool Notepad_plus::isShortcutsXmlSafe()
{
	// NPLX: upstream repeats this block in command() (ID_MACRO / ID_USER_CMD) and in WM_MACRODLGRUNMACRO

	//-- shortcuts.xml security validation --//

	NppParameters& nppParams = NppParameters::getInstance();
	NppGUI& nppGUI = nppParams.getNppGUI();

	// If HMAC is absent from config.xml, force user enable security validation, and generate and save HMAC in config.xml
	if (nppGUI._shortcutsXmlHmacInConfig.empty())
	{
		// Open shortcuts.xml in read-only for user to review
		BufferID shortcutsBufId = doOpen(nppParams.getShortcutsPath(), false, true);
		if (shortcutsBufId != BUFFER_INVALID)
		{
			switchToFile(shortcutsBufId);

			_nativeLangSpeaker.messageBox("ShortcutsXmlHMACMissing",
				L"The security information for shortcuts.xml is missing in config.xml.\r\rFor security reasons, the integrity of shortcuts.xml will be checked. To run your customized command, please review the opened shortcuts.xml. If the file content is OK, use \"Validate shortcuts.xml\" from the \"Run\" menu to confirm it.",
				L"Security Warning",
				MB_OK);
		}
		return false;
	}

	// If HMAC is present, calculate shortcuts.xml HMAC and compare with the one from config.xml
	else
	{
		if (nppGUI._shortcutsOnDiskHmac != nppGUI._shortcutsXmlHmacInConfig)
		{
			// if they don't match, it means shortcuts.xml could be tampered with, so show warning message and calculate shortcuts.xml HMAC

			// Open shortcuts.xml in read-only for user to review
			BufferID shortcutsBufId = doOpen(nppParams.getShortcutsPath(), false, true);
			if (shortcutsBufId != BUFFER_INVALID)
			{
				switchToFile(shortcutsBufId);

				_nativeLangSpeaker.messageBox("ShortcutsXmlTampered",
					L"The shortcuts.xml file appears to have been modified manually.\r\rFor security reasons, please review the opened shortcuts.xml. If the file content is OK, use \"Validate shortcuts.xml\" from the \"Run\" menu to confirm it.",
					L"Security Warning",
					MB_OK);
			}
			return false;
		}
		// else if they match, it means shortcuts.xml is safe
	}

	//-- End shortcuts.xml security validation --//
	return true;
}

void Notepad_plus::validateShortcutsXml()
{
	// upstream IDM_EXECUTE_VALIDATE_SHORTCUTSXML
	NppParameters& nppParams = NppParameters::getInstance();

	// Read back the file content as bytes
	std::string fileContent = getFileContent(nppParams.getShortcutsPath().c_str());

	// Compute HMAC
	std::string machineGUID = getMachineGUID();
	std::string hmac = computeHMAC(machineGUID, fileContent);

	// Store in config.xml
	nppParams.getNppGUI()._shortcutsXmlHmacInConfig = hmac;
	// NPLX: upstream compares with the HMAC computed at startup; the file may have been reviewed and kept as is
	nppParams.getNppGUI()._shortcutsOnDiskHmac = hmac;
}

void Notepad_plus::runShortcutsXmlCommand(int id)
{
	// upstream command(): default branch, shortcuts.xml commands
	if (!isShortcutsXmlSafe())
		return;

	NppParameters& nppParams = NppParameters::getInstance();
	if ((id >= ID_MACRO) && (id < ID_MACRO_LIMIT))
	{
		size_t i = id - ID_MACRO;
		vector<MacroShortcut>& theMacros = nppParams.getMacroList();
		if (i >= theMacros.size()) return;
		macroPlayback(theMacros[i].getMacro());
	}
	else if ((id >= ID_USER_CMD) && (id < ID_USER_CMD_LIMIT))
	{
		size_t i = id - ID_USER_CMD;
		const vector<UserCommand>& theUserCommands = nppParams.getUserCommandList();
		if (i >= theUserCommands.size()) return;
		UserCommand ucmd = theUserCommands[i];

		Command cmd(string2wstring(ucmd.getCmd(), CP_UTF8));
		cmd.run([this](unsigned int message, uptr_t wParam, sptr_t lParam) { return nppMessage(message, wParam, lParam); });
	}
}

void Notepad_plus::runMacroFromDlg()
{
	// upstream WM_MACRODLGRUNMACRO
	if (!_recordingMacro) // if we're not currently recording, then playback the recorded keystrokes
	{
		if (!isShortcutsXmlSafe())
			return;

		NppParameters& nppParam = NppParameters::getInstance();

		int times = _runMacroDlg->isMulti() ? _runMacroDlg->getTimes() : -1;

		int counter = 0;
		intptr_t lastLine = _pEditView->execute(SCI_GETLINECOUNT) - 1;
		intptr_t currLine = _pEditView->getCurrentLineNumber();
		int indexMacro = _runMacroDlg->getMacro2Exec();
		intptr_t deltaLastLine = 0;
		intptr_t deltaCurrLine = 0;
		bool cursorMovedUp = false;

		Macro m = _macro;

		if (indexMacro != -1)
		{
			vector<MacroShortcut> & ms = nppParam.getMacroList();
			m = ms[indexMacro].getMacro();
		}

		// to be able to roll back all the possible macro-steps changes at once, no matter how many times
		// the current macro will be played, we need to establish HERE the list of the macro playback affected docs
		// intended for the ending undo actions later and also close HERE all the opened undo actions of the docs
		// (we do not use here an explicit begin undo action call, the macroPlayback() func handles all the needed undo action openings)
		std::vector<Document> docs4EndUA;

		for (;;)
		{
			macroPlayback(m, &docs4EndUA);
			++counter;
			if ( times >= 0 )
			{
				if ( counter >= times )
					break;
			}
			else // run until eof
			{
				if (counter > 2 && cursorMovedUp != (deltaCurrLine < 0) && deltaLastLine >= 0)
				{
					// the current line must be monotonically increasing or monotonically
					// decreasing. Otherwise we don't know that the loop will end,
					// unless the number of lines is decreasing with every iteration.
					break;
				}
				cursorMovedUp = deltaCurrLine < 0;
				deltaLastLine = _pEditView->execute(SCI_GETLINECOUNT) - 1 - lastLine;
				deltaCurrLine = _pEditView->getCurrentLineNumber() - currLine;

				if (( deltaCurrLine == 0 )	// line no. not changed?
					&& (deltaLastLine >= 0))  // and no lines removed?
					break; // exit

				// Update the line count, but only if the number of lines remaining is shrinking.
				// Otherwise, the macro playback may never end.
				if (deltaLastLine < deltaCurrLine)
					lastLine += deltaLastLine;

				// save current line
				currLine += deltaCurrLine;

				// eof?
				if ((currLine > lastLine) || (currLine < 0)
					|| ((deltaCurrLine == 0) && (currLine == 0) && ((deltaLastLine >= 0) || cursorMovedUp)))
				{
					break;
				}
			}
		}

		// handle all the affected docs undo actions closing
		Document invisSciDoc = _invisibleEditView->execute(SCI_GETDOCPOINTER); // store the view's original doc
		while (!docs4EndUA.empty())
		{
			Document doc = docs4EndUA.back();
			if (MainFileManager.getBufferFromDocument(doc) == BUFFER_INVALID)
			{
				// affected doc no longer exists (a macro step closed its associated Notepad++ tab/buffer),
				// the ending undo action is not needed (until Notepad++ supports tab/buffer closing undo)
			}
			else
			{
				// complete the open undo action for existing doc object
				_invisibleEditView->execute(SCI_SETDOCPOINTER, 0, doc);
				_invisibleEditView->execute(SCI_ENDUNDOACTION);
			}
			docs4EndUA.pop_back();
		}
		_invisibleEditView->execute(SCI_SETDOCPOINTER, 0, invisSciDoc); // restore
	}
}

void Notepad_plus::toggleMacroRecording()
{
	// upstream command(): IDM_MACRO_STARTRECORDINGMACRO / IDM_MACRO_STOPRECORDINGMACRO / IDC_EDIT_TOGGLEMACRORECORDING
	if (_recordingMacro)
	{
		// STOP !!!
		_mainEditView->execute(SCI_STOPRECORD);
		_subEditView->execute(SCI_STOPRECORD);

		_mainEditView->execute(SCI_SETCURSOR, static_cast<uptr_t>(SC_CURSORNORMAL));
		_subEditView->execute(SCI_SETCURSOR, static_cast<uptr_t>(SC_CURSORNORMAL));

		_recordingMacro = false;
		_runMacroDlg->initMacroList();
	}
	else
	{
		// NPLX: cursor 9 is the Win32 "recording" arrow; Scintilla Qt maps it to the default arrow
		_mainEditView->execute(SCI_SETCURSOR, 9);
		_subEditView->execute(SCI_SETCURSOR, 9);
		_macro.clear();

		// START !!!
		_mainEditView->execute(SCI_STARTRECORD);
		_subEditView->execute(SCI_STARTRECORD);
		_recordingMacro = true;
	}
	_recordingSaved = false;
	checkMacroState();
}

void Notepad_plus::saveShortcuts()
{
	NppParameters::getInstance().writeShortcuts();
}
