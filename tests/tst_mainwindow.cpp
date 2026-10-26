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

// Phase 2 tests: the Qt port of Notepad_plus (tabs, views, menus built from the upstream .rc,
// styling from stylers.xml, commands). Set NPLX_SCREENSHOT_DIR to save screenshots.

#include <QtTest>
#include <QAction>
#include <QMenuBar>
#include <QMenu>
#include <QTemporaryDir>
#include <QLabel>
#include <QStatusBar>
#include <QTimer>

#include <algorithm>

#include <cstdlib>
#include <fstream>

#include "Buffer.h"
#include "DocTabView.h"
#include "Notepad_plus.h"
#include "Parameters.h"
#include "QtUiServices.h"
#include "ScintillaEditView.h"
#include "localization.h"
#include "menuCmdID.h"

namespace
{
	// Answers every message box with the default (IDYES / IDOK) without showing it
	class SilentUiServices : public QtUiServices
	{
	public:
		int messageBox(const std::wstring&, const std::wstring&, unsigned int type) override
		{
			++_count;
			const unsigned int buttons = type & 0x0F;
			return (buttons == MB_YESNO || buttons == MB_YESNOCANCEL) ? _answerYesNo : IDOK;
		}
		int saveOrNotMultiBox(const std::wstring&, const std::wstring&) override
		{
			++_count;
			return _answerYesNo;
		}
		int _count = 0;
		int _answerYesNo = IDNO;
	};

	void writeFile(const QString& path, const QByteArray& bytes)
	{
		std::ofstream f(path.toStdString(), std::ios::binary);
		f.write(bytes.constData(), bytes.size());
	}

	QByteArray readFile(const QString& path)
	{
		QFile f(path);
		return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
	}
}

