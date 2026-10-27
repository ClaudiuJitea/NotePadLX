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

// Start-up as in main.cpp: files from the command line are opened before the window shows.

#include <QtTest>
#include <QTemporaryDir>

#include <cstdlib>
#include <fstream>

#include "DocTabView.h"
#include "Notepad_plus.h"
#include "Parameters.h"
#include "QtUiServices.h"
#include "localization.h"

class TestLaunch : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir _tmp;
	QtUiServices _ui;
	NativeLangSpeaker _nativeLangSpeaker;

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
	}

	void cleanupTestCase()
	{
		nplx::setUiServices(nullptr);
	}

	void fileOpenedBeforeShowKeepsTheTabBar()
	{
		// Opening a file from the file manager: its tab replaces the "new 1" document before the window shows, and
		// the tab bar must still get its height
		const QString path = _tmp.path() + "/launch.txt";
		std::ofstream(path.toStdString()) << "launched\n";

		Notepad_plus npp;
		npp.init();
		CmdLineParamsDTO dto;
		npp.loadCommandlineParams({ path.toStdWString() }, &dto);
		npp.resize(800, 500);
		npp.show();
		QVERIFY(QTest::qWaitForWindowExposed(&npp));

		DocTabView* tab = npp.getDocTab(MAIN_VIEW);
		QCOMPARE(tab->count(), 1);
		QVERIFY(tab->height() > 0);
		QCOMPARE(tab->height(), tab->sizeHint().height());
	}
};

QTEST_MAIN(TestLaunch)
#include "tst_launch.moc"
