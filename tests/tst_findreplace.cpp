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

// Phase 4 tests: the port of upstream FindReplaceDlg (search engine, dialog commands by control ID),
// the Finder (search results panel), Find in Files, Replace in Files, Mark, smart highlighting
// and brace matching. Set NPLX_SCREENSHOT_DIR to save screenshots.

#include <QtTest>
#include <QAbstractButton>
#include <QComboBox>
#include <QDir>
#include <QTabBar>
#include <QToolBar>
#include <QLabel>
#include <QLineEdit>
#include <QTemporaryDir>

#include <cstdlib>
#include <fstream>

#include "Buffer.h"
#include "FindReplaceDlg.h"
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
			return (buttons == MB_YESNO || buttons == MB_YESNOCANCEL) ? IDNO : IDOK;
		}
		int saveOrNotMultiBox(const std::wstring&, const std::wstring&) override
		{
			++_count;
			return IDNO;
		}
		int _count = 0;
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

class TestFindReplace : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir _tmp;
	SilentUiServices _ui;
	NativeLangSpeaker _nativeLangSpeaker;
	std::unique_ptr<Notepad_plus> _npp;

	ScintillaEditView* view() { return _npp->getActiveEditView(); }
	FindReplaceDlg* dlg() { return _npp->getFindReplaceDlg(); }

	void newDocument(const char* text)
	{
		_npp->command(IDM_FILE_NEW);
		view()->setText(text);
		view()->execute(SCI_GOTOPOS, 0);
	}

	void setCombo(int id, const QString& text)
	{
		dlg()->comboBox(id)->setEditText(text);
	}

	void setCheck(int id, bool checked)
	{
		auto* button = qobject_cast<QAbstractButton*>(dlg()->dlgItem(id));
		QVERIFY(button);
		button->setChecked(checked);
	}

	// Normal mode, no whole word, no case, wrap around: the most lax options
	void resetOptions()
	{
		setCheck(IDNORMAL, true);
		setCheck(IDWHOLEWORD, false);
		setCheck(IDMATCHCASE, false);
		setCheck(IDWRAP, true);
		setCheck(IDC_BACKWARDDIRECTION, false);
		setCheck(IDC_IN_SELECTION_CHECK, false);
	}

	QString docText()
	{
		return QString::fromStdString(view()->getText());
	}

	QString selection()
	{
		return QString::fromStdWString(view()->getSelectedTextToWChar(false));
	}

	QString status()
	{
		return QString::fromStdWString(dlg()->statusbarMessage());
	}

	QString finderText()
	{
		Finder* finder = dlg()->getMainFinder();
		return finder ? QString::fromStdString(finder->getScintView()->getText()) : QString();
	}

	void screenshot(QWidget* w, const char* name)
	{
		const QByteArray dir = qgetenv("NPLX_SCREENSHOT_DIR");
		if (!dir.isEmpty())
		{
			QCoreApplication::processEvents();
			w->grab().save(QString::fromUtf8(dir) + "/" + name);
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

		// The dialog is created on first use, like upstream
		QVERIFY(!dlg()->isCreated());
		_npp->command(IDM_SEARCH_FIND);
		QVERIFY(dlg()->isCreated());
		QVERIFY(dlg()->isVisible());
		resetOptions();
	}

	void cleanupTestCase()
	{
		_npp.reset();
		nplx::setUiServices(nullptr);
	}

	void extendedModeEscapes()
	{
		// upstream Searching::convertExtendedToString
		wchar_t out[64]{};
		const wchar_t* in = L"a\\tb\\n\\x41\\u00e9\\d065\\o101\\b01000001\\\\\\q";
		const int len = Searching::convertExtendedToString(in, out, static_cast<int>(wcslen(in)));
		QCOMPARE(QString::fromWCharArray(out, len), QString::fromUtf8("a\tb\nAéAAA\\\\q"));
	}

	void findNextWrapsAndReportsStatus()
	{
		newDocument("one two\nthree two\nfour\n");
		_npp->command(IDM_SEARCH_FIND);
		resetOptions();
		setCombo(IDFINDWHAT, "TWO");

		dlg()->command(IDOK);
		QCOMPARE(selection(), QString("two"));
		QCOMPARE(view()->execute(SCI_GETSELECTIONSTART), 4);

		dlg()->command(IDOK);
		QCOMPARE(view()->execute(SCI_GETSELECTIONSTART), 14);

		// wrap around: back to the first one, with the upstream status message
		dlg()->command(IDOK);
		QCOMPARE(view()->execute(SCI_GETSELECTIONSTART), 4);
		QCOMPARE(status(), QString::fromWCharArray(FIND_STATUS_END_REACHED_TEXT));

		// Find Previous from the menu (F3/Shift+F3 use the dialog's text and options)
		_npp->command(IDM_SEARCH_FINDPREV);
		QCOMPARE(view()->execute(SCI_GETSELECTIONSTART), 14);

		// Match case: nothing found, upstream message with the scope
		setCheck(IDMATCHCASE, true);
		dlg()->command(IDOK);
		QVERIFY2(status().startsWith("Find: Can't find the text \"TWO\""), qPrintable(status()));
		QVERIFY(status().contains("in entire file"));
		setCheck(IDMATCHCASE, false);

		// the search text went into the history combo
		QCOMPARE(dlg()->comboBox(IDFINDWHAT)->itemText(0), QString("TWO"));
		screenshot(dlg(), "findreplace-find.png");
	}

	void regexSearchAndInvalidRegex()
	{
		newDocument("id=42; name=bob; id=7;\n");
		setCheck(IDREGEXP, true);
		// regex mode turns whole word off and disables it (upstream)
		QVERIFY(!dlg()->dlgItem(IDWHOLEWORD)->isEnabled());

		setCombo(IDFINDWHAT, "(?<=id=)\\d+");
		dlg()->command(IDOK);
		QCOMPARE(selection(), QString("42"));
		dlg()->command(IDOK);
		QCOMPARE(selection(), QString("7"));

		setCombo(IDFINDWHAT, "(abc");
		dlg()->command(IDOK);
		QCOMPARE(status(), QString("Find: Invalid Regular Expression"));
		resetOptions();
		QVERIFY(dlg()->dlgItem(IDWHOLEWORD)->isEnabled());
	}

	void countAndReplaceAll()
	{
		newDocument("cat dog cat\nbird cat\n");
		_npp->command(IDM_SEARCH_FIND);
		setCombo(IDFINDWHAT, "cat");
		dlg()->command(IDCCOUNTALL);
		QCOMPARE(status(), QString("Count: 3 matches in entire file"));

		_npp->command(IDM_SEARCH_REPLACE);
		QCOMPARE(dlg()->getCurrentStatus(), REPLACE_DLG);
		QVERIFY(dlg()->dlgItem(IDREPLACEWITH)->isVisible());
		setCombo(IDFINDWHAT, "cat");
		setCombo(IDREPLACEWITH, "lion");
		dlg()->command(IDREPLACEALL);
		QCOMPARE(docText(), QString("lion dog lion\nbird lion\n"));
		QCOMPARE(status(), QString("Replace All: 3 occurrences were replaced in entire file"));
		QVERIFY(view()->getCurrentBuffer()->isDirty());

		// one undo step for the whole Replace All
		view()->execute(SCI_UNDO);
		QCOMPARE(docText(), QString("cat dog cat\nbird cat\n"));

		// regex with back references
		newDocument("john@example bob@test\n");
		setCheck(IDREGEXP, true);
		setCombo(IDFINDWHAT, "(\\w+)@(\\w+)");
		setCombo(IDREPLACEWITH, "\\2 at \\1");
		dlg()->command(IDREPLACEALL);
		QCOMPARE(docText(), QString("example at john test at bob\n"));

		// extended mode: \n in the replacement
		newDocument("a,b,c");
		setCheck(IDEXTENDED, true);
		setCombo(IDFINDWHAT, ",");
		setCombo(IDREPLACEWITH, "\\n");
		dlg()->command(IDREPLACEALL);
		QCOMPARE(docText(), QString("a\nb\nc"));
		resetOptions();

		// Replace (one at a time): replaces the selected match then selects the next one
		newDocument("x1 x2 x3");
		setCombo(IDFINDWHAT, "x");
		setCombo(IDREPLACEWITH, "y");
		dlg()->command(IDREPLACE); // first press only selects the first match
		QCOMPARE(view()->execute(SCI_GETSELECTIONSTART), 0);
		dlg()->command(IDREPLACE);
		QCOMPARE(docText(), QString("y1 x2 x3"));
		QCOMPARE(view()->execute(SCI_GETSELECTIONSTART), 3);

		// swap button
		dlg()->command(IDD_FINDREPLACE_SWAP_BUTTON);
		QCOMPARE(dlg()->comboBox(IDFINDWHAT)->currentText(), QString("y"));
		QCOMPARE(dlg()->comboBox(IDREPLACEWITH)->currentText(), QString("x"));
		screenshot(dlg(), "findreplace-replace.png");
	}

	void replaceInSelectionOnly()
	{
		newDocument("aaa\naaa\naaa\n");
		view()->execute(SCI_SETSEL, 4, 8); // second line
		_npp->command(IDM_SEARCH_REPLACE); // activation enables "In selection" (upstream WM_ACTIVATE)
		QVERIFY(dlg()->dlgItem(IDC_IN_SELECTION_CHECK)->isEnabled());
		setCheck(IDC_IN_SELECTION_CHECK, true);
		setCombo(IDFINDWHAT, "a");
		setCombo(IDREPLACEWITH, "b");
		dlg()->command(IDREPLACEALL);
		QCOMPARE(docText(), QString("aaa\nbbb\naaa\n"));
		QVERIFY(status().contains("in selected text"));
		setCheck(IDC_IN_SELECTION_CHECK, false);
	}

	void markAllWithBookmarks()
	{
		newDocument("red\ngreen\nred apple\nblue\n");
		_npp->command(IDM_SEARCH_MARK);
		QCOMPARE(dlg()->getCurrentStatus(), MARK_DLG);
		QVERIFY(dlg()->dlgItem(IDCMARKALL)->isVisible());
		QVERIFY(!dlg()->dlgItem(IDOK)->isVisible());

		setCheck(IDC_MARKLINE_CHECK, true);
		setCombo(IDFINDWHAT, "red");
		dlg()->command(IDCMARKALL);
		QCOMPARE(status(), QString("Mark: 2 matches in entire file"));
		QVERIFY(view()->execute(SCI_INDICATORVALUEAT, SCE_UNIVERSAL_FOUND_STYLE, 1));
		QVERIFY(!view()->execute(SCI_INDICATORVALUEAT, SCE_UNIVERSAL_FOUND_STYLE, 5));
		QVERIFY(view()->execute(SCI_MARKERGET, 0) & (1 << MARK_BOOKMARK));
		QVERIFY(!(view()->execute(SCI_MARKERGET, 1) & (1 << MARK_BOOKMARK)));
		QVERIFY(view()->execute(SCI_MARKERGET, 2) & (1 << MARK_BOOKMARK));

		// Search > Jump down / up (default mark style)
		view()->execute(SCI_GOTOPOS, 5);
		_npp->command(IDM_SEARCH_GONEXTMARKER_DEF);
		QCOMPARE(view()->execute(SCI_GETSELECTIONSTART), 10);

		dlg()->command(IDC_CLEAR_ALL);
		QVERIFY(!view()->execute(SCI_INDICATORVALUEAT, SCE_UNIVERSAL_FOUND_STYLE, 1));
		QVERIFY(!(view()->execute(SCI_MARKERGET, 0) & (1 << MARK_BOOKMARK)));
		setCheck(IDC_MARKLINE_CHECK, false);
		screenshot(dlg(), "findreplace-mark.png");
	}

	void styleTokens()
	{
		// Search > Mark All > Using 1st Style, and Clear
		newDocument("foo bar foo baz foo\n");
		view()->execute(SCI_SETSEL, 0, 3);
		_npp->command(IDM_SEARCH_MARKALLEXT1);
		QVERIFY(view()->execute(SCI_INDICATORVALUEAT, SCE_UNIVERSAL_FOUND_STYLE_EXT1, 9));
		QVERIFY(view()->execute(SCI_INDICATORVALUEAT, SCE_UNIVERSAL_FOUND_STYLE_EXT1, 17));
		QVERIFY(!view()->execute(SCI_INDICATORVALUEAT, SCE_UNIVERSAL_FOUND_STYLE_EXT1, 5));

		view()->execute(SCI_GOTOPOS, 0);
		_npp->command(IDM_SEARCH_GONEXTMARKER1);
		QCOMPARE(view()->execute(SCI_GETSELECTIONSTART), 8);
		_npp->command(IDM_SEARCH_GONEXTMARKER1);
		QCOMPARE(view()->execute(SCI_GETSELECTIONSTART), 16);
		view()->execute(SCI_GOTOPOS, 16);
		_npp->command(IDM_SEARCH_GOPREVMARKER1); // caret at the start of the token: goes to the previous one
		QCOMPARE(view()->execute(SCI_GETSELECTIONSTART), 8);

		_npp->command(IDM_SEARCH_UNMARKALLEXT1);
		QVERIFY(!view()->execute(SCI_INDICATORVALUEAT, SCE_UNIVERSAL_FOUND_STYLE_EXT1, 9));
	}

	void findAllInCurrentDocumentFillsFinder()
	{
		newDocument("alpha\nbeta alpha\ngamma\nalpha alpha\n");
		_npp->command(IDM_SEARCH_FIND);
		resetOptions();
		setCombo(IDFINDWHAT, "alpha");
		dlg()->command(IDC_FINDALL_CURRENTFILE);

		Finder* finder = dlg()->getMainFinder();
		QVERIFY(finder);
		QVERIFY(finder->isVisible());
		const QString text = finderText();
		QVERIFY2(text.startsWith("Search \"alpha\" (4 hits in 1 file of 1 searched) [Normal]"), qPrintable(text));
		QVERIFY2(text.contains("\tLine 1: alpha"), qPrintable(text));
		QVERIFY(text.contains("\tLine 2: beta alpha"));
		QVERIFY(text.contains("\tLine 4: alpha alpha"));
		QVERIFY(text.contains("(4 hits)"));

		// the searchResult lexer styles the hits (SCE_SEARCHRESULT_WORD2SEARCH)
		ScintillaEditView* finderView = finder->getScintView();
		finderView->execute(SCI_COLOURISE, 0, -1);
		const int line2 = static_cast<int>(text.indexOf("\tLine 2: beta alpha"));
		const intptr_t hitPos = line2 + QString("\tLine 2: beta ").size();
		QCOMPARE(finderView->execute(SCI_GETSTYLEAT, hitPos), SCE_SEARCHRESULT_WORD2SEARCH);

		// F4 / Shift+F4: go to the next / previous result in the document
		view()->execute(SCI_GOTOPOS, 0);
		_npp->command(IDM_SEARCH_GOTONEXTFOUND);
		QCOMPARE(view()->execute(SCI_GETSELECTIONSTART), 0);
		_npp->command(IDM_SEARCH_GOTONEXTFOUND);
		QCOMPARE(view()->execute(SCI_GETSELECTIONSTART), 11);
		_npp->command(IDM_SEARCH_GOTONEXTFOUND);
		QCOMPARE(view()->execute(SCI_GETSELECTIONSTART), 23);
		_npp->command(IDM_SEARCH_GOTONEXTFOUND); // second hit on the same line
		QCOMPARE(view()->execute(SCI_GETSELECTIONSTART), 29);
		_npp->command(IDM_SEARCH_GOTOPREVFOUND);
		QCOMPARE(view()->execute(SCI_GETSELECTIONSTART), 23);
		screenshot(_npp.get(), "findreplace-finder.png");
	}

	void findAllInOpenedDocuments()
	{
		const QString a = _tmp.path() + "/opened_a.txt";
		const QString b = _tmp.path() + "/opened_b.txt";
		writeFile(a, "needle here\n");
		writeFile(b, "no\nneedle and needle\n");
		_npp->switchToFile(_npp->doOpen(a.toStdWString()));
		_npp->switchToFile(_npp->doOpen(b.toStdWString()));

		_npp->command(IDM_SEARCH_FIND);
		setCombo(IDFINDWHAT, "needle");
		dlg()->command(IDC_FINDALL_OPENEDFILES);
		const QString text = finderText();
		QVERIFY2(text.contains("Search \"needle\" (3 hits in 2 files of"), qPrintable(text));
		QVERIFY(text.contains("  " + a + " (1 hit)"));
		QVERIFY(text.contains("  " + b + " (2 hits)"));
	}

	void findAndReplaceInFiles()
	{
		const QString root = _tmp.path() + "/fif";
		QVERIFY(QDir().mkpath(root + "/sub"));
		QVERIFY(QDir().mkpath(root + "/.hidden"));
		writeFile(root + "/one.txt", "token one\n");
		writeFile(root + "/two.cpp", "token two\n");
		writeFile(root + "/sub/three.txt", "x\ntoken three token\n");
		writeFile(root + "/.hidden/four.txt", "token four\n");

		_npp->command(IDM_SEARCH_FINDINFILES);
		QCOMPARE(dlg()->getCurrentStatus(), FINDINFILES_DLG);
		QVERIFY(dlg()->dlgItem(IDD_FINDINFILES_DIR_COMBO)->isVisible());
		QVERIFY(!dlg()->dlgItem(IDCCOUNTALL)->isVisible());
		setCombo(IDFINDWHAT, "token");
		setCombo(IDD_FINDINFILES_FILTERS_COMBO, "*.txt");
		setCombo(IDD_FINDINFILES_DIR_COMBO, root);
		setCheck(IDD_FINDINFILES_RECURSIVE_CHECK, true);
		setCheck(IDD_FINDINFILES_INHIDDENDIR_CHECK, false);
		screenshot(dlg(), "findreplace-fif.png");

		dlg()->command(IDD_FINDINFILES_FIND_BUTTON);
		QString text = finderText();
		QVERIFY2(text.contains("Search \"token\" (3 hits in 2 files of 2 searched)"), qPrintable(text));
		QVERIFY(text.contains(root + "/one.txt (1 hit)"));
		QVERIFY(text.contains(root + "/sub/three.txt (2 hits)"));
		QVERIFY(!text.contains("two.cpp"));
		QVERIFY(!text.contains("four.txt"));

		// hidden folders on request; exclusion of a folder with !\sub (upstream filter syntax)
		setCheck(IDD_FINDINFILES_INHIDDENDIR_CHECK, true);
		setCombo(IDD_FINDINFILES_FILTERS_COMBO, "*.txt !\\sub");
		dlg()->command(IDD_FINDINFILES_FIND_BUTTON);
		text = finderText();
		QVERIFY2(text.contains("Search \"token\" (2 hits in 2 files of 2 searched)"), qPrintable(text));
		QVERIFY(text.contains(root + "/.hidden/four.txt (1 hit)"));
		setCheck(IDD_FINDINFILES_INHIDDENDIR_CHECK, false);

		// Replace in Files writes the files on disk (confirmation answered OK)
		setCombo(IDD_FINDINFILES_FILTERS_COMBO, "*.txt");
		setCombo(IDREPLACEWITH, "word");
		const int boxesBefore = _ui._count;
		dlg()->command(IDD_FINDINFILES_REPLACEINFILES);
		QCOMPARE(_ui._count, boxesBefore + 1);
		QCOMPARE(readFile(root + "/one.txt"), QByteArray("word one\n"));
		QCOMPARE(readFile(root + "/sub/three.txt"), QByteArray("x\nword three word\n"));
		QCOMPARE(readFile(root + "/two.cpp"), QByteArray("token two\n"));
		QCOMPARE(status(), QString("Replace in Files: 3 occurrences were replaced."));

		// double-click equivalent: the Finder opens the file of a result
		Finder* finder = dlg()->getMainFinder();
		ScintillaEditView* finderView = finder->getScintView();
		text = finderText();
		const int lineStart = static_cast<int>(text.indexOf("\tLine 2: token three"));
		QVERIFY(lineStart > 0);
		finderView->execute(SCI_GOTOPOS, lineStart + 12);
		finder->gotoFoundLine();
		QCOMPARE(QString::fromWCharArray(view()->getCurrentBuffer()->getFullPathName()), root + "/sub/three.txt");
	}

	void smartHighlighting()
	{
		newDocument("value = value + other_value;\nvalue\n");
		const NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();
		QVERIFY(nppGUI._enableSmartHilite);
		// upstream: jumping to a marker (styleTokens) skips the next smart highlighting once
		view()->execute(SCI_GOTOPOS, 1);
		view()->viewport()->repaint();
		QVERIFY(!nppGUI._disableSmartHiliteTmp);

		view()->execute(SCI_SETSEL, 0, 5); // "value"
		view()->viewport()->repaint(); // Scintilla sends SCN_UPDATEUI when it paints
		QTRY_VERIFY(view()->execute(SCI_INDICATORVALUEAT, SCE_UNIVERSAL_FOUND_STYLE_SMART, 9));
		QVERIFY(view()->execute(SCI_INDICATORVALUEAT, SCE_UNIVERSAL_FOUND_STYLE_SMART, 30));
		// whole word only by default: "other_value" is not highlighted
		QVERIFY(!view()->execute(SCI_INDICATORVALUEAT, SCE_UNIVERSAL_FOUND_STYLE_SMART, 23));

		// cleared when the selection goes away
		view()->execute(SCI_GOTOPOS, 2);
		view()->viewport()->repaint();
		QTRY_VERIFY(!view()->execute(SCI_INDICATORVALUEAT, SCE_UNIVERSAL_FOUND_STYLE_SMART, 9));
	}

	void braceMatching()
	{
		newDocument("f(a[1], {b});\n");
		view()->execute(SCI_GOTOPOS, 1); // before '('
		QCoreApplication::processEvents();
		_npp->command(IDM_SEARCH_SELECTMATCHINGBRACES);
		QCOMPARE(selection(), QString("(a[1], {b})"));

		view()->execute(SCI_GOTOPOS, 9); // just after '{' (the brace before the caret has priority)
		_npp->command(IDM_SEARCH_GOTOMATCHINGBRACE);
		QCOMPARE(view()->execute(SCI_GETCURRENTPOS), 10);
	}

	void tabsKeepTheLayout()
	{
		// upstream IDD_FIND_REPLACE_DLG: one dialog size for all the tabs, and the controls that several tabs show
		// stay in their place when the tab changes
		_npp->command(IDM_SEARCH_FIND);
		QVERIFY(QTest::qWaitForWindowExposed(dlg()));
		auto* tabs = dlg()->findChild<QTabBar*>();
		QVERIFY(tabs);
		const QList<int> shared = { IDFINDWHAT, IDWHOLEWORD, IDMATCHCASE, IDC_MODE_STATIC, IDCANCEL, IDC_TRANSPARENT_GRPBOX };
		QSize size;
		QMap<int, QRect> places;
		const QString shots = qEnvironmentVariable("NPLX_FIND_SCREENSHOTS_DIR");
		for (int index = 0; index < tabs->count(); ++index)
		{
			tabs->setCurrentIndex(index);
			QCoreApplication::processEvents();
			if (!shots.isEmpty())
				dlg()->grab().save(QDir(shots).filePath(QStringLiteral("find-tab-%1.png").arg(index)));
			if (index == 0)
			{
				size = dlg()->size();
				for (int id : shared)
					places[id] = dlg()->dlgItem(id)->geometry();
				continue;
			}
			QCOMPARE(dlg()->size(), size);
			for (int id : shared)
				QVERIFY2(dlg()->dlgItem(id)->geometry() == places[id], qPrintable(QStringLiteral("control %1 moved in tab %2").arg(id).arg(index)));
		}
		// Replace with and the Find in Files fields share one column with Find what
		tabs->setCurrentIndex(2);
		QCoreApplication::processEvents();
		for (int id : { IDREPLACEWITH, IDD_FINDINFILES_FILTERS_COMBO, IDD_FINDINFILES_DIR_COMBO })
		{
			QCOMPARE(dlg()->dlgItem(id)->x(), places[IDFINDWHAT].x());
			QCOMPARE(dlg()->dlgItem(id)->width(), places[IDFINDWHAT].width());
		}
		tabs->setCurrentIndex(0);
		dlg()->command(IDCANCEL);
	}

	void incrementalSearch()
	{
		// Search > Incremental Search (upstream FindIncrementDlg): a bar that searches while typing
		QVERIFY(Notepad_plus::isCommandPorted(IDM_SEARCH_FINDINCREMENT));
		newDocument("alpha beta\nalpha gamma\nALPHA delta\n");
		FindIncrementDlg* inc = _npp->getIncrementFindDlg();
		QVERIFY(inc);
		QVERIFY(!inc->isVisibleBar());
		_npp->command(IDM_SEARCH_FINDINCREMENT);
		QVERIFY(inc->isVisibleBar());

		auto check = [inc](int id, bool on) {
			auto* box = qobject_cast<QAbstractButton*>(inc->dlgItem(id));
			if (box->isChecked() != on)
				box->click();
		};
		check(IDC_INCFINDMATCHCASE, false);
		check(IDC_INCFINDCOUNT, true);
		check(IDC_INCFINDHILITEALL, false);

		auto* edit = qobject_cast<QLineEdit*>(inc->dlgItem(IDC_INCFINDTEXT));
		QVERIFY(edit);
		edit->setFocus();
		QTest::keyClicks(edit, QStringLiteral("alpha"));
		QCOMPARE(view()->execute(SCI_GETSELECTIONSTART), 0);
		QCOMPARE(view()->execute(SCI_GETSELECTIONEND), 5);
		auto status = [inc]() { return qobject_cast<QLabel*>(inc->dlgItem(IDC_INCFINDSTATUS))->text(); };
		QCOMPARE(status(), QStringLiteral("1/3")); // ignore case: ALPHA too

		// Enter: the next one; Shift+Enter: back
		QTest::keyClick(edit, Qt::Key_Return);
		QCOMPARE(view()->execute(SCI_GETSELECTIONSTART), 11);
		QCOMPARE(status(), QStringLiteral("2/3"));
		QTest::keyClick(edit, Qt::Key_Return, Qt::ShiftModifier);
		QCOMPARE(view()->execute(SCI_GETSELECTIONSTART), 0);

		// Match case: ALPHA is not counted
		check(IDC_INCFINDMATCHCASE, true);
		QCOMPARE(status(), QStringLiteral("1/2"));

		// not found: the status says so
		QTest::keyClicks(edit, QStringLiteral("zzz"));
		QCOMPARE(inc->getFindStatus(), FSNotFound);
		QCOMPARE(status(), QStringLiteral("Phrase not found"));
		check(IDC_INCFINDMATCHCASE, false);

		// the bar moves to the top and back (upstream NPPM_INTERNAL_INCSEARCHBARPOSITION)
		auto* bar = _npp->findChild<QToolBar*>(QStringLiteral("incrementalSearchBar"));
		QVERIFY(bar);
		QCOMPARE(_npp->toolBarArea(bar), Qt::BottomToolBarArea);
		qobject_cast<QAbstractButton*>(inc->dlgItem(IDC_GOUPDOWN_TOGGLE_BUTTON))->click();
		QCOMPARE(_npp->toolBarArea(bar), Qt::TopToolBarArea);
		QVERIFY(inc->isVisibleBar());
		qobject_cast<QAbstractButton*>(inc->dlgItem(IDC_GOUPDOWN_TOGGLE_BUTTON))->click();
		QCOMPARE(_npp->toolBarArea(bar), Qt::BottomToolBarArea);

		// Esc closes the bar and gives the focus back to the editor
		edit->setFocus();
		QTest::keyClick(edit, Qt::Key_Escape);
		QVERIFY(!inc->isVisibleBar());
		_npp->command(IDM_FILE_CLOSE);
	}

	void escapeClosesDialog()
	{
		_npp->command(IDM_SEARCH_FIND);
		QVERIFY(dlg()->isVisible());
		dlg()->command(IDCANCEL);
		QVERIFY(!dlg()->isVisible());

		// history goes to config.xml at exit (upstream saveFindHistory)
		dlg()->saveFindHistory();
		const FindHistory& findHistory = NppParameters::getInstance().getFindHistory();
		QVERIFY(!findHistory._findHistoryFinds.empty());
	}
};

QTEST_MAIN(TestFindReplace)
#include "tst_findreplace.moc"
