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

// Phase 7: macro recording / playback, the Shortcut, Run and Run Macro dialogs, the dynamic Macro and Run menus,
// the shortcuts.xml security validation. NPLX_SCREENSHOT_DIR=<dir> saves screenshots.

#include <QtTest>
#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QTemporaryDir>

#include <cstdlib>
#include <functional>

#include "Buffer.h"
#include "Notepad_plus.h"
#include "Parameters.h"
#include "QtUiServices.h"
#include "RunDlg.h"
#include "RunDlg_rc.h"
#include "RunMacroDlg.h"
#include "ScintillaEditView.h"
#include "ShortcutDlg.h"
#include "localization.h"
#include "menuCmdID.h"
#include "resource.h"
#include "shortcutRc.h"
#include "hmac.h"
#include "FindReplaceDlg_rc.h"

namespace
{
	class SilentUiServices : public QtUiServices
	{
	public:
		int messageBox(const std::wstring& message, const std::wstring&, unsigned int) override { ++_nbMessages; _lastMessage = message; return IDOK; }
		int saveOrNotMultiBox(const std::wstring&, const std::wstring&) override { return IDNO; }
		std::wstring getOpenFileName(const std::wstring&, const std::wstring&, const std::wstring&) override { return _fileAnswer; }
		int _nbMessages = 0;
		std::wstring _lastMessage;
		std::wstring _fileAnswer;
	};

	// answers the next modal dialog (a ShortcutDlg) once the event loop runs it
	void answerShortcutDlg(std::function<void(ShortcutDlg*)> answer)
	{
		QTimer::singleShot(0, [answer]() {
			auto* dlg = qobject_cast<ShortcutDlg*>(QApplication::activeModalWidget());
			QVERIFY(dlg);
			answer(dlg);
		});
	}
}

