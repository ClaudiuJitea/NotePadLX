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

// The README screenshots (docs/screenshots), rendered from the real application with the default look: dark mode
// and the Solarized theme. Regenerate them with
//   NPLX_README_SCREENSHOTS_DIR=$PWD/docs/screenshots QT_QPA_PLATFORM=offscreen build/tests/tst_screenshots
// Without the variable, the test only checks that every scene renders.

#include <QtTest>
#include <QAbstractButton>
#include <QComboBox>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QTemporaryDir>

#include <cstdlib>

#include "Buffer.h"
#include "NplxStyle.h"
#include "Notepad_plus.h"
#include "Parameters.h"
#include "QtUiServices.h"
#include "ScintillaEditView.h"
#include "FindReplaceDlg.h"
#include "FindReplaceDlg_rc.h"
#include "columnEditor.h"
#include "columnEditor_rc.h"
#include "localization.h"
#include "md5Dlgs.h"
#include "md5Dlgs_rc.h"
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
}

class TestScreenshots : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir _tmp;
	SilentUiServices _ui;
	NativeLangSpeaker _nativeLangSpeaker;
	std::unique_ptr<Notepad_plus> _npp;
	QString _outDir;

	void save(const QPixmap& pixmap, const QString& name)
	{
		QVERIFY(!pixmap.isNull());
		if (!_outDir.isEmpty())
			QVERIFY(pixmap.save(_outDir + "/" + name));
	}

	QPixmap grabWindow()
	{
		QCoreApplication::processEvents();
		QTest::qWait(50);
		return _npp->grab();
	}

	// the window with a popup (menu, dialog) painted where it is shown
	QPixmap grabWithPopup(QWidget* popup)
	{
		QPixmap shot = grabWindow();
		QPainter p(&shot);
		p.drawPixmap(_npp->mapFromGlobal(popup->mapToGlobal(QPoint(0, 0))), popup->grab());
		return shot;
	}

	void openSource(const char* relativePath)
	{
		const QString path = QStringLiteral(NPLX_TEST_SOURCE_DIR "/") + QString::fromLatin1(relativePath);
		const BufferID id = _npp->doOpen(path.toStdWString());
		QVERIFY2(id != BUFFER_INVALID, qPrintable(path));
		_npp->switchToFile(id);
	}

	QMenu* topMenu(const QString& title)
	{
		for (QAction* a : _npp->menuBar()->actions())
			if (a->menu() && a->text().remove(QLatin1Char('&')) == title)
				return a->menu();
		return nullptr;
	}

