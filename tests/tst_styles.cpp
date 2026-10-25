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

// Phase 5 tests: the Style Configurator (port of upstream WordStyleDlg) and theme switching.
// Every theme of data/themes must load and render (Phase 5 acceptance). Set NPLX_SCREENSHOT_DIR for screenshots.

#include <QtTest>
#include <QAction>
#include <QCheckBox>
#include <QMenu>
#include <QComboBox>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QDockWidget>
#include <QPushButton>

#include <cstdlib>

#include "Buffer.h"
#include "SciLexer.h"
#include "ColourPicker.h"
#include "Notepad_plus.h"
#include "Parameters.h"
#include "QtUiServices.h"
#include "ScintillaEditView.h"
#include "UserDefineDialog.h"
#include "UserDefineResource.h"
#include "WordStyleDlg.h"
#include "WordStyleDlgRes.h"
#include "localization.h"
#include "menuCmdID.h"

namespace
{
	class SilentUiServices : public QtUiServices
	{
	public:
		int messageBox(const std::wstring&, const std::wstring&, unsigned int) override { return _answer; }
		int _answer = IDNO;
		int saveOrNotMultiBox(const std::wstring&, const std::wstring&) override { return IDNO; }
	};
}

class TestStyles : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir _tmp;
	SilentUiServices _ui;
	NativeLangSpeaker _nativeLangSpeaker;
	std::unique_ptr<Notepad_plus> _npp;

	ScintillaEditView* view() { return _npp->getActiveEditView(); }
	WordStyleDlg* dlg() { return _npp->getConfigStyleDlg(); }
	QComboBox* themeCombo() { return qobject_cast<QComboBox*>(dlg()->dlgItem(IDC_SWITCH2THEME_COMBO)); }

	void screenshot(QWidget* w, const QString& name)
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

		_npp->command(IDM_FILE_NEW);
		view()->setText("int main() { return 0; } // comment\n");
		_npp->command(IDM_LANG_CPP);
		_npp->command(IDM_LANGSTYLE_CONFIG_DLG);
		QVERIFY(dlg()->isCreated());
		QVERIFY(dlg()->isVisible());
	}

	void cleanupTestCase()
	{
		_npp.reset();
		nplx::setUiServices(nullptr);
	}

	void everyThemeRenders()
	{
		ThemeSwitcher& themeSwitcher = NppParameters::getInstance().getThemeSwitcher();
		QVERIFY2(themeSwitcher.size() >= 23, qPrintable(QString::number(themeSwitcher.size()))); // Default + 22 themes
		QCOMPARE(themeCombo()->count(), static_cast<int>(themeSwitcher.size()));

		for (int i = 0; i < themeCombo()->count(); ++i)
		{
			const QString name = themeCombo()->itemText(i);
			themeCombo()->setCurrentIndex(i);
			dlg()->command(IDC_SWITCH2THEME_COMBO);

			// the editor uses the theme's Default Style
			const Style* defaultStyle = NppParameters::getInstance().getGlobalStylers().findByID(STYLE_DEFAULT);
			QVERIFY2(defaultStyle, qPrintable(name));
			QCOMPARE(static_cast<COLORREF>(view()->execute(SCI_STYLEGETBACK, STYLE_DEFAULT)), defaultStyle->_bgColor);
			QCOMPARE(static_cast<COLORREF>(view()->execute(SCI_STYLEGETFORE, STYLE_DEFAULT)), defaultStyle->_fgColor);

			// and the theme's C++ keyword style
			LexerStyler* cpp = NppParameters::getInstance().getLStylerArray().getLexerStylerByName(L"cpp");
			QVERIFY2(cpp, qPrintable(name));
			const Style* instruction = cpp->findByID(SCE_C_WORD);
			if (instruction && (instruction->_colorStyle & COLORSTYLE_FOREGROUND))
				QCOMPARE(static_cast<COLORREF>(view()->execute(SCI_STYLEGETFORE, SCE_C_WORD)), instruction->_fgColor);

			if (name == QStringLiteral("Monokai"))
			{
				screenshot(_npp.get(), "theme-monokai.png");
				screenshot(dlg(), "style-configurator.png");
			}
		}
	}

	void editStyleAndCancel()
	{
		// back to the default theme, saved as the current one
		themeCombo()->setCurrentIndex(0);
		dlg()->command(IDC_SWITCH2THEME_COMBO);
		dlg()->command(IDC_SAVECLOSE_BUTTON);
		QVERIFY(!dlg()->isVisible());
		const COLORREF before = static_cast<COLORREF>(view()->execute(SCI_STYLEGETFORE, SCE_C_COMMENTLINE));

		_npp->command(IDM_LANGSTYLE_CONFIG_DLG);
		QVERIFY(dlg()->goToSection(L"C++:COMMENT LINE"));
		dlg()->fgColourPicker()->setColour(RGB(0x12, 0x34, 0x56));
		emit dlg()->fgColourPicker()->colourPicked();
		QCOMPARE(static_cast<COLORREF>(view()->execute(SCI_STYLEGETFORE, SCE_C_COMMENTLINE)), static_cast<COLORREF>(RGB(0x12, 0x34, 0x56)));
		QVERIFY(dlg()->dlgItem(IDC_SAVECLOSE_BUTTON)->isEnabled());

		// bold
		qobject_cast<QCheckBox*>(dlg()->dlgItem(IDC_BOLD_CHECK))->setChecked(true);
		QVERIFY(view()->execute(SCI_STYLEGETBOLD, SCE_C_COMMENTLINE));

		// Cancel restores what was there
		dlg()->command(IDCANCEL);
		QCOMPARE(static_cast<COLORREF>(view()->execute(SCI_STYLEGETFORE, SCE_C_COMMENTLINE)), before);
		QVERIFY(!view()->execute(SCI_STYLEGETBOLD, SCE_C_COMMENTLINE));
	}

	void globalOverride()
	{
		_npp->command(IDM_LANGSTYLE_CONFIG_DLG);
		QVERIFY(dlg()->goToSection(L"Global Styles:Global override"));
		QVERIFY(dlg()->dlgItem(IDC_GLOBAL_FG_CHECK)->isVisible());
		QVERIFY(!dlg()->dlgItem(IDC_DEF_KEYWORDS_EDIT)->isVisible());

		dlg()->fgColourPicker()->setColour(RGB(0xAA, 0x00, 0x00));
		emit dlg()->fgColourPicker()->colourPicked();
		qobject_cast<QCheckBox*>(dlg()->dlgItem(IDC_GLOBAL_FG_CHECK))->setChecked(true);
		QVERIFY(NppParameters::getInstance().getGlobalOverrideStyle().enableFg);
		// every style now uses the forced foreground
		QCOMPARE(static_cast<COLORREF>(view()->execute(SCI_STYLEGETFORE, SCE_C_WORD)), static_cast<COLORREF>(RGB(0xAA, 0x00, 0x00)));

		dlg()->command(IDCANCEL);
		QVERIFY(!NppParameters::getInstance().getGlobalOverrideStyle().enableFg);
		QVERIFY(static_cast<COLORREF>(view()->execute(SCI_STYLEGETFORE, SCE_C_WORD)) != static_cast<COLORREF>(RGB(0xAA, 0x00, 0x00)));
	}

	void userDefinedLanguagesInMenu()
	{
		// preinstalled UDLs (markdown) copied to the user folder on first run, listed in the Language menu
		NppParameters& nppParam = NppParameters::getInstance();
		QVERIFY(nppParam.getNbUserLang() >= 1);
		const std::wstring udlName = nppParam.getULCFromIndex(0)->getName();
		QAction* udlAction = nullptr;
		for (QAction* a : _npp->languageMenu()->actions())
			if (a->data().toInt() == IDM_LANG_USER + 1)
				udlAction = a;
		QVERIFY(udlAction);
		QCOMPARE(udlAction->text().toStdWString(), udlName);

		_npp->command(IDM_FILE_NEW);
		udlAction->trigger();
		Buffer* buf = view()->getCurrentBuffer();
		QCOMPARE(buf->getLangType(), L_USER);
		QCOMPARE(std::wstring(buf->getUserDefineLangName()), udlName);
		QVERIFY(udlAction->isChecked());
		// the user lexer is set on the view
		char lexer[32] = {};
		view()->execute(SCI_GETLEXERLANGUAGE, 0, reinterpret_cast<LPARAM>(lexer));
		QCOMPARE(QString::fromLatin1(lexer), QStringLiteral("user"));
	}

	void userKeywords()
	{
		_npp->command(IDM_LANGSTYLE_CONFIG_DLG);
		QVERIFY(dlg()->goToSection(L"C++:INSTRUCTION WORD"));
		QVERIFY(dlg()->dlgItem(IDC_DEF_KEYWORDS_EDIT)->isVisible());
		dlg()->command(IDCANCEL);
	}

	QAction* langMenuAction(int cmdID)
	{
		for (QAction* a : _npp->languageMenu()->actions())
			if (a->data().toInt() == cmdID)
				return a;
		return nullptr;
	}

	void udlDialogCreatesLanguage()
	{
		NppParameters& nppParam = NppParameters::getInstance();
		const int nbBefore = nppParam.getNbUserLang();

		_npp->command(IDM_LANG_USER_DLG);
		UserDefineDialog* udd = _npp->getUserDefineDlg();
		QVERIFY(udd->isCreated());
		QVERIFY(udd->isVisible());
		QVERIFY(_npp->isMenuItemChecked(IDM_LANG_USER_DLG));
		QVERIFY(udd->windowTitle().startsWith(QStringLiteral("User Defined Language v.")));
		QCOMPARE(udd->tabBar()->count(), 4);
		QCOMPARE(udd->comboBox(IDC_LANGNAME_COMBO)->count(), nbBefore + 1);
		QVERIFY(udd->folderStyleDlg()->isVisible());
		udd->tabBar()->setCurrentIndex(1);
		QVERIFY(udd->keyWordsStyleDlg()->isVisible());
		QVERIFY(!udd->folderStyleDlg()->isVisible());
		QCOMPARE(udd->keyWordsStyleDlg()->parentWidget(), udd);
		QVERIFY(!udd->keyWordsStyleDlg()->isWindow()); // a page of the dialog, not a window
		for (int i = 0; i < 4; ++i)
		{
			udd->tabBar()->setCurrentIndex(i);
			screenshot(udd, QStringLiteral("udl-dialog-%1.png").arg(i));
		}
		udd->tabBar()->setCurrentIndex(1);

		// Create New...
		udd->setNextNameAnswer(L"NplxTestLang");
		udd->command(IDC_ADDNEW_BUTTON);
		QCOMPARE(nppParam.getNbUserLang(), nbBefore + 1);
		QCOMPARE(udd->comboBox(IDC_LANGNAME_COMBO)->currentText(), QStringLiteral("NplxTestLang"));
		QVERIFY(udd->dlgItem(IDC_RENAME_BUTTON)->isVisibleTo(udd));
		QAction* action = langMenuAction(IDM_LANG_USER + nbBefore + 1);
		QVERIFY(action);
		QCOMPARE(action->text(), QStringLiteral("NplxTestLang"));

		// a duplicated name is refused
		udd->setNextNameAnswer(L"NplxTestLang");
		udd->command(IDC_ADDNEW_BUTTON);
		QCOMPARE(nppParam.getNbUserLang(), nbBefore + 1);

		// keywords typed in the dialog go to the language (EN_CHANGE)
		auto* kw1 = qobject_cast<QPlainTextEdit*>(udd->keyWordsStyleDlg()->dlgItem(IDC_KEYWORD1_EDIT));
		QVERIFY(kw1);
		kw1->setPlainText(QStringLiteral("alpha beta"));

		// a document in the new language styles the keywords
		_npp->command(IDM_FILE_NEW);
		view()->setText("gamma alpha\n");
		action->trigger();
		QCOMPARE(std::wstring(view()->getCurrentBuffer()->getUserDefineLangName()), std::wstring(L"NplxTestLang"));
		view()->execute(SCI_COLOURISE, 0, -1);
		QCOMPARE(int(view()->execute(SCI_GETSTYLEAT, 7)), SCE_USER_STYLE_KEYWORD1);
		QVERIFY(int(view()->execute(SCI_GETSTYLEAT, 1)) != SCE_USER_STYLE_KEYWORD1);

		// the dialog toggles
		_npp->command(IDM_LANG_USER_DLG);
		QVERIFY(!udd->isVisible());
		QVERIFY(!_npp->isMenuItemChecked(IDM_LANG_USER_DLG));
		_npp->command(IDM_LANG_USER_DLG);
		QVERIFY(udd->isVisible());
	}

	void udlStylerOkAndCancel()
	{
		UserDefineDialog* udd = _npp->getUserDefineDlg();
		UserLangContainer* ulc = SharedParametersDialog::currentUserLang();
		QCOMPARE(QString::fromWCharArray(ulc->getName()), QStringLiteral("NplxTestLang"));
		QVERIFY(!view()->execute(SCI_STYLEGETBOLD, SCE_USER_STYLE_KEYWORD1));

		auto clickBoldThen = [](int closeId) {
			QTimer::singleShot(0, [closeId]() {
				auto* styler = qobject_cast<RcDialog*>(QApplication::activeModalWidget());
				QVERIFY(styler);
				styler->setChecked(IDC_STYLER_CHECK_BOLD, true);
				styler->command(IDC_STYLER_CHECK_BOLD);
				styler->command(closeId);
			});
		};

		clickBoldThen(IDCANCEL);
		udd->keyWordsStyleDlg()->command(IDC_KEYWORD1_STYLER);
		QVERIFY(!view()->execute(SCI_STYLEGETBOLD, SCE_USER_STYLE_KEYWORD1)); // restored

		clickBoldThen(IDOK);
		udd->keyWordsStyleDlg()->command(IDC_KEYWORD1_STYLER);
		// kept, and the open document is restyled
		QVERIFY(view()->execute(SCI_STYLEGETBOLD, SCE_USER_STYLE_KEYWORD1));
	}

	void udlRenameAndRemove()
	{
		NppParameters& nppParam = NppParameters::getInstance();
		UserDefineDialog* udd = _npp->getUserDefineDlg();
		const int nb = nppParam.getNbUserLang();
		const int cmdID = IDM_LANG_USER + nb;
		Buffer* buf = view()->getCurrentBuffer();

		udd->setNextNameAnswer(L"NplxRenamed");
		udd->command(IDC_RENAME_BUTTON);
		QCOMPARE(langMenuAction(cmdID)->text(), QStringLiteral("NplxRenamed"));
		QCOMPARE(udd->comboBox(IDC_LANGNAME_COMBO)->currentText(), QStringLiteral("NplxRenamed"));
		QCOMPARE(std::wstring(buf->getUserDefineLangName()), std::wstring(L"NplxRenamed"));

		_ui._answer = IDYES;
		udd->command(IDC_REMOVELANG_BUTTON);
		_ui._answer = IDNO;
		QCOMPARE(nppParam.getNbUserLang(), nb - 1);
		QVERIFY(!langMenuAction(cmdID));
		QCOMPARE(udd->comboBox(IDC_LANGNAME_COMBO)->count(), nb);
		QCOMPARE(buf->getLangType(), L_USER);
		QCOMPARE(std::wstring(buf->getUserDefineLangName()), std::wstring());

		udd->command(IDCANCEL);
		QVERIFY(!udd->isVisible());
		QVERIFY(!_npp->isMenuItemChecked(IDM_LANG_USER_DLG));
	}

	void udlDialogDocking()
	{
		UserDefineDialog* udd = _npp->getUserDefineDlg();
		_npp->command(IDM_LANG_USER_DLG);
		QVERIFY(udd->isVisible());
		QVERIFY(udd->isWindow());
		QCOMPARE(qobject_cast<QPushButton*>(udd->dlgItem(IDC_DOCK_BUTTON))->text(), QStringLiteral("Dock"));

		// Dock: the dialog becomes a panel on the right of the documents
		udd->command(IDC_DOCK_BUTTON);
		QVERIFY(udd->isDocked());
		QVERIFY(!udd->isWindow());
		QDockWidget* dock = _npp->udlDock();
		QVERIFY(dock && dock->isVisible());
		QVERIFY(dock->isAncestorOf(udd));
		QCOMPARE(_npp->dockWidgetArea(dock), Qt::RightDockWidgetArea);
		QCOMPARE(qobject_cast<QPushButton*>(udd->dlgItem(IDC_DOCK_BUTTON))->text(), QStringLiteral("Undock"));
		QVERIFY(!udd->dlgItem(IDC_UD_TRANSPARENT_CHECK)->isVisible()); // no transparency when docked

		// the menu command hides / shows the docked dialog
		_npp->command(IDM_LANG_USER_DLG);
		QVERIFY(!dock->isVisible());
		QVERIFY(!_npp->isMenuItemChecked(IDM_LANG_USER_DLG));
		_npp->command(IDM_LANG_USER_DLG);
		QVERIFY(dock->isVisible() && udd->isVisible());
		QVERIFY(_npp->isMenuItemChecked(IDM_LANG_USER_DLG));

		// Close (IDCANCEL) also hides the dock
		udd->command(IDCANCEL);
		QVERIFY(!dock->isVisible());
		_npp->command(IDM_LANG_USER_DLG);
		QVERIFY(dock->isVisible());

		// Undock: a window again
		udd->command(IDC_DOCK_BUTTON);
		QVERIFY(!udd->isDocked());
		QVERIFY(udd->isWindow());
		QVERIFY(udd->isVisible());
		QVERIFY(!dock->isVisible());
		QCOMPARE(qobject_cast<QPushButton*>(udd->dlgItem(IDC_DOCK_BUTTON))->text(), QStringLiteral("Dock"));
		udd->command(IDCANCEL);
	}
};

QTEST_MAIN(TestStyles)
#include "tst_styles.moc"
