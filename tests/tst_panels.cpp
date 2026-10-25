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

// Phase 6: the docking panels (Document List, ...). NPLX_SCREENSHOT_DIR=<dir> saves screenshots.

#include <QtTest>
#include <QClipboard>
#include <QComboBox>
#include <QHeaderView>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QTemporaryDir>
#include <QTreeWidget>

#include <cstdlib>

#include "Buffer.h"
#include "DocTabView.h"
#include "Notepad_plus.h"
#include "Parameters.h"
#include "QtUiServices.h"
#include "ScintillaEditView.h"
#include "AnsiCharPanel.h"
#include "DocumentMap.h"
#include "FileBrowser.h"
#include "FindReplaceDlg.h"
#include "ProjectPanel.h"
#include "TaskListDlg.h"
#include "WindowsDlg.h"
#include "resource.h"
#include "ClipboardHistoryPanel.h"
#include "VerticalFileSwitcher.h"
#include "localization.h"
#include "menuCmdID.h"

namespace
{
	class SilentUiServices : public QtUiServices
	{
	public:
		int messageBox(const std::wstring&, const std::wstring&, unsigned int) override { return _answer; }
		int saveOrNotMultiBox(const std::wstring&, const std::wstring&) override { return IDNO; }
		std::wstring getExistingDirectory(const std::wstring&) override { return _folderAnswer; }
		std::vector<std::wstring> getOpenFileNames(const std::wstring&, const std::wstring&, const std::wstring&) override { return _filesAnswer; }
		std::wstring getOpenFileName(const std::wstring&, const std::wstring&, const std::wstring&) override { return _fileAnswer; }
		std::wstring getSaveFileName(const std::wstring&, const std::wstring&, const std::wstring&) override { return _fileAnswer; }
		bool getText(const std::wstring&, const std::wstring&, std::wstring& text) override { text = _textAnswer; return !_textAnswer.empty(); }
		int _answer = IDNO;
		std::wstring _folderAnswer;
		std::vector<std::wstring> _filesAnswer;
		std::wstring _fileAnswer;
		std::wstring _textAnswer;
	};
}

