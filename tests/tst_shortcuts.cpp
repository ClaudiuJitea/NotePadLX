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

// Phase 7: the Shortcut Mapper (Settings > Shortcut Mapper...), the Scintilla key dialog, the accelerator updates
// and the Scintilla keys from shortcuts.xml. NPLX_SCREENSHOT_DIR=<dir> saves screenshots.

#include <QtTest>
#include <QAction>
#include <QComboBox>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QTabBar>
#include <QTemporaryDir>

#include <cstdlib>
#include <functional>

#include "BabyGridWrapper.h"
#include "Notepad_plus.h"
#include "Parameters.h"
#include "QtUiServices.h"
#include "ScintillaEditView.h"
#include "ShortcutDlg.h"
#include "ShortcutMapper.h"
#include "ShortcutMapper_rc.h"
#include "localization.h"
#include "menuCmdID.h"
#include "resource.h"
#include "shortcutRc.h"

namespace
{
	class SilentUiServices : public QtUiServices
	{
	public:
		int messageBox(const std::wstring&, const std::wstring&, unsigned int) override { ++_nbMessages; return IDOK; }
		int saveOrNotMultiBox(const std::wstring&, const std::wstring&) override { return IDNO; }
		int _nbMessages = 0;
	};

	// runs 'answer' on the next modal dialog of type T, once the event loop shows it
	template <typename T>
	void whenModal(std::function<void(T*)> answer)
	{
		QTimer::singleShot(0, [answer]() {
			auto* dlg = qobject_cast<T*>(QApplication::activeModalWidget());
			if (!dlg)
			{
				QFAIL("the expected dialog is not shown");
				return;
			}
			answer(dlg);
		});
	}

	void setKey(RcDialog* d, bool ctrl, bool alt, bool shift, const char* key)
	{
		d->setChecked(IDC_CTRL_CHECK, ctrl);
		d->setChecked(IDC_ALT_CHECK, alt);
		d->setChecked(IDC_SHIFT_CHECK, shift);
		d->command(IDC_CTRL_CHECK);
		d->command(IDC_ALT_CHECK);
		d->command(IDC_SHIFT_CHECK);
		QComboBox* keys = d->comboBox(IDC_KEY_COMBO);
		keys->setCurrentIndex(keys->findText(QString::fromLatin1(key)));
	}
}