private slots:
	void initTestCase()
	{
		QVERIFY(_tmp.isValid());
		_outDir = qEnvironmentVariable("NPLX_README_SCREENSHOTS_DIR");
		if (!_outDir.isEmpty())
			QDir().mkpath(_outDir);
		::setenv("XDG_CONFIG_HOME", (_tmp.path() + "/config").toUtf8().constData(), 1);
		::setenv("XDG_DATA_HOME", (_tmp.path() + "/share").toUtf8().constData(), 1);
		::setenv("XDG_CACHE_HOME", (_tmp.path() + "/cache").toUtf8().constData(), 1);
		::setenv("NPLX_DATA_DIR", NPLX_TEST_SOURCE_DATA_DIR, 1);
		::unsetenv("NPLX_THEME");

		nplx::setUiServices(&_ui);
		NppParameters& nppParam = NppParameters::getInstance();
		nppParam.load(); // data/config.model.xml: dark mode, Solarized
		_nativeLangSpeaker.init(nppParam.getNativeLang());
		nppParam.setNativeLangSpeaker(&_nativeLangSpeaker);

		nplx::style::initDarkMode();
		nplx::style::apply(*qApp);
		QVERIFY(nplx::style::isDark());

		_npp = std::make_unique<Notepad_plus>();
		_npp->init();
		_npp->resize(1440, 860);
		_npp->show();
		QVERIFY(QTest::qWaitForWindowExposed(_npp.get()));
	}

	void cleanupTestCase()
	{
		_npp.reset();
		nplx::setUiServices(nullptr);
	}

	void mainWindow()
	{
		openSource("src/core/hash/sha512.cpp");
		openSource("tools/gen_rcdialogs.py");
		openSource("data/config.model.xml");
		openSource("src/ui/columnEditor.cpp");
		_npp->getActiveEditView()->execute(SCI_GOTOLINE, 120);
		_npp->getActiveEditView()->execute(SCI_SETFIRSTVISIBLELINE, 100);
		_npp->command(IDM_VIEW_FUNC_LIST);
		_npp->command(IDM_VIEW_DOC_MAP);
		_npp->command(IDM_VIEW_FUNC_LIST); // back to the Function List tab
		_npp->command(IDM_VIEW_FUNC_LIST);
		QTest::qWait(300);
		save(grabWindow(), "main-dark.png");
	}

	void fileMenu()
	{
		QMenu* menu = topMenu(QStringLiteral("View"));
		QVERIFY(menu);
		const QRect barItem = _npp->menuBar()->actionGeometry(menu->menuAction());
		menu->popup(_npp->menuBar()->mapToGlobal(barItem.bottomLeft()));
		QVERIFY(QTest::qWaitForWindowExposed(menu));
		save(grabWithPopup(menu), "menu-dark.png");
		menu->hide();
	}

	void findInFiles()
	{
		FindReplaceDlg* dlg = _npp->getFindReplaceDlg();
		_npp->command(IDM_SEARCH_FINDINFILES);
		dlg->comboBox(IDFINDWHAT)->setEditText(QStringLiteral("doDialog"));
		dlg->comboBox(IDD_FINDINFILES_FILTERS_COMBO)->setEditText(QStringLiteral("*.cpp *.h"));
		// a copy of src/ui under /tmp/NotePadLX, so that no personal path shows in the results
		const QString root = QDir::tempPath() + QStringLiteral("/NotePadLX");
		QDir(root).removeRecursively();
		QDir().mkpath(root + "/src/ui");
		for (const QString& name : QDir(QStringLiteral(NPLX_TEST_SOURCE_DIR "/src/ui")).entryList({ "*.cpp", "*.h" }, QDir::Files))
			QFile::copy(QStringLiteral(NPLX_TEST_SOURCE_DIR "/src/ui/") + name, root + "/src/ui/" + name);
		dlg->comboBox(IDD_FINDINFILES_DIR_COMBO)->setEditText(root + "/src/ui");
		qobject_cast<QAbstractButton*>(dlg->dlgItem(IDD_FINDINFILES_RECURSIVE_CHECK))->setChecked(true);
		QTest::qWait(100);
		save(dlg->grab(), "find-in-files-dark.png");
		dlg->command(IDD_FINDINFILES_FIND_BUTTON);
		dlg->hide();
		QTest::qWait(300);
		save(grabWindow(), "finder-dark.png");
		QDir(root).removeRecursively();
	}

	void tools()
	{
		_npp->command(IDM_TOOL_SHA256_GENERATE);
		auto* hash = qobject_cast<HashFromTextDlg*>(_npp->getHashDlg(IDM_TOOL_SHA256_GENERATE));
		QVERIFY(hash);
		hash->setItemText(IDC_HASH_TEXT_EDIT, L"NotePadLX\nNotepad++ on Linux");
		hash->setChecked(IDC_HASH_EACHLINE_CHECK, true);
		hash->command(IDC_HASH_EACHLINE_CHECK);
		QTest::qWait(100);
		save(hash->grab(), "hash-dark.png");
		hash->hide();

		_npp->command(IDM_EDIT_COLUMNMODE);
		ColumnEditorDlg* col = _npp->getColumnEditorDlg();
		QVERIFY(col);
		col->setChecked(IDC_COL_NUM_RADIO, true);
		col->command(IDC_COL_NUM_RADIO);
		for (auto [id, value] : { std::pair{ IDC_COL_INITNUM_EDIT, L"1" }, std::pair{ IDC_COL_INCREASENUM_EDIT, L"1" }, std::pair{ IDC_COL_REPEATNUM_EDIT, L"1" } })
			col->setItemText(id, value);
		QTest::qWait(100);
		save(col->grab(), "column-editor-dark.png");
		col->hide();
	}

	void lightTheme()
	{
		::setenv("NPLX_THEME", "light", 1);
		nplx::style::refresh();
		QTest::qWait(200);
		save(grabWindow(), "main-light-ui.png");
		::unsetenv("NPLX_THEME");
		nplx::style::refresh();
	}
};

// NPLX: the offscreen platform's default screen is smaller than the window, which would clamp the menus;
// give it a 1920x1200 screen (QTEST_MAIN creates the application before any test code runs)
int main(int argc, char** argv)
{
	QTemporaryDir configDir;
	if (qEnvironmentVariable("QT_QPA_PLATFORM") == QStringLiteral("offscreen") && configDir.isValid())
	{
		const QString configFile = configDir.filePath(QStringLiteral("offscreen.json"));
		QFile f(configFile);
		if (f.open(QIODevice::WriteOnly))
		{
			f.write(R"({ "screens": [ { "name": "screen", "x": 0, "y": 0, "width": 1920, "height": 1200, "logicalDpi": 96, "logicalBaseDpi": 96, "dpr": 1 } ] })");
			f.close();
			qputenv("QT_QPA_PLATFORM", ("offscreen:configfile=" + configFile).toUtf8());
		}
	}
	QApplication app(argc, argv);
	TestScreenshots test;
	return QTest::qExec(&test, argc, argv);
}
#include "tst_screenshots.moc"
