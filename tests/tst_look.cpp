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

// NotePadLX look: icons, style and stylesheet (NplxStyle). NPLX_SCREENSHOT_DIR=<dir> saves screenshots of the main
// window, a menu, dialogs and a message box, in the light and dark themes.

#include <QtTest>
#include <QAction>
#include <QDialogButtonBox>
#include <QDirIterator>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QStyle>
#include <QTemporaryDir>
#include <QToolBar>
#include <QToolButton>

#include <cstdlib>

#include "FindReplaceDlg.h"
#include "GoToLineDlg.h"
#include "RunDlg.h"
#include "NplxStyle.h"
#include "Notepad_plus.h"
#include "Parameters.h"
#include "QtUiServices.h"
#include "ScintillaEditView.h"
#include "UserDefineDialog.h"
#include "localization.h"
#include "menuCmdID.h"

class TestLook : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir _tmp;
	QtUiServices _ui;
	NativeLangSpeaker _nativeLangSpeaker;
	std::unique_ptr<Notepad_plus> _npp;

	void screenshot(QWidget* w, const QString& name)
	{
		const QByteArray dir = qgetenv("NPLX_SCREENSHOT_DIR");
		if (!dir.isEmpty())
		{
			QCoreApplication::processEvents();
			w->grab().save(QString::fromUtf8(dir) + "/" + name);
		}
	}

	void setTheme(const char* theme)
	{
		::setenv("NPLX_THEME", theme, 1);
		nplx::style::refresh();
		QCoreApplication::processEvents();
	}

	void screenshots(const QString& suffix)
	{
		_npp->command(IDM_VIEW_TAB_PREV);
		screenshot(_npp.get(), QStringLiteral("look-main-%1.png").arg(suffix));
		_npp->command(IDM_VIEW_TAB_NEXT);

		for (const QString& title : { QStringLiteral("View"), QStringLiteral("File"), QStringLiteral("Edit"), QStringLiteral("?") })
		{
			QMenu* menu = nullptr;
			for (QAction* a : _npp->menuBar()->actions())
				if (a->menu() && a->text().remove(QLatin1Char('&')) == title)
					menu = a->menu();
			QVERIFY(menu);
			menu->popup(_npp->mapToGlobal(QPoint(150, 30)));
			QVERIFY(QTest::qWaitForWindowExposed(menu));
			screenshot(menu, title == QStringLiteral("View") ? QStringLiteral("look-menu-%1.png").arg(suffix)
				: QStringLiteral("look-menu-%1-%2.png").arg(title == QStringLiteral("?") ? QStringLiteral("help") : title.toLower(), suffix));
			menu->hide();
		}

		_npp->command(IDM_EXECUTE);
		screenshot(static_cast<QWidget*>(_npp->getRunDlg()), QStringLiteral("look-run-%1.png").arg(suffix));
		static_cast<QWidget*>(_npp->getRunDlg())->hide();
		_npp->command(IDM_SEARCH_GOTOLINE);
		if (QWidget* gotoDlg = static_cast<QWidget*>(_npp->getGoToLineDlg()))
		{
			screenshot(gotoDlg, QStringLiteral("look-goto-%1.png").arg(suffix));
			gotoDlg->hide();
		}

		_npp->command(IDM_SEARCH_FIND);
		QDialog* find = _npp->getFindReplaceDlg();
		QVERIFY(QTest::qWaitForWindowExposed(find));
		screenshot(find, QStringLiteral("look-find-%1.png").arg(suffix));
		find->hide();

		_npp->command(IDM_LANG_USER_DLG);
		screenshot(_npp->getUserDefineDlg(), QStringLiteral("look-udl-%1.png").arg(suffix));
		_npp->command(IDM_LANG_USER_DLG);

		QMessageBox box(QMessageBox::Question, QStringLiteral("NotePadLX"),
			QStringLiteral("Save file \"new 1\" ?"), QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, _npp.get());
		box.show();
		QVERIFY(QTest::qWaitForWindowExposed(&box));
		screenshot(&box, QStringLiteral("look-msgbox-%1.png").arg(suffix));
		box.hide();
	}