class TestShortcuts : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir _tmp;
	SilentUiServices _ui;
	NativeLangSpeaker _nativeLangSpeaker;
	std::unique_ptr<Notepad_plus> _npp;

	ScintillaEditView* view() { return _npp->getActiveEditView(); }

	void screenshot(QWidget* w, const QString& name)
	{
		const QByteArray dir = qgetenv("NPLX_SCREENSHOT_DIR");
		if (!dir.isEmpty())
		{
			QCoreApplication::processEvents();
			w->grab().save(QString::fromUtf8(dir) + "/" + name);
		}
	}

	QAction* action(int id)
	{
		for (QAction* a : _npp->menuBar()->findChildren<QAction*>())
			if (a->data().toInt() == id)
				return a;
		return nullptr;
	}

	QAction* menuItem(const QString& menuTitle, const QString& text)
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

	// the grid row (1-based) of the item named 'name'
	int rowOf(ShortcutMapper* mapper, const QString& name)
	{
		BabyGridWrapper* grid = mapper->grid();
		for (int r = 0; r < grid->rowCount(); ++r)
			if (grid->item(r, 0) && grid->item(r, 0)->text() == name)
				return r + 1;
		return 0;
	}

	// opens the Shortcut Mapper on 'cmd' and runs 'body' in it, then closes it
	void withMapper(int cmd, std::function<void(ShortcutMapper*)> body)
	{
		bool ran = false;
		whenModal<ShortcutMapper>([&](ShortcutMapper* mapper) {
			ran = true;
			// close the mapper even when an assertion in 'body' returns early
			struct Closer { ShortcutMapper* _m; ~Closer() { _m->command(IDOK); } } closer{ mapper };
			QCOMPARE(_npp->activeShortcutMapper(), mapper);
			body(mapper);
		});
		_npp->command(cmd);
		QVERIFY(ran);
		QVERIFY(!_npp->activeShortcutMapper());
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

	void scintillaKeysAreApplied()
	{
		// upstream ScintillaAccelerator::updateKeys at startup: the Scintilla keys of shortcuts.xml
		_npp->command(IDM_FILE_NEW);
		view()->setFocus();
		view()->setText("one\ntwo\nthree\n");
		view()->execute(SCI_GOTOPOS, 0);
		QTest::keyClick(view(), Qt::Key_Down);
		QCOMPARE(view()->execute(SCI_LINEFROMPOSITION, view()->execute(SCI_GETCURRENTPOS)), 1);
		QTest::keyClick(view(), Qt::Key_End, Qt::ControlModifier); // SCI_DOCUMENTEND
		QCOMPARE(view()->execute(SCI_GETCURRENTPOS), view()->execute(SCI_GETLENGTH));
		QTest::keyClick(view(), Qt::Key_Home, Qt::ControlModifier | Qt::ShiftModifier); // SCI_DOCUMENTSTARTEXTEND
		QCOMPARE(view()->execute(SCI_GETSELECTIONSTART), 0);

		// the menu item of a Scintilla command shows its key, without being a menu shortcut
		QAction* selectAll = action(IDM_EDIT_SELECTALL);
		QVERIFY(selectAll);
		QVERIFY(selectAll->text().endsWith(QStringLiteral("\tCtrl+A")));
		QVERIFY(selectAll->shortcut().isEmpty());
	}

	void mapperTabsAndCategories()
	{
		QVERIFY(Notepad_plus::isCommandPorted(IDM_SETTING_SHORTCUT_MAPPER));
		NppParameters& nppParam = NppParameters::getInstance();
		withMapper(IDM_SETTING_SHORTCUT_MAPPER, [&](ShortcutMapper* mapper) {
			auto* tabs = qobject_cast<QTabBar*>(mapper->dlgItem(IDC_BABYGRID_TABBAR));
			QVERIFY(tabs);
			QCOMPARE(tabs->count(), 5);
			QCOMPARE(tabs->tabText(0), QStringLiteral("Main menu"));
			QCOMPARE(tabs->tabText(4), QStringLiteral("Scintilla commands"));
			QCOMPARE(mapper->currentState(), STATE_MENU);

			// every menu command but the Windows-only ones removed from the menus
			int nbShown = 0;
			for (const CommandShortcut& csc : nppParam.getUserShortcuts())
				if (!Notepad_plus::isRemovedOnLinux(static_cast<int>(csc.getID())))
					++nbShown;
			QVERIFY(nbShown < static_cast<int>(nppParam.getUserShortcuts().size()));
			BabyGridWrapper* grid = mapper->grid();
			QCOMPARE(grid->rowCount(), nbShown);
			QCOMPARE(rowOf(mapper, QStringLiteral("Update Notepad++")), 0);
			QCOMPARE(grid->columnCount(), 3);
			QCOMPARE(grid->horizontalHeaderItem(2)->text(), QStringLiteral("Category"));
			const int newRow = rowOf(mapper, QStringLiteral("New"));
			QVERIFY(newRow > 0);
			QCOMPARE(grid->item(newRow - 1, 1)->text(), QStringLiteral("Ctrl+N"));
			QCOMPARE(grid->item(newRow - 1, 2)->text(), QStringLiteral("File")); // setCategoryFromMenu

			// the filter keeps the items whose name or keys contain every word
			mapper->setItemText(IDC_BABYGRID_FILTER, L"save ctrl");
			mapper->command(IDC_BABYGRID_FILTER, RcNotifyEditChange);
			QVERIFY(grid->rowCount() > 0 && grid->rowCount() < 20);
			for (int r = 0; r < grid->rowCount(); ++r)
			{
				const QString all = (grid->item(r, 0)->text() + " " + (grid->item(r, 1) ? grid->item(r, 1)->text() : QString())).toLower();
				QVERIFY2(all.contains("save") && all.contains("ctrl"), qPrintable(all));
			}
			mapper->command(IDC_BABYGRID_FILTER_CLEAR);
			QCOMPARE(grid->rowCount(), nbShown);

			mapper->switchToTab(4);
			QCOMPARE(mapper->currentState(), STATE_SCINTILLA);
			QCOMPARE(grid->rowCount(), static_cast<int>(nppParam.getScintillaKeyList().size()));
			QVERIFY(!mapper->dlgItem(IDM_BABYGRID_CLEAR)->isEnabled());
			QVERIFY(!mapper->dlgItem(IDM_BABYGRID_DELETE)->isEnabled());
			mapper->switchToTab(0);
			screenshot(mapper, "shortcut-mapper.png");
		});
	}

	void modifyAndClearMenuShortcut()
	{
		withMapper(IDM_SETTING_SHORTCUT_MAPPER, [&](ShortcutMapper* mapper) {
			const int row = rowOf(mapper, QStringLiteral("New"));
			mapper->grid()->setCurrentCell(row - 1, 0);

			// Ctrl+S is Save: the Shortcut dialog shows the conflict, the info box tells where
			whenModal<ShortcutDlg>([mapper](ShortcutDlg* d) {
				setKey(d, true, false, false, "S");
				QVERIFY(d->dlgItem(IDC_CONFLICT_STATIC)->isVisibleTo(d));
				QVERIFY(QString::fromStdWString(mapper->getItemText(IDC_BABYGRID_INFO)).contains(QStringLiteral("Save")));
				setKey(d, true, true, false, "F11");
				QVERIFY(!d->dlgItem(IDC_CONFLICT_STATIC)->isVisibleTo(d));
				d->command(IDOK);
			});
			mapper->command(IDM_BABYGRID_MODIFY);
			QCOMPARE(mapper->grid()->item(row - 1, 1)->text(), QStringLiteral("Ctrl+Alt+F11"));
		});
		// the accelerator follows (upstream Accelerator::updateShortcuts)
		QCOMPARE(action(IDM_FILE_NEW)->shortcut(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F11));

		withMapper(IDM_SETTING_SHORTCUT_MAPPER, [&](ShortcutMapper* mapper) {
			const int row = rowOf(mapper, QStringLiteral("New"));
			mapper->grid()->setCurrentCell(row - 1, 0);
			mapper->command(IDM_BABYGRID_CLEAR);
			QVERIFY(!mapper->grid()->item(row - 1, 1)); // no key
		});
		QVERIFY(action(IDM_FILE_NEW)->shortcut().isEmpty());
	}

	void conflictsAreMarked()
	{
		// give New the key of Save: both rows are marked, the info box lists the other one
		NppParameters& nppParam = NppParameters::getInstance();
		for (CommandShortcut& csc : nppParam.getUserShortcuts())
			if (csc.getID() == IDM_FILE_NEW)
				csc.setKeyCombo(KeyCombo{ true, false, false, 'S' });
		withMapper(IDM_SETTING_SHORTCUT_MAPPER, [&](ShortcutMapper* mapper) {
			const int row = rowOf(mapper, QStringLiteral("New"));
			QVERIFY(mapper->grid()->item(row - 1, 0)->background().color() != QColor());
			mapper->grid()->setCurrentCell(row - 1, 0);
			const QString info = QString::fromStdWString(mapper->getItemText(IDC_BABYGRID_INFO));
			QVERIFY2(info.contains(QStringLiteral("Main menu")) && info.contains(QStringLiteral("Save")), qPrintable(info));
			mapper->command(IDM_BABYGRID_CLEAR);
		});
	}

	void deleteMacrosRenumbersTheMenu()
	{
		NppParameters& nppParam = NppParameters::getInstance();
		// two more macros, so that one in the middle can be removed
		for (const char* name : { "Second", "Third" })
		{
			Macro m;
			m.push_back(recordedMacroStep(SCI_NEWLINE, 0, 0, nullptr, recordedMacroStep::mtUseLParameter));
			const int id = ID_MACRO + static_cast<int>(nppParam.getMacroList().size());
			MacroShortcut ms(Shortcut(name, false, false, false, 0), m, id);
			nppParam.getMacroList().push_back(ms);
			nppParam.getMacroMenuItems().push_back(MenuItemUnit(id, string2wstring(name, CP_UTF8)));
		}
		nppParam.getMacroMenuItems().clearMenu();
		nppParam.getMacroMenuItems().createMenu();
		_npp->updateShortcuts();
		const size_t nbMacros = nppParam.getMacroList().size();
		QVERIFY(nbMacros >= 3);

		// Macro > Modify Shortcut/Delete Macro... opens the Macros tab
		QAction* modifyDelete = menuItem("&Macro", "Modify Shortcut/Delete Macro...");
		QVERIFY(modifyDelete && modifyDelete->isEnabled());
		bool ran = false;
		whenModal<ShortcutMapper>([&](ShortcutMapper* mapper) {
			QCOMPARE(mapper->currentState(), STATE_MACRO);
			QCOMPARE(mapper->grid()->rowCount(), static_cast<int>(nbMacros));
			mapper->grid()->setCurrentCell(rowOf(mapper, QStringLiteral("Second")) - 1, 0);
			mapper->command(IDM_BABYGRID_DELETE);
			ran = true;
			mapper->command(IDOK);
		});
		modifyDelete->trigger();
		QVERIFY(ran);

		QCOMPARE(nppParam.getMacroList().size(), nbMacros - 1);
		QVERIFY(!menuItem("&Macro", "Second"));
		QAction* third = menuItem("&Macro", "Third");
		QVERIFY(third);
		// the IDs after the deleted one move down: the menu item still runs its own macro
		QCOMPARE(third->data().toInt(), static_cast<int>(nppParam.getMacroList().back().getID()));
		QCOMPARE(nppParam.getMacroList().back().getID(), static_cast<unsigned long>(ID_MACRO + nbMacros - 2));
	}

	void renameRunCommand()
	{
		NppParameters& nppParam = NppParameters::getInstance();
		if (nppParam.getUserCommandList().empty())
		{
			_npp->addUserCommand(UserCommand(Shortcut("Echo", false, false, false, 0), "true", ID_USER_CMD));
		}
		const QString oldName = QString::fromUtf8(nppParam.getUserCommandList().front().getName());
		QVERIFY(menuItem("&Run", oldName));

		withMapper(IDM_SETTING_SHORTCUT_MAPPER_RUN, [&](ShortcutMapper* mapper) {
			QCOMPARE(mapper->currentState(), STATE_USER);
			mapper->grid()->setCurrentCell(0, 0);
			whenModal<ShortcutDlg>([](ShortcutDlg* d) {
				d->setItemText(IDC_NAME_EDIT, L"Renamed command");
				setKey(d, true, true, true, "F9");
				d->command(IDOK);
			});
			mapper->command(IDM_BABYGRID_MODIFY);
		});
		QAction* renamed = menuItem("&Run", QStringLiteral("Renamed command"));
		QVERIFY(renamed);
		QCOMPARE(renamed->shortcut(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F9));
	}

	void scintillaKeyDialog()
	{
		// a second key for SCI_LINEDOWN (Alt+Shift+F12), then the editor uses it
		NppParameters& nppParam = NppParameters::getInstance();
		withMapper(IDM_SETTING_SHORTCUT_MAPPER, [&](ShortcutMapper* mapper) {
			mapper->switchToTab(4);
			const int row = rowOf(mapper, QStringLiteral("SCI_LINEDOWN"));
			QVERIFY(row > 0);
			mapper->grid()->setCurrentCell(row - 1, 0);
			whenModal<ScintillaKeyMapDlg>([this](ScintillaKeyMapDlg* d) {
				QCOMPARE(d->listBox(IDC_LIST_KEYS)->count(), 1);
				setKey(d, false, true, true, "F12");
				QVERIFY(d->dlgItem(IDC_BUTTON_ADD)->isEnabled());
				d->command(IDC_BUTTON_ADD);
				QCOMPARE(d->listBox(IDC_LIST_KEYS)->count(), 2);
				QVERIFY(d->dlgItem(IDC_BUTTON_RMVE)->isEnabled());
				screenshot(d, "scintilla-key-dialog.png");
				d->command(IDOK);
			});
			mapper->command(IDM_BABYGRID_MODIFY);
			QVERIFY(mapper->grid()->item(row - 1, 1)->text().contains(QStringLiteral("Alt+Shift+F12")));
		});
		bool found = false;
		for (const ScintillaKeyMap& skm : nppParam.getScintillaKeyList())
			if (skm.getScintillaKeyID() == SCI_LINEDOWN)
				found = skm.getSize() == 2;
		QVERIFY(found);

		// the editor follows (upstream ScintillaAccelerator::updateKeys): the new key moves down a line
		view()->setFocus();
		view()->setText("a\nb\nc\n");
		view()->execute(SCI_GOTOPOS, 0);
		QTest::keyClick(view(), Qt::Key_F12, Qt::AltModifier | Qt::ShiftModifier);
		QCOMPARE(view()->execute(SCI_LINEFROMPOSITION, view()->execute(SCI_GETCURRENTPOS)), 1);
		QTest::keyClick(view(), Qt::Key_Down); // the first key still works
		QCOMPARE(view()->execute(SCI_LINEFROMPOSITION, view()->execute(SCI_GETCURRENTPOS)), 2);
	}

	void keyTranslateMatchesScintillaQt()
	{
		// NPLX: the key definitions of SCI_ASSIGNCMDKEY are the key codes ScintillaEditView passes to Scintilla
		QCOMPARE(keyTranslate(VK_F12), static_cast<size_t>(VK_F12)); // ScintillaEditView passes F-keys as VK_F*
		QCOMPARE(keyTranslate('A'), static_cast<size_t>(Qt::Key_A));
		QCOMPARE(keyTranslate('7'), static_cast<size_t>(Qt::Key_7));
		QCOMPARE(keyTranslate(VK_OEM_1), static_cast<size_t>(Qt::Key_Semicolon));
		QCOMPARE(keyTranslate(VK_OEM_PERIOD), static_cast<size_t>(Qt::Key_Period));
		QCOMPARE(keyTranslate(VK_DOWN), static_cast<size_t>(SCK_DOWN));
	}

	void shortcutsAreSaved()
	{
		NppParameters& nppParam = NppParameters::getInstance();
		_npp->saveShortcuts();
		QFile f(QString::fromStdWString(nppParam.getShortcutsPath()));
		QVERIFY(f.open(QIODevice::ReadOnly));
		const QByteArray xml = f.readAll();
		QVERIFY(xml.contains("Renamed command"));
		QVERIFY(xml.contains("<ScintillaKeys>") || xml.contains("<ScintillaKeys "));
		QVERIFY(xml.contains(QByteArray::number(SCI_LINEDOWN)));
	}
};

QTEST_MAIN(TestShortcuts)
#include "tst_shortcuts.moc"
