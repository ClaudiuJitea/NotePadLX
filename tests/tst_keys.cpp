// This file is part of NotePadLX, a Linux port of Notepad++.
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

// The Notepad++ default keys, end to end: each key is pressed in the editor of the real window and must reach its
// menu command (recorded, not run, so that no dialog opens) or its Scintilla command (seen by Scintilla's macro
// recorder). The list is the Notepad++ Shortcut Mapper's default list.

#include <QtTest>
#include <QAction>
#include <QKeySequence>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdlib>
#include <vector>

#include "Notepad_plus.h"
#include "Parameters.h"
#include "QtUiServices.h"
#include "ScintillaEditView.h"
#include "localization.h"
#include "menuCmdID.h"
#include "resource.h"

namespace
{
	class SilentUiServices : public QtUiServices
	{
	public:
		int messageBox(const std::wstring&, const std::wstring&, unsigned int) override { return IDOK; }
		int saveOrNotMultiBox(const std::wstring&, const std::wstring&) override { return IDNO; }
	};

	// records the menu commands instead of running them
	class RecordingNpp : public Notepad_plus
	{
	public:
		void command(int id) override
		{
			if (_record)
				_commands.push_back(id);
			else
				Notepad_plus::command(id);
		}
		bool _record = false;
		std::vector<int> _commands;
	};

	struct MenuKey { const char* _description; int _cmdID; const char* _keys; };