class TestMainWindow : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir _tmp;
	SilentUiServices _ui;
	NativeLangSpeaker _nativeLangSpeaker;
	std::unique_ptr<Notepad_plus> _npp;

	ScintillaEditView* currentView() { return _npp->getActiveEditView(); }
	DocTabView* tabOf(int view) { return _npp->getDocTab(view); }
	QAction* action(int id)
	{
		for (QAction* a : _npp->menuBar()->findChildren<QAction*>())
			if (a->data().toInt() == id)
				return a;
		return nullptr;
	}
	void screenshot(const char* name)
	{
		const QByteArray dir = qgetenv("NPLX_SCREENSHOT_DIR");
		if (!dir.isEmpty())
		{
			QCoreApplication::processEvents();
			_npp->grab().save(QString::fromUtf8(dir) + "/" + name);
		}
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

		_npp = std::make_unique<Notepad_plus>();
		_npp->init();
		_npp->resize(1000, 640);
		_npp->show();
		QVERIFY(QTest::qWaitForWindowExposed(_npp.get()));
	}

	void cleanupTestCase()
	{
		_npp.reset();
		nplx::setUiServices(nullptr);
	}

	void menusFollowUpstream()
	{
		// Top-level menus from Notepad_plus.rc (one Language menu kept)
		QStringList titles;
		for (QAction* a : _npp->menuBar()->actions())
			titles << a->text();
		QCOMPARE(titles, (QStringList{ "&File", "&Edit", "&Search", "&View", "E&ncoding", "&Language",
			"Se&ttings", "T&ools", "&Macro", "&Run", "&Plugins", "&Window", "&?" }));

		// Default shortcuts from upstream winKeyDefs
		QCOMPARE(action(IDM_FILE_SAVE)->shortcut(), QKeySequence(QStringLiteral("Ctrl+S")));
		QCOMPARE(action(IDM_EDIT_LINE_UP)->shortcut(), QKeySequence(QStringLiteral("Ctrl+Shift+Up")));
		QCOMPARE(action(IDM_FILE_SAVEAS)->shortcut(), QKeySequence(QStringLiteral("Ctrl+Alt+S")));
		QVERIFY(action(IDM_FILE_SAVE)->isEnabled() == false); // nothing to save yet
		QVERIFY(action(IDM_LANG_CPP));

		// A command that is not ported is disabled, not silently missing
		QVERIFY(!Notepad_plus::isCommandPorted(IDM_SETTING_IMPORTPLUGIN));
		QVERIFY(action(IDM_SETTING_IMPORTPLUGIN));
		QVERIFY(!action(IDM_SETTING_IMPORTPLUGIN)->isEnabled());
		QCOMPARE(action(IDM_SETTING_IMPORTPLUGIN)->toolTip(), QStringLiteral("Not yet ported to NotePadLX"));
	}

	void windowsOnlyItemsAreRemoved()
	{
		// NPLX: Windows-only commands are not in the menus (no Linux equivalent)
		for (int id : { IDM_FILE_OPEN_POWERSHELL, IDM_CONFUPDATERPROXY, IDM_ONLINEDOCUMENT, IDM_FORUM })
			QVERIFY2(!action(id), qPrintable(QString::number(id)));

		// Linux / NotePadLX names
		QCOMPARE(action(IDM_FILE_OPEN_FOLDER)->text(), QStringLiteral("File Manager"));
		QCOMPARE(action(IDM_FILE_OPEN_CMD)->text(), QStringLiteral("Terminal"));
		QCOMPARE(action(IDM_FILE_DELETE)->text(), QStringLiteral("Move to &Trash"));
		QCOMPARE(action(IDM_HOMESWEETHOME)->text(), QStringLiteral("NotePadLX on GitHub"));
		QCOMPARE(action(IDM_PROJECTPAGE)->text(), QStringLiteral("Report an Issue"));
		QCOMPARE(action(IDM_ABOUT)->text(), QStringLiteral("About NotePadLX"));
		QCOMPARE(action(IDM_UPDATE_NPP)->text(), QStringLiteral("Check for &Updates...")); // replaces the WinGUp updater
		QCOMPARE(QStringLiteral(NPLX_HOMEPAGE_URL), QStringLiteral("https://github.com/ClaudiuJitea/NotePadLX"));

		// no doubled, leading or trailing separators left behind
		for (QMenu* menu : _npp->menuBar()->findChildren<QMenu*>())
		{
			const QList<QAction*> items = menu->actions();
			if (items.isEmpty())
				continue;
			QVERIFY2(!items.last()->isSeparator(), qPrintable(menu->title()));
			for (qsizetype i = 1; i < items.size(); ++i)
				QVERIFY2(!(items[i]->isSeparator() && items[i - 1]->isSeparator()), qPrintable(menu->title()));
		}
	}

	void openFileHighlightsWithStylers()
	{
		const QString path = _tmp.path() + "/hello.cpp";
		writeFile(path, "#include <cstdio>\n// comment\nint main() { return 0; }\n");

		BufferID id = _npp->doOpen(path.toStdWString());
		QVERIFY(id != BUFFER_INVALID);
		QVERIFY(_npp->switchToFile(id));

		// The initial empty "new 1" tab was replaced (upstream loadBufferIntoView behaviour)
		QCOMPARE(tabOf(MAIN_VIEW)->count(), 1);
		QCOMPARE(tabOf(MAIN_VIEW)->tabText(0), QStringLiteral("hello.cpp"));
		QVERIFY(_npp->windowTitle().startsWith(path));

		ScintillaEditView* view = currentView();
		QCOMPARE(QString::fromStdString(view->lexerName()), QStringLiteral("cpp"));
		view->execute(SCI_COLOURISE, 0, -1);
		// NotePadLX default theme, themes/Solarized.xml: cpp INSTRUCTION WORD (style 5) 859900, COMMENT LINE (2) 586E75
		QCOMPARE(view->execute(SCI_STYLEGETFORE, 5), LRESULT(RGB(0x85, 0x99, 0x00)));
		QCOMPARE(view->execute(SCI_STYLEGETFORE, 2), LRESULT(RGB(0x58, 0x6E, 0x75)));
		QCOMPARE(view->execute(SCI_STYLEGETBACK, STYLE_DEFAULT), LRESULT(RGB(0x00, 0x2B, 0x36)));
		QCOMPARE(view->execute(SCI_GETSTYLEAT, 29), LRESULT(SCE_C_WORD2)); // "int" (offset 29) is a type1 keyword
		QVERIFY(action(IDM_LANG_CPP)->isChecked());
		screenshot("mainwindow-cpp.png");
	}

	void editSaveAndDirtyState()
	{
		ScintillaEditView* view = currentView();
		Buffer* buf = view->getCurrentBuffer();
		QVERIFY(!buf->isDirty());

		view->execute(SCI_DOCUMENTEND);
		view->execute(SCI_ADDTEXT, 10, reinterpret_cast<LPARAM>("// edited\n"));
		QVERIFY(buf->isDirty());
		QVERIFY(_npp->windowTitle().startsWith(QStringLiteral("*")));
		QVERIFY(action(IDM_FILE_SAVE)->isEnabled());

		QVERIFY(!buf->isUntitled()); // never open a file dialog in the test
		_npp->command(IDM_FILE_SAVE);
		QVERIFY(!buf->isDirty());
		QVERIFY(readFile(QString::fromWCharArray(buf->getFullPathName())).endsWith("// edited\n"));
	}

	void languageMenuChangesLexer()
	{
		_npp->command(IDM_LANG_PYTHON);
		QCOMPARE(QString::fromStdString(currentView()->lexerName()), QStringLiteral("python"));
		QVERIFY(action(IDM_LANG_PYTHON)->isChecked());
		QVERIFY(!action(IDM_LANG_CPP)->isChecked());
		_npp->command(IDM_LANG_CPP);
		QCOMPARE(QString::fromStdString(currentView()->lexerName()), QStringLiteral("cpp"));
	}

	void newDocumentsAndClose()
	{
		const int before = tabOf(MAIN_VIEW)->count();
		_npp->command(IDM_FILE_NEW);
		_npp->command(IDM_FILE_NEW);
		QCOMPARE(tabOf(MAIN_VIEW)->count(), before + 2);
		QCOMPARE(tabOf(MAIN_VIEW)->tabText(before + 1), QStringLiteral("new 2"));

		// Closing a clean untitled document needs no question
		_ui._count = 0;
		_npp->command(IDM_FILE_CLOSE);
		QCOMPARE(tabOf(MAIN_VIEW)->count(), before + 1);
		QCOMPARE(_ui._count, 0);

		// Closing a dirty one asks; "No" discards
		currentView()->execute(SCI_ADDTEXT, 3, reinterpret_cast<LPARAM>("abc"));
		_ui._answerYesNo = IDNO;
		_npp->command(IDM_FILE_CLOSE);
		QCOMPARE(_ui._count, 1);
		QCOMPARE(tabOf(MAIN_VIEW)->count(), before);
	}

	void eolAndEncodingCommands()
	{
		Buffer* buf = currentView()->getCurrentBuffer();
		_npp->command(IDM_FORMAT_TODOS);
		QCOMPARE(int(buf->getEolFormat()), int(EolType::windows));
		QVERIFY(action(IDM_FORMAT_TODOS)->isChecked());
		_npp->command(IDM_FORMAT_TOUNIX);
		QCOMPARE(int(buf->getEolFormat()), int(EolType::unix));

		_npp->command(IDM_FORMAT_UTF_8); // UTF-8 with BOM
		QCOMPARE(int(buf->getUnicodeMode()), int(uniUTF8));
		QVERIFY(action(IDM_FORMAT_UTF_8)->isChecked());
		QVERIFY(!buf->isUntitled()); // never open a file dialog in the test
		_npp->command(IDM_FILE_SAVE);
		QVERIFY(readFile(QString::fromWCharArray(buf->getFullPathName())).startsWith("\xEF\xBB\xBF"));
	}

	void cloneAndMoveToOtherView()
	{
		BufferID id = currentView()->getCurrentBufferID();
		_npp->command(IDM_VIEW_CLONE_TO_ANOTHER_VIEW);
		QVERIFY(_npp->bothActive());
		QCOMPARE(_npp->currentView(), SUB_VIEW);
		QCOMPARE(currentView()->getCurrentBufferID(), id); // same buffer shown in both views
		QCOMPARE(tabOf(SUB_VIEW)->getIndexByBuffer(id), 0);
		screenshot("mainwindow-two-views.png");

		// Closing the clone in the sub view hides that view again (upstream canHideView)
		_npp->command(IDM_FILE_CLOSE);
		QVERIFY(!_npp->bothActive());
		QCOMPARE(_npp->currentView(), MAIN_VIEW);
	}

	void sessionSaveAndRestore()
	{
		// Two files, a bookmark in the second, plus an unsaved untitled document (snapshot mode)
		NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();
		const bool wasSnapshotMode = nppGUI._isSnapshotMode;
		nppGUI._isSnapshotMode = true; // NPLX default: off (data/config.model.xml)
		const QString a = _tmp.path() + "/a.txt";
		const QString b = _tmp.path() + "/b.py";
		writeFile(a, "alpha\n");
		writeFile(b, "line1\nline2\nline3\n");
		_npp->switchToFile(_npp->doOpen(a.toStdWString()));
		_npp->switchToFile(_npp->doOpen(b.toStdWString()));
		_npp->bookmarkAdd(1);
		_npp->command(IDM_FILE_NEW);
		currentView()->execute(SCI_ADDTEXT, 9, reinterpret_cast<LPARAM>("unsaved!\n"));
		QVERIFY(NppParameters::getInstance().getNppGUI().isSnapshotMode());
		QVERIFY(MainFileManager.backupCurrentBuffer());
		Buffer* untitled = currentView()->getCurrentBuffer();
		const std::wstring backupPath = untitled->getBackupFileName();
		QVERIFY(!backupPath.empty());
		QVERIFY(doesFileExist(backupPath.c_str()));
		_npp->switchToFile(_npp->getDocTab(MAIN_VIEW)->findBufferByName(b.toStdWString().c_str()));

		Session session;
		_npp->getCurrentOpenedFiles(session, true);
		const std::wstring sessionFile = (_tmp.path() + "/session-ui.xml").toStdWString();
		NppParameters::getInstance().writeSession(session, sessionFile.c_str());

		// Close everything (snapshot mode: no question, backups are kept)
		_ui._count = 0;
		QVERIFY(_npp->fileCloseAll(false, true));
		QCOMPARE(_ui._count, 0);
		QCOMPARE(_npp->getDocTab(MAIN_VIEW)->count(), 1);

		Session loaded;
		QVERIFY(NppParameters::getInstance().loadSession(loaded, sessionFile.c_str(), true));
		QVERIFY(_npp->loadSession(loaded, true));

		DocTabView* tab = _npp->getDocTab(MAIN_VIEW);
		QVERIFY(tab->findBufferByName(a.toStdWString().c_str()) != BUFFER_INVALID);
		BufferID bId = tab->findBufferByName(b.toStdWString().c_str());
		QVERIFY(bId != BUFFER_INVALID);
		QCOMPARE(currentView()->getCurrentBufferID(), bId); // the active document is restored
		QCOMPARE(int(bId->getLangType()), int(L_PYTHON));
		QVERIFY(_npp->bookmarkPresent(1));

		// The untitled document comes back from its backup, still dirty
		bool foundUntitled = false;
		for (size_t i = 0; i < tab->nbItem(); ++i)
		{
			Buffer* buf = tab->getBufferByIndex(i);
			if (buf->isUntitled())
			{
				foundUntitled = true;
				QVERIFY(buf->isDirty());
				currentView()->execute(SCI_SETDOCPOINTER, 0, buf->getDocument());
				QCOMPARE(QString::fromStdString(currentView()->getText()), QStringLiteral("unsaved!\n"));
				_npp->activateBuffer(bId, MAIN_VIEW, true);
			}
		}
		QVERIFY(foundUntitled);
		screenshot("mainwindow-session.png");
		nppGUI._isSnapshotMode = wasSnapshotMode;
	}

	void closeAsksToSaveUnsavedChanges()
	{
		// NotePadLX default (no session snapshot): quitting with an unsaved document asks to save it
		QVERIFY(!NppParameters::getInstance().getNppGUI().isSnapshotMode());
		_npp->command(IDM_FILE_NEW);
		currentView()->execute(SCI_ADDTEXT, 6, reinterpret_cast<LPARAM>("draft\n"));
		QVERIFY(currentView()->getCurrentBuffer()->isDirty());

		_ui._count = 0;
		_ui._answerYesNo = IDCANCEL;
		QVERIFY(!_npp->close()); // Cancel: the application stays open
		QVERIFY(_ui._count >= 1);
		QVERIFY(_npp->isVisible());
		QVERIFY(currentView()->getCurrentBuffer()->isDirty());

		_ui._answerYesNo = IDNO;
		_npp->command(IDM_FILE_CLOSE); // discard the draft
		_ui._count = 0;
	}

	void commandLineParameters()
	{
		// upstream loadCommandlineParams: -n<line> -lpython -ro on a file
		const QString path = _tmp.path() + "/cmdline.txt";
		writeFile(path, "one\ntwo\nthree\nfour\n");
		CmdLineParamsDTO dto;
		dto._line2go = 3;
		dto._column2go = -1;
		dto._pos2go = -1;
		dto._langType = NppParameters::getLangIDFromStr(L"python");
		dto._isReadOnly = true;
		_npp->loadCommandlineParams({ path.toStdWString() }, &dto);

		Buffer* buf = currentView()->getCurrentBuffer();
		QCOMPARE(QString::fromWCharArray(buf->getFullPathName()), path);
		QCOMPARE(currentView()->getCurrentLineNumber(), intptr_t(2)); // line 3, 0-based
		QCOMPARE(int(buf->getLangType()), int(L_PYTHON));
		QVERIFY(buf->getUserReadOnly());
		QVERIFY(currentView()->execute(SCI_GETREADONLY) != 0);
		_npp->command(IDM_EDIT_TOGGLEREADONLY);
		QVERIFY(!buf->getUserReadOnly());
	}

	void externalChangeIsDetected()
	{
		// A file changed by another program: on activation the user is asked; "Yes" reloads it
		const QString path = _tmp.path() + "/external.txt";
		writeFile(path, "before\n");
		_npp->switchToFile(_npp->doOpen(path.toStdWString()));
		Buffer* buf = currentView()->getCurrentBuffer();
		QCOMPARE(QString::fromStdString(currentView()->getText()), QStringLiteral("before\n"));

		QTest::qWait(1100); // make sure the modification time changes even on coarse file systems
		writeFile(path, "after\n");
		_ui._count = 0;
		_ui._answerYesNo = IDYES;
		QVERIFY(_npp->checkDocStatus());
		QCOMPARE(_ui._count, 1);
		QCOMPARE(QString::fromStdString(currentView()->getText()), QStringLiteral("after\n"));
		QVERIFY(!buf->isDirty());
		_ui._answerYesNo = IDNO;

		// Deleted outside: "No" (don't keep) closes it
		QFile::remove(path);
		_ui._count = 0;
		_npp->checkDocStatus();
		QCOMPARE(_ui._count, 1);
		QCOMPARE(_npp->getDocTab(MAIN_VIEW)->findBufferByName(path.toStdWString().c_str()), BUFFER_INVALID);
	}

	void recentFilesList()
	{
		// Closing a file puts it on top of the recent list in the File menu; restoring reopens it
		const QString path = _tmp.path() + "/recent.txt";
		writeFile(path, "recent\n");
		BufferID id = _npp->doOpen(path.toStdWString());
		_npp->switchToFile(id);
		_npp->command(IDM_FILE_CLOSE);
		QCOMPARE(_npp->getDocTab(MAIN_VIEW)->findBufferByName(path.toStdWString().c_str()), BUFFER_INVALID);

		QMenu* fileMenu = _npp->menuBar()->actions().at(0)->menu();
		QStringList texts;
		for (QAction* a : fileMenu->actions())
			texts << a->text();
		QVERIFY(texts.contains(QStringLiteral("&1: ") + path));
		QVERIFY(texts.contains(QStringLiteral("Restore Recent Closed File")));
		// The list sits before "Exit"
		QVERIFY(texts.indexOf(QStringLiteral("&1: ") + path) < texts.indexOf(QStringLiteral("E&xit")));

		_npp->command(IDM_FILE_RESTORELASTCLOSEDFILE);
		QVERIFY(_npp->getDocTab(MAIN_VIEW)->findBufferByName(path.toStdWString().c_str()) != BUFFER_INVALID);
		// An open file is not in the recent list
		texts.clear();
		for (QAction* a : fileMenu->actions())
			texts << a->text();
		QVERIFY(!texts.contains(QStringLiteral("&1: ") + path));
	}

	void dropAndTrash()
	{
		// Dropping files opens them; "Move to Recycle Bin" (trash) deletes and closes
		const QString d1 = _tmp.path() + "/dropped1.txt";
		const QString d2 = _tmp.path() + "/dropped2.txt";
		writeFile(d1, "1\n");
		writeFile(d2, "2\n");
		_npp->dropFiles({ d1, d2 }, MAIN_VIEW);
		DocTabView* tab = _npp->getDocTab(MAIN_VIEW);
		QVERIFY(tab->findBufferByName(d1.toStdWString().c_str()) != BUFFER_INVALID);
		BufferID id2 = tab->findBufferByName(d2.toStdWString().c_str());
		QVERIFY(id2 != BUFFER_INVALID);
		QCOMPARE(currentView()->getCurrentBufferID(), id2); // the last dropped file is active

		// QFile::moveToTrash may be unavailable (e.g. no trash on the test file system): then the
		// file stays and the document stays open, both are accepted outcomes
		_ui._count = 0;
		const bool deleted = _npp->fileDelete();
		QCOMPARE(_ui._count, deleted ? 1 : 2); // confirmation (+ failure message)
		QCOMPARE(QFile::exists(d2), !deleted);
		QCOMPARE(tab->findBufferByName(d2.toStdWString().c_str()) == BUFFER_INVALID, deleted);
	}

	void statusBarShowsDocumentInfo()
	{
		_npp->command(IDM_LANG_PYTHON);
		_npp->command(IDM_FORMAT_TOUNIX);
		_npp->command(IDM_FORMAT_AS_UTF_8);
		const QStringList texts = [this]() {
			QStringList t;
			for (QLabel* l : _npp->statusBar()->findChildren<QLabel*>())
				t << l->text();
			return t;
		}();
		QVERIFY(texts.contains(QStringLiteral("Python file")));
		QVERIFY(texts.contains(QStringLiteral("Unix (LF)")));
		QVERIFY(texts.contains(QStringLiteral("UTF-8")));
		QVERIFY(texts.contains(QStringLiteral("INS")));
	}

	void contextMenus()
	{
		// contextMenu.xml (generated from upstream's default) resolved against the main menu
		NppParameters& nppParam = NppParameters::getInstance();
		const std::vector<MenuItemUnit>& items = nppParam.getContextMenuItems();
		QVERIFY(nppParam.hasCustomContextMenu());
		auto find = [&items](unsigned long id) {
			return std::find_if(items.begin(), items.end(), [id](const MenuItemUnit& u) { return u._cmdID == id; });
		};
		QVERIFY(find(IDM_EDIT_CUT) != items.end());
		QCOMPARE(QString::fromStdWString(find(IDM_EDIT_CUT)->_itemName), QStringLiteral("Cut"));
		QVERIFY(find(IDM_EDIT_UPPERCASE) != items.end());
		QCOMPARE(QString::fromStdWString(find(IDM_SEARCH_MARKALLEXT1)->_parentFolderName), QStringLiteral("Style all occurrences of token"));

		// Editor context menu: built from those items, closed as soon as it opens
		auto closePopupSoon = []() {
			QTimer::singleShot(50, []() {
				if (QWidget* popup = QApplication::activePopupWidget())
					popup->close();
			});
		};
		_npp->command(IDM_FILE_NEW);
		closePopupSoon();
		_npp->showEditorContextMenu(currentView(), _npp->mapToGlobal(QPoint(100, 100)));

		// Tab context menu: upstream default items, states from the current document
		closePopupSoon();
		_npp->showTabContextMenu(_npp->currentView(), _npp->mapToGlobal(QPoint(100, 60)));
		const ContextMenu& tabMenu = _npp->getTabPopupMenu();
		QVERIFY(tabMenu.isCreated());
		QVERIFY(tabMenu.action(IDM_FILE_CLOSE)->isEnabled());
		QVERIFY(tabMenu.action(IDM_FILE_RENAME)->isEnabled());
		QVERIFY(!tabMenu.action(IDM_FILE_DELETE)->isEnabled()); // untitled: no file to trash
		QVERIFY(!tabMenu.action(IDM_VIEW_GOTO_NEW_INSTANCE)->isEnabled()); // untitled: no file for a new instance
		QCOMPARE(tabMenu.getMenuHandle()->actions().first()->text(), QStringLiteral("Close"));

		// a command chosen in the menu runs through Notepad_plus::command
		const size_t before = tabOf(_npp->currentView())->nbItem();
		tabMenu.action(IDM_FILE_CLOSE)->trigger();
		QCOMPARE(tabOf(_npp->currentView())->nbItem(), before - 1);
	}

	void rightButtonWheelSwitchesDocuments()
	{
		// upstream WM_MOUSEWHEEL with MK_RBUTTON: the previous / next document, and no context menu on the release
		NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();
		const bool doTaskList = nppGUI._doTaskList;
		nppGUI._doTaskList = false;
		_npp->command(IDM_FILE_NEW);
		_npp->command(IDM_FILE_NEW);
		DocTabView* tab = _npp->getDocTab(MAIN_VIEW);
		QVERIFY(tab->nbItem() >= 3);
		_npp->activateDoc(1);
		QCOMPARE(tab->getCurrentTabIndex(), 1);

		QWidget* viewport = currentView()->viewport();
		const QPoint pos(50, 30);
		bool menuShown = false;
		auto closeMenu = [&menuShown]() {
			QTimer::singleShot(50, [&menuShown]() {
				if (QWidget* popup = QApplication::activePopupWidget())
				{
					menuShown = true;
					popup->close();
				}
			});
		};
		auto rightButtonWheel = [&](int delta) {
			QWheelEvent wheel(QPointF(pos), QPointF(viewport->mapToGlobal(pos)), QPoint(), QPoint(0, delta), Qt::RightButton,
				Qt::NoModifier, Qt::NoScrollPhase, false);
			QApplication::sendEvent(viewport, &wheel);
		};

		// as Qt does it on Linux: the context menu event comes with the press
		auto rightPress = [&]() {
			QTest::mousePress(viewport, Qt::RightButton, Qt::NoModifier, pos);
			QContextMenuEvent contextEvent(QContextMenuEvent::Mouse, pos, viewport->mapToGlobal(pos));
			QApplication::sendEvent(viewport, &contextEvent);
		};
		QVERIFY(NppParameters::getInstance().hasCustomContextMenu());

		rightPress();
		rightButtonWheel(-120); // down: next document
		QCOMPARE(tab->getCurrentTabIndex(), 2);
		rightButtonWheel(120); // up: previous
		rightButtonWheel(120);
		QCOMPARE(tab->getCurrentTabIndex(), 0);
		closeMenu();
		QTest::mouseRelease(viewport, Qt::RightButton, Qt::NoModifier, pos);
		QTest::qWait(100);
		QVERIFY(!menuShown);
		QVERIFY(!NppParameters::getInstance()._isTaskListRBUTTONUP_Active);

		// a right click without the wheel: the menu, on the release
		rightPress();
		QVERIFY(!QApplication::activePopupWidget()); // not on the press
		closeMenu();
		QTest::mouseRelease(viewport, Qt::RightButton, Qt::NoModifier, pos);
		QTest::qWait(100);
		QVERIFY(menuShown);
		nppGUI._doTaskList = doTaskList;
	}
};

QTEST_MAIN(TestMainWindow)
#include "tst_mainwindow.moc"
