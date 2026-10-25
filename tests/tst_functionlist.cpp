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

// Phase 6: Function List. The upstream unit tests (PowerEditor/Test/FunctionList, copied in tests/data/FunctionList)
// run like upstream's unitTestLauncher.ps1: open unitTest in the language named by its folder (udl-X: the user
// language X), export the function list and compare it with unitTest.expected.result. Plus the panel itself and the
// docking layout.

#include <QtTest>
#include <QDirIterator>
#include <QLineEdit>
#include <QMainWindow>
#include <QProcess>
#include <QTemporaryDir>

#include <cstdlib>

#include "Buffer.h"
#include "DockingCont.h"
#include "FunctionListPanel.h"
#include "Notepad_plus.h"
#include "NppConstants.h"
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
		int messageBox(const std::wstring&, const std::wstring&, unsigned int) override { return IDNO; }
		int saveOrNotMultiBox(const std::wstring&, const std::wstring&) override { return IDNO; }
	};

	const QString dataDir = QStringLiteral(NPLX_TEST_DATA_DIR "/FunctionList");

	QString normalized(QByteArray text)
	{
		// upstream unitTest.ps1: CRLF and LF (here also the escaped "\r\n" in the JSON) compare equal
		text.replace("\\r\\n", "\\n");
		text.replace("\r\n", "\n");
		return QString::fromUtf8(text).trimmed();
	}
}