	// the Notepad++ default keys of the menu commands (Settings > Shortcut Mapper > Main menu)
	const MenuKey menuKeys[] = {
		{ "New", IDM_FILE_NEW, "Ctrl+N" },
		{ "Open...", IDM_FILE_OPEN, "Ctrl+O" },
		{ "Reload from Disk", IDM_FILE_RELOAD, "Ctrl+R" },
		{ "Save", IDM_FILE_SAVE, "Ctrl+S" },
		{ "Save As...", IDM_FILE_SAVEAS, "Ctrl+Alt+S" },
		{ "Save All", IDM_FILE_SAVEALL, "Ctrl+Shift+S" },
		{ "Close", IDM_FILE_CLOSE, "Ctrl+W" },
		{ "Close All", IDM_FILE_CLOSEALL, "Ctrl+Shift+W" },
		{ "Print...", IDM_FILE_PRINT, "Ctrl+P" },
		{ "Exit", IDM_FILE_EXIT, "Alt+F4" },
		{ "Restore Recent Closed File", IDM_FILE_RESTORELASTCLOSEDFILE, "Ctrl+Shift+T" },
		{ "UPPERCASE", IDM_EDIT_UPPERCASE, "Ctrl+Shift+U" },
		{ "lowercase", IDM_EDIT_LOWERCASE, "Ctrl+U" },
		{ "Proper Case", IDM_EDIT_PROPERCASE_FORCE, "Alt+U" },
		{ "Proper Case (blend)", IDM_EDIT_PROPERCASE_BLEND, "Alt+Shift+U" },
		{ "Sentence case", IDM_EDIT_SENTENCECASE_FORCE, "Ctrl+Alt+U" },
		{ "Sentence case (blend)", IDM_EDIT_SENTENCECASE_BLEND, "Ctrl+Alt+Shift+U" },
		{ "Split Lines", IDM_EDIT_SPLIT_LINES, "Ctrl+I" },
		{ "Join Lines", IDM_EDIT_JOIN_LINES, "Ctrl+J" },
		{ "Move Up Current Line", IDM_EDIT_LINE_UP, "Ctrl+Shift+Up" },
		{ "Move Down Current Line", IDM_EDIT_LINE_DOWN, "Ctrl+Shift+Down" },
		{ "Insert Blank Line Above Current", IDM_EDIT_BLANKLINEABOVECURRENT, "Ctrl+Alt+Return" },
		{ "Insert Blank Line Below Current", IDM_EDIT_BLANKLINEBELOWCURRENT, "Ctrl+Alt+Shift+Return" },
		{ "Toggle Single Line Comment", IDM_EDIT_BLOCK_COMMENT, "Ctrl+Q" },
		{ "Single Line Comment", IDM_EDIT_BLOCK_COMMENT_SET, "Ctrl+K" },
		{ "Single Line Uncomment", IDM_EDIT_BLOCK_UNCOMMENT, "Ctrl+Shift+K" },
		{ "Block Comment", IDM_EDIT_STREAM_COMMENT, "Ctrl+Shift+Q" },
		{ "Function Completion", IDM_EDIT_AUTOCOMPLETE, "Ctrl+Space" },
		{ "Path Completion", IDM_EDIT_AUTOCOMPLETE_PATH, "Ctrl+Alt+Space" },
		{ "Word Completion", IDM_EDIT_AUTOCOMPLETE_CURRENTFILE, "Ctrl+Return" },
		{ "Function Parameters Hint", IDM_EDIT_FUNCCALLTIP, "Ctrl+Shift+Space" },
		{ "Column Editor...", IDM_EDIT_COLUMNMODE, "Alt+C" },
		{ "Text Direction RTL", IDM_EDIT_RTL, "Ctrl+Alt+R" },
		{ "Text Direction LTR", IDM_EDIT_LTR, "Ctrl+Alt+L" },
		{ "Toggle macro record", IDC_EDIT_TOGGLEMACRORECORDING, "Ctrl+Shift+R" },
		{ "Playback", IDM_MACRO_PLAYBACKRECORDEDMACRO, "Ctrl+Shift+P" },
		{ "Find...", IDM_SEARCH_FIND, "Ctrl+F" },
		{ "Find in Files...", IDM_SEARCH_FINDINFILES, "Ctrl+Shift+F" },
		{ "Find Next", IDM_SEARCH_FINDNEXT, "F3" },
		{ "Find Previous", IDM_SEARCH_FINDPREV, "Shift+F3" },
		{ "Select and Find Next", IDM_SEARCH_SETANDFINDNEXT, "Ctrl+F3" },
		{ "Select and Find Previous", IDM_SEARCH_SETANDFINDPREV, "Ctrl+Shift+F3" },
		{ "Find (Volatile) Next", IDM_SEARCH_VOLATILE_FINDNEXT, "Ctrl+Alt+F3" },
		{ "Find (Volatile) Previous", IDM_SEARCH_VOLATILE_FINDPREV, "Ctrl+Alt+Shift+F3" },
		{ "Replace...", IDM_SEARCH_REPLACE, "Ctrl+H" },
		{ "Incremental Search", IDM_SEARCH_FINDINCREMENT, "Ctrl+Alt+I" },
		{ "Search Results Window", IDM_FOCUS_ON_FOUND_RESULTS, "F7" },
		{ "Previous Search Result", IDM_SEARCH_GOTOPREVFOUND, "Shift+F4" },
		{ "Next Search Result", IDM_SEARCH_GOTONEXTFOUND, "F4" },
		{ "Go to...", IDM_SEARCH_GOTOLINE, "Ctrl+G" },
		{ "Go to Matching Brace", IDM_SEARCH_GOTOMATCHINGBRACE, "Ctrl+B" },
		{ "Select All Between Matching Braces", IDM_SEARCH_SELECTMATCHINGBRACES, "Ctrl+Alt+B" },
		{ "Mark...", IDM_SEARCH_MARK, "Ctrl+M" },
		{ "Previous mark using 1st Style", IDM_SEARCH_GOPREVMARKER1, "Ctrl+Shift+1" },
		{ "Previous mark using 2nd Style", IDM_SEARCH_GOPREVMARKER2, "Ctrl+Shift+2" },
		{ "Previous mark using 3rd Style", IDM_SEARCH_GOPREVMARKER3, "Ctrl+Shift+3" },
		{ "Previous mark using 4th Style", IDM_SEARCH_GOPREVMARKER4, "Ctrl+Shift+4" },
		{ "Previous mark using 5th Style", IDM_SEARCH_GOPREVMARKER5, "Ctrl+Shift+5" },
		{ "Previous mark created with Mark", IDM_SEARCH_GOPREVMARKER_DEF, "Ctrl+Shift+0" },
		{ "Next mark using 1st Style", IDM_SEARCH_GONEXTMARKER1, "Ctrl+1" },
		{ "Next mark using 2nd Style", IDM_SEARCH_GONEXTMARKER2, "Ctrl+2" },
		{ "Next mark using 3rd Style", IDM_SEARCH_GONEXTMARKER3, "Ctrl+3" },
		{ "Next mark using 4th Style", IDM_SEARCH_GONEXTMARKER4, "Ctrl+4" },
		{ "Next mark using 5th Style", IDM_SEARCH_GONEXTMARKER5, "Ctrl+5" },
		{ "Next mark created with Mark", IDM_SEARCH_GONEXTMARKER_DEF, "Ctrl+0" },
		{ "Toggle Bookmark", IDM_SEARCH_TOGGLE_BOOKMARK, "Ctrl+F2" },
		{ "Next Bookmark", IDM_SEARCH_NEXT_BOOKMARK, "F2" },
		{ "Previous Bookmark", IDM_SEARCH_PREV_BOOKMARK, "Shift+F2" },
		{ "Toggle Full Screen Mode", IDM_VIEW_FULLSCREENTOGGLE, "F11" },
		{ "Post-It", IDM_VIEW_POSTIT, "F12" },
		{ "1st Tab", IDM_VIEW_TAB1, "Ctrl+Num+1" },
		{ "2nd Tab", IDM_VIEW_TAB2, "Ctrl+Num+2" },
		{ "3rd Tab", IDM_VIEW_TAB3, "Ctrl+Num+3" },
		{ "4th Tab", IDM_VIEW_TAB4, "Ctrl+Num+4" },
		{ "5th Tab", IDM_VIEW_TAB5, "Ctrl+Num+5" },
		{ "6th Tab", IDM_VIEW_TAB6, "Ctrl+Num+6" },
		{ "7th Tab", IDM_VIEW_TAB7, "Ctrl+Num+7" },
		{ "8th Tab", IDM_VIEW_TAB8, "Ctrl+Num+8" },
		{ "9th Tab", IDM_VIEW_TAB9, "Ctrl+Num+9" },
		{ "Next Tab", IDM_VIEW_TAB_NEXT, "Ctrl+PgDown" },
		{ "Previous Tab", IDM_VIEW_TAB_PREV, "Ctrl+PgUp" },
		{ "Move Tab Forward", IDM_VIEW_TAB_MOVEFORWARD, "Ctrl+Shift+PgDown" },
		{ "Move Tab Backward", IDM_VIEW_TAB_MOVEBACKWARD, "Ctrl+Shift+PgUp" },
		{ "Switch to previous document", IDC_PREV_DOC, "Ctrl+Shift+Tab" },
		{ "Switch to next document", IDC_NEXT_DOC, "Ctrl+Tab" },
		{ "Hide Lines", IDM_VIEW_HIDELINES, "Alt+H" },
		{ "Focus on Another View", IDM_VIEW_SWITCHTO_OTHER_VIEW, "F8" },
		{ "Fold All", IDM_VIEW_FOLDALL, "Alt+0" },
		{ "Unfold All", IDM_VIEW_UNFOLDALL, "Alt+Shift+0" },
		{ "Collapse Current Level", IDM_VIEW_FOLD_CURRENT, "Ctrl+Alt+F" },
		{ "Uncollapse Current Level", IDM_VIEW_UNFOLD_CURRENT, "Ctrl+Alt+Shift+F" },
		{ "Collapse Level 1", IDM_VIEW_FOLD_1, "Alt+1" },
		{ "Collapse Level 2", IDM_VIEW_FOLD_2, "Alt+2" },
		{ "Collapse Level 3", IDM_VIEW_FOLD_3, "Alt+3" },
		{ "Collapse Level 4", IDM_VIEW_FOLD_4, "Alt+4" },
		{ "Collapse Level 5", IDM_VIEW_FOLD_5, "Alt+5" },
		{ "Collapse Level 6", IDM_VIEW_FOLD_6, "Alt+6" },
		{ "Collapse Level 7", IDM_VIEW_FOLD_7, "Alt+7" },
		{ "Collapse Level 8", IDM_VIEW_FOLD_8, "Alt+8" },
		{ "Uncollapse Level 1", IDM_VIEW_UNFOLD_1, "Alt+Shift+1" },
		{ "Uncollapse Level 2", IDM_VIEW_UNFOLD_2, "Alt+Shift+2" },
		{ "Uncollapse Level 3", IDM_VIEW_UNFOLD_3, "Alt+Shift+3" },
		{ "Uncollapse Level 4", IDM_VIEW_UNFOLD_4, "Alt+Shift+4" },
		{ "Uncollapse Level 5", IDM_VIEW_UNFOLD_5, "Alt+Shift+5" },
		{ "Uncollapse Level 6", IDM_VIEW_UNFOLD_6, "Alt+Shift+6" },
		{ "Uncollapse Level 7", IDM_VIEW_UNFOLD_7, "Alt+Shift+7" },
		{ "Uncollapse Level 8", IDM_VIEW_UNFOLD_8, "Alt+Shift+8" },
		{ "About", IDM_ABOUT, "F1" },
		{ "Run...", IDM_EXECUTE, "F5" },
		// the cut / copy / paste keys of the menu (upstream winKeyDefs)
		{ "Cut", IDM_EDIT_CUT, "Ctrl+X" },
		{ "Cut (Shift+Del)", IDM_EDIT_CUT, "Shift+Del" },
		{ "Copy", IDM_EDIT_COPY, "Ctrl+C" },
		{ "Copy (Ctrl+Ins)", IDM_EDIT_COPY, "Ctrl+Ins" },
		{ "Paste", IDM_EDIT_PASTE, "Ctrl+V" },
		{ "Paste (Shift+Ins)", IDM_EDIT_PASTE, "Shift+Ins" },
		// shortcuts.xml: the default macro and Run commands
		{ "Trim Trailing Space and Save", ID_MACRO, "Alt+Shift+S" },
		{ "Get PHP help", ID_USER_CMD, "Alt+F1" },
		{ "Google Search", ID_USER_CMD + 1, "Alt+F2" },
		{ "Wikipedia Search", ID_USER_CMD + 2, "Alt+F3" },
		{ "Open selected file path in new instance", ID_USER_CMD + 3, "Alt+F6" },
	};