class TestMacro : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir _tmp;
	SilentUiServices _ui;
	NativeLangSpeaker _nativeLangSpeaker;
	std::unique_ptr<Notepad_plus> _npp;

	ScintillaEditView* view() { return _npp->getActiveEditView(); }
	QString text() { return QString::fromStdString(view()->getText()); }

	void screenshot(QWidget* w, const QString& name)
	{
		const QByteArray dir = qgetenv("NPLX_SCREENSHOT_DIR");
		if (!dir.isEmpty())
		{
			QCoreApplication::processEvents();
			w->grab().save(QString::fromUtf8(dir) + "/" + name);
		}
	}

	void newDoc(const char* content)
	{
		_npp->command(IDM_FILE_NEW);
		view()->execute(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(content));
		view()->execute(SCI_DOCUMENTEND);
	}

	// record: "abc", Enter, Upper Case (a menu command) on the line selected with Home+Shift+End
	void recordSample()
	{
		_npp->command(IDM_MACRO_STARTRECORDINGMACRO);
		QVERIFY(_npp->isRecordingMacro());
		view()->execute(SCI_REPLACESEL, 0, reinterpret_cast<sptr_t>("abc"));
		view()->execute(SCI_HOME);
		view()->execute(SCI_LINEENDEXTEND);
		_npp->command(IDM_EDIT_UPPERCASE);
		view()->execute(SCI_LINEEND);
		view()->execute(SCI_NEWLINE);
		_npp->command(IDM_MACRO_STOPRECORDINGMACRO);
		QVERIFY(!_npp->isRecordingMacro());
	}

	QAction* menuAction(const QString& menuTitle, const QString& text)
	{
		for (QAction* top : _npp->menuBar()->actions())
		{
			if (!top->menu() || top->text() != menuTitle)
				continue;
			for (QAction* a : top->menu()->actions())
				if (a->text() == text)
					return a;
		}
		return nullptr;
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
		_npp->resize(1100, 700);
		_npp->show();
		QVERIFY(QTest::qWaitForWindowExposed(_npp.get()));
	}

	void cleanupTestCase()
	{
		_npp.reset();
		nplx::setUiServices(nullptr);
	}

	void macroMenuState()
	{
		// upstream checkMacroState at startup: nothing recorded yet
		QVERIFY(Notepad_plus::isCommandPorted(IDM_MACRO_STARTRECORDINGMACRO));
		QVERIFY(Notepad_plus::isCommandPorted(IDM_EXECUTE));
		QAction* start = menuAction("&Macro", "Start Re&cording");
		QAction* stop = menuAction("&Macro", "S&top Recording");
		QAction* play = menuAction("&Macro", "&Playback");
		QVERIFY(start && stop && play);
		if (_npp->currentMacro().empty())
		{
			QVERIFY(start->isEnabled());
			QVERIFY(!stop->isEnabled());
			QVERIFY(!play->isEnabled());
		}
	}

	void recordAndPlayback()
	{
		newDoc("");
		recordSample();
		QCOMPARE(text(), QString("ABC\n"));

		const Macro& m = _npp->currentMacro();
		QVERIFY(m.size() >= 5);
		QCOMPARE(m.front()._message, SCI_REPLACESEL);
		QCOMPARE(QString::fromStdString(m.front()._sParameter), QString("abc"));
		bool hasMenuCmd = false;
		for (const recordedMacroStep& step : m)
			if (step._macroType == recordedMacroStep::mtMenuCommand && step._wParameter == IDM_EDIT_UPPERCASE)
				hasMenuCmd = true;
		QVERIFY(hasMenuCmd);
		// Start/Stop/Playback commands are not recorded
		for (const recordedMacroStep& step : m)
			QVERIFY(step._wParameter != IDM_MACRO_STOPRECORDINGMACRO || step._macroType != recordedMacroStep::mtMenuCommand);

		QVERIFY(menuAction("&Macro", "&Playback")->isEnabled());
		QVERIFY(menuAction("&Macro", "Start Re&cording")->isEnabled());
		QVERIFY(!menuAction("&Macro", "S&top Recording")->isEnabled());
		QCOMPARE(_npp->nppMessage(NPPM_GETCURRENTMACROSTATUS, 0, 0), static_cast<intptr_t>(MacroStatus::RecordingStopped));

		_npp->command(IDM_MACRO_PLAYBACKRECORDEDMACRO);
		QCOMPARE(text(), QString("ABC\nABC\n"));

		// one undo step per playback (upstream SCI_BEGINUNDOACTION / ENDUNDOACTION over the affected documents)
		_npp->command(IDM_EDIT_UNDO);
		QCOMPARE(text(), QString("ABC\n"));
	}

	void newlineIsNormalized()
	{
		// upstream SCN_MACRORECORD: SCI_REPLACESEL "\n" becomes SCI_NEWLINE (inserts the document EOL)
		newDoc("");
		_npp->command(IDM_MACRO_STARTRECORDINGMACRO);
		view()->execute(SCI_REPLACESEL, 0, reinterpret_cast<sptr_t>("\n"));
		_npp->command(IDM_MACRO_STOPRECORDINGMACRO);
		QCOMPARE(_npp->currentMacro().size(), size_t(1));
		QCOMPARE(_npp->currentMacro().front()._message, SCI_NEWLINE);
	}

	void findReplaceSteps()
	{
		// the Find dialog records its options and the command through WM_FRSAVE_INT/STR
		newDoc("");
		_npp->command(IDM_MACRO_STARTRECORDINGMACRO);
		QCOMPARE(_npp->nppMessage(NPPM_GETCURRENTMACROSTATUS, 0, 0), static_cast<intptr_t>(MacroStatus::RecordInProgress));
		_npp->nppMessage(WM_FRSAVE_STR, IDFINDWHAT, reinterpret_cast<sptr_t>("needle"));
		_npp->nppMessage(WM_FRSAVE_INT, IDC_FRCOMMAND_EXEC, IDOK);
		_npp->command(IDM_MACRO_STOPRECORDINGMACRO);
		const Macro& m = _npp->currentMacro();
		QCOMPARE(m.size(), size_t(2));
		QCOMPARE(m[0]._macroType, recordedMacroStep::mtSavedSnR);
		QCOMPARE(QString::fromStdString(m[0]._sParameter), QString("needle"));
		QCOMPARE(m[1]._lParameter, uptr_t(IDOK));
		QVERIFY(!m[0].isScintillaMacro());
	}

	void shortcutDlgConflict()
	{
		Shortcut sc;
		std::vector<KeyCombo> asked;
		ShortcutDlg dlg(sc, [&asked](const KeyCombo& kc) { asked.push_back(kc); return kc._isCtrl && kc._key == 'S'; }, _npp.get());
		answerShortcutDlg([this](ShortcutDlg* d) {
			d->setChecked(IDC_CTRL_CHECK, true);
			d->command(IDC_CTRL_CHECK);
			QComboBox* keys = d->comboBox(IDC_KEY_COMBO);
			keys->setCurrentIndex(keys->findText("S"));
			QVERIFY(d->dlgItem(IDC_CONFLICT_STATIC)->isVisibleTo(d));
			screenshot(d, "shortcut-dlg-conflict.png");
			keys->setCurrentIndex(keys->findText("F7"));
			QVERIFY(!d->dlgItem(IDC_CONFLICT_STATIC)->isVisibleTo(d));
			d->command(IDCANCEL);
		});
		QCOMPARE(dlg.doDialog(), -1);
		QVERIFY(!asked.empty());
		QCOMPARE(sc.getKeyCombo()._key, UCHAR(0)); // cancelled: unchanged
	}

	void saveMacroAndRunFromMenu()
	{
		newDoc("");
		recordSample();

		NppParameters& nppParam = NppParameters::getInstance();
		const size_t nbMacros = nppParam.getMacroList().size();
		answerShortcutDlg([](ShortcutDlg* d) {
			d->setItemText(IDC_NAME_EDIT, L"Shout line");
			d->setChecked(IDC_CTRL_CHECK, true);
			d->setChecked(IDC_ALT_CHECK, true);
			d->command(IDC_CTRL_CHECK);
			d->command(IDC_ALT_CHECK);
			QComboBox* keys = d->comboBox(IDC_KEY_COMBO);
			keys->setCurrentIndex(keys->findText("F9"));
			d->command(IDOK);
		});
		_npp->command(IDM_MACRO_SAVECURRENTMACRO);
		QCOMPARE(nppParam.getMacroList().size(), nbMacros + 1);
		const MacroShortcut& saved = nppParam.getMacroList().back();
		QCOMPARE(QString::fromStdString(saved.getName()), QString("Shout line"));
		QVERIFY(saved.getKeyCombo()._isCtrl && saved.getKeyCombo()._isAlt);
		QVERIFY(!menuAction("&Macro", "&Save Current Recorded Macro...")->isEnabled()); // _recordingSaved

		// the dynamic Macro menu: the new item, with its shortcut
		QAction* item = menuAction("&Macro", "Shout line");
		QVERIFY(item);
		QCOMPARE(item->data().toInt(), static_cast<int>(saved.getID()));
		QCOMPARE(item->shortcut(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F9));
		screenshot(qobject_cast<QWidget*>(item->parent()), "macro-menu.png");

		newDoc("");
		item->trigger();
		QCOMPARE(text(), QString("ABC\n"));
	}

	void runMacroMultipleTimes()
	{
		newDoc("");
		_npp->command(IDM_MACRO_RUNMULTIMACRODLG);
		RunMacroDlg* dlg = _npp->getRunMacroDlg();
		QVERIFY(dlg && dlg->isVisible());
		QComboBox* combo = dlg->comboBox(IDC_MACRO_COMBO);
		QCOMPARE(combo->itemText(0), QString("Current recorded macro"));
		QVERIFY(combo->findText("Shout line") > 0);
		screenshot(dlg, "run-macro-dlg.png");

		dlg->setItemText(IDC_M_RUN_TIMES, L"3");
		dlg->command(IDC_M_RUN_TIMES, RcNotifyEditChange);
		QCOMPARE(dlg->getTimes(), 3);
		QCOMPARE(dlg->getMacro2Exec(), -1); // the current recorded macro
		dlg->command(IDOK);
		QCOMPARE(text(), QString("ABC\nABC\nABC\n"));

		// one undo for the whole multiple run
		_npp->command(IDM_EDIT_UNDO);
		QCOMPARE(text(), QString(""));

		// "Run until the end of file" on the saved macro: uppercase every line
		newDoc("one\ntwo\nthree");
		view()->execute(SCI_DOCUMENTSTART);
		_npp->command(IDM_MACRO_STARTRECORDINGMACRO);
		view()->execute(SCI_LINEENDEXTEND);
		_npp->command(IDM_EDIT_UPPERCASE);
		view()->execute(SCI_LINEDOWN);
		view()->execute(SCI_HOME);
		_npp->command(IDM_MACRO_STOPRECORDINGMACRO);
		QCOMPARE(text(), QString("ONE\ntwo\nthree"));
		dlg->doDialog();
		dlg->setChecked(IDC_M_RUN_MULTI, false);
		dlg->setChecked(IDC_M_RUN_EOF, true);
		dlg->command(IDC_M_RUN_EOF);
		QVERIFY(!dlg->dlgItem(IDC_M_RUN_TIMES)->isEnabled());
		dlg->command(IDOK);
		QCOMPARE(text(), QString("ONE\nTWO\nTHREE"));
		dlg->hide();
	}

	void runVariables()
	{
		const QString path = _tmp.path() + "/files/hello.world.txt";
		QDir().mkpath(QFileInfo(path).path());
		{
			QFile f(path);
			QVERIFY(f.open(QIODevice::WriteOnly));
			f.write("first line\nsecond word here\n");
		}
		BufferID id = _npp->doOpen(path.toStdWString());
		QVERIFY(id != BUFFER_INVALID);
		_npp->switchToFile(id);
		view()->execute(SCI_GOTOPOS, view()->execute(SCI_POSITIONFROMLINE, 1) + 7); // in "word"

		const ParentMessenger npp = [this](unsigned int m, uptr_t w, sptr_t l) { return _npp->nppMessage(m, w, l); };
		auto expand = [&npp](const wchar_t* s) { return QString::fromStdWString(expandNppEnvironmentStrs(s, npp)); };
		QCOMPARE(expand(L"$(FULL_CURRENT_PATH)"), path);
		QCOMPARE(expand(L"$(CURRENT_DIRECTORY)"), QFileInfo(path).path());
		QCOMPARE(expand(L"$(FILE_NAME)"), QString("hello.world.txt"));
		QCOMPARE(expand(L"$(NAME_PART)"), QString("hello.world"));
		QCOMPARE(expand(L"$(EXT_PART)"), QString(".txt"));
		QCOMPARE(expand(L"$(CURRENT_LINE)"), QString("1")); // upstream: 0-based
		QCOMPARE(expand(L"$(CURRENT_COLUMN)"), QString("7"));
		QCOMPARE(expand(L"$(CURRENT_WORD)"), QString("word")); // upstream: selects the word under the caret
		QCOMPARE(expand(L"$(CURRENT_LINESTR)").trimmed(), QString("second word here"));
		QCOMPARE(expand(L"$(NPP_DIRECTORY)"), QCoreApplication::applicationDirPath());
		QCOMPARE(expand(L"x $(UNKNOWN) $(FILE_NAME"), QString("x $(UNKNOWN) $(FILE_NAME"));

		::setenv("NPLX_TST_VAR", "val", 1);
		QCOMPARE(QString::fromStdWString(expandEnvironmentStrings(L"%NPLX_TST_VAR%/$NPLX_TST_VAR/${NPLX_TST_VAR}/$(FILE_NAME)/%NOPE_X%")),
			QString("val/val/val/$(FILE_NAME)/%NOPE_X%"));

		std::wstring cmd, args;
		Command::extractArgs(cmd, args, L"\"/opt/my app/run\"   -a \"b c\"  ");
		QCOMPARE(QString::fromStdWString(cmd), QString("\"/opt/my app/run\""));
		QCOMPARE(QString::fromStdWString(args), QString("-a \"b c\""));
	}

	void runCommand()
	{
		const ParentMessenger npp = [this](unsigned int m, uptr_t w, sptr_t l) { return _npp->nppMessage(m, w, l); };

		// a program from PATH with its arguments; $(FILE_NAME) expanded
		const QString marker = _tmp.path() + "/ran-hello.world.txt";
		Command touch(QString("touch \"%1/ran-$(FILE_NAME)\"").arg(_tmp.path()).toStdWString());
		QVERIFY(touch.run(npp));
		QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(marker), 5000);

		// not found: the upstream "ShellExecute - ERROR" message
		const int nbMessages = _ui._nbMessages;
		Command missing(L"no-such-program-nplx --flag");
		QVERIFY(!missing.run(npp));
		QCOMPARE(_ui._nbMessages, nbMessages + 1);
		QVERIFY(QString::fromStdWString(_ui._lastMessage).contains("no-such-program-nplx"));
	}

	void runDlgAndSaveCommand()
	{
		_npp->command(IDM_EXECUTE);
		RunDlg* dlg = _npp->getRunDlg();
		QVERIFY(dlg && dlg->isVisible());
		screenshot(dlg, "run-dlg.png");

		// "+": the variables menu
		QTimer::singleShot(0, []() {
			auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
			QVERIFY(menu);
			QCOMPARE(menu->actions().size(), 11);
			menu->setActiveAction(menu->actions().at(2)); // FILE_NAME
			QTest::keyClick(menu, Qt::Key_Return);
		});
		dlg->setItemText(IDC_COMBO_RUN_PATH, L"touch ");
		dlg->command(IDC_BUTTON_VARIABLES);
		QCOMPARE(QString::fromStdWString(dlg->getItemText(IDC_COMBO_RUN_PATH)), QString("touch $(FILE_NAME)"));

		// Run: the command goes to the history and the dialog closes
		const QString cmd = QString("touch \"%1/dlg-run\"").arg(_tmp.path());
		dlg->setItemText(IDC_COMBO_RUN_PATH, cmd.toStdWString());
		dlg->command(IDOK);
		QVERIFY(!dlg->isVisible());
		QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(_tmp.path() + "/dlg-run"), 5000);
		QVERIFY(dlg->comboBox(IDC_COMBO_RUN_PATH)->findText(cmd) >= 0);

		// Save...: a new Run menu entry
		NppParameters& nppParam = NppParameters::getInstance();
		const size_t nbCmds = nppParam.getUserCommandList().size();
		const QString saveCmd = QString("touch \"%1/menu-run\"").arg(_tmp.path());
		dlg->doDialog();
		dlg->setItemText(IDC_COMBO_RUN_PATH, saveCmd.toStdWString());
		answerShortcutDlg([](ShortcutDlg* d) {
			d->setItemText(IDC_NAME_EDIT, L"Touch marker");
			d->command(IDOK);
		});
		dlg->command(IDC_BUTTON_SAVE);
		dlg->hide();
		QCOMPARE(nppParam.getUserCommandList().size(), nbCmds + 1);
		QCOMPARE(QString::fromUtf8(nppParam.getUserCommandList().back().getCmd()), saveCmd);

		QAction* item = menuAction("&Run", "Touch marker");
		QVERIFY(item);
		QVERIFY(menuAction("&Run", "Modify Shortcut/Delete Command...")); // added with the first command
		item->trigger();
		QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(_tmp.path() + "/menu-run"), 5000);
	}

	void shortcutsXmlSecurity()
	{
		// shortcuts.xml changed outside: the commands are refused, shortcuts.xml is opened for review
		NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();
		const std::string goodHmac = nppGUI._shortcutsOnDiskHmac;
		nppGUI._shortcutsOnDiskHmac = "tampered";

		QFile::remove(_tmp.path() + "/menu-run");
		const int nbMessages = _ui._nbMessages;
		menuAction("&Run", "Touch marker")->trigger();
		QCOMPARE(_ui._nbMessages, nbMessages + 1);
		QVERIFY(QString::fromStdWString(_ui._lastMessage).contains("modified manually"));
		QCOMPARE(QString::fromStdWString(_npp->getActiveEditView()->getCurrentBuffer()->getFullPathName()),
			QString::fromStdWString(NppParameters::getInstance().getShortcutsPath()));
		QTest::qWait(300);
		QVERIFY(!QFileInfo::exists(_tmp.path() + "/menu-run"));

		// Run > Validate shortcuts.xml
		_npp->command(IDM_EXECUTE_VALIDATE_SHORTCUTSXML);
		QVERIFY(!nppGUI._shortcutsXmlHmacInConfig.empty());
		QCOMPARE(nppGUI._shortcutsOnDiskHmac, nppGUI._shortcutsXmlHmacInConfig);
		menuAction("&Run", "Touch marker")->trigger();
		QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(_tmp.path() + "/menu-run"), 5000);
		Q_UNUSED(goodHmac);
	}

	void shortcutsSavedAtExit()
	{
		// the new macro and Run command reach shortcuts.xml, and its HMAC reaches config.xml
		NppParameters& nppParam = NppParameters::getInstance();
		_npp->saveShortcuts();
		QFile f(QString::fromStdWString(nppParam.getShortcutsPath()));
		QVERIFY(f.open(QIODevice::ReadOnly));
		const QByteArray xml = f.readAll();
		QVERIFY(xml.contains("Shout line"));
		QVERIFY(xml.contains("Touch marker"));
		QVERIFY(xml.contains("menu-run"));
		const std::string hmac = computeHMAC(getMachineGUID(), getFileContent(nppParam.getShortcutsPath().c_str()));
		QCOMPARE(nppParam.getNppGUI()._shortcutsXmlHmacInConfig, hmac);
	}
};

QTEST_MAIN(TestMacro)
#include "tst_macro.moc"