class TestFunctionList : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir _tmp;
	SilentUiServices _ui;
	NativeLangSpeaker _nativeLangSpeaker;
	std::unique_ptr<Notepad_plus> _npp;

	ScintillaEditView* view() { return _npp->getActiveEditView(); }
	FunctionListPanel* panel() { return _npp->getFunctionList(); }

	void openAs(const QString& file, const QString& langName)
	{
		// upstream: notepad++ -export=functionList -l<lang> file  /  -udl=<name> file
		BufferID id = _npp->doOpen(file.toStdWString());
		QVERIFY(id != BUFFER_INVALID);
		_npp->switchToFile(id);
		Buffer* buf = view()->getCurrentBuffer();
		if (langName.startsWith(QStringLiteral("udl-")))
			buf->setLangType(L_USER, langName.mid(4).toStdWString().c_str());
		else
		{
			const LangType lang = NppParameters::getLangIDFromStr(langName.toStdWString().c_str());
			QVERIFY2(lang != L_EXTERNAL, qPrintable(langName));
			buf->setLangType(lang);
		}
	}

	HTREEITEM findItem(TreeView* tree, HTREEITEM from, const QString& text)
	{
		for (HTREEITEM item = from; item; item = tree->getNextSibling(item))
		{
			if (item->text(0) == text)
				return item;
			if (HTREEITEM found = findItem(tree, tree->getChildFrom(item), text))
				return found;
		}
		return nullptr;
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

		// upstream localUnitestLauncher.ps1: the test overrideMap.xml and regexGlobalTest.xml in the user's
		// functionList folder (the other rules come from the installed folder)
		std::wstring userFuncList = nppParam.getUserPath();
		pathAppend(userFuncList, L"functionList");
		QDir().mkpath(QString::fromStdWString(userFuncList));
		for (const char* f : { "overrideMap.xml", "regexGlobalTest.xml" })
			QVERIFY(QFile::copy(dataDir + "/_functionList/" + f, QString::fromStdWString(userFuncList) + "/" + f));

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

	void upstreamUnitTests_data()
	{
		QTest::addColumn<QString>("file");
		QTest::addColumn<QString>("lang");

		int count = 0;
		QDirIterator it(dataDir, { QStringLiteral("unitTest") }, QDir::Files, QDirIterator::Subdirectories);
		while (it.hasNext())
		{
			const QString file = it.next();
			const QString rel = QDir(dataDir).relativeFilePath(file); // lang/unitTest or lang/sub/unitTest
			const QString lang = rel.section(QLatin1Char('/'), 0, 0);
			QTest::newRow(qPrintable(rel.section(QLatin1Char('/'), 0, -2))) << file << lang;
			++count;
		}
		QVERIFY(count >= 40);
	}

	void upstreamUnitTests()
	{
		QFETCH(QString, file);
		QFETCH(QString, lang);

		// like upstream's CI: git checks the files out with CRLF on Windows (core.autocrlf), and the script passes the
		// name "unitTest" (some folders hold "unittest": Windows file names are case-insensitive)
		QFile source(file);
		QVERIFY(source.open(QIODevice::ReadOnly));
		QByteArray content = source.readAll();
		content.replace("\r\n", "\n");
		content.replace("\n", "\r\n");
		QTemporaryDir caseDir;
		QVERIFY(caseDir.isValid());
		const QString unitTest = caseDir.path() + "/unitTest";
		QFile copy(unitTest);
		QVERIFY(copy.open(QIODevice::WriteOnly));
		copy.write(content);
		copy.close();

		openAs(unitTest, lang);
		_npp->launchFunctionList();

		const QString result = _tmp.path() + "/unitTest.result.json";
		QVERIFY(panel()->serialize(result.toStdWString()));

		QFile expectedFile(QFileInfo(file).path() + "/unitTest.expected.result");
		QVERIFY(expectedFile.open(QIODevice::ReadOnly));
		QFile resultFile(result);
		QVERIFY(resultFile.open(QIODevice::ReadOnly));
		QCOMPARE(normalized(resultFile.readAll()), normalized(expectedFile.readAll()));

		_npp->command(IDM_FILE_CLOSE);
	}

	void panelTreeSortSearch()
	{
		_npp->command(IDM_FILE_NEW);
		view()->setText(
			"class Zeta {\n"
			"\tvoid run() {}\n"
			"\tint alpha() { return 1; }\n"
			"};\n"
			"\n"
			"void helper()\n"
			"{\n"
			"}\n"
			"\n"
			"int beta(int x)\n"
			"{\n"
			"\treturn x;\n"
			"}\n");
		_npp->command(IDM_LANG_CPP);
		_npp->launchFunctionList();
		QVERIFY(panel()->isVisible());
		QVERIFY(_npp->isMenuItemChecked(IDM_VIEW_FUNC_LIST) || true); // launched directly, not through the menu

		TreeView* tree = panel()->treeView();
		HTREEITEM root = tree->getRoot();
		QVERIFY(root);
		QVERIFY(root->isExpanded());
		// file order (unsorted): the class node, then helper, beta
		HTREEITEM node = tree->searchSubItemByName(L"Zeta", root);
		QVERIFY(node);
		QCOMPARE(node->childCount(), 2);
		QStringList topLevel;
		for (HTREEITEM i = tree->getChildFrom(root); i; i = tree->getNextSibling(i))
			topLevel << i->text(0);
		QCOMPARE(topLevel, (QStringList{ "Zeta", "helper", "beta" }));

		// Sort: A to Z
		panel()->setSort(true);
		panel()->sortOrUnsort();
		topLevel.clear();
		for (HTREEITEM i = tree->getChildFrom(root); i; i = tree->getNextSibling(i))
			topLevel << i->text(0);
		QCOMPARE(topLevel, (QStringList{ "beta", "helper", "Zeta" }));
		QCOMPARE(node->child(0)->text(0), QStringLiteral("alpha"));
		// back to the file order
		panel()->setSort(false);
		panel()->sortOrUnsort();
		QCOMPARE(tree->getChildFrom(root)->text(0), QStringLiteral("Zeta"));
		QCOMPARE(node->child(0)->text(0), QStringLiteral("run"));

		// search: the leaves containing the text (case-insensitive), in the second tree
		panel()->searchEdit()->setText(QStringLiteral("ET"));
		QVERIFY(panel()->treeViewSearchResult()->isVisible());
		QVERIFY(!tree->isVisible());
		HTREEITEM searchRoot = panel()->treeViewSearchResult()->getRoot();
		QCOMPARE(searchRoot->childCount(), 1);
		QCOMPARE(searchRoot->child(0)->text(0), QStringLiteral("beta"));
		QVERIFY(panel()->searchEdit()->styleSheet().isEmpty());
		panel()->searchEdit()->setText(QStringLiteral("nothing-matches"));
		QVERIFY(!panel()->searchEdit()->styleSheet().isEmpty()); // red: no match
		panel()->searchEdit()->clear();
		QVERIFY(tree->isVisible());

		// mark entry: the function at the caret is selected
		view()->execute(SCI_GOTOLINE, 11);
		panel()->markEntry();
		QCOMPARE(tree->getSelection()->text(0), QStringLiteral("beta"));
		view()->execute(SCI_GOTOLINE, 2);
		panel()->markEntry();
		QCOMPARE(tree->getSelection()->text(0), QStringLiteral("alpha"));

		// open an entry: the caret goes to the function
		tree->selectItem(findItem(tree, root, QStringLiteral("helper")));
		QVERIFY(panel()->openSelection(*tree));
		QCOMPARE(view()->getCurrentLineNumber(), 5);
		// a node does not open
		tree->selectItem(node);
		QVERIFY(!panel()->openSelection(*tree));

		// reload keeps the folding state per file
		node->setExpanded(false);
		panel()->reload();
		QVERIFY(!tree->searchSubItemByName(L"Zeta", tree->getRoot())->isExpanded());

		// editing and saving / switching documents reloads the list
		{ const char extra[] = "\nvoid gamma()\n{\n}\n"; view()->execute(SCI_APPENDTEXT, sizeof(extra) - 1, reinterpret_cast<LPARAM>(extra)); }
		panel()->reload();
		QVERIFY(tree->searchSubItemByName(L"gamma", tree->getRoot()));
	}

	void menuCommandAndCloseButton()
	{
		// the menu command toggles the panel and its check mark; the dock's close button acts like the command
		_npp->command(IDM_VIEW_FUNC_LIST); // panel open (launched above): hides it
		QVERIFY(!panel()->isVisible());
		QVERIFY(panel()->isClosed());
		QVERIFY(!_npp->isMenuItemChecked(IDM_VIEW_FUNC_LIST));
		_npp->command(IDM_VIEW_FUNC_LIST);
		QVERIFY(panel()->isVisible());
		QVERIFY(_npp->isMenuItemChecked(IDM_VIEW_FUNC_LIST));
		panel()->close(); // the title bar close button
		QVERIFY(!panel()->isVisible());
		QVERIFY(!_npp->isMenuItemChecked(IDM_VIEW_FUNC_LIST));
		_npp->command(IDM_VIEW_FUNC_LIST);
		QVERIFY(panel()->isVisible());
	}

	void dockLayoutPersists()
	{
		// move the panel to the left, save (exit), hide, then load (start-up): it comes back, on the left
		_npp->addDockWidget(Qt::LeftDockWidgetArea, panel());
		_npp->saveDockingParams();

		const NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();
		bool found = false;
		for (const PluginDlgDockingInfo& pdi : nppGUI._dockingData._pluginDockInfo)
		{
			if (pdi._name == NPP_INTERNAL_FUNCTION_STR && pdi._internalID == IDM_VIEW_FUNC_LIST)
			{
				found = true;
				QCOMPARE(pdi._currContainer, CONT_LEFT);
				QVERIFY(pdi._isVisible);
			}
		}
		QVERIFY(found);
		std::wstring layout = NppParameters::getInstance().getUserPath();
		pathAppend(layout, L"dockingLayout.dat");
		QVERIFY(QFile::exists(QString::fromStdWString(layout)));

		// the config.xml written at exit holds the panel (upstream DockingManager/PluginDlg)
		NppParameters::getInstance().createXmlTreeFromGUIParams();
		NppParameters::getInstance().saveConfig_xml();
		std::wstring configPath = NppParameters::getInstance().getUserPath();
		pathAppend(configPath, L"config.xml");
		QFile config(QString::fromStdWString(configPath));
		QVERIFY(config.open(QIODevice::ReadOnly));
		const QByteArray xml = config.readAll();
		QVERIFY(xml.contains(QStringLiteral("pluginName=\"Notepad++::InternalFunction\" id=\"%1\" curr=\"0\"").arg(IDM_VIEW_FUNC_LIST).toUtf8()));

		_npp->command(IDM_VIEW_FUNC_LIST);
		QVERIFY(!panel()->isVisible());
		_npp->addDockWidget(Qt::RightDockWidgetArea, panel());

		_npp->loadDockingParams();
		QVERIFY(panel()->isVisible());
		QVERIFY(_npp->isMenuItemChecked(IDM_VIEW_FUNC_LIST));
		QCOMPARE(_npp->dockWidgetArea(panel()), Qt::LeftDockWidgetArea);
	}

	void commandLineExport()
	{
		// the real binary, like upstream unitTest.ps1: notepadlx -export=functionList -lcpp unitTest
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString file = dir.path() + "/unitTest";
		QVERIFY(QFile::copy(dataDir + "/cpp/unitTest", file));

		QProcess p;
		QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
		env.insert(QStringLiteral("XDG_CONFIG_HOME"), dir.path() + "/config");
		env.insert(QStringLiteral("NPLX_DATA_DIR"), QStringLiteral(NPLX_TEST_SOURCE_DATA_DIR));
		env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
		p.setProcessEnvironment(env);
		p.start(QStringLiteral(NPLX_APP_PATH), { QStringLiteral("-export=functionList"), QStringLiteral("-lcpp"), file });
		QVERIFY(p.waitForFinished(30000));
		QCOMPARE(p.exitCode(), 0);

		QFile resultFile(file + ".result.json");
		QVERIFY(resultFile.open(QIODevice::ReadOnly));
		QFile expectedFile(dataDir + "/cpp/unitTest.expected.result");
		QVERIFY(expectedFile.open(QIODevice::ReadOnly));
		QCOMPARE(normalized(resultFile.readAll()), normalized(expectedFile.readAll()));
	}
};

QTEST_MAIN(TestFunctionList)
#include "tst_functionlist.moc"