	struct SciKey { const char* _name; int _message; const char* _keys; };

	// the Notepad++ default keys of the Scintilla commands (Settings > Shortcut Mapper > Scintilla commands)
	const SciKey sciKeys[] = {
		{ "SCI_SELECTALL", SCI_SELECTALL, "Ctrl+A" },
		{ "SCI_CLEAR", SCI_CLEAR, "Del" },
		{ "SCI_UNDO", SCI_UNDO, "Ctrl+Z" },
		{ "SCI_UNDO (Alt+Backspace)", SCI_UNDO, "Alt+Backspace" },
		{ "SCI_REDO", SCI_REDO, "Ctrl+Y" },
		{ "SCI_REDO (Ctrl+Shift+Z)", SCI_REDO, "Ctrl+Shift+Z" },
		{ "SCI_NEWLINE", SCI_NEWLINE, "Return" },
		{ "SCI_NEWLINE (Shift+Enter)", SCI_NEWLINE, "Shift+Return" },
		{ "SCI_TAB", SCI_TAB, "Tab" },
		{ "SCI_BACKTAB", SCI_BACKTAB, "Shift+Tab" },
		{ "SCI_ZOOMIN", SCI_ZOOMIN, "Ctrl+Num++" },
		{ "SCI_ZOOMOUT", SCI_ZOOMOUT, "Ctrl+Num+-" },
		{ "SCI_SETZOOM", SCI_SETZOOM, "Ctrl+Num+/" },
		{ "SCI_SELECTIONDUPLICATE", SCI_SELECTIONDUPLICATE, "Ctrl+D" },
		{ "SCI_EDITTOGGLEOVERTYPE", SCI_EDITTOGGLEOVERTYPE, "Ins" },
		{ "SCI_LINEDOWN", SCI_LINEDOWN, "Down" },
		{ "SCI_LINEDOWNEXTEND", SCI_LINEDOWNEXTEND, "Shift+Down" },
		{ "SCI_LINEDOWNRECTEXTEND", SCI_LINEDOWNRECTEXTEND, "Alt+Shift+Down" },
		{ "SCI_LINESCROLLDOWN", SCI_LINESCROLLDOWN, "Ctrl+Down" },
		{ "SCI_LINEUP", SCI_LINEUP, "Up" },
		{ "SCI_LINEUPEXTEND", SCI_LINEUPEXTEND, "Shift+Up" },
		{ "SCI_LINEUPRECTEXTEND", SCI_LINEUPRECTEXTEND, "Alt+Shift+Up" },
		{ "SCI_LINESCROLLUP", SCI_LINESCROLLUP, "Ctrl+Up" },
		{ "SCI_PARADOWN", SCI_PARADOWN, "Ctrl+]" },
		{ "SCI_PARADOWNEXTEND", SCI_PARADOWNEXTEND, "Ctrl+Shift+]" },
		{ "SCI_PARAUP", SCI_PARAUP, "Ctrl+[" },
		{ "SCI_PARAUPEXTEND", SCI_PARAUPEXTEND, "Ctrl+Shift+[" },
		{ "SCI_CHARLEFT", SCI_CHARLEFT, "Left" },
		{ "SCI_CHARLEFTEXTEND", SCI_CHARLEFTEXTEND, "Shift+Left" },
		{ "SCI_CHARLEFTRECTEXTEND", SCI_CHARLEFTRECTEXTEND, "Alt+Shift+Left" },
		{ "SCI_CHARRIGHT", SCI_CHARRIGHT, "Right" },
		{ "SCI_CHARRIGHTEXTEND", SCI_CHARRIGHTEXTEND, "Shift+Right" },
		{ "SCI_CHARRIGHTRECTEXTEND", SCI_CHARRIGHTRECTEXTEND, "Alt+Shift+Right" },
		{ "SCI_WORDLEFT", SCI_WORDLEFT, "Ctrl+Left" },
		{ "SCI_WORDLEFTEXTEND", SCI_WORDLEFTEXTEND, "Ctrl+Shift+Left" },
		{ "SCI_WORDRIGHT", SCI_WORDRIGHT, "Ctrl+Right" },
		{ "SCI_WORDRIGHTENDEXTEND", SCI_WORDRIGHTENDEXTEND, "Ctrl+Shift+Right" },
		{ "SCI_WORDPARTLEFT", SCI_WORDPARTLEFT, "Ctrl+/" },
		{ "SCI_WORDPARTLEFTEXTEND", SCI_WORDPARTLEFTEXTEND, "Ctrl+Shift+/" },
		{ "SCI_WORDPARTRIGHT", SCI_WORDPARTRIGHT, "Ctrl+\\" },
		{ "SCI_WORDPARTRIGHTEXTEND", SCI_WORDPARTRIGHTEXTEND, "Ctrl+Shift+\\" },
		{ "SCI_HOMEDISPLAY", SCI_HOMEDISPLAY, "Alt+Home" },
		{ "SCI_VCHOMEWRAPEXTEND", SCI_VCHOMEWRAPEXTEND, "Shift+Home" },
		{ "SCI_VCHOMERECTEXTEND", SCI_VCHOMERECTEXTEND, "Alt+Shift+Home" },
		{ "SCI_VCHOMEWRAP", SCI_VCHOMEWRAP, "Home" },
		{ "SCI_LINEENDWRAPEXTEND", SCI_LINEENDWRAPEXTEND, "Shift+End" },
		{ "SCI_LINEENDRECTEXTEND", SCI_LINEENDRECTEXTEND, "Alt+Shift+End" },
		{ "SCI_LINEENDDISPLAY", SCI_LINEENDDISPLAY, "Alt+End" },
		{ "SCI_LINEENDWRAP", SCI_LINEENDWRAP, "End" },
		{ "SCI_DOCUMENTSTART", SCI_DOCUMENTSTART, "Ctrl+Home" },
		{ "SCI_DOCUMENTSTARTEXTEND", SCI_DOCUMENTSTARTEXTEND, "Ctrl+Shift+Home" },
		{ "SCI_DOCUMENTEND", SCI_DOCUMENTEND, "Ctrl+End" },
		{ "SCI_DOCUMENTENDEXTEND", SCI_DOCUMENTENDEXTEND, "Ctrl+Shift+End" },
		{ "SCI_PAGEUP", SCI_PAGEUP, "PgUp" },
		{ "SCI_PAGEUPEXTEND", SCI_PAGEUPEXTEND, "Shift+PgUp" },
		{ "SCI_PAGEUPRECTEXTEND", SCI_PAGEUPRECTEXTEND, "Alt+Shift+PgUp" },
		{ "SCI_PAGEDOWN", SCI_PAGEDOWN, "PgDown" },
		{ "SCI_PAGEDOWNEXTEND", SCI_PAGEDOWNEXTEND, "Shift+PgDown" },
		{ "SCI_PAGEDOWNRECTEXTEND", SCI_PAGEDOWNRECTEXTEND, "Alt+Shift+PgDown" },
		{ "SCI_DELETEBACK", SCI_DELETEBACK, "Backspace" },
		{ "SCI_DELETEBACK (Shift+Backspace)", SCI_DELETEBACK, "Shift+Backspace" },
		{ "SCI_DELWORDLEFT", SCI_DELWORDLEFT, "Ctrl+Backspace" },
		{ "SCI_DELWORDRIGHT", SCI_DELWORDRIGHT, "Ctrl+Del" },
		{ "SCI_DELLINELEFT", SCI_DELLINELEFT, "Ctrl+Shift+Backspace" },
		{ "SCI_DELLINERIGHT", SCI_DELLINERIGHT, "Ctrl+Shift+Del" },
		{ "SCI_LINEDELETE", SCI_LINEDELETE, "Ctrl+Shift+L" },
		{ "SCI_LINECUT", SCI_LINECUT, "Ctrl+L" },
		{ "SCI_LINECOPY", SCI_LINECOPY, "Ctrl+Shift+X" },
		{ "SCI_LINETRANSPOSE", SCI_LINETRANSPOSE, "Ctrl+T" },
		{ "SCI_CANCEL", SCI_CANCEL, "Esc" },
	};

