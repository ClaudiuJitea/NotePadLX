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

// Phase 9 tests: the plugin host. The two sample plugins (plugins/samples, built into <build>/sample-plugins) are
// loaded through NPLX_PLUGIN_PATH: they add menu items, a shortcut and a toolbar button, receive notifications,
// edit text, use the NPPM_*_UTF8 messages and dock a Qt panel. A broken plugin in the user's plugin folder is
// refused with a message.

#include <QtTest>
#include <QAction>
#include <QDockWidget>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QTemporaryDir>
#include <QToolBar>

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "AboutDlg.h"
#include "Buffer.h"
#include "DocTabView.h"
#include "DockingPanel.h"
#include "HelloWorld.h"
#include "Notepad_plus.h"
#include "Parameters.h"
#include "PluginDockingQt.h"
#include "PluginsManager.h"
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
			_messages.push_back(message);
			return (type & 0x0F) == MB_YESNO ? IDNO : IDOK;
		}
		int saveOrNotMultiBox(const std::wstring&, const std::wstring&) override { return IDNO; }
		std::vector<std::wstring> getOpenFileNames(const std::wstring&, const std::wstring&, const std::wstring&) override { return _filesAnswer; }
		std::vector<std::wstring> _messages;
		std::vector<std::wstring> _filesAnswer;
	};
}

class TestPlugins : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir _tmp;
	SilentUiServices _ui;
	NativeLangSpeaker _nativeLangSpeaker;
	std::unique_ptr<Notepad_plus> _npp;

	ScintillaEditView* view() { return _npp->getActiveEditView(); }
	QString text() { return QString::fromStdString(view()->getText()); }

	// what a plugin calls: NppData::_nppSend on the NotePadLX handle
	LRESULT send(UINT msg, WPARAM w = 0, LPARAM l = 0) { return _npp->pluginMessage(msg, w, l); }

	HelloWorldCounters counters()
	{
		HelloWorldCounters c;
		CommunicationInfo info{ HelloWorldMsgGetCounters, L"tst_plugins", &c };
		const bool ok = send(NPPM_MSGTOPLUGIN_UTF8, reinterpret_cast<WPARAM>("HelloWorld.so"), reinterpret_cast<LPARAM>(&info));
		if (!ok)
			qWarning("NPPM_MSGTOPLUGIN_UTF8 failed");
		return c;
	}

	QMenu* pluginsMenu()
	{
		for (QAction* a : _npp->menuBar()->actions())
			if (a->menu() && a->menu()->title() == QStringLiteral("&Plugins"))
				return a->menu();
		return nullptr;
	}

	QAction* pluginAction(const QString& pluginName, const QString& itemName)
	{
		QMenu* plugins = pluginsMenu();
		if (!plugins)
			return nullptr;
		for (QAction* a : plugins->actions())
		{
			if (a->menu() && a->menu()->title() == pluginName)
			{
				for (QAction* item : a->menu()->actions())
					if (item->text() == itemName)
						return item;
			}
		}
		return nullptr;
	}

	void writeFile(const QString& path, const QByteArray& bytes)
	{
		QFile f(path);
		QVERIFY(f.open(QIODevice::WriteOnly));
		f.write(bytes);
	}

	std::string utf8Path()
	{
		const LRESULT len = send(NPPM_GETFULLCURRENTPATH_UTF8, 0, 0);
		std::string path(len > 0 ? static_cast<size_t>(len) : 0, '\0');
		send(NPPM_GETFULLCURRENTPATH_UTF8, path.size() + 1, reinterpret_cast<LPARAM>(path.data()));
		return path;
	}