private slots:
	void initTestCase()
	{
		QVERIFY(_tmp.isValid());
		::setenv("XDG_CONFIG_HOME", (_tmp.path() + "/config").toUtf8().constData(), 1);
		::setenv("XDG_DATA_HOME", (_tmp.path() + "/share").toUtf8().constData(), 1);
		::setenv("XDG_CACHE_HOME", (_tmp.path() + "/cache").toUtf8().constData(), 1);
		::setenv("NPLX_DATA_DIR", NPLX_TEST_SOURCE_DATA_DIR, 1);
		::setenv("NPLX_THEME", "light", 1);

		nplx::setUiServices(&_ui);
		NppParameters& nppParam = NppParameters::getInstance();
		nppParam.load();
		_nativeLangSpeaker.init(nppParam.getNativeLang());
		nppParam.setNativeLangSpeaker(&_nativeLangSpeaker);

		nplx::style::apply(*qApp);

		_npp = std::make_unique<Notepad_plus>();
		_npp->init();
		_npp->resize(1180, 560);
		_npp->show();
		QVERIFY(QTest::qWaitForWindowExposed(_npp.get()));

		_npp->getActiveEditView()->setText("#include <cstdio>\n\nint main()\n{\n\t// NotePadLX\n\tstd::puts(\"Hello\");\n\treturn 0;\n}\n");
		_npp->command(IDM_LANG_CPP);
		_npp->command(IDM_VIEW_FUNC_LIST); // a docked panel in the screenshots
		_npp->command(IDM_FILE_NEW);
		_npp->getActiveEditView()->setText("modified");
	}

	void cleanupTestCase()
	{
		_npp.reset();
		nplx::setUiServices(nullptr);
	}

	void everyIconRenders()
	{
		// all the templates render (needs the Qt SVG image format plugin), in both states
		int count = 0;
		QDirIterator it(QStringLiteral(":/nplx-icons"), { QStringLiteral("*.svg") });
		while (it.hasNext())
		{
			const QString name = QFileInfo(it.next()).completeBaseName();
			const QIcon icon = nplx::style::icon(name);
			QVERIFY2(!icon.isNull(), qPrintable(name));
			for (QIcon::Mode mode : { QIcon::Normal, QIcon::Disabled })
			{
				const QImage img = icon.pixmap(QSize(24, 24), mode).toImage();
				QVERIFY2(!img.isNull(), qPrintable(name));
				bool painted = false;
				for (int y = 0; y < img.height() && !painted; ++y)
					for (int x = 0; x < img.width() && !painted; ++x)
						painted = qAlpha(img.pixel(x, y)) > 0;
				QVERIFY2(painted, qPrintable(name));
			}
			++count;
		}
		QVERIFY(count >= 40);
	}

	void toolbarUsesNplxIcons()
	{
		auto* toolBar = _npp->findChild<QToolBar*>(QStringLiteral("mainToolBar"));
		QVERIFY(toolBar);
		QCOMPARE(toolBar->iconSize(), QSize(18, 18)); // the default light set: upstream "Standard icons: small"
		int buttons = 0;
		for (QAction* a : toolBar->actions())
		{
			if (a->isSeparator())
				continue;
			QVERIFY2(!a->icon().isNull(), qPrintable(a->text()));
			QCOMPARE(a->icon().pixmap(QSize(24, 24)).width(), 24);
			++buttons;
		}
		QVERIFY(buttons >= 30);
	}

	void styleSheetIsComplete()
	{
		for (const char* theme : { "light", "dark" })
		{
			setTheme(theme);
			const QString qss = qApp->styleSheet();
			QVERIFY(!qss.isEmpty());
			// every {token} was replaced
			static const QRegularExpression token(QStringLiteral("\\{[a-zA-Z_]+\\}"));
			const QRegularExpressionMatch m = token.match(qss);
			QVERIFY2(!m.hasMatch(), qPrintable(m.captured(0)));
			// the stylesheet images exist
			static const QRegularExpression url(QStringLiteral("url\\(([^)]+)\\)"));
			for (auto i = url.globalMatch(qss); i.hasNext();)
			{
				const QString path = i.next().captured(1);
				QVERIFY2(QFile::exists(path), qPrintable(path));
			}
			QCOMPARE(nplx::style::isDark(), std::string(theme) == "dark");
			QCOMPARE(qApp->palette().color(QPalette::Window), nplx::style::tokens()._window);
		}
		setTheme("light");
	}

	void dialogButtonsHaveNoStockIcons()
	{
		QDialogButtonBox box(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
		QVERIFY(box.button(QDialogButtonBox::Ok)->icon().isNull());
		QVERIFY(box.button(QDialogButtonBox::Cancel)->icon().isNull());
		QVERIFY(!qApp->style()->standardIcon(QStyle::SP_MessageBoxQuestion).isNull());
	}

	void caretBlinksAtLineStart()
	{
		// the first pixel column of the text is painted (the editor has a frame), so that a caret there is erased when
		// it blinks off
		ScintillaEditView* view = _npp->getActiveEditView();
		view->setFocus();
		view->execute(SCI_GOTOPOS, 0);
		const int x = static_cast<int>(view->execute(SCI_POINTXFROMPOSITION, 0, 0));
		const int y = static_cast<int>(view->execute(SCI_POINTYFROMPOSITION, 0, 0)) + 2;
		const long sciCaret = static_cast<long>(view->execute(SCI_GETELEMENTCOLOUR, SC_ELEMENT_CARET));
		const QColor caret(sciCaret & 0xff, (sciCaret >> 8) & 0xff, (sciCaret >> 16) & 0xff);
		const long sciBack = static_cast<long>(view->execute(SCI_GETELEMENTCOLOUR, SC_ELEMENT_CARET_LINE_BACK));
		const QColor caretLine(sciBack & 0xff, (sciBack >> 8) & 0xff, (sciBack >> 16) & 0xff);
		bool on = false;
		bool off = false;
		for (int i = 0; i < 20 && !(on && off); ++i)
		{
			const QColor pixel = view->viewport()->grab().toImage().pixelColor(x, y);
			QVERIFY2(pixel == caret || pixel == caretLine, qPrintable(pixel.name()));
			(pixel == caret ? on : off) = true;
			QTest::qWait(100);
		}
		QVERIFY(on && off);
	}

	void screenshotsLight()
	{
		setTheme("light");
		screenshots(QStringLiteral("light"));
	}

	void screenshotsDark()
	{
		setTheme("dark");
		screenshots(QStringLiteral("dark"));
		setTheme("light");
	}
};

QTEST_MAIN(TestLook)
#include "tst_look.moc"