	// the menu command a Scintilla key also reaches (upstream: the Scintilla command of a menu item)
	int menuCommandOf(int sciMessage)
	{
		for (const ScintillaKeyMap& skm : NppParameters::getInstance().getScintillaKeyList())
			if (static_cast<int>(skm.getScintillaKeyID()) == sciMessage)
				return skm.getMenuCmdID();
		return 0;
	}
}

class TestKeys : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir _tmp;
	SilentUiServices _ui;
	NativeLangSpeaker _nativeLangSpeaker;
	std::unique_ptr<RecordingNpp> _npp;
	std::vector<int> _recordedSci;

	ScintillaEditView* view() { return _npp->getActiveEditView(); }

	void press(const QKeySequence& seq)
	{
		const QKeyCombination kc = seq[0];
		QTest::keyClick(view(), kc.key(), kc.keyboardModifiers());
	}

private slots:
	void initTestCase()
	{
		QVERIFY(_tmp.isValid());
		::setenv("XDG_CONFIG_HOME", (_tmp.path() + "/config").toUtf8().constData(), 1);
		::setenv("XDG_DATA_HOME", (_tmp.path() + "/share").toUtf8().constData(), 1);
		::setenv("NPLX_DATA_DIR", NPLX_TEST_SOURCE_DATA_DIR, 1);

		nplx::setUiServices(&_ui);
		NppParameters& nppParam = NppParameters::getInstance();
		nppParam.load();
		_nativeLangSpeaker.init(nppParam.getNativeLang());
		nppParam.setNativeLangSpeaker(&_nativeLangSpeaker);

		_npp = std::make_unique<RecordingNpp>();
		_npp->init();
		_npp->resize(1100, 700);
		_npp->show();
		QVERIFY(QTest::qWaitForWindowExposed(_npp.get()));
		_npp->activateWindow();

		// Scintilla's macro recorder tells which Scintilla command a key ran
		for (ScintillaEditView* v : { _npp->getActiveEditView() })
			connect(v, &ScintillaEditBase::notify, this, [this](Scintilla::NotificationData* scn) {
				auto* n = reinterpret_cast<SCNotification*>(scn);
				if (n->nmhdr.code == SCN_MACRORECORD)
					_recordedSci.push_back(static_cast<int>(n->message));
			});
	}

	void cleanupTestCase()
	{
		_npp->_record = false;
		_npp.reset();
		nplx::setUiServices(nullptr);
	}

	void menuCommandKeys_data()
	{
		QTest::addColumn<int>("cmdID");
		QTest::addColumn<QString>("keys");
		for (const MenuKey& k : menuKeys)
			QTest::newRow(k._description) << k._cmdID << QString::fromLatin1(k._keys);
	}

	void menuCommandKeys()
	{
		QFETCH(int, cmdID);
		QFETCH(QString, keys);

		const bool isShortcutsXmlCommand = (cmdID >= ID_MACRO && cmdID < ID_MACRO_LIMIT) || (cmdID >= ID_USER_CMD && cmdID < ID_USER_CMD_LIMIT);
		if (!isShortcutsXmlCommand && !Notepad_plus::isCommandPorted(cmdID))
			QSKIP("not yet ported to NotePadLX");

		// a text with a selection, so that the commands that need one are enabled
		_npp->_record = false;
		if (cmdID == IDM_FILE_RELOAD)
		{
			// Reload needs a file on disk
			const QString path = _tmp.path() + "/reload.txt";
			QFile f(path);
			QVERIFY(f.open(QIODevice::WriteOnly));
			f.write("text\n");
			f.close();
			_npp->switchToFile(_npp->doOpen(path.toStdWString()));
		}
		else if (_npp->getActiveEditView()->getCurrentBuffer()->isUntitled() == false)
		{
			_npp->command(IDM_FILE_NEW);
		}
		view()->setFocus();
		view()->setText("int main() { return 0; }\nsecond line\n");
		view()->execute(SCI_SETSEL, 0, 3);
		if (cmdID == IDM_MACRO_PLAYBACKRECORDEDMACRO)
		{
			// Playback needs a recorded macro
			_npp->command(IDM_MACRO_STARTRECORDINGMACRO);
			view()->execute(SCI_CHARRIGHT);
			_npp->command(IDM_MACRO_STOPRECORDINGMACRO);
		}
		if (cmdID == IDM_SEARCH_GOTOMATCHINGBRACE || cmdID == IDM_SEARCH_SELECTMATCHINGBRACES)
			view()->execute(SCI_GOTOPOS, 11); // on '{': the brace commands are enabled
		QCoreApplication::processEvents(); // SCN_UPDATEUI: the selection enables Cut, Copy, the case commands

		_npp->_commands.clear();
		_npp->_record = true;
		press(QKeySequence::fromString(keys));
		_npp->_record = false;
		QVERIFY2(!_npp->_commands.empty(), qPrintable(keys + " reached no command"));
		QCOMPARE(_npp->_commands.back(), cmdID);
	}

	void scintillaKeys_data()
	{
		QTest::addColumn<int>("message");
		QTest::addColumn<QString>("keys");
		for (const SciKey& k : sciKeys)
			QTest::newRow(k._name) << k._message << QString::fromLatin1(k._keys);
	}

	void scintillaKeys()
	{
		QFETCH(int, message);
		QFETCH(QString, keys);

		_npp->_record = false;
		view()->setFocus();
		view()->setText("alpha beta\ngamma delta\n\nepsilon zeta\n");
		view()->execute(SCI_GOTOPOS, 14);
		view()->execute(SCI_SETZOOM, 0);
		const intptr_t zoom = view()->execute(SCI_GETZOOM);
		const bool overtype = view()->execute(SCI_GETOVERTYPE) != 0;
		view()->execute(SCI_SETSEL, 12, 14);
		if (message == SCI_UNDO || message == SCI_REDO)
		{
			// an undo history: a change, and for Redo, that change undone
			view()->execute(SCI_EMPTYUNDOBUFFER);
			view()->execute(SCI_REPLACESEL, 0, reinterpret_cast<sptr_t>("X"));
			if (message == SCI_REDO)
				view()->execute(SCI_UNDO);
		}
		const std::string textBefore = view()->getText();
		QCoreApplication::processEvents();

		_recordedSci.clear();
		_npp->_commands.clear();
		_npp->_record = true;
		view()->execute(SCI_STARTRECORD);
		press(QKeySequence::fromString(keys));
		view()->execute(SCI_STOPRECORD);
		_npp->_record = false;

		// the menu item of the Scintilla command is a way too (e.g. Ctrl+Z = Edit > Undo)
		const int menuCmd = menuCommandOf(message);
		const bool viaMenu = menuCmd != 0 && !_npp->_commands.empty() && _npp->_commands.back() == menuCmd;
		bool viaScintilla = std::find(_recordedSci.begin(), _recordedSci.end(), message) != _recordedSci.end();

		// the commands Scintilla does not record: check their effect
		if (!viaScintilla && !viaMenu)
		{
			switch (message)
			{
				case SCI_ZOOMIN: viaScintilla = view()->execute(SCI_GETZOOM) > zoom; break;
				case SCI_ZOOMOUT: viaScintilla = view()->execute(SCI_GETZOOM) < zoom; break;
				case SCI_SETZOOM: viaScintilla = view()->execute(SCI_GETZOOM) == 0; break;
				case SCI_EDITTOGGLEOVERTYPE: viaScintilla = (view()->execute(SCI_GETOVERTYPE) != 0) != overtype; break;
				case SCI_CANCEL: viaScintilla = view()->execute(SCI_GETSELECTIONEMPTY) != 0; break;
				case SCI_UNDO:
				case SCI_REDO: viaScintilla = view()->getText() != textBefore; break;
				// a new line is recorded as the text it inserts (upstream turns it into SCI_NEWLINE)
				case SCI_NEWLINE: viaScintilla = std::count(_recordedSci.begin(), _recordedSci.end(), SCI_REPLACESEL) > 0; break;
				default: break;
			}
		}
		if (view()->execute(SCI_GETOVERTYPE))
			view()->execute(SCI_SETOVERTYPE, 0);

		QVERIFY2(viaScintilla || viaMenu, qPrintable(QStringLiteral("%1: Scintilla ran %2, menu commands %3").arg(keys)
			.arg(_recordedSci.empty() ? QStringLiteral("nothing") : QString::number(_recordedSci.back()))
			.arg(_npp->_commands.empty() ? QStringLiteral("none") : QString::number(_npp->_commands.back()))));
	}

	void noKeyIsAmbiguous()
	{
		// two QActions with the same key: Qt runs neither ("Ambiguous shortcut overload")
		std::map<QString, QStringList> owners;
		for (QAction* a : _npp->findChildren<QAction*>())
			for (const QKeySequence& k : a->shortcuts())
				if (a->isEnabled() || true)
					owners[k.toString()] << a->text();
		for (const auto& [key, actions] : owners)
			QVERIFY2(actions.size() == 1, qPrintable(key + ": " + actions.join(QStringLiteral(", "))));
	}
};

QTEST_MAIN(TestKeys)
#include "tst_keys.moc"