private slots:
	void initTestCase()
	{
		QVERIFY(_tmp.isValid());
		::setenv("XDG_CONFIG_HOME", (_tmp.path() + "/config").toUtf8().constData(), 1);
		::setenv("XDG_DATA_HOME", (_tmp.path() + "/share").toUtf8().constData(), 1);
		::setenv("NPLX_DATA_DIR", NPLX_TEST_SOURCE_DATA_DIR, 1);
		::setenv("NPLX_PLUGIN_PATH", NPLX_SAMPLE_PLUGINS_DIR, 1);

		nplx::setUiServices(&_ui);
		NppParameters& nppParam = NppParameters::getInstance();
		nppParam.load();
		_nativeLangSpeaker.init(nppParam.getNativeLang());
		nppParam.setNativeLangSpeaker(&_nativeLangSpeaker);

		// a broken plugin in the user's plugin folder: not an ELF library
		const QString brokenDir = QString::fromWCharArray(nppParam.getPluginRootDir()) + "/Broken";
		QVERIFY(QDir().mkpath(brokenDir));
		writeFile(brokenDir + "/Broken.so", "not a shared library");

		_npp = std::make_unique<Notepad_plus>();
		_npp->init();
		_npp->resize(1100, 700);
		_npp->show();
		QVERIFY(QTest::qWaitForWindowExposed(_npp.get()));
		_npp->notifyReady();
	}

	void cleanupTestCase()
	{
		_npp.reset();
		nplx::setUiServices(nullptr);
	}

	void pluginsAreLoaded()
	{
		PluginsManager* pm = _npp->pluginsManager();
		QVERIFY(pm);
		QCOMPARE(pm->nbPlugins(), size_t(2));
		QVERIFY(pm->findPlugin(L"HelloWorld.so"));
		QVERIFY(pm->findPlugin(L"DockingSample.so"));

		// the broken one was refused with upstream's message (answered No: the file is kept)
		bool refused = false;
		for (const std::wstring& m : _ui._messages)
			refused |= m.find(L"Broken.so is not compatible") != std::wstring::npos;
		QVERIFY(refused);
		QVERIFY(QFile::exists(QString::fromWCharArray(NppParameters::getInstance().getPluginRootDir()) + "/Broken/Broken.so"));

		// Plugins menu: a sub-menu per plugin, a separator, then "Open Plugins Folder..."
		QMenu* plugins = pluginsMenu();
		QVERIFY(plugins);
		const QList<QAction*> items = plugins->actions();
		QVERIFY(items.size() >= 4);
		QCOMPARE(items[0]->menu()->title(), QStringLiteral("Docking Sample"));
		QCOMPARE(items[1]->menu()->title(), QStringLiteral("Hello World"));
		QVERIFY(items[2]->isSeparator());
		QCOMPARE(items[3]->data().toInt(), IDM_SETTING_OPENPLUGINSDIR);
		QVERIFY(items[3]->isEnabled());

		// the FuncItem separator, the command IDs
		QMenu* hello = items[1]->menu();
		QCOMPARE(hello->actions().size(), 4);
		QVERIFY(hello->actions()[2]->isSeparator());
		const int sayHelloId = hello->actions()[0]->data().toInt();
		QVERIFY(sayHelloId >= ID_PLUGINS_CMD && sayHelloId < ID_PLUGINS_CMD_LIMIT);
		QVERIFY(Notepad_plus::isCommandPorted(sayHelloId));

		// Debug Info lists them
		_npp->command(IDM_DEBUGINFO);
		const std::wstring debugInfo = _npp->getDebugInfoDlg()->debugInfo();
		QVERIFY2(debugInfo.find(L"HelloWorld (") != std::wstring::npos, qPrintable(QString::fromStdWString(debugInfo)));
		_npp->getDebugInfoDlg()->hide();
	}

	void commandEditsText()
	{
		QAction* sayHello = pluginAction("Hello World", "Say Hello");
		QVERIFY(sayHello);
		// the default shortcut of the FuncItem (ShortcutKey)
		QCOMPARE(sayHello->shortcut(), QKeySequence(QStringLiteral("Ctrl+Alt+Shift+H")));
		ShortcutKey sk;
		QVERIFY(send(NPPM_GETSHORTCUTBYCMDID, sayHello->data().toInt(), reinterpret_cast<LPARAM>(&sk)));
		QVERIFY(sk._isCtrl && sk._isAlt && sk._isShift);
		QCOMPARE(int(sk._key), int('H'));

		const size_t before = _npp->getDocTab(MAIN_VIEW)->nbItem();
		sayHello->trigger();
		QCOMPARE(_npp->getDocTab(MAIN_VIEW)->nbItem(), before + 1);
		QCOMPARE(text(), QStringLiteral("Hello, NotePadLX!\n"));
		QCOMPARE(counters()._helloCount, 1);

		// the shortcut runs it too
		QTest::keyClick(_npp.get(), Qt::Key_H, Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier);
		QTRY_COMPARE(counters()._helloCount, 2);

		// the toolbar button with the plugin's SVG icon
		QToolBar* toolBar = _npp->findChild<QToolBar*>(QStringLiteral("mainToolBar"));
		QVERIFY(toolBar);
		QVERIFY(toolBar->actions().contains(sayHello));
		QVERIFY(!sayHello->icon().isNull());
	}

	void notifications()
	{
		HelloWorldCounters c = counters();
		QVERIFY(c._ready);
		QVERIFY(c._notifications > 0);
		QVERIFY(c._buffersActivated > 0);
		QCOMPARE(c._lastActivatedBuffer, reinterpret_cast<uintptr_t>(view()->getCurrentBufferID()));
		// SCN_MODIFIED from the Scintilla view, with the handle of NppData
		QVERIFY(c._textInserted > 0);
		QCOMPARE(c._lastScintilla, static_cast<void*>(view()));

		// NPPN_FILEOPENED, NPPN_LANGCHANGED, NPPN_FILESAVED
		const QString path = _tmp.path() + "/notified.txt";
		writeFile(path, "abc\n");
		const int opened = c._filesOpened;
		QVERIFY(send(NPPM_DOOPEN, 0, reinterpret_cast<LPARAM>(path.toStdWString().c_str())));
		c = counters();
		QCOMPARE(c._filesOpened, opened + 1);
		QCOMPARE(c._lastActivatedBuffer, reinterpret_cast<uintptr_t>(view()->getCurrentBufferID()));

		const int langChanged = c._langChanged;
		QVERIFY(send(NPPM_SETCURRENTLANGTYPE, 0, L_CPP));
		LangType lang = L_TEXT;
		QVERIFY(send(NPPM_GETCURRENTLANGTYPE, 0, reinterpret_cast<LPARAM>(&lang)));
		QCOMPARE(lang, L_CPP);
		QCOMPARE(counters()._langChanged, langChanged + 1);

		const int saved = c._filesSaved;
		view()->execute(SCI_APPENDTEXT, 3, reinterpret_cast<LPARAM>("def"));
		QVERIFY(send(NPPM_SAVECURRENTFILE));
		QCOMPARE(counters()._filesSaved, saved + 1);
	}

	void utf8Messages()
	{
		const QString dir = _tmp.path() + QStringLiteral("/dossier é");
		QVERIFY(QDir().mkpath(dir));
		const QString path = dir + QStringLiteral("/héllo wörld.txt");
		writeFile(path, "x\n");

		// NPPM_DOOPEN_UTF8 and the getters
		QVERIFY(send(NPPM_DOOPEN_UTF8, 0, reinterpret_cast<LPARAM>(path.toUtf8().constData())));
		QCOMPARE(QString::fromStdString(utf8Path()), path);
		const LRESULT len = send(NPPM_GETFULLCURRENTPATH_UTF8, 0, 0);
		QCOMPARE(len, LRESULT(path.toUtf8().size()));
		char tooSmall[4];
		QCOMPARE(send(NPPM_GETFULLCURRENTPATH_UTF8, sizeof(tooSmall), reinterpret_cast<LPARAM>(tooSmall)), LRESULT(0));

		char name[256];
		QVERIFY(send(NPPM_GETFILENAME_UTF8, sizeof(name), reinterpret_cast<LPARAM>(name)) > 0);
		QCOMPARE(QString::fromUtf8(name), QStringLiteral("héllo wörld.txt"));
		QVERIFY(send(NPPM_GETCURRENTDIRECTORY_UTF8, sizeof(name), reinterpret_cast<LPARAM>(name)) > 0);
		QCOMPARE(QString::fromUtf8(name), dir);

		// the wchar_t messages still work (Linux wchar_t)
		wchar_t wide[512];
		QVERIFY(send(NPPM_GETFULLCURRENTPATH, 512, reinterpret_cast<LPARAM>(wide)));
		QCOMPARE(QString::fromWCharArray(wide), path);

		// by buffer ID; -1 for an unknown one
		const LPARAM bufferID = send(NPPM_GETCURRENTBUFFERID);
		QCOMPARE(bufferID, reinterpret_cast<LPARAM>(view()->getCurrentBufferID()));
		const LRESULT idLen = send(NPPM_GETFULLPATHFROMBUFFERID_UTF8, bufferID, 0);
		std::string byId(static_cast<size_t>(idLen), '\0');
		send(NPPM_GETFULLPATHFROMBUFFERID_UTF8, bufferID, reinterpret_cast<LPARAM>(byId.data()));
		QCOMPARE(QString::fromStdString(byId), path);
		int notABuffer = 0;
		QCOMPARE(send(NPPM_GETFULLPATHFROMBUFFERID_UTF8, reinterpret_cast<WPARAM>(&notABuffer), 0), LRESULT(-1));

		char langName[64];
		QVERIFY(send(NPPM_GETLANGUAGENAME_UTF8, L_PYTHON, reinterpret_cast<LPARAM>(langName)) > 0);
		QCOMPARE(QString::fromUtf8(langName), QStringLiteral("Python"));

		// a status bar part, and the switch to a file by its UTF-8 name
		QVERIFY(send(NPPM_SETSTATUSBAR_UTF8, STATUSBAR_DOC_TYPE, reinterpret_cast<LPARAM>("Plugin: ✓")));
		bool shown = false;
		for (QLabel* l : _npp->findChildren<QLabel*>())
			shown |= l->text() == QStringLiteral("Plugin: ✓");
		QVERIFY(shown);
		_npp->command(IDM_FILE_NEW);
		QVERIFY(send(NPPM_SWITCHTOFILE_UTF8, 0, reinterpret_cast<LPARAM>(path.toUtf8().constData())));
		QCOMPARE(QString::fromStdString(utf8Path()), path);

		// the Hello World command that inserts the path (NPPM_GETFULLCURRENTPATH_UTF8 + SCI_REPLACESEL)
		view()->execute(SCI_CLEARALL);
		pluginAction("Hello World", "Insert Current File Path")->trigger();
		QCOMPARE(text(), path);
		view()->execute(SCI_UNDO);
		_npp->command(IDM_FILE_CLOSE);
	}

	void otherMessages()
	{
		// the API version of upstream Notepad++ that NotePadLX follows (VERSION_INTERNAL_VALUE "8.981")
		QCOMPARE(send(NPPM_GETNPPVERSION, TRUE), LRESULT(MAKELONG(981, 8)));

		int first = 0, second = 0;
		QVERIFY(send(NPPM_ALLOCATECMDID, 3, reinterpret_cast<LPARAM>(&first)));
		QVERIFY(send(NPPM_ALLOCATECMDID, 2, reinterpret_cast<LPARAM>(&second)));
		QCOMPARE(first, ID_PLUGINS_CMD_DYNAMIC);
		QCOMPARE(second, first + 3);
		int marker = 0;
		QVERIFY(send(NPPM_ALLOCATEMARKER, 2, reinterpret_cast<LPARAM>(&marker)));
		QCOMPARE(marker, MARKER_PLUGINS);
		int indicator = 0;
		QVERIFY(send(NPPM_ALLOCATEINDICATOR, 1, reinterpret_cast<LPARAM>(&indicator)));
		QCOMPARE(indicator, INDICATOR_PLUGINS);

		QCOMPARE(send(NPPM_GETNBOPENFILES, 0, ALL_OPEN_FILES),
			LRESULT(_npp->getDocTab(MAIN_VIEW)->nbItem() + _npp->getDocTab(SUB_VIEW)->nbItem()));
		QCOMPARE(send(NPPM_GETCURRENTVIEW), LRESULT(MAIN_VIEW));
		QCOMPARE(send(NPPM_GETBUFFERIDFROMPOS, 0, MAIN_VIEW), reinterpret_cast<LRESULT>(_npp->getDocTab(MAIN_VIEW)->getBufferByIndex(0)));

		char confDir[1024];
		QVERIFY(send(NPPM_GETPLUGINSCONFIGDIR_UTF8, sizeof(confDir), reinterpret_cast<LPARAM>(confDir)) > 0);
		QCOMPARE(QString::fromUtf8(confDir), QString::fromWCharArray(NppParameters::getInstance().getUserPluginConfDir()));
		char homeDir[1024];
		QVERIFY(send(NPPM_GETPLUGINHOMEPATH_UTF8, sizeof(homeDir), reinterpret_cast<LPARAM>(homeDir)) > 0);
		QCOMPARE(QString::fromUtf8(homeDir), QString::fromWCharArray(NppParameters::getInstance().getPluginRootDir()));

		// a Scintilla of its own (NPPM_CREATESCINTILLAHANDLE), driven through _nppSend / the handle
		void* sciHandle = reinterpret_cast<void*>(send(NPPM_CREATESCINTILLAHANDLE));
		QVERIFY(sciHandle);
		ScintillaEditView* own = _npp->pluginScintilla(sciHandle);
		QVERIFY(own && own != view());
		own->execute(SCI_SETTEXT, 0, reinterpret_cast<LPARAM>("private"));
		QCOMPARE(own->execute(SCI_GETLENGTH), sptr_t(7));
		QVERIFY(!_npp->pluginScintilla(reinterpret_cast<void*>(&first)));

		// the menus as Qt objects
		QCOMPARE(reinterpret_cast<QMenu*>(send(NPPM_GETMENUHANDLE, NPPPLUGINMENU)), pluginsMenu());
		QCOMPARE(reinterpret_cast<QMenuBar*>(send(NPPM_GETMENUHANDLE, NPPMAINMENU)), _npp->menuBar());

		// NPPM_MENUCOMMAND and NPPM_SETMENUITEMCHECK on a plugin item
		const size_t before = _npp->getDocTab(MAIN_VIEW)->nbItem();
		QVERIFY(send(NPPM_MENUCOMMAND, 0, IDM_FILE_NEW));
		QCOMPARE(_npp->getDocTab(MAIN_VIEW)->nbItem(), before + 1);
		_npp->command(IDM_FILE_CLOSE);
		QAction* counter = pluginAction("Hello World", "Show Notification Counters");
		QVERIFY(send(NPPM_SETMENUITEMCHECK, counter->data().toInt(), TRUE));
		QVERIFY(counter->isChecked());
		QVERIFY(send(NPPM_SETMENUITEMCHECK, counter->data().toInt(), FALSE));
		QVERIFY(!counter->isChecked());

		// the shortcut removed by the plugin (NPPM_REMOVESHORTCUTBYCMDID)
		QAction* sayHello = pluginAction("Hello World", "Say Hello");
		QVERIFY(send(NPPM_REMOVESHORTCUTBYCMDID, sayHello->data().toInt()));
		QVERIFY(sayHello->shortcut().isEmpty());
		ShortcutKey sk;
		QVERIFY(!send(NPPM_GETSHORTCUTBYCMDID, sayHello->data().toInt(), reinterpret_cast<LPARAM>(&sk)));

		// an unknown NPPM_ is 0, no crash
		QCOMPARE(send(NPPMSG + 999, 1, 2), LRESULT(0));
	}

	void dockingPanel()
	{
		QAction* show = pluginAction("Docking Sample", "Document Info");
		QVERIFY(show);
		_npp->command(IDM_FILE_NEW);
		view()->execute(SCI_SETTEXT, 0, reinterpret_cast<LPARAM>("make me loud\nsecond"));

		show->trigger();
		auto* dock = _npp->findChild<DockingPanel*>(QStringLiteral("plugin:DockingSample.so:0"));
		QVERIFY(dock);
		QVERIFY(dock->isVisible());
		QCOMPARE(dock->windowTitle(), QStringLiteral("Document Info - Docking Sample"));
		QCOMPARE(_npp->dockWidgetArea(dock), Qt::RightDockWidgetArea);
		QVERIFY(show->isChecked()); // NPPM_SETMENUITEMCHECK from the plugin

		QWidget* panel = dock->findChild<QWidget*>(QStringLiteral("DockingSamplePanel"));
		QVERIFY(panel);
		QCOMPARE(reinterpret_cast<QWidget*>(send(NPPM_DMMGETPLUGINHWNDBYNAME_UTF8, 0, reinterpret_cast<LPARAM>("DockingSample.so"))), panel);
		QCOMPARE(reinterpret_cast<QWidget*>(send(NPPM_DMMGETPLUGINHWNDBYNAME_UTF8, reinterpret_cast<WPARAM>("Document Info"),
			reinterpret_cast<LPARAM>("DockingSample.so"))), panel);
		QVERIFY(!send(NPPM_DMMGETPLUGINHWNDBYNAME_UTF8, 0, reinterpret_cast<LPARAM>("Other.so")));

		// it follows the document (SCN_UPDATEUI, NPPN_BUFFERACTIVATED)
		auto* stats = panel->findChild<QLabel*>(QStringLiteral("stats"));
		QVERIFY(stats);
		QTRY_VERIFY2(stats->text().startsWith(QStringLiteral("19 characters, 2 lines")), qPrintable(stats->text()));

		// its button edits the text
		view()->execute(SCI_SETSEL, 0, 7);
		panel->findChild<QPushButton*>(QStringLiteral("upperCase"))->click();
		QCOMPARE(text(), QStringLiteral("MAKE ME loud\nsecond"));

		// the dock's close button: hidden, the menu item unchecked; the command shows it again
		QVERIFY(!dock->close()); // DockingPanel hides it itself (upstream DMN_CLOSE)
		QVERIFY(!dock->isVisible());
		QVERIFY(!show->isChecked());
		show->trigger();
		QVERIFY(dock->isVisible());
		QVERIFY(show->isChecked());

		// saved in config.xml by module name and dlgID, visible
		_npp->saveDockingParams();
		bool saved = false;
		for (const PluginDlgDockingInfo& pddi : NppParameters::getInstance().getNppGUI()._dockingData._pluginDockInfo)
			saved |= pddi._name == L"DockingSample.so" && pddi._internalID == 0 && pddi._isVisible && pddi._currContainer == CONT_RIGHT;
		QVERIFY(saved);

		// NPPM_DMMHIDE from the plugin
		QVERIFY(send(NPPM_DMMHIDE, 0, reinterpret_cast<LPARAM>(panel)));
		QVERIFY(!dock->isVisible());
		_npp->command(IDM_FILE_CLOSE);
	}

	void importPlugin()
	{
		// Settings > Import > Import plugin(s)...: copied to <plugin home>/<Name>/<Name>.so, restart message
		const QString src = _tmp.path() + "/MyTool.so";
		writeFile(src, "binary");
		_ui._filesAnswer = { src.toStdWString() };
		const size_t nbMessages = _ui._messages.size();
		_npp->command(IDM_SETTING_IMPORTPLUGIN);
		_ui._filesAnswer.clear();
		const QString dest = QString::fromWCharArray(NppParameters::getInstance().getPluginRootDir()) + "/MyTool/MyTool.so";
		QVERIFY(QFile::exists(dest));
		QCOMPARE(_ui._messages.size(), nbMessages + 1);
		QVERIFY(_ui._messages.back().find(L"restart NotePadLX") != std::wstring::npos);
	}

	void disabledManager()
	{
		// -noPlugin (upstream _pluginsManager.disable()): nothing is loaded
		PluginsManager pm;
		pm.disable();
		QVERIFY(!pm.loadPlugins(QStringLiteral(NPLX_SAMPLE_PLUGINS_DIR).toStdWString().c_str()));
		QCOMPARE(pm.nbPlugins(), size_t(0));
	}

	void shutdownIsNotified()
	{
		QVERIFY(!counters()._shutdown);
		_npp->close();
		QVERIFY(counters()._shutdown);
	}
};

QTEST_MAIN(TestPlugins)
#include "tst_plugins.moc"
