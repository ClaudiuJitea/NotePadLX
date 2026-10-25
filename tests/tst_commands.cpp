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

// Tests of the menu commands ported from upstream NppCommands.cpp / Notepad_plus.cpp:
// line operations, comments, bookmarked lines, multi-selection, closing several tabs,
// tab navigation, folding, read-only, encodings.

#include <QtTest>
#include <QAction>
#include <QClipboard>
#include <QGuiApplication>
#include <QLineEdit>
#include <QMenuBar>
#include <QTemporaryDir>

#include <cstdlib>
#include <fstream>

#include "Buffer.h"
#include "DocTabView.h"
#include "GoToLineDlg.h"
#include "resource.h"
#include "Notepad_plus.h"
#include "Parameters.h"
#include "QtUiServices.h"
#include "ScintillaEditView.h"
#include "localization.h"
#include "menuCmdID.h"

namespace
{
	class SilentUiServices : public QtUiServices
	{
	public:
		int messageBox(const std::wstring& message, const std::wstring&, unsigned int type) override
		{
			++_count;
			_lastMessage = message;
			const unsigned int buttons = type & 0x0F;
			return (buttons == MB_YESNO || buttons == MB_YESNOCANCEL) ? _answerYesNo : IDOK;
		}
		int saveOrNotMultiBox(const std::wstring&, const std::wstring&) override
		{
			++_count;
			return _answerYesNo;
		}
		int _count = 0;
		int _answerYesNo = IDNO; // "No": never save, so no Save As dialog opens
		std::wstring _lastMessage;
	};

	void writeFile(const QString& path, const QByteArray& bytes)
	{
		std::ofstream f(path.toStdString(), std::ios::binary);
		f.write(bytes.constData(), bytes.size());
	}
}

