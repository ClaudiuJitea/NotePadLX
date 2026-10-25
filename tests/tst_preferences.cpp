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

// Phase 7 tests: Settings > Preferences (the port of upstream preferenceDlg): the pages, and the settings reaching
// the main window through the upstream NPPM_INTERNAL_* messages.

#include <QtTest>
#include <QAbstractButton>
#include <QComboBox>
#include <QDir>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QSlider>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QToolBar>

#include <cstdlib>

#include "DocTabView.h"
#include "Notepad_plus.h"
#include "Parameters.h"
#include "QtUiServices.h"
#include "ScintillaEditView.h"
#include "localization.h"
#include "menuCmdID.h"
#include "preferenceDlg.h"
#include "preference_rc.h"

namespace
{
	class SilentUiServices : public QtUiServices
	{
	public:
		int messageBox(const std::wstring&, const std::wstring&, unsigned int) override
		{
			++_count;
			return IDOK;
		}
		int _count = 0;
	};
}

class TestPreferences : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir _tmp;
	SilentUiServices _ui;
	NativeLangSpeaker _nativeLangSpeaker;
	std::unique_ptr<Notepad_plus> _npp;

	PreferenceDlg* dlg() { return _npp->getPreferenceDlg(); }
	ScintillaEditView* view() { return _npp->getActiveEditView(); }

	RcDialog* page(const wchar_t* name)
	{
		RcDialog* p = dlg()->pageByName(name);
		[&]() { QVERIFY2(p, qPrintable(QString::fromWCharArray(name))); }();
		return p;
	}

	// a click, as the user does it (the control notifies its page)
	void click(RcDialog* p, int id)
	{
		auto* button = qobject_cast<QAbstractButton*>(p->dlgItem(id));
		QVERIFY(button);
		QVERIFY(button->isEnabled());
		button->click();
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

		QVERIFY(Notepad_plus::isCommandPorted(IDM_SETTING_PREFERENCE));
		QVERIFY(!dlg()); // created on first use, like upstream
		_npp->command(IDM_SETTING_PREFERENCE);
		QVERIFY(dlg());
		QVERIFY(dlg()->isVisible());
	}

	void cleanupTestCase()
	{
		_npp.reset();
		nplx::setUiServices(nullptr);
	}

	void pagesListedLikeUpstream()
	{
		// upstream order, without File Association (Windows registry)
		const QStringList expected = { "General", "Toolbar", "Tab Bar", "Editing 1", "Editing 2", "Dark Mode",
			"Margins/Border/Edge", "New Document", "Default Directory", "Recent Files History", "Language", "Indentation",
			"Highlighting", "Print", "Searching", "Backup", "Auto-Completion", "Multi-Instance & Date", "Delimiter",
			"Performance", "Cloud & Link", "Search Engine", "MISC." };
		QListWidget* list = dlg()->listBox(IDC_LIST_DLGTITLE);
		QVERIFY(list);
		QCOMPARE(list->count(), expected.size());
		for (int i = 0; i < expected.size(); ++i)
			QCOMPARE(list->item(i)->text(), expected[i]);

		// one page at a time, inside the dialog
		const QString shots = qEnvironmentVariable("NPLX_PREFERENCES_SCREENSHOTS_DIR");
		for (int i = 0; i < list->count(); ++i)
		{
			list->setCurrentRow(i);
			for (size_t j = 0; j < dlg()->nbPages(); ++j)
				QCOMPARE(dlg()->page(j)->isVisible(), static_cast<int>(j) == i);
			QVERIFY(dlg()->rect().contains(dlg()->page(i)->geometry()));
			if (!shots.isEmpty())
				dlg()->grab().save(QDir(shots).filePath(QStringLiteral("pref-%1.png").arg(i, 2, 10, QLatin1Char('0'))));
		}
		list->setCurrentRow(0);
	}

	void searchEngineCommandOpensItsPage()
	{
		_npp->command(IDM_EDIT_CHANGESEARCHENGINE);
		QVERIFY(dlg()->pageByName(L"SearchEngine")->isVisible());
		QCOMPARE(dlg()->listBox(IDC_LIST_DLGTITLE)->currentItem()->text(), QStringLiteral("Search Engine"));
	}

	void generalHidesStatusBarAndMenu()
	{
		RcDialog* p = page(L"Global");
		click(p, IDC_CHECK_HIDESTATUSBAR);
		QVERIFY(!_npp->statusBar()->isVisible());
		QVERIFY(!NppParameters::getInstance().getNppGUI()._statusBarShow);
		click(p, IDC_CHECK_HIDESTATUSBAR);
		QVERIFY(_npp->statusBar()->isVisible());

		click(p, IDC_CHECK_HIDEMENUBAR);
		QVERIFY(!_npp->menuBar()->isVisible());
		click(p, IDC_CHECK_HIDEMENUBAR);
		QVERIFY(_npp->menuBar()->isVisible());
	}

	void toolbarIconSize()
	{
		RcDialog* p = page(L"Toolbar");
		auto* toolBar = _npp->findChild<QToolBar*>(QStringLiteral("mainToolBar"));
		QVERIFY(toolBar);
		click(p, IDC_RADIO_BIGICON);
		QCOMPARE(toolBar->iconSize(), QSize(24, 24));
		QCOMPARE(NppParameters::getInstance().getNppGUI()._tbIconInfo._tbIconSet, TB_LARGE);
		click(p, IDC_RADIO_SMALLICON);
		QCOMPARE(toolBar->iconSize(), QSize(18, 18));

		// the standard set is not colorized
		click(p, IDC_RADIO_STANDARD);
		QVERIFY(!p->dlgItem(IDC_RADIO_RED)->isEnabled());
		click(p, IDC_RADIO_SMALLICON);
		QVERIFY(p->dlgItem(IDC_RADIO_RED)->isEnabled());

		click(p, IDC_CHECK_HIDE);
		QVERIFY(!toolBar->isVisible());
		click(p, IDC_CHECK_HIDE);
		QVERIFY(toolBar->isVisible());
	}

	void tabBarSettings()
	{
		RcDialog* p = page(L"Tabbar");
		DocTabView* tabs = _npp->getDocTab(MAIN_VIEW);
		NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();

		if (p->isCheckedOrNot(IDC_CHECK_TAB_HIDE))
			click(p, IDC_CHECK_TAB_HIDE);
		QVERIFY(tabs->isVisible());
		click(p, IDC_CHECK_TAB_HIDE);
		QVERIFY(!tabs->isVisible());
		QVERIFY(!p->dlgItem(IDC_CHECK_TAB_VERTICAL)->isEnabled());
		click(p, IDC_CHECK_TAB_HIDE);
		QVERIFY(tabs->isVisible());

		click(p, IDC_CHECK_TAB_VERTICAL);
		QCOMPARE(tabs->shape(), QTabBar::RoundedWest);
		click(p, IDC_CHECK_TAB_VERTICAL);
		QCOMPARE(tabs->shape(), QTabBar::RoundedNorth);

		const bool locked = p->isCheckedOrNot(IDC_CHECK_LOCK);
		QCOMPARE(tabs->isMovable(), !locked);
		click(p, IDC_CHECK_LOCK);
		QCOMPARE(tabs->isMovable(), locked);
		click(p, IDC_CHECK_LOCK);

		if (!p->isCheckedOrNot(IDC_CHECK_ENABLETABCLOSE))
			click(p, IDC_CHECK_ENABLETABCLOSE);
		QVERIFY(tabs->tabsClosable());
		click(p, IDC_CHECK_ENABLETABCLOSE);
		QVERIFY(!tabs->tabsClosable());
		QVERIFY(!(nppGUI._tabStatus & TAB_CLOSEBUTTON));
		click(p, IDC_CHECK_ENABLETABCLOSE);

		// the label length: Enter applies it
		auto* lenEdit = qobject_cast<QLineEdit*>(p->dlgItem(IDC_EDIT_TABCOMPACTLABELLEN));
		QVERIFY(lenEdit);
		lenEdit->setText(QStringLiteral("5000"));
		emit lenEdit->editingFinished();
		QCOMPARE(lenEdit->text(), QString::number(NB_MAX_TAB_COMPACT_LABEL_LEN));
		QCOMPARE(static_cast<int>(NppParameters::getInstance().getNbTabCompactLabelLen()), NB_MAX_TAB_COMPACT_LABEL_LEN);
		lenEdit->setText(QStringLiteral("0"));
		emit lenEdit->editingFinished();
	}

	void editingSettingsReachTheViews()
	{
		RcDialog* p = page(L"Scintillas");
		p->comboBox(IDC_WIDTH_COMBO)->setCurrentIndex(4); // Block
		QCOMPARE(view()->execute(SCI_GETCARETSTYLE) & CARETSTYLE_BLOCK, CARETSTYLE_BLOCK);
		p->comboBox(IDC_WIDTH_COMBO)->setCurrentIndex(1);
		QCOMPARE(view()->execute(SCI_GETCARETSTYLE), CARETSTYLE_LINE);
		QCOMPARE(view()->execute(SCI_GETCARETWIDTH), 1);

		const bool beyond = NppParameters::getInstance().getSVP()._scrollBeyondLastLine;
		click(p, IDC_CHECK_SCROLLBEYONDLASTLINE);
		QCOMPARE(view()->execute(SCI_GETENDATLASTLINE) != 0, beyond); // the opposite of the new value
		click(p, IDC_CHECK_SCROLLBEYONDLASTLINE);

		click(p, IDC_RADIO_CLM_FRAME);
		QVERIFY(p->dlgItem(IDC_CARETLINEFRAME_WIDTH_SLIDER)->isEnabled());
		p->slider(IDC_CARETLINEFRAME_WIDTH_SLIDER)->setValue(3);
		QCOMPARE(view()->execute(SCI_GETCARETLINEFRAME), 3);
		click(p, IDC_RADIO_CLM_HILITE);
		QCOMPARE(view()->execute(SCI_GETCARETLINEFRAME), 0);
		QVERIFY(!p->dlgItem(IDC_CARETLINEFRAME_WIDTH_SLIDER)->isEnabled());

		RcDialog* p2 = page(L"Scintillas2");
		click(p2, IDC_CHECK_MULTISELECTION);
		QCOMPARE(view()->execute(SCI_GETMULTIPLESELECTION) != 0, NppParameters::getInstance().getSVP()._multiSelection);
		QVERIFY(!p2->dlgItem(IDC_CHECK_COLUMN2MULTIEDITING)->isEnabled() || NppParameters::getInstance().getSVP()._multiSelection);
		click(p2, IDC_CHECK_MULTISELECTION);
		QVERIFY(NppParameters::getInstance().getSVP()._multiSelection);
	}

	void marginsAndEdge()
	{
		RcDialog* p = page(L"MarginsBorderEdge");
		const int lnMargin = ScintillaEditView::_SC_MARGE_LINENUMBER;
		if (!p->isCheckedOrNot(IDC_CHECK_LINENUMBERMARGE))
			click(p, IDC_CHECK_LINENUMBERMARGE);
		QVERIFY(view()->hasMarginShown(lnMargin));
		// changing the width mode keeps the line numbers (upstream toggled the margin)
		click(p, IDC_RADIO_CONSTANT);
		QVERIFY(view()->hasMarginShown(lnMargin));
		click(p, IDC_RADIO_DYNAMIC);
		QVERIFY(view()->hasMarginShown(lnMargin));
		click(p, IDC_CHECK_LINENUMBERMARGE);
		QVERIFY(!view()->hasMarginShown(lnMargin));
		QVERIFY(!p->dlgItem(IDC_RADIO_DYNAMIC)->isEnabled());
		click(p, IDC_CHECK_LINENUMBERMARGE);
		QVERIFY(view()->hasMarginShown(lnMargin));

		// vertical edges: two columns, then one in background mode
		auto* edges = qobject_cast<QPlainTextEdit*>(p->dlgItem(IDC_COLUMNPOS_EDIT));
		QVERIFY(edges);
		edges->setPlainText(QStringLiteral("80 120"));
		QCOMPARE(view()->execute(SCI_GETEDGEMODE), EDGE_MULTILINE);
		QVERIFY(!p->dlgItem(IDC_CHECK_EDGEBGMODE)->isEnabled());
		edges->setPlainText(QStringLiteral("80"));
		QVERIFY(p->dlgItem(IDC_CHECK_EDGEBGMODE)->isEnabled());
		click(p, IDC_CHECK_EDGEBGMODE);
		QCOMPARE(view()->execute(SCI_GETEDGEMODE), EDGE_BACKGROUND);
		QCOMPARE(view()->execute(SCI_GETEDGECOLUMN), 80);
		edges->setPlainText(QString());
		QCOMPARE(view()->execute(SCI_GETEDGEMODE), EDGE_NONE);

		p->slider(IDC_PADDINGLEFT_SLIDER)->setValue(7);
		QCOMPARE(view()->execute(SCI_GETMARGINLEFT), 7);
		p->slider(IDC_PADDINGLEFT_SLIDER)->setValue(0);

		click(p, IDC_RADIO_ARROW);
		QCOMPARE(NppParameters::getInstance().getSVP()._folderStyle, FOLDER_STYLE_ARROW);
		click(p, IDC_RADIO_BOX);
	}

	void languageMenuItems()
	{
		RcDialog* p = page(L"Language");
		QListWidget* enabled = p->listBox(IDC_LIST_ENABLEDLANG);
		QListWidget* disabled = p->listBox(IDC_LIST_DISABLEDLANG);
		const QList<QListWidgetItem*> found = enabled->findItems(QStringLiteral("Ada"), Qt::MatchExactly);
		QCOMPARE(found.size(), 1);
		enabled->setCurrentItem(found.first());
		QVERIFY(p->dlgItem(IDC_BUTTON_REMOVE)->isEnabled());
		click(p, IDC_BUTTON_REMOVE);
		QCOMPARE(disabled->findItems(QStringLiteral("Ada"), Qt::MatchExactly).size(), 1);

		auto langAction = [this]() -> QAction* {
			for (QAction* a : _npp->findChildren<QAction*>())
				if (a->data().toInt() == IDM_LANG_ADA)
					return a;
			return nullptr;
		};
		QVERIFY(langAction());
		QVERIFY(!langAction()->isVisible());

		disabled->setCurrentItem(disabled->findItems(QStringLiteral("Ada"), Qt::MatchExactly).first());
		click(p, IDC_BUTTON_RESTORE);
		QVERIFY(langAction()->isVisible());
		QVERIFY(NppParameters::getInstance().getNppGUI()._excludedLangList.empty());
	}

	void indentationPerLanguage()
	{
		RcDialog* p = page(L"Indentation");
		QListWidget* list = p->listBox(IDC_LIST_TABSETTNG);
		QCOMPARE(list->item(0)->text(), QStringLiteral("[Default]"));
		QVERIFY(p->dlgItem(IDC_CHECK_DEFAULTTABVALUE)->isHidden());

		const QList<QListWidgetItem*> python = list->findItems(QStringLiteral("python"), Qt::MatchFixedString);
		QCOMPARE(python.size(), 1);
		list->setCurrentItem(python.first());
		QVERIFY(!p->dlgItem(IDC_CHECK_DEFAULTTABVALUE)->isHidden());

		// the default tab size of the default entry
		list->setCurrentRow(0);
		auto* size = qobject_cast<QLineEdit*>(p->dlgItem(IDC_EDIT_TABSIZEVAL));
		size->setText(QStringLiteral("3"));
		QCOMPARE(NppParameters::getInstance().getNppGUI()._tabSize, 3);
		QCOMPARE(view()->execute(SCI_GETTABWIDTH), 3);
		size->setText(QStringLiteral("4"));
	}

	void recentFilesInSubMenu()
	{
		RcDialog* p = page(L"RecentFilesHistory");
		const bool before = NppParameters::getInstance().putRecentFileInSubMenu();
		click(p, IDC_CHECK_INSUBMENU);
		QCOMPARE(NppParameters::getInstance().putRecentFileInSubMenu(), !before);
		click(p, IDC_CHECK_INSUBMENU);
		QCOMPARE(NppParameters::getInstance().putRecentFileInSubMenu(), before);
	}

	void multiInstanceDateFormat()
	{
		RcDialog* p = page(L"MultiInstance");
		auto* edit = qobject_cast<QLineEdit*>(p->dlgItem(IDC_DATETIMEFORMAT_EDIT));
		edit->setText(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
		// upstream's example is the "Back to the Future" time
		QCOMPARE(QString::fromStdWString(p->getItemText(IDD_DATETIMEFORMAT_RESULT_STATIC)), QStringLiteral("1985-10-26 16:24:42"));
	}

	void autoCompletionEnable()
	{
		RcDialog* p = page(L"AutoCompletion");
		NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();
		if (!p->isCheckedOrNot(IDD_AUTOC_ENABLECHECK))
			click(p, IDD_AUTOC_ENABLECHECK);
		click(p, IDD_AUTOC_ENABLECHECK);
		QCOMPARE(nppGUI._autocStatus, nppGUI.autoc_none);
		QVERIFY(!p->isCheckedOrNot(IDD_AUTOC_BOTHRADIO));
		QVERIFY(!p->dlgItem(IDD_AUTOC_WORDRADIO)->isEnabled());
		click(p, IDD_AUTOC_ENABLECHECK);
		QCOMPARE(nppGUI._autocStatus, nppGUI.autoc_both);
		QVERIFY(p->isCheckedOrNot(IDD_AUTOC_BOTHRADIO));

		click(p, IDD_AUTOCPARENTHESES_CHECK);
		QCOMPARE(p->getItemText(IDD_AUTOCPARENTHESES_CHECK), nppGUI._matchedPairConf._doParentheses ? std::wstring(L" (  )") : std::wstring(L" ("));
		click(p, IDD_AUTOCPARENTHESES_CHECK);
	}

	void darkModeTone()
	{
		RcDialog* p = page(L"DarkMode");
		NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();
		click(p, IDC_RADIO_DARKMODE_DARKMODE);
		QVERIFY(nppGUI._darkmode._isEnabled);
		QVERIFY(p->dlgItem(IDC_RADIO_DARKMODE_BLUE)->isEnabled());
		click(p, IDC_RADIO_DARKMODE_BLUE);
		QCOMPARE(nppGUI._darkmode._colorTone, NppDarkMode::blueTone);
		// the Qt palette takes the tone's background
		const NppDarkMode::Colors blue = NppDarkMode::getDarkModeDefaultColors(NppDarkMode::blueTone);
		QCOMPARE(qApp->palette().color(QPalette::Base), QColor(GetRValue(blue.background), GetGValue(blue.background), GetBValue(blue.background)));
		click(p, IDC_RADIO_DARKMODE_BLACK);
		click(p, IDC_RADIO_DARKMODE_LIGHTMODE);
		QVERIFY(!nppGUI._darkmode._isEnabled);
		QVERIFY(!p->dlgItem(IDC_RADIO_DARKMODE_BLUE)->isEnabled());
	}

	void closeHides()
	{
		dlg()->command(IDC_BUTTON_CLOSE);
		QVERIFY(!dlg()->isVisible());
		_npp->command(IDM_SETTING_PREFERENCE);
		QVERIFY(dlg()->isVisible());
		dlg()->command(IDC_BUTTON_CLOSE);
	}
};

QTEST_MAIN(TestPreferences)
#include "tst_preferences.moc"
