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

// Phase 8: the interface language (nativeLang.xml), switched at runtime from Settings > Preferences > General, as
// upstream: the menus, the tab context menu and the dialogs already open (Notepad_plus::reloadLang).

#include <QtTest>
#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QTabBar>
#include <QTemporaryDir>

#include <cstdlib>

#include "FindReplaceDlg.h"
#include "GoToLineDlg.h"
#include "Notepad_plus.h"
#include "Parameters.h"
#include "QtUiServices.h"
#include "RunDlg.h"
#include "UserDefineDialog.h"
#include "WordStyleDlg.h"
#include "localization.h"
#include "menuCmdID.h"
#include "preferenceDlg.h"
#include "preference_rc.h"
#include "resource.h"

namespace
{
	class SilentUiServices : public QtUiServices
	{
	public:
		int messageBox(const std::wstring&, const std::wstring&, unsigned int) override
		{
			return IDOK;
		}
	};
}

class TestLocalization : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir _tmp;
	SilentUiServices _ui;
	NativeLangSpeaker _nativeLangSpeaker;
	std::unique_ptr<Notepad_plus> _npp;

	QString topMenuTitle(int pos) const
	{
		return _npp->menuBar()->actions().at(pos)->text();
	}

	QString commandText(int id) const
	{
		for (const QAction* a : _npp->menuBar()->findChildren<QAction*>())
		{
			if (a->data().toInt() == id)
				return a->text();
		}
		return QString();
	}

	bool hasSubMenu(const QString& title) const
	{
		for (const QMenu* menu : _npp->menuBar()->findChildren<QMenu*>())
		{
			if (menu->title() == title)
				return true;
		}
		return false;
	}

	QString tabMenuFirstItem()
	{
		QTimer::singleShot(50, []() {
			if (QWidget* popup = QApplication::activePopupWidget())
				popup->close();
		});
		_npp->showTabContextMenu(_npp->currentView(), _npp->mapToGlobal(QPoint(100, 60)));
		return _npp->getTabPopupMenu().getMenuHandle()->actions().first()->text();
	}

	// the user picks a language in Preferences > General
	void selectLanguage(const QString& name)
	{
		RcDialog* general = _npp->getPreferenceDlg()->pageByName(L"Global");
		QVERIFY(general);
		QComboBox* combo = general->comboBox(IDC_COMBO_LOCALIZATION);
		QVERIFY(combo);
		const int index = combo->findText(name);
		QVERIFY2(index >= 0, qPrintable(name));
		combo->setCurrentIndex(index);
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

	void englishWithoutNativeLang()
	{
		QCOMPARE(topMenuTitle(0), QStringLiteral("&File"));
		QCOMPARE(commandText(IDM_FILE_NEW), QStringLiteral("&New"));
		QCOMPARE(tabMenuFirstItem(), QStringLiteral("Close"));

		// the dialogs, opened before the switch: they follow it
		_npp->command(IDM_SETTING_PREFERENCE);
		QCOMPARE(_npp->getPreferenceDlg()->windowTitle(), QStringLiteral("Preferences"));
		_npp->command(IDM_SEARCH_FIND);
		QVERIFY(_npp->getFindReplaceDlg()->isVisible());
		QCOMPARE(_npp->getFindReplaceDlg()->windowTitle(), QStringLiteral("Find"));
		_npp->command(IDM_LANGSTYLE_CONFIG_DLG);
		QVERIFY(_npp->getConfigStyleDlg()->isCreated());
		_npp->command(IDM_LANG_USER_DLG);
		QVERIFY(_npp->getUserDefineDlg()->isCreated());
		_npp->command(IDM_SEARCH_GOTOLINE);
		QVERIFY(_npp->getGoToLineDlg());
		QCOMPARE(_npp->getGoToLineDlg()->windowTitle(), QStringLiteral("Go To..."));
	}

	void switchToFrenchAtRuntime()
	{
		selectLanguage(QStringLiteral("Français"));

		// upstream changeMenuLang: <Entries>, <SubEntries> and the commands
		QCOMPARE(topMenuTitle(0), QStringLiteral("&Fichier"));
		QCOMPARE(topMenuTitle(1), QStringLiteral("&Édition"));
		QVERIFY(hasSubMenu(QStringLiteral("Fermer plusieurs documents"))); // file-closeMore
		QVERIFY(hasSubMenu(QStringLiteral("MAJUSCULE/minuscule"))); // edit-convertCaseTo
		QCOMPARE(commandText(IDM_FILE_NEW), QStringLiteral("&Nouveau"));
		QCOMPARE(tabMenuFirstItem(), QStringLiteral("Fermer"));

		// changePreferenceDlgLang: the title, the page list, the page controls
		PreferenceDlg* pref = _npp->getPreferenceDlg();
		QCOMPARE(pref->windowTitle(), QStringLiteral("Préférences"));
		QCOMPARE(pref->listBox(IDC_LIST_DLGTITLE)->item(0)->text(), QStringLiteral("Général"));
		auto* langLabel = pref->pageByName(L"Global")->dlgItem(6123);
		QVERIFY(langLabel);
		QCOMPARE(langLabel->property(langLabel->inherits("QGroupBox") ? "title" : "text").toString(), QStringLiteral("Langue"));

		// changeFindReplaceDlgLang: the tab names, the window shows the current one
		FindReplaceDlg* find = _npp->getFindReplaceDlg();
		QCOMPARE(find->windowTitle(), QStringLiteral("Rechercher"));
		QCOMPARE(find->dlgItem(IDOK)->property("text").toString(), QStringLiteral("Suivant"));

		// changeConfigLang, changeUserDefineLang, changeDlgLang(GoToLine)
		QCOMPARE(_npp->getConfigStyleDlg()->windowTitle(), QStringLiteral("Configurateur de coloration syntaxique"));
		UserDefineDialog* udd = _npp->getUserDefineDlg();
		QCOMPARE(udd->windowTitle(), QStringLiteral("Langage utilisateur"));
		QCOMPARE(udd->tabBar()->tabText(0), QStringLiteral("Relief syntaxique && défaut"));
		QCOMPARE(_npp->getGoToLineDlg()->windowTitle(), QStringLiteral("Aller à..."));
	}

	void dialogCreatedAfterTheSwitch()
	{
		// upstream: a dialog opened for the first time is translated (Run, created on first use)
		_npp->command(IDM_EXECUTE);
		QVERIFY(_npp->getRunDlg());
		QCOMPARE(_npp->getRunDlg()->windowTitle(), QStringLiteral("Exécuter..."));
		_npp->getRunDlg()->hide();
	}

	void backToEnglish()
	{
		selectLanguage(QStringLiteral("English"));

		QCOMPARE(topMenuTitle(0), QStringLiteral("&File"));
		QVERIFY(hasSubMenu(QStringLiteral("Close &Multiple Documents")));
		QCOMPARE(commandText(IDM_FILE_NEW), QStringLiteral("&New"));
		QCOMPARE(tabMenuFirstItem(), QStringLiteral("Close"));
		QCOMPARE(_npp->getPreferenceDlg()->windowTitle(), QStringLiteral("Preferences"));
		QCOMPARE(_npp->getPreferenceDlg()->listBox(IDC_LIST_DLGTITLE)->item(0)->text(), QStringLiteral("General"));
		QCOMPARE(_npp->getFindReplaceDlg()->windowTitle(), QStringLiteral("Find"));
		QCOMPARE(_npp->getGoToLineDlg()->windowTitle(), QStringLiteral("Go to...")); // english.xml's spelling
		QCOMPARE(_npp->getUserDefineDlg()->tabBar()->tabText(0), QStringLiteral("Folder && Default"));
	}
};

QTEST_MAIN(TestLocalization)
#include "tst_localization.moc"