class TestCommands : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir _tmp;
	SilentUiServices _ui;
	NativeLangSpeaker _nativeLangSpeaker;
	std::unique_ptr<Notepad_plus> _npp;

	ScintillaEditView* view() { return _npp->getActiveEditView(); }
	DocTabView* tab() { return _npp->getDocTab(_npp->currentView()); }

	void newDocument(const char* text)
	{
		_npp->command(IDM_FILE_NEW);
		view()->setText(text);
		view()->execute(SCI_GOTOPOS, 0);
	}

	QString text() { return QString::fromStdString(view()->getText()); }

	QAction* action(int id)
	{
		for (QAction* a : _npp->menuBar()->findChildren<QAction*>())
			if (a->data().toInt() == id)
				return a;
		return nullptr;
	}

	// Leaves only one empty "new" tab in the current view
	void closeAll()
	{
		_npp->command(IDM_FILE_CLOSEALL);
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

	void portedCommandsAreEnabled()
	{
		// generated from the case labels of Notepad_plus::command
		for (int id : { IDM_EDIT_SORTLINES_LEXICOGRAPHIC_ASCENDING, IDM_EDIT_BLOCK_COMMENT, IDM_EDIT_TRIMTRAILING,
		                IDM_FILE_CLOSEALL_BUT_CURRENT, IDM_VIEW_TAB_NEXT, IDM_SEARCH_CUTMARKEDLINES, IDM_FORMAT_WIN_1252 })
		{
			QVERIFY2(action(id), qPrintable(QString::number(id)));
			QVERIFY2(action(id)->isEnabled(), qPrintable(QString::number(id)));
		}
		// still disabled with a tooltip
		QVERIFY(!action(IDM_FILE_PRINT)->isEnabled());
		QCOMPARE(action(IDM_FILE_PRINT)->toolTip(), QStringLiteral("Not yet ported to NotePadLX"));
	}

	void sortLines()
	{
		// upstream: without a selection every line is sorted, the last (empty) one included
		newDocument("pear\nApple\nbanana\n10\n9\n");
		_npp->command(IDM_EDIT_SORTLINES_LEXICOGRAPHIC_ASCENDING);
		QCOMPARE(text(), QString("\n10\n9\nApple\nbanana\npear"));

		newDocument("pear\nApple\nbanana\n10\n9");
		_npp->command(IDM_EDIT_SORTLINES_LEXICO_CASE_INSENS_DESCENDING);
		QCOMPARE(text(), QString("pear\nbanana\nApple\n9\n10"));
		_npp->command(IDM_EDIT_SORTLINES_REVERSE_ORDER);
		QCOMPARE(text(), QString("10\n9\nApple\nbanana\npear"));

		// with a selection of whole lines, only those lines
		newDocument("b\na\nz\n");
		view()->execute(SCI_SETSEL, 0, 4);
		_npp->command(IDM_EDIT_SORTLINES_LEXICOGRAPHIC_ASCENDING);
		QCOMPARE(text(), QString("a\nb\nz\n"));

		newDocument("10\n9\n100\n-3");
		_npp->command(IDM_EDIT_SORTLINES_INTEGER_ASCENDING);
		QCOMPARE(text(), QString("-3\n9\n10\n100"));
		_npp->command(IDM_EDIT_SORTLINES_LENGTH_DESCENDING);
		QCOMPARE(text().section('\n', 0, 0), QString("100"));

		// decimal sort of a non-number line: upstream error message with the line number
		newDocument("3.5\n-\n1.25"); // "-" is kept for the conversion, which fails
		const int boxes = _ui._count;
		_npp->command(IDM_EDIT_SORTLINES_DECIMALDOT_ASCENDING);
		QCOMPARE(_ui._count, boxes + 1);
		QVERIFY(QString::fromStdWString(_ui._lastMessage).contains("2"));

		// locale-aware sort (QCollator, digits as numbers)
		newDocument("item10\nitem9\nÉcole\necole");
		_npp->command(IDM_EDIT_SORTLINES_LOCALE_ASCENDING);
		const QStringList lines = text().split('\n', Qt::SkipEmptyParts);
		QCOMPARE(lines.size(), 4);
		QVERIFY(lines.indexOf("item9") < lines.indexOf("item10"));
		QVERIFY(lines.indexOf("ecole") < lines.indexOf("item9"));
	}

	void trimAndBlanks()
	{
		newDocument("  a  \n\tb\t\n c \n");
		_npp->command(IDM_EDIT_TRIMTRAILING);
		QCOMPARE(text(), QString("  a\n\tb\n c\n"));
		_npp->command(IDM_EDIT_TRIMLINEHEAD);
		QCOMPARE(text(), QString("a\nb\nc\n"));

		newDocument("a\n\n  \nb\n\n");
		_npp->command(IDM_EDIT_REMOVEEMPTYLINES); // upstream also removes the last line break
		QCOMPARE(text(), QString("a\n  \nb"));
		_npp->command(IDM_EDIT_REMOVEEMPTYLINESWITHBLANK);
		QCOMPARE(text(), QString("a\nb"));

		newDocument("x\nx\ny\nx\n");
		_npp->command(IDM_EDIT_REMOVE_CONSECUTIVE_DUP_LINES);
		QCOMPARE(text(), QString("x\ny\nx\n"));

		newDocument("a\nb\nc");
		_npp->command(IDM_EDIT_EOL2WS);
		QCOMPARE(text(), QString("a b c"));
	}

	void tabsAndSpaces()
	{
		newDocument("\tx\n\t\ty\n");
		view()->execute(SCI_SETTABWIDTH, 4);
		_npp->command(IDM_EDIT_TAB2SW);
		QCOMPARE(text(), QString("    x\n        y\n"));
		_npp->command(IDM_EDIT_SW2TAB_LEADING);
		QCOMPARE(text(), QString("\tx\n\t\ty\n"));
	}

	void comments()
	{
		newDocument("int a;\nint b;\n");
		_npp->command(IDM_LANG_CPP);
		view()->execute(SCI_SETSEL, 0, 13); // both lines
		_npp->command(IDM_EDIT_BLOCK_COMMENT);
		QCOMPARE(text(), QString("// int a;\n// int b;\n")); // comment symbol followed by a space (upstream default)
		view()->execute(SCI_SETSEL, 0, 19);
		_npp->command(IDM_EDIT_BLOCK_COMMENT); // toggle back
		QCOMPARE(text(), QString("int a;\nint b;\n"));

		view()->execute(SCI_SETSEL, 4, 5); // "a"
		_npp->command(IDM_EDIT_STREAM_COMMENT);
		QCOMPARE(text(), QString("int /* a */;\nint b;\n"));
		_npp->command(IDM_EDIT_STREAM_UNCOMMENT);
		QCOMPARE(text(), QString("int a;\nint b;\n"));

		// Python: '#'
		newDocument("x = 1\n");
		_npp->command(IDM_LANG_PYTHON);
		_npp->command(IDM_EDIT_BLOCK_COMMENT_SET);
		QCOMPARE(text(), QString("# x = 1\n"));
		_npp->command(IDM_EDIT_BLOCK_UNCOMMENT);
		QCOMPARE(text(), QString("x = 1\n"));
	}

	void bookmarkedLines()
	{
		newDocument("one\ntwo\nthree\nfour\n");
		_npp->bookmarkAdd(1);
		_npp->bookmarkAdd(3);

		_npp->command(IDM_SEARCH_COPYMARKEDLINES);
		QCOMPARE(QGuiApplication::clipboard()->text(), QString("two\nfour\n"));

		_npp->command(IDM_SEARCH_INVERSEMARKS); // the last (empty) line gets a bookmark too
		QVERIFY(_npp->bookmarkPresent(0));
		QVERIFY(!_npp->bookmarkPresent(1));
		QVERIFY(_npp->bookmarkPresent(4));

		_npp->command(IDM_SEARCH_DELETEUNMARKEDLINES); // removes "two" and "four"
		QCOMPARE(text(), QString("one\nthree\n"));

		QGuiApplication::clipboard()->setText("X");
		_npp->command(IDM_SEARCH_PASTEMARKEDLINES);
		QCOMPARE(text(), QString("X\nX\nX"));

		_npp->command(IDM_SEARCH_CUTMARKEDLINES);
		QCOMPARE(text(), QString(""));
		QCOMPARE(QGuiApplication::clipboard()->text(), QString("X\nX\nX"));
	}

	void multiSelection()
	{
		newDocument("foo bar foo baz foo\n");
		view()->execute(SCI_SETSEL, 0, 3);
		_npp->command(IDM_EDIT_MULTISELECTALL);
		QCOMPARE(view()->execute(SCI_GETSELECTIONS), 3);

		view()->execute(SCI_SETSEL, 0, 3);
		_npp->command(IDM_EDIT_MULTISELECTNEXT);
		QCOMPARE(view()->execute(SCI_GETSELECTIONS), 2);
		_npp->command(IDM_EDIT_MULTISELECTUNDO);
		QCOMPARE(view()->execute(SCI_GETSELECTIONS), 1);
	}

	void closeSeveralTabs()
	{
		closeAll();
		const QString dir = _tmp.path();
		QStringList paths;
		for (const char* name : { "c1.txt", "c2.txt", "c3.txt", "c4.txt" })
		{
			paths << dir + "/" + name;
			writeFile(paths.last(), "x\n");
			_npp->switchToFile(_npp->doOpen(paths.last().toStdWString()));
		}
		// "new 1" was replaced by the first file (upstream: an empty untitled tab is reused)
		QCOMPARE(tab()->nbItem(), size_t(4));

		_npp->switchToFile(_npp->doOpen(paths[1].toStdWString()));
		_npp->command(IDM_FILE_CLOSEALL_TOLEFT);
		QCOMPARE(tab()->nbItem(), size_t(3));
		QCOMPARE(QString::fromWCharArray(view()->getCurrentBuffer()->getFullPathName()), paths[1]);

		_npp->command(IDM_FILE_CLOSEALL_TORIGHT);
		QCOMPARE(tab()->nbItem(), size_t(1));

		_npp->switchToFile(_npp->doOpen(paths[2].toStdWString()));
		_npp->switchToFile(_npp->doOpen(paths[3].toStdWString()));
		view()->execute(SCI_APPENDTEXT, 1, reinterpret_cast<LPARAM>("y")); // c4 is now dirty
		_npp->command(IDM_FILE_CLOSEALL_UNCHANGED);
		QCOMPARE(tab()->nbItem(), size_t(1));
		QVERIFY(view()->getCurrentBuffer()->isDirty());

		_npp->switchToFile(_npp->doOpen(paths[0].toStdWString()));
		_npp->switchToFile(_npp->doOpen(paths[3].toStdWString())); // the dirty one
		_npp->command(IDM_FILE_CLOSEALL_BUT_CURRENT); // "No": the other documents are not saved
		QCOMPARE(tab()->nbItem(), size_t(1));
		QCOMPARE(QString::fromWCharArray(view()->getCurrentBuffer()->getFullPathName()), paths[3]);
		view()->execute(SCI_UNDO);
	}

	void tabNavigation()
	{
		closeAll();
		newDocument("a");
		newDocument("b");
		newDocument("c");
		const size_t n = tab()->nbItem();
		QVERIFY(n >= 3);
		const int last = static_cast<int>(n) - 1;
		QCOMPARE(tab()->getCurrentTabIndex(), last);

		_npp->command(IDM_VIEW_TAB_NEXT); // wraps to the first tab
		QCOMPARE(tab()->getCurrentTabIndex(), 0);
		_npp->command(IDM_VIEW_TAB_PREV);
		QCOMPARE(tab()->getCurrentTabIndex(), last);
		_npp->command(IDM_VIEW_TAB1);
		QCOMPARE(tab()->getCurrentTabIndex(), 0);
		_npp->command(IDM_VIEW_TAB_END);
		QCOMPARE(tab()->getCurrentTabIndex(), last);

		const BufferID current = view()->getCurrentBufferID();
		_npp->command(IDM_VIEW_TAB_MOVEBACKWARD);
		QCOMPARE(tab()->getCurrentTabIndex(), last - 1);
		QCOMPARE(tab()->getBufferByIndex(last - 1), current);
		_npp->command(IDM_VIEW_GOTO_START);
		QCOMPARE(tab()->getBufferByIndex(0), current);
		QCOMPARE(view()->getCurrentBufferID(), current);
	}

	void foldLevels()
	{
		newDocument("int f() {\n\tif (x) {\n\t\ty();\n\t}\n}\n");
		_npp->command(IDM_LANG_CPP);
		view()->execute(SCI_COLOURISE, 0, -1);
		_npp->command(IDM_VIEW_FOLD_2);
		QVERIFY(!view()->execute(SCI_GETFOLDEXPANDED, 1));
		QVERIFY(view()->execute(SCI_GETFOLDEXPANDED, 0));
		_npp->command(IDM_VIEW_UNFOLD_2);
		QVERIFY(view()->execute(SCI_GETFOLDEXPANDED, 1));
	}

	void insertDateTime()
	{
		NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();
		const std::wstring saved = nppGUI._dateTimeFormat;
		nppGUI._dateTimeFormat = L"yyyy";
		newDocument("");
		_npp->command(IDM_EDIT_INSERT_DATETIME_CUSTOMIZED);
		QCOMPARE(text(), QString::number(QDate::currentDate().year()));
		nppGUI._dateTimeFormat = saved;

		newDocument("");
		_npp->command(IDM_EDIT_INSERT_DATETIME_SHORT);
		QVERIFY(!text().isEmpty());
	}

	void readOnlyForAllDocuments()
	{
		newDocument("a");
		newDocument("b");
		_npp->command(IDM_EDIT_SETREADONLYFORALLDOCS);
		QVERIFY(view()->getCurrentBuffer()->getUserReadOnly());
		QVERIFY(MainFileManager.getBufferByID(tab()->getBufferByIndex(0))->getUserReadOnly());
		_npp->command(IDM_EDIT_CLEARREADONLYFORALLDOCS);
		QVERIFY(!view()->getCurrentBuffer()->getUserReadOnly());
	}

	void systemReadOnly()
	{
		const QString path = _tmp.path() + "/ro.txt";
		writeFile(path, "ro\n");
		_npp->switchToFile(_npp->doOpen(path.toStdWString()));
		_npp->command(IDM_EDIT_TOGGLESYSTEMREADONLY);
		QVERIFY(!(QFileInfo(path).permissions() & QFileDevice::WriteOwner));
		QVERIFY(view()->getCurrentBuffer()->getFileReadOnly());
		_npp->command(IDM_EDIT_TOGGLESYSTEMREADONLY);
		QVERIFY(QFileInfo(path).permissions() & QFileDevice::WriteOwner);
		QVERIFY(!view()->getCurrentBuffer()->getFileReadOnly());
	}

	void encodings()
	{
		// Character set: reinterpret the saved bytes (here Latin-1 é = 0xE9)
		const QString path = _tmp.path() + "/latin1.txt";
		writeFile(path, "caf\xE9\n");
		_npp->switchToFile(_npp->doOpen(path.toStdWString()));
		_npp->command(IDM_FORMAT_WIN_1252);
		QCOMPARE(view()->getCurrentBuffer()->getEncoding(), 1252);
		QCOMPARE(text(), QString::fromUtf8("café\n"));

		// Convert to UTF-8: the text is kept, the buffer becomes UTF-8 and dirty
		_npp->command(IDM_FORMAT_CONV2_AS_UTF_8);
		QCOMPARE(view()->getCurrentBuffer()->getUnicodeMode(), uniUTF8_NoBOM);
		QCOMPARE(view()->getCurrentBuffer()->getEncoding(), -1);
		QVERIFY(view()->getCurrentBuffer()->isDirty());
		QCOMPARE(text(), QString::fromUtf8("café\n"));
	}

	void goToLine()
	{
		newDocument("l1\nl2\nl3\nl4\n");
		_npp->command(IDM_SEARCH_GOTOLINE);
		GoToLineDlg* dlg = _npp->getGoToLineDlg();
		QVERIFY(dlg->isVisible());
		dlg->goLineEdit()->setText("3");
		dlg->command(IDOK);
		QVERIFY(!dlg->isVisible());
		QCOMPARE(view()->getCurrentLineNumber(), 2);

		// offset mode
		_npp->command(IDM_SEARCH_GOTOLINE);
		dlg->command(IDC_RADIO_GOTOOFFSET);
		dlg->goLineEdit()->setText("4");
		dlg->command(IDOK);
		QCOMPARE(view()->execute(SCI_GETCURRENTPOS), 4);
		dlg->command(IDC_RADIO_GOTOLINE);
	}

	void monitoringTailF()
	{
		const QString path = _tmp.path() + "/log.txt";
		writeFile(path, "line 1\n");
		_npp->switchToFile(_npp->doOpen(path.toStdWString()));
		_npp->command(IDM_VIEW_MONITORING);
		Buffer* buf = view()->getCurrentBuffer();
		QVERIFY(buf->isMonitoringOn());
		QVERIFY(buf->getUserReadOnly());
		QVERIFY(action(IDM_VIEW_MONITORING)->isChecked());

		// another program appends to the file: reloaded, caret at the end
		{
			std::ofstream f(path.toStdString(), std::ios::app);
			f << "line 2\n";
		}
		QTRY_COMPARE(text(), QString("line 1\nline 2\n"));
		QCOMPARE(view()->execute(SCI_GETCURRENTPOS), view()->execute(SCI_GETLENGTH));

		_npp->command(IDM_VIEW_MONITORING);
		QVERIFY(!buf->isMonitoringOn());
		QVERIFY(!buf->getUserReadOnly());
		{
			std::ofstream f(path.toStdString(), std::ios::app);
			f << "line 3\n";
		}
		QTest::qWait(600);
		QCOMPARE(text(), QString("line 1\nline 2\n"));
	}

	void checkableMenuItemsToggleOnce()
	{
		// clicking the menu item runs the command once, which sets the check mark (Win32 semantics)
		const ScintillaViewParams& svp = NppParameters::getInstance().getSVP();
		const bool before = svp._npcShow;
		action(IDM_VIEW_NPC)->trigger();
		QCOMPARE(svp._npcShow, !before);
		QCOMPARE(action(IDM_VIEW_NPC)->isChecked(), !before);
		action(IDM_VIEW_NPC)->trigger();
		QCOMPARE(svp._npcShow, before);
		QCOMPARE(action(IDM_VIEW_NPC)->isChecked(), before);
	}

	void synchronizedScrolling()
	{
		closeAll();
		QByteArray lines;
		for (int i = 1; i <= 300; ++i)
			lines += "line " + QByteArray::number(i) + "\n";
		newDocument(lines.constData());
		_npp->command(IDM_VIEW_CLONE_TO_ANOTHER_VIEW);
		QVERIFY(_npp->bothActive());
		QVERIFY(action(IDM_VIEW_SYNSCROLLV)->isEnabled());

		_npp->command(IDM_VIEW_SYNSCROLLV);
		QVERIFY(action(IDM_VIEW_SYNSCROLLV)->isChecked());
		ScintillaEditView* main = _npp->getDocTab(MAIN_VIEW)->getScintillaEditView();
		ScintillaEditView* sub = _npp->getDocTab(SUB_VIEW)->getScintillaEditView();
		main->execute(SCI_SETFIRSTVISIBLELINE, 100);
		main->viewport()->repaint(); // SCN_PAINTED drives the synchronization
		QTRY_COMPARE(sub->execute(SCI_GETFIRSTVISIBLELINE), 100);

		_npp->command(IDM_VIEW_SYNSCROLLV);
		main->execute(SCI_SETFIRSTVISIBLELINE, 10);
		main->viewport()->repaint();
		QTest::qWait(50);
		QCOMPARE(sub->execute(SCI_GETFIRSTVISIBLELINE), 100);

		// closing the clone leaves one view: synchronization is disabled
		_npp->command(IDM_VIEW_SYNSCROLLV);
		_npp->switchEditViewTo(SUB_VIEW);
		_npp->command(IDM_FILE_CLOSE);
		QVERIFY(!_npp->bothActive());
		QVERIFY(!action(IDM_VIEW_SYNSCROLLV)->isEnabled());
		QVERIFY(!action(IDM_VIEW_SYNSCROLLV)->isChecked());
	}

	void autoCompletionAndCallTips()
	{
		// word completion from the document (Ctrl+Enter)
		newDocument("alphabet alpha\nal");
		view()->execute(SCI_DOCUMENTEND);
		_npp->command(IDM_EDIT_AUTOCOMPLETE_CURRENTFILE);
		QVERIFY(view()->execute(SCI_AUTOCACTIVE));
		view()->execute(SCI_AUTOCCANCEL);

		// API completion from data/autoCompletion/cpp.xml (Ctrl+Space)
		newDocument("strc");
		_npp->command(IDM_LANG_CPP);
		view()->execute(SCI_DOCUMENTEND);
		_npp->command(IDM_EDIT_AUTOCOMPLETE);
		QVERIFY(view()->execute(SCI_AUTOCACTIVE));
		view()->execute(SCI_AUTOCSELECT, 0, reinterpret_cast<LPARAM>("strcmp"));
		view()->execute(SCI_AUTOCCOMPLETE);
		QCOMPARE(text(), QString("strcmp"));

		// function call tip (Ctrl+Shift+Space) after "("
		view()->execute(SCI_APPENDTEXT, 1, reinterpret_cast<LPARAM>("("));
		view()->execute(SCI_DOCUMENTEND);
		_npp->command(IDM_EDIT_FUNCCALLTIP);
		QVERIFY(view()->execute(SCI_CALLTIPACTIVE));
		view()->execute(SCI_CALLTIPCANCEL);

		// path completion
		newDocument("/us");
		view()->execute(SCI_DOCUMENTEND);
		_npp->command(IDM_EDIT_AUTOCOMPLETE_PATH);
		QVERIFY(view()->execute(SCI_AUTOCACTIVE));
		view()->execute(SCI_AUTOCSELECT, 0, reinterpret_cast<LPARAM>("/usr/"));
		view()->execute(SCI_AUTOCCOMPLETE);
		QCOMPARE(text(), QString("/usr/"));
	}

	void autoIndent()
	{
		// typing Enter after "{" in C++ indents the new line (upstream maintainIndentation, advanced mode)
		newDocument("int f() {");
		_npp->command(IDM_LANG_CPP);
		view()->execute(SCI_SETUSETABS, true);
		view()->execute(SCI_DOCUMENTEND);
		view()->setFocus();
		QTest::keyClick(view(), Qt::Key_Return);
		QCOMPARE(text(), QString("int f() {\n\t"));
	}

	void clickableLinks()
	{
		newDocument("see https://notepad-plus-plus.org/ and more\n");
		view()->viewport()->repaint(); // SCN_PAINTED runs addHotSpot
		QTRY_VERIFY(view()->execute(SCI_INDICATORVALUEAT, URL_INDIC, 10));
		QVERIFY(!view()->execute(SCI_INDICATORVALUEAT, URL_INDIC, 1));
		QCOMPARE(view()->execute(SCI_INDICATOREND, URL_INDIC, 10), 34);

		// editing re-runs the detection
		view()->execute(SCI_SETSEL, 0, 3);
		view()->execute(SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>("ftp://x.org"));
		view()->viewport()->repaint();
		QTRY_VERIFY(view()->execute(SCI_INDICATORVALUEAT, URL_INDIC, 2));
	}

	void undoStateFollowsTheDocument()
	{
		newDocument("");
		view()->execute(SCI_EMPTYUNDOBUFFER);
		view()->viewport()->repaint(); // SCN_UPDATEUI
		QTRY_VERIFY(!action(IDM_EDIT_UNDO)->isEnabled());
		view()->execute(SCI_ADDTEXT, 1, reinterpret_cast<LPARAM>("z"));
		view()->viewport()->repaint();
		QTRY_VERIFY(action(IDM_EDIT_UNDO)->isEnabled());
	}
};

QTEST_MAIN(TestCommands)
#include "tst_commands.moc"