class TestPanels : public QObject
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

	QString writeFile(const QString& name, const QByteArray& content)
	{
		const QString path = _tmp.path() + "/files/" + name;
		QDir().mkpath(QFileInfo(path).path());
		QFile f(path);
		if (!f.open(QIODevice::WriteOnly))
			qFatal("cannot write %s", qPrintable(path));
		f.write(content);
		return path;
	}

	QStringList docListNames(VerticalFileSwitcher* list)
	{
		QStringList names;
		for (int i = 0; i < list->itemCount(); ++i)
			names << list->itemAt(i)->text(0);
		return names;
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

	void documentList()
	{
		const QString a = writeFile("alpha.cpp", "int a;\n");
		const QString b = writeFile("beta.txt", "b\n");
		const QString c = writeFile("sub/gamma.py", "c = 1\n");
		for (const QString& f : { a, b, c })
		{
			BufferID id = _npp->doOpen(f.toStdWString());
			QVERIFY(id != BUFFER_INVALID);
			_npp->switchToFile(id); // upstream fileOpen: doOpen, then switchToFile
		}

		_npp->command(IDM_VIEW_DOCLIST);
		VerticalFileSwitcher* list = _npp->getDocumentListPanel();
		QVERIFY(list);
		QVERIFY(list->isVisible());
		QVERIFY(_npp->isMenuItemChecked(IDM_VIEW_DOCLIST));
		QCOMPARE(_npp->dockWidgetArea(list), Qt::LeftDockWidgetArea);

		// the documents in tab order: name without extension, Ext. column; the current one selected
		const QStringList names = docListNames(list);
		QCOMPARE(names.mid(names.size() - 3), (QStringList{ "alpha", "beta", "gamma" }));
		QTreeWidgetItem* gamma = list->itemAt(list->itemCount() - 1);
		QCOMPARE(gamma->text(1), QStringLiteral(".py"));
		QVERIFY(gamma->isSelected());
		QCOMPARE(gamma->toolTip(0), c);

		// opening and closing documents updates the list
		const QString d = writeFile("delta.md", "# d\n");
		BufferID dId = _npp->doOpen(d.toStdWString());
		QVERIFY(dId != BUFFER_INVALID);
		_npp->switchToFile(dId);
		QCOMPARE(docListNames(list).last(), QStringLiteral("delta"));
		QVERIFY(list->itemAt(list->itemCount() - 1)->isSelected());
		_npp->command(IDM_FILE_CLOSE);
		QVERIFY(!docListNames(list).contains(QStringLiteral("delta")));

		// a click activates the document
		const int alphaIndex = static_cast<int>(docListNames(list).indexOf(QStringLiteral("alpha")));
		QTreeWidgetItem* alphaItem = list->itemAt(alphaIndex);
		const QRect rect = list->listView()->visualItemRect(alphaItem);
		QTest::mouseClick(list->listView()->viewport(), Qt::LeftButton, Qt::NoModifier, rect.center());
		QCOMPARE(QString::fromWCharArray(view()->getCurrentBuffer()->getFileName()), QStringLiteral("alpha.cpp"));
		QVERIFY(alphaItem->isSelected());

		// switching tabs selects the document
		_npp->command(IDM_VIEW_TAB_NEXT);
		QVERIFY(list->itemAt(alphaIndex + 1)->isSelected());

		// the dirty state shows (the icon follows the tab icons)
		view()->execute(SCI_APPENDTEXT, 1, reinterpret_cast<LPARAM>("x"));
		QCOMPARE(list->statusAt(alphaIndex + 1)->_status, static_cast<int>(tb_unsaved));

		// tab order changes follow (when the list is not sorted)
		_npp->command(IDM_VIEW_TAB_MOVEBACKWARD);
		QCOMPARE(docListNames(list).at(alphaIndex), QStringLiteral("beta"));
		_npp->command(IDM_VIEW_TAB_MOVEFORWARD);

		// sorting by a column: up, down, none (tab order)
		list->columnClick(0);
		QStringList sorted = docListNames(list);
		QStringList expected = sorted;
		std::sort(expected.begin(), expected.end());
		QCOMPARE(sorted, expected);
		list->columnClick(0);
		std::sort(expected.begin(), expected.end(), std::greater<QString>());
		QCOMPARE(docListNames(list), expected);
		list->columnClick(0);
		QCOMPARE(docListNames(list).mid(docListNames(list).size() - 3), (QStringList{ "alpha", "beta", "gamma" }));

		// columns: the Path column on, the Ext. column off
		QCOMPARE(list->listView()->columnCount(), 2);
		NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();
		nppGUI._fileSwitcherWithoutPathColumn = false;
		nppGUI._fileSwitcherWithoutExtColumn = true;
		list->reload();
		QCOMPARE(list->listView()->columnCount(), 2);
		QTreeWidgetItem* gammaRow = list->itemAt(static_cast<int>(docListNames(list).indexOf(QStringLiteral("gamma.py"))));
		QCOMPARE(gammaRow->text(1), QFileInfo(c).path() + "/");
		nppGUI._fileSwitcherWithoutPathColumn = true;
		nppGUI._fileSwitcherWithoutExtColumn = false;
		list->reload();

		// several selected: close the selected files
		list->listView()->clearSelection();
		const QStringList before = docListNames(list);
		list->itemAt(static_cast<int>(before.indexOf("beta")))->setSelected(true);
		list->itemAt(static_cast<int>(before.indexOf("gamma")))->setSelected(true);
		QCOMPARE(list->nbSelectedFiles(), 2);
		_npp->command(IDM_DOCLIST_COPYNAMES);
		QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("beta.txt\ngamma.py\n"));
		_npp->command(IDM_DOCLIST_FILESCLOSE);
		QVERIFY(!docListNames(list).contains("beta"));
		QVERIFY(!docListNames(list).contains("gamma"));
		QVERIFY(docListNames(list).contains("alpha"));

		screenshot(_npp.get(), QStringLiteral("panels-doclist.png"));

		// the menu command hides it
		_npp->command(IDM_VIEW_DOCLIST);
		QVERIFY(!list->isVisible());
		QVERIFY(!_npp->isMenuItemChecked(IDM_VIEW_DOCLIST));
	}

	void documentListGroupsViews()
	{
		_npp->command(IDM_VIEW_DOCLIST);
		VerticalFileSwitcher* list = _npp->getDocumentListPanel();
		const QString e = writeFile("epsilon.c", "int e;\n");
		BufferID eId = _npp->doOpen(e.toStdWString());
		QVERIFY(eId != BUFFER_INVALID);
		_npp->switchToFile(eId);
		_npp->command(IDM_VIEW_GOTO_ANOTHER_VIEW); // epsilon to the second view
		list->reload();
		// the second view's documents come last, after a separator row
		const TaskLstFnStatus* last = list->statusAt(list->itemCount() - 1);
		QCOMPARE(last->_iView, static_cast<int>(SUB_VIEW));
		QCOMPARE(list->listView()->topLevelItemCount(), list->itemCount() + 1);
		_npp->command(IDM_FILE_CLOSE);
		list->reload();
		QCOMPARE(list->listView()->topLevelItemCount(), list->itemCount());
	}

	void characterPanel()
	{
		_npp->command(IDM_FILE_NEW);
		_npp->command(IDM_EDIT_CHAR_PANEL);
		AnsiCharPanel* panel = _npp->getAnsiCharPanel();
		QVERIFY(panel && panel->isVisible());
		QVERIFY(_npp->isMenuItemChecked(IDM_EDIT_CHAR_PANEL));
		QTreeWidget* list = panel->listView();
		QCOMPARE(list->topLevelItemCount(), 256);
		QCOMPARE(list->columnCount(), 6);
		// upstream values: 65 = 'A', 10 = LF, 38 = &amp;, 128 = euro sign (Windows-1252)
		QCOMPARE(list->topLevelItem(65)->text(1), QStringLiteral("41"));
		QCOMPARE(list->topLevelItem(65)->text(2), QStringLiteral("A"));
		QCOMPARE(list->topLevelItem(65)->text(4), QStringLiteral("&#65;"));
		QCOMPARE(list->topLevelItem(10)->text(2), QStringLiteral("LF"));
		QCOMPARE(list->topLevelItem(38)->text(3), QStringLiteral("&amp;"));
		QCOMPARE(list->topLevelItem(128)->text(2), QString(QChar(0x20AC)));
		QCOMPARE(list->topLevelItem(128)->text(4), QStringLiteral("&#8364;"));
		QCOMPARE(list->topLevelItem(233)->text(2), QStringLiteral("é"));

		// double-click: the Character column inserts the character (UTF-8 document), the others their text
		panel->insertChar(233);
		panel->insertChar(65);
		panel->insertString(L"&amp;");
		char text[32] = {};
		view()->execute(SCI_GETTEXT, sizeof(text), reinterpret_cast<LPARAM>(text));
		QCOMPARE(QString::fromUtf8(text), QStringLiteral("éA&amp;"));

		_npp->command(IDM_EDIT_CHAR_PANEL);
		QVERIFY(!panel->isVisible());
	}

	void clipboardHistory()
	{
		_npp->command(IDM_FILE_NEW);
		_npp->command(IDM_EDIT_CLIPBOARDHISTORY_PANEL);
		ClipboardHistoryPanel* panel = _npp->getClipboardHistoryPanel();
		QVERIFY(panel && panel->isVisible());
		const size_t before = panel->nbItems();

		// every clipboard change is recorded, the newest first, without duplicates
		QGuiApplication::clipboard()->setText(QStringLiteral("first"));
		QGuiApplication::clipboard()->setText(QStringLiteral("second line\nnext"));
		QTRY_COMPARE(panel->nbItems(), before + 2);
		QCOMPARE(panel->listBox()->item(0)->text(), QStringLiteral("second line next"));
		QCOMPARE(panel->listBox()->item(1)->text(), QStringLiteral("first"));
		QGuiApplication::clipboard()->setText(QStringLiteral("first"));
		QTRY_COMPARE(panel->listBox()->item(0)->text(), QStringLiteral("first"));
		QCOMPARE(panel->nbItems(), before + 2);

		// long texts are cut for display
		QGuiApplication::clipboard()->setText(QString(200, QLatin1Char('x')));
		QTRY_COMPARE(panel->nbItems(), before + 3);
		QVERIFY(panel->listBox()->item(0)->text().endsWith(QStringLiteral("...")));
		QCOMPARE(panel->listBox()->item(0)->text().size(), 64);

		// activating an entry pastes it in place of the selection
		view()->setText("[sel]");
		view()->execute(SCI_SETSEL, 1, 4);
		panel->pasteItem(2); // "second line\nnext"
		char text[64] = {};
		view()->execute(SCI_GETTEXT, sizeof(text), reinterpret_cast<LPARAM>(text));
		QCOMPARE(QString::fromUtf8(text), QStringLiteral("[second line\nnext]"));

		// an encoding conversion uses the clipboard without recording it
		const size_t count = panel->nbItems();
		view()->setText("conversion text");
		_npp->command(IDM_FORMAT_CONV2_UTF_8);
		QCoreApplication::processEvents();
		for (int i = 0; i < panel->listBox()->count(); ++i)
			QVERIFY(panel->listBox()->item(i)->text() != QStringLiteral("conversion text"));
		QVERIFY(panel->nbItems() <= count + 1);

		screenshot(_npp.get(), QStringLiteral("panels-right.png"));
	}

	void documentMap()
	{
		_npp->command(IDM_FILE_NEW);
		QByteArray text;
		for (int i = 0; i < 600; ++i)
			text += "int function" + QByteArray::number(i) + "() { return " + QByteArray::number(i) + "; }\n";
		view()->setText(text.constData());
		_npp->command(IDM_LANG_CPP);
		view()->execute(SCI_GOTOLINE, 0);

		_npp->command(IDM_VIEW_DOC_MAP);
		DocumentMap* map = _npp->getDocumentMap();
		QVERIFY(map && map->isVisible());
		QVERIFY(_npp->isMenuItemChecked(IDM_VIEW_DOC_MAP));
		ScintillaEditView* mapView = map->mapView();

		// the map shows the current document, zoomed out, with the same lexer
		QCOMPARE(mapView->execute(SCI_GETDOCPOINTER), view()->execute(SCI_GETDOCPOINTER));
		QCOMPARE(mapView->execute(SCI_GETZOOM), -10);
		char lexer[32] = {};
		mapView->execute(SCI_GETLEXERLANGUAGE, 0, reinterpret_cast<LPARAM>(lexer));
		QCOMPARE(QString::fromLatin1(lexer), QStringLiteral("cpp"));

		// the view zone marks the editor's visible part
		QCoreApplication::processEvents();
		map->scrollMap();
		ViewZoneDlg* zone = map->viewZone();
		const long top0 = zone->higherY();
		QVERIFY(zone->lowerY() > top0);
		QCOMPARE(zone->geometry(), mapView->geometry());

		screenshot(_npp.get(), QStringLiteral("panels-docmap.png"));

		// the editor scrolls: the zone follows
		view()->execute(SCI_LINESCROLL, 0, 200);
		map->scrollMap();
		QVERIFY(zone->higherY() != top0 || mapView->execute(SCI_GETFIRSTVISIBLELINE) > 0);
		const auto firstLine = view()->execute(SCI_GETFIRSTVISIBLELINE);
		QCOMPARE(firstLine, 200);

		// a click in the map scrolls the editor (the zone centre moves to the click)
		map->mouseClicked(zone->getCurrentCenterPosY() + 20 * static_cast<int>(mapView->execute(SCI_TEXTHEIGHT, 0)));
		QCOMPARE(view()->execute(SCI_GETFIRSTVISIBLELINE), firstLine + 20);

		// arrow / page keys on the zone scroll the editor
		QTest::keyClick(zone, Qt::Key_Down);
		QCOMPARE(view()->execute(SCI_GETFIRSTVISIBLELINE), firstLine + 21);

		// folding follows
		view()->setText("void f()\n{\n\tint a;\n\tint b;\n}\nvoid g()\n{\n}\n");
		_npp->command(IDM_VIEW_FOLDALL);
		// (the C++ fold header is the "{" line)
		QVERIFY(!view()->execute(SCI_GETFOLDEXPANDED, 1));
		QVERIFY(!mapView->execute(SCI_GETFOLDEXPANDED, 1));
		_npp->command(IDM_VIEW_UNFOLDALL);
		QVERIFY(mapView->execute(SCI_GETFOLDEXPANDED, 1));

		// wrapping: the map wraps at the editor width ratio
		_npp->command(IDM_VIEW_WRAP);
		QVERIFY(mapView->isWrap());
		_npp->command(IDM_VIEW_WRAP);
		QVERIFY(!mapView->isWrap());

		// another document: the map follows
		_npp->command(IDM_FILE_NEW);
		QCOMPARE(mapView->execute(SCI_GETDOCPOINTER), view()->execute(SCI_GETDOCPOINTER));

		_npp->command(IDM_VIEW_DOC_MAP);
		QVERIFY(!map->isVisible());
	}

	void folderAsWorkspace()
	{
		const QString root = _tmp.path() + "/workspace";
		writeFile("../workspace/src/main.cpp", "int main() {}\n");
		writeFile("../workspace/src/util.h", "#pragma once\n");
		writeFile("../workspace/README.md", "# readme\n");
		writeFile("../workspace/Zfile.txt", "z\n");
		writeFile("../workspace/.git/config", "[core]\n");
		writeFile("../workspace/docs/guide.txt", "guide\n");

		_npp->launchFileBrowser({ root.toStdWString() }, L"");
		FileBrowser* fb = _npp->getFileBrowser();
		QVERIFY(fb && fb->isVisible());
		QVERIFY(_npp->isMenuItemChecked(IDM_VIEW_FILEBROWSER));
		QCOMPARE(_npp->dockWidgetArea(fb), Qt::LeftDockWidgetArea);
		TreeView* tree = fb->treeView();

		// the root, then folders first and names case-insensitively; hidden (dot) folders left out
		HTREEITEM rootItem = tree->getRoot();
		QCOMPARE(rootItem->text(0), QStringLiteral("workspace"));
		QCOMPARE(fb->getNodeType(rootItem), browserNodeType_root);
		QStringList children;
		for (HTREEITEM i = tree->getChildFrom(rootItem); i; i = tree->getNextSibling(i))
			children << i->text(0);
		QCOMPARE(children, (QStringList{ "docs", "src", "README.md", "Zfile.txt" }));
		HTREEITEM src = tree->searchSubItemByName(L"src", rootItem);
		QCOMPARE(fb->getNodeType(src), browserNodeType_folder);
		QCOMPARE(QString::fromStdWString(fb->getNodePath(src->child(0))), root + "/src/main.cpp");
		QCOMPARE(fb->getRoots(), std::vector<std::wstring>{ root.toStdWString() });

		// "Locate current file": the current document's item
		BufferID id = _npp->doOpen((root + "/src/util.h").toStdWString());
		_npp->switchToFile(id);
		QVERIFY(fb->selectCurrentEditingFile());
		QCOMPARE(QString::fromStdWString(fb->getSelectedItemPath()), root + "/src/util.h");
		QVERIFY(src->isExpanded() && rootItem->isExpanded()); // revealed

		// open a file from the tree
		tree->selectItem(tree->searchSubItemByName(L"README.md", rootItem));
		fb->openSelectFile();
		QTRY_COMPARE(QString::fromWCharArray(view()->getCurrentBuffer()->getFileName()), QStringLiteral("README.md"));

		// copy path
		tree->selectItem(src);
		fb->popupMenuCmd(IDM_FILEBROWSER_COPYPATH);
		QCOMPARE(QGuiApplication::clipboard()->text(), root + "/src");

		// the folders are watched: new and deleted files / folders show up
		writeFile("../workspace/src/added.cpp", "\n");
		QTRY_VERIFY(tree->searchSubItemByName(L"added.cpp", src));
		QDir(root).mkpath("newdir/inner");
		writeFile("../workspace/newdir/inner/deep.txt", "\n");
		QTRY_VERIFY(tree->searchSubItemByName(L"newdir", rootItem));
		HTREEITEM newdir = tree->searchSubItemByName(L"newdir", rootItem);
		QTRY_VERIFY(newdir->childCount() == 1 && tree->searchSubItemByName(L"deep.txt", newdir->child(0)));
		QFile::remove(root + "/Zfile.txt");
		QTRY_VERIFY(!tree->searchSubItemByName(L"Zfile.txt", rootItem));
		// the order is kept: folders first
		QCOMPARE(tree->getChildFrom(rootItem)->text(0), QStringLiteral("docs"));
		QCOMPARE(rootItem->child(2)->text(0), QStringLiteral("src"));

		// the roots and the expanded folders are saved (config.xml, like upstream)
		src->setExpanded(true);
		QVERIFY(_npp->saveFileBrowserParam());
		NppParameters::getInstance().saveConfig_xml();
		std::wstring configPath = NppParameters::getInstance().getUserPath();
		pathAppend(configPath, L"config.xml");
		QFile config(QString::fromStdWString(configPath));
		QVERIFY(config.open(QIODevice::ReadOnly));
		const QString xml = QString::fromUtf8(config.readAll());
		QVERIFY(xml.contains(QStringLiteral("<root foldername=\"%1\"").arg(root)));
		QVERIFY(xml.contains(QStringLiteral("<expanded path=\"%1/src\"").arg(root)));
		QVERIFY(xml.contains(QStringLiteral("latestSelectedItem=")));

		screenshot(_npp.get(), QStringLiteral("panels-faw.png"));

		// a second root through File > Open Folder as Workspace; removing roots
		const QString other = _tmp.path() + "/other";
		writeFile("../other/a.txt", "a\n");
		_ui._folderAnswer = other.toStdWString();
		_npp->command(IDM_FILE_OPENFOLDERASWORKSPACE);
		QCOMPARE(fb->getRoots().size(), size_t(2));
		tree->selectItem(tree->getNextSibling(tree->getRoot()));
		fb->popupMenuCmd(IDM_FILEBROWSER_REMOVEROOTFOLDER);
		QCOMPARE(fb->getRoots().size(), size_t(1));
		fb->popupMenuCmd(IDM_FILEBROWSER_REMOVEALLROOTS);
		QVERIFY(fb->getRoots().empty());

		// open containing folder as workspace
		_npp->command(IDM_FILE_CONTAININGFOLDERASWORKSPACE);
		QCOMPARE(QString::fromStdWString(fb->getRoots().at(0)), root);
		QCOMPARE(QString::fromStdWString(fb->getSelectedItemPath()), root + "/README.md");

		_npp->command(IDM_VIEW_FILEBROWSER);
		QVERIFY(!fb->isVisible());
	}

	void openDocs(const QStringList& names)
	{
		_npp->command(IDM_FILE_CLOSEALL);
		for (const QString& n : names)
		{
			BufferID id = _npp->doOpen(writeFile("win/" + n, n.toUtf8()).toStdWString());
			QVERIFY(id != BUFFER_INVALID);
			_npp->switchToFile(id);
		}
	}

	QStringList currentViewNames()
	{
		QStringList names;
		TaskListInfo tli;
		_npp->taskListInfo(&tli, true);
		for (const TaskLstFnStatus& t : tli._tlfsLst)
			if (t._iView == MAIN_VIEW)
				names << QFileInfo(QString::fromStdWString(t._fn)).fileName();
		return names;
	}

	QString currentName() { return QString::fromWCharArray(view()->getCurrentBuffer()->getFileName()); }

	void windowsDialogList()
	{
		openDocs({ "b10.txt", "a2.cpp", "c1.py", "b9.txt" });
		// the tab bar of the current view (main view)
		DocTabView* mainTab = nullptr;
		for (DocTabView* t : _npp->findChildren<DocTabView*>())
			if (t->nbItem() == 4)
				mainTab = t;
		QVERIFY(mainTab);

		WindowsDlg dlg(_npp.get());
		dlg.init(mainTab, [this](NMWINDLG& nmdlg) { _npp->windowsDlgNotify(nmdlg); });
		dlg.doRefresh(true);
		QTreeWidget* list = dlg.list();
		QCOMPARE(list->topLevelItemCount(), 4);
		QCOMPARE(list->topLevelItem(0)->text(0), QStringLiteral("b10.txt"));
		QCOMPARE(list->topLevelItem(3)->text(3), QStringLiteral("6")); // size
		QVERIFY(list->topLevelItem(3)->isSelected()); // the current document
		QVERIFY(list->headerItem()->text(0).startsWith(QStringLiteral("⇵")));

		// name column: numeric-aware sort (b9 before b10), then reversed
		dlg.columnClick(0);
		QStringList names;
		for (int i = 0; i < 4; ++i) names << list->topLevelItem(i)->text(0);
		QCOMPARE(names, (QStringList{ "a2.cpp", "b9.txt", "b10.txt", "c1.py" }));
		QVERIFY(list->headerItem()->text(0).startsWith(QStringLiteral("▽")));
		dlg.columnClick(0);
		names.clear();
		for (int i = 0; i < 4; ++i) names << list->topLevelItem(i)->text(0);
		QCOMPARE(names, (QStringList{ "c1.py", "b10.txt", "b9.txt", "a2.cpp" }));

		// "Sort tabs": the tabs follow the list
		dlg.columnClick(2); // type
		dlg.columnClick(0); // name ascending again
		dlg.sortTabs();
		QCOMPARE(currentViewNames(), (QStringList{ "a2.cpp", "b9.txt", "b10.txt", "c1.py" }));

		// close the selected windows
		list->clearSelection();
		list->topLevelItem(1)->setSelected(true);
		list->topLevelItem(2)->setSelected(true);
		dlg.doClose();
		QCOMPARE(currentViewNames(), (QStringList{ "a2.cpp", "c1.py" }));
		QCOMPARE(list->topLevelItemCount(), 2);

		// activate one
		list->clearSelection();
		list->topLevelItem(0)->setSelected(true);
		dlg.activateCurrent();
		QCOMPARE(currentName(), QStringLiteral("a2.cpp"));
	}

	void windowMenuSortAndList()
	{
		openDocs({ "b10.txt", "a2.cpp", "c1.py", "b9.txt" });
		_npp->command(IDM_WINDOW_SORT_FN_DSC);
		QCOMPARE(currentViewNames(), (QStringList{ "c1.py", "b10.txt", "b9.txt", "a2.cpp" }));
		QCOMPARE(currentName(), QStringLiteral("b9.txt")); // the active document stays active
		_npp->command(IDM_WINDOW_SORT_FN_ASC);
		QCOMPARE(currentViewNames(), (QStringList{ "a2.cpp", "b9.txt", "b10.txt", "c1.py" }));

		// the Window menu lists the documents of the current view
		QMenu* windowMenu = nullptr;
		for (QAction* a : _npp->menuBar()->actions())
			if (a->menu() && a->text().remove(QLatin1Char('&')) == QStringLiteral("Window"))
				windowMenu = a->menu();
		QVERIFY(windowMenu);
		_npp->initWindowsMenu(windowMenu);
		QList<QAction*> docs;
		for (QAction* a : windowMenu->actions())
			if (a->data().toInt() >= IDM_WINDOW_MRU_FIRST && a->data().toInt() <= IDM_WINDOW_MRU_LIMIT)
				docs << a;
		QCOMPARE(docs.size(), 4);
		QVERIFY(docs[0]->text().contains(QStringLiteral("a2.cpp")));
		QVERIFY(docs[1]->isChecked()); // b9.txt is current
		docs[3]->trigger();
		QCOMPARE(currentName(), QStringLiteral("c1.py"));
	}

	void ctrlTab()
	{
		openDocs({ "one.txt", "two.txt", "three.txt" });
		NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();

		// a hidden action carries the shortcut of the commands without a menu item
		QAction* nextDoc = nullptr;
		for (QAction* a : _npp->actions())
			if (a->data().toInt() == IDC_NEXT_DOC)
				nextDoc = a;
		QVERIFY(nextDoc);
		QVERIFY(nextDoc->shortcuts().contains(QKeySequence(Qt::CTRL | Qt::Key_Tab)));

		// without the document switcher: the next / previous tab
		nppGUI._doTaskList = false;
		_npp->command(IDC_NEXT_DOC);
		QCOMPARE(currentName(), QStringLiteral("one.txt")); // wraps around
		_npp->command(IDC_PREV_DOC);
		QCOMPARE(currentName(), QStringLiteral("three.txt"));

		// the document switcher: most recently used first; a quick Ctrl+Tab (Ctrl not held when it shows) switches
		// to the previous document
		nppGUI._doTaskList = true;
		_npp->switchToFile(MainFileManager.getBufferFromName((_tmp.path() + "/files/win/one.txt").toStdWString().c_str()));
		_npp->switchToFile(MainFileManager.getBufferFromName((_tmp.path() + "/files/win/two.txt").toStdWString().c_str()));
		_npp->command(IDC_NEXT_DOC);
		QCOMPARE(currentName(), QStringLiteral("one.txt"));

		// the switcher itself: the list in MRU order, the selection moves and wraps, picking activates
		TaskListDlg tld(_npp.get(), dirDown, [this](TaskListInfo& tli) { _npp->taskListInfo(&tli, false); },
			[this](int v, int index) { _npp->switchEditViewTo(v); _npp->activateDoc(index); });
		QCOMPARE(tld.list()->count(), 3);
		QVERIFY(tld.list()->item(0)->text().endsWith(QStringLiteral("one.txt"))); // the current one first
		QVERIFY(tld.list()->item(1)->text().endsWith(QStringLiteral("two.txt")));
		QCOMPARE(tld.currentIndex(), 1);
		tld.moveSelection(1);
		tld.moveSelection(1);
		QCOMPARE(tld.currentIndex(), 0); // wrapped
		tld.moveSelection(-1);
		QVERIFY(tld.list()->item(tld.currentIndex())->text().endsWith(QStringLiteral("three.txt")));
		tld.pick();
		QCOMPARE(currentName(), QStringLiteral("three.txt"));
		nppGUI._doTaskList = true;
	}

	void projectPanels()
	{
		const QString dir = _tmp.path() + "/proj";
		writeFile("../proj/src/app.cpp", "int needle = 1;\n");
		writeFile("../proj/src/lib/util.cpp", "// needle here\n");
		writeFile("../proj/docs/readme.txt", "no match\n");
		writeFile("../proj/.hidden/x.txt", "needle\n");
		writeFile("../proj/single.h", "#define needle\n");

		_npp->command(IDM_VIEW_PROJECT_PANEL_1);
		ProjectPanel* pp = _npp->getProjectPanel(0);
		QVERIFY(pp && pp->isVisible());
		QVERIFY(_npp->isMenuItemChecked(IDM_VIEW_PROJECT_PANEL_1));
		QCOMPARE(pp->windowTitle(), QStringLiteral("Project Panel 1"));
		TreeView* tree = pp->treeView();
		HTREEITEM root = tree->getRoot();
		QCOMPARE(root->text(0), QStringLiteral("Workspace"));
		QCOMPARE(pp->getNodeType(root), nodeType_root);
		QVERIFY(!pp->isDirty());

		// Add New Project, renamed in place (TVN_ENDLABELEDIT)
		tree->selectItem(root);
		pp->popupMenuCmd(IDM_PROJECT_NEWPROJECT);
		HTREEITEM project = tree->getChildFrom(root);
		QVERIFY(project);
		QCOMPARE(pp->getNodeType(project), nodeType_project);
		QVERIFY(pp->isDirty());
		QVERIFY(tree->isEditing());
		project->setText(0, QStringLiteral("MyProject")); // the editor commits
		QCOMPARE(project->text(0), QStringLiteral("MyProject"));

		// Add Files from Directory (recursive, hidden folders ignored)
		tree->selectItem(project);
		_ui._folderAnswer = dir.toStdWString();
		pp->popupMenuCmd(IDM_PROJECT_ADDFILESRECUSIVELY);
		QStringList top;
		for (HTREEITEM i = tree->getChildFrom(project); i; i = tree->getNextSibling(i))
			top << i->text(0);
		QCOMPARE(top, (QStringList{ "docs", "src", "single.h" }));
		HTREEITEM src = tree->searchSubItemByName(L"src", project);
		QCOMPARE(pp->getNodeType(src), nodeType_folder);
		HTREEITEM lib = tree->searchSubItemByName(L"lib", src);
		QVERIFY(lib && tree->searchSubItemByName(L"util.cpp", lib));
		HTREEITEM single = tree->searchSubItemByName(L"single.h", project);
		QCOMPARE(pp->getNodeType(single), nodeType_file);
		QCOMPARE(QString::fromStdWString(tree->getItemParam(single)), dir + "/single.h");

		// Add Files (the file dialog)
		const QString extra = writeFile("../other/extra.txt", "needle\n");
		tree->selectItem(tree->searchSubItemByName(L"docs", project));
		_ui._filesAnswer = { extra.toStdWString() };
		pp->popupMenuCmd(IDM_PROJECT_ADDFILES);
		QVERIFY(tree->searchSubItemByName(L"extra.txt", tree->searchSubItemByName(L"docs", project)));

		// Move up / down
		tree->selectItem(single);
		pp->popupMenuCmd(IDM_PROJECT_MOVEUP);
		QCOMPARE(tree->getChildFrom(project)->child(0) ? project->child(1)->text(0) : QString(), QStringLiteral("single.h"));
		pp->popupMenuCmd(IDM_PROJECT_MOVEDOWN);
		QCOMPARE(project->child(2)->text(0), QStringLiteral("single.h"));

		// Save As: relative paths, the root shows the file name, clean again
		const QString wsFile = dir + "/my.workspace";
		_ui._fileAnswer = wsFile.toStdWString();
		tree->selectItem(root);
		pp->popupMenuCmd(IDM_PROJECT_SAVEASWS);
		QVERIFY(!pp->isDirty());
		QCOMPARE(root->text(0), QStringLiteral("my.workspace"));
		QFile f(wsFile);
		QVERIFY(f.open(QIODevice::ReadOnly));
		const QString xml = QString::fromUtf8(f.readAll());
		QVERIFY(xml.contains(QStringLiteral("<Project name=\"MyProject\">")));
		QVERIFY(xml.contains(QStringLiteral("<Folder name=\"src\">")));
		QVERIFY(xml.contains(QStringLiteral("<File name=\"src/lib/util.cpp\"")));
		QVERIFY(xml.contains(QStringLiteral("<File name=\"single.h\"")));
		QVERIFY(xml.contains(QStringLiteral("<File name=\"") + extra)); // outside the workspace folder: absolute

		// open a file from the tree
		tree->selectItem(single);
		pp->openSelectFile();
		QTRY_COMPARE(QString::fromWCharArray(view()->getCurrentBuffer()->getFileName()), QStringLiteral("single.h"));

		// a workspace written on Windows (backslashes), with a missing file, in panel 2
		const QString winWs = writeFile("../proj/win.workspace",
			"<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n<NotepadPlus>\n<Project name=\"Win\">\n"
			"<Folder name=\"code\"><File name=\"src\\lib\\util.cpp\" /></Folder>\n<File name=\"gone.txt\" />\n</Project>\n</NotepadPlus>\n");
		_npp->command(IDM_VIEW_PROJECT_PANEL_2);
		ProjectPanel* pp2 = _npp->getProjectPanel(1);
		QVERIFY(pp2->openWorkSpace(winWs.toStdWString().c_str(), true));
		HTREEITEM winProject = pp2->treeView()->getChildFrom(pp2->treeView()->getRoot());
		QCOMPARE(winProject->text(0), QStringLiteral("Win"));
		HTREEITEM code = pp2->treeView()->searchSubItemByName(L"code", winProject);
		QCOMPARE(QString::fromStdWString(pp2->treeView()->getItemParam(code->child(0))), dir + "/src/lib/util.cpp");
		HTREEITEM gone = pp2->treeView()->searchSubItemByName(L"gone.txt", winProject);
		QCOMPARE(pp2->treeView()->getItemImage(gone), 6); // INDEX_LEAF_INVALID

		// Find in Projects: panel 1 only
		FindReplaceDlg* frd = _npp->getFindReplaceDlg();
		tree->selectItem(root);
		pp->popupMenuCmd(IDM_PROJECT_FINDINPROJECTSWS);
		QVERIFY(frd->isVisible());
		QVERIFY(frd->isProjectPanel_1());
		QVERIFY(!frd->isProjectPanel_2());
		frd->comboBox(IDFINDWHAT)->setEditText(QStringLiteral("needle"));
		frd->command(IDD_FINDINFILES_FIND_BUTTON);
		Finder* finder = frd->getMainFinder();
		QVERIFY(finder);
		const QString results = QString::fromStdString(finder->getScintView()->getText());
		QVERIFY(results.contains(QStringLiteral("app.cpp")));
		QVERIFY(results.contains(QStringLiteral("util.cpp")));
		QVERIFY(results.contains(QStringLiteral("extra.txt")));
		QVERIFY(!results.contains(QStringLiteral("x.txt"))); // the hidden folder was not added
		frd->hide();

		// remove a file (confirmed), then exit: the workspace path is saved for the panel
		_ui._answer = IDYES;
		tree->selectItem(tree->searchSubItemByName(L"extra.txt", tree->searchSubItemByName(L"docs", project)));
		pp->popupMenuCmd(IDM_PROJECT_DELETEFILE);
		QVERIFY(!tree->searchSubItemByName(L"extra.txt", tree->searchSubItemByName(L"docs", project)));
		QVERIFY(pp->isDirty());
		QVERIFY(_npp->saveProjectPanelsParams()); // asks to save (yes)
		_ui._answer = IDNO;
		QVERIFY(!pp->isDirty());
		QCOMPARE(QString::fromWCharArray(NppParameters::getInstance().getWorkSpaceFilePath(0)), wsFile);

		screenshot(_npp.get(), QStringLiteral("panels-project.png"));

		_npp->command(IDM_VIEW_PROJECT_PANEL_1);
		QVERIFY(!pp->isVisible());
		_npp->command(IDM_VIEW_PROJECT_PANEL_2);
	}

	void closeActivatesMostRecentlyUsed()
	{
		openDocs({ "one.txt", "two.txt", "three.txt" });
		NppParameters::getInstance().getNppGUI()._styleMRU = true;
		_npp->switchToFile(MainFileManager.getBufferFromName((_tmp.path() + "/files/win/one.txt").toStdWString().c_str()));
		_npp->switchToFile(MainFileManager.getBufferFromName((_tmp.path() + "/files/win/three.txt").toStdWString().c_str()));
		_npp->command(IDM_FILE_CLOSE); // three.txt: the previous one used is one.txt (not the neighbour two.txt)
		QCOMPARE(currentName(), QStringLiteral("one.txt"));
	}

	void dropFolderAsWorkspace()
	{
		const QString folder = _tmp.path() + "/dropped";
		writeFile("../dropped/a.txt", "a\n");
		NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();
		nppGUI._isFolderDroppedOpenFiles = false; // upstream default: a dropped folder goes to Folder as Workspace
		_npp->dropFiles({ folder }, -1);
		FileBrowser* fb = _npp->getFileBrowser();
		QVERIFY(fb && fb->isVisible());
		const auto roots = fb->getRoots();
		QVERIFY(std::find(roots.begin(), roots.end(), folder.toStdWString()) != roots.end());
		nppGUI._isFolderDroppedOpenFiles = true;
	}
};

QTEST_MAIN(TestPanels)
#include "tst_panels.moc"
