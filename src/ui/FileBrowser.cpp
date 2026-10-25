// This file is part of NotePadLX, a Linux port of Notepad++.
// Copyright (C)2021 Don HO <don.h@free.fr>
// Modified for NotePadLX (Linux port).
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

#include "FileBrowser.h"

#include <algorithm>
#include <functional>

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMenu>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "Common.h"
#include "DesktopIntegration.h"
#include "NplxStyle.h"
#include "Parameters.h"
#include "UiServices.h"
#include "localization.h"
#include "menuCmdID.h"

using namespace std;

enum ItemIdx
{
	INDEX_OPEN_ROOT,
	INDEX_CLOSE_ROOT,
	INDEX_OPEN_NODE,
	INDEX_CLOSE_NODE,
	INDEX_LEAF
};

namespace
{
	constexpr wchar_t pathSep = L'/'; // NPLX: was '\\'

	std::vector<std::wstring> split(const std::wstring& string2split, wchar_t sep)
	{
		vector<wstring> splitedStrings;
		size_t len = string2split.length();
		size_t beginPos = 0;
		for (size_t i = 0; i < len + 1; ++i)
		{
			if (i == len || string2split[i] == sep)
			{
				splitedStrings.push_back(string2split.substr(beginPos, i - beginPos));
				beginPos = i + 1;
			}
		}
		return splitedStrings;
	}

	bool isRelatedRootFolder(const std::wstring& relatedRoot, const std::wstring& subFolder)
	{
		if (relatedRoot.empty())
			return false;

		if (subFolder.empty())
			return false;

		size_t pos = subFolder.find(relatedRoot);
		if (pos != 0) // pos == 0 is the necessary condition, but not enough
			return false;

		vector<wstring> relatedRootArray = split(relatedRoot, pathSep);
		vector<wstring> subFolderArray = split(subFolder, pathSep);

		size_t index2Compare = relatedRootArray.size() - 1;
		return index2Compare < subFolderArray.size() && relatedRootArray[index2Compare] == subFolderArray[index2Compare];
	}

	std::wstring joinPath(const std::wstring& dir, const std::wstring& name)
	{
		if (!dir.empty() && dir.back() == pathSep)
			return dir + name;
		return dir + pathSep + name;
	}

	// NPLX: the entries of a folder as upstream's scan sees them: no "." / "..", hidden (dot) folders skipped,
	// symbolic links skipped unless allowed (upstream stops the whole folder at the first one, a Windows junction
	// guard; here only the link is left out)
	struct DirEntries { std::vector<std::wstring> _folders; std::vector<std::wstring> _files; };

	DirEntries readDir(const std::wstring& dir, bool isInHiddenDir)
	{
		DirEntries entries;
		const bool isSymlinkAllowed = NppParameters::getInstance().getNppGUI()._isFawSymlinkAllowed;
		const QFileInfoList infos = QDir(QString::fromStdWString(dir)).entryInfoList(
			QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::NoSort);
		for (const QFileInfo& fi : infos)
		{
			if (fi.isSymLink() && !isSymlinkAllowed)
				continue;
			const std::wstring name = fi.fileName().toStdWString();
			if (fi.isDir())
			{
				if (!isInHiddenDir && fi.isHidden())
					continue;
				entries._folders.push_back(name);
			}
			else
				entries._files.push_back(name);
		}
		return entries;
	}
}

// ============================================================================
// FolderUpdater
// ============================================================================

FolderUpdater::~FolderUpdater()
{
	stopWatcher();
}

void FolderUpdater::startWatcher()
{
	// upstream: a thread waiting on ReadDirectoryChanges (FILE_NOTIFY_CHANGE_CREATION | DIR_NAME | FILE_NAME)
	_watcher = new QFileSystemWatcher();
	_timer = new QTimer();
	_timer->setSingleShot(true);
	_timer->setInterval(150);

	std::function<void(const FolderInfo&, const std::wstring&)> addAll = [this, &addAll](const FolderInfo& fi, const std::wstring& path) {
		watchFolder(path);
		for (const FolderInfo& sub : fi._subFolders)
			addAll(sub, joinPath(path, sub._name));
	};
	addAll(_rootFolder, _rootFolder._rootPath);

	QObject::connect(_watcher, &QFileSystemWatcher::directoryChanged, _timer, [this](const QString& path) {
		_changedFolders.insert(path.toStdWString());
		_timer->start();
	});
	QObject::connect(_timer, &QTimer::timeout, _timer, [this]() {
		const auto changed = _changedFolders;
		_changedFolders.clear();
		for (const std::wstring& folder : changed)
			_pFileBrowser->folderChanged(_rootFolder._rootPath, folder);
	});
}

void FolderUpdater::stopWatcher()
{
	delete _watcher;
	_watcher = nullptr;
	delete _timer;
	_timer = nullptr;
}

void FolderUpdater::watchFolder(const std::wstring& path)
{
	if (_watcher)
		_watcher->addPath(QString::fromStdWString(path));
}

// ============================================================================
// FileBrowser
// ============================================================================

FileBrowser::FileBrowser(QWidget* parent)
	: DockingPanel(QString::fromWCharArray(FB_PANELTITLE), IDM_VIEW_FILEBROWSER, QStringLiteral("FolderAsWorkspace"), parent)
{
	// upstream WM_INITDIALOG: the toolbar (locate current file, fold all, expand all) above the tree view
	NppParameters& nppParam = NppParameters::getInstance();
	NativeLangSpeaker *pNativeSpeaker = nppParam.getNativeLangSpeaker();
	if (pNativeSpeaker)
	{
		setWindowTitle(QString::fromStdWString(pNativeSpeaker->getAttrNameStr(FB_PANELTITLE, FOLDERASWORKSPACE_NODE, "PanelTitle")));
		_expandAllFolders = pNativeSpeaker->getAttrNameStr(_expandAllFolders.c_str(), FOLDERASWORKSPACE_NODE, "ExpandAllFoldersTip");
		_collapseAllFolders = pNativeSpeaker->getAttrNameStr(_collapseAllFolders.c_str(), FOLDERASWORKSPACE_NODE, "CollapseAllFoldersTip");
		_locateCurrentFile = pNativeSpeaker->getAttrNameStr(_locateCurrentFile.c_str(), FOLDERASWORKSPACE_NODE, "LocateCurrentFileTip");
	}

	auto* content = new QWidget(this);
	auto* layout = new QVBoxLayout(content);
	layout->setContentsMargins(4, 4, 4, 4);
	layout->setSpacing(4);

	auto* toolbar = new QHBoxLayout();
	toolbar->setSpacing(2);
	toolbar->addStretch(1); // upstream: the toolbar is right-aligned (WS_EX_LAYOUTRTL)
	auto makeButton = [content, toolbar](const char* icon, const std::wstring& tip) {
		auto* b = new QToolButton(content);
		b->setIcon(nplx::style::icon(QString::fromLatin1(icon)));
		b->setIconSize(QSize(18, 18));
		b->setAutoRaise(true);
		b->setToolTip(QString::fromStdWString(tip));
		toolbar->addWidget(b);
		return b;
	};
	QToolButton* aimFile = makeButton("locate", _locateCurrentFile);     // FB_CMD_AIMFILE
	QToolButton* foldAll = makeButton("collapse_all", _collapseAllFolders); // FB_CMD_FOLDALL
	QToolButton* expandAll = makeButton("expand_all", _expandAllFolders);   // FB_CMD_EXPANDALL
	layout->addLayout(toolbar);

	_treeView = new TreeView(content);
	_treeView->setImageList({ nplx::style::icon(QStringLiteral("folder_root")), nplx::style::icon(QStringLiteral("folder_root")),
		nplx::style::icon(QStringLiteral("folder_open")), nplx::style::icon(QStringLiteral("folder")), nplx::style::icon(QStringLiteral("file_plain")) });
	_treeView->makeLabelEditable(false);
	_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
	_treeView->installEventFilter(this);
	layout->addWidget(_treeView, 1);
	setWidget(content);

	initPopupMenus();

	connect(aimFile, &QToolButton::clicked, this, [this]() { selectCurrentEditingFile(); });
	connect(foldAll, &QToolButton::clicked, this, [this]() { _treeView->foldAll(); });
	connect(expandAll, &QToolButton::clicked, this, [this]() { _treeView->expandAll(); });
	connect(_treeView, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) { showContextMenu(pos); });

	// upstream NM_DBLCLK: open a file (a folder toggles)
	connect(_treeView, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
		if (getNodeType(item) == browserNodeType_file)
			openSelectFile();
		else
			_treeView->toggleExpandCollapse(item);
	});

	// upstream TVN_ITEMEXPANDED: the open / closed folder images
	connect(_treeView, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* item) {
		const BrowserNodeType type = getNodeType(item);
		if (type == browserNodeType_folder)
			_treeView->setItemImage(item, INDEX_OPEN_NODE);
		else if (type == browserNodeType_root)
			_treeView->setItemImage(item, INDEX_OPEN_ROOT);
	});
	connect(_treeView, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* item) {
		const BrowserNodeType type = getNodeType(item);
		if (type == browserNodeType_folder)
			_treeView->setItemImage(item, INDEX_CLOSE_NODE);
		else if (type == browserNodeType_root)
			_treeView->setItemImage(item, INDEX_CLOSE_ROOT);
	});
}

FileBrowser::~FileBrowser()
{
	for (const auto folder : _folderUpdaters)
		delete folder;
}

void FileBrowser::initPopupMenus()
{
	NativeLangSpeaker* pNativeSpeaker = NppParameters::getInstance().getNativeLangSpeaker();
	auto str = [pNativeSpeaker](int id, const wchar_t* defaultStr) {
		return QString::fromStdWString(pNativeSpeaker ? pNativeSpeaker->getDlgLangMenuStr(FOLDERASWORKSPACE_NODE, nullptr, id, defaultStr) : defaultStr);
	};

	const QString addRoot = str(IDM_FILEBROWSER_ADDROOT, FB_ADDROOT);
	const QString removeAllRoot = str(IDM_FILEBROWSER_REMOVEALLROOTS, FB_REMOVEALLROOTS);
	const QString removeRootFolder = str(IDM_FILEBROWSER_REMOVEROOTFOLDER, FB_REMOVEROOTFOLDER);
	const QString copyPath = str(IDM_FILEBROWSER_COPYPATH, FB_COPYPATH);
	const QString copyFileName = str(IDM_FILEBROWSER_COPYFILENAME, FB_COPYFILENAME);
	const QString findInFile = str(IDM_FILEBROWSER_FINDINFILES, FB_FINDINFILES);
	// NPLX: "Explorer here" opens the file manager, "CMD here" a terminal; PowerShell here is Windows only
	const QString explorerHere = QStringLiteral("File manager here");
	const QString cmdHere = QStringLiteral("Terminal here");
	const QString openInNpp = str(IDM_FILEBROWSER_OPENINNPP, FB_OPENINNPP);
	const QString shellExecute = str(IDM_FILEBROWSER_SHELLEXECUTE, FB_SHELLEXECUTE);

	auto add = [this](QMenu* menu, int id, const QString& text) {
		if (id == -1)
		{
			menu->addSeparator();
			return;
		}
		QAction* a = menu->addAction(text);
		connect(a, &QAction::triggered, this, [this, id]() { popupMenuCmd(id); });
	};

	// upstream inserts each item at position 0: the menus read bottom-up there
	_hGlobalMenu = new QMenu(this);
	add(_hGlobalMenu, IDM_FILEBROWSER_REMOVEALLROOTS, removeAllRoot);
	add(_hGlobalMenu, IDM_FILEBROWSER_ADDROOT, addRoot);

	_hRootMenu = new QMenu(this);
	add(_hRootMenu, IDM_FILEBROWSER_CMDHERE, cmdHere);
	add(_hRootMenu, IDM_FILEBROWSER_EXPLORERHERE, explorerHere);
	add(_hRootMenu, -1, QString());
	add(_hRootMenu, IDM_FILEBROWSER_FINDINFILES, findInFile);
	add(_hRootMenu, IDM_FILEBROWSER_COPYPATH, copyPath);
	add(_hRootMenu, -1, QString());
	add(_hRootMenu, IDM_FILEBROWSER_REMOVEROOTFOLDER, removeRootFolder);

	_hFolderMenu = new QMenu(this);
	add(_hFolderMenu, IDM_FILEBROWSER_CMDHERE, cmdHere);
	add(_hFolderMenu, IDM_FILEBROWSER_EXPLORERHERE, explorerHere);
	add(_hFolderMenu, -1, QString());
	add(_hFolderMenu, IDM_FILEBROWSER_FINDINFILES, findInFile);
	add(_hFolderMenu, IDM_FILEBROWSER_COPYPATH, copyPath);

	_hFileMenu = new QMenu(this);
	add(_hFileMenu, IDM_FILEBROWSER_CMDHERE, cmdHere);
	add(_hFileMenu, IDM_FILEBROWSER_EXPLORERHERE, explorerHere);
	add(_hFileMenu, -1, QString());
	add(_hFileMenu, IDM_FILEBROWSER_SHELLEXECUTE, shellExecute);
	add(_hFileMenu, IDM_FILEBROWSER_COPYFILENAME, copyFileName);
	add(_hFileMenu, IDM_FILEBROWSER_COPYPATH, copyPath);
	add(_hFileMenu, -1, QString());
	add(_hFileMenu, IDM_FILEBROWSER_OPENINNPP, openInNpp);
}

bool FileBrowser::selectItemFromPath(const wstring& itemPath) const
{
	if (itemPath.empty())
		return false;

	size_t itemPathLen = itemPath.size();

	for (const auto f : _folderUpdaters)
	{
		if (isRelatedRootFolder(f->_rootFolder._rootPath, itemPath))
		{
			wstring rootPath = f->_rootFolder._rootPath;
			size_t rootPathLen = rootPath.size();
			if (rootPathLen > itemPathLen) // It should never happen
				return false;

			vector<wstring> linarPathArray;
			if (rootPathLen == itemPathLen)
			{
				// Do nothing and use empty linarPathArray
			}
			else
			{
				const size_t skip = (rootPath.back() == pathSep) ? 0 : 1; // NPLX: the "/" root
				wstring pathSuffix = itemPath.substr(rootPathLen + skip, itemPathLen - rootPathLen);
				linarPathArray = split(pathSuffix, pathSep);
			}
			HTREEITEM foundItem = findInTree(rootPath, nullptr, linarPathArray);

			if (foundItem)
			{
				_treeView->selectItem(foundItem);
				_treeView->setFocus();
				return true;
			}
		}
	}
	return false;
}

bool FileBrowser::selectCurrentEditingFile() const
{
	// upstream NPPM_GETFULLCURRENTPATH
	const wstring currentDocPathStr = _host._currentFilePath ? _host._currentFilePath() : wstring();
	return selectItemFromPath(currentDocPathStr);
}

wstring FileBrowser::getNodePath(HTREEITEM node) const
{
	if (!node) return L"";

	vector<wstring> fullPathArray;
	wstring fullPath;

	// go up until to root, then get the full path
	HTREEITEM parent = node;
	for (; parent != nullptr;)
	{
		wstring folderName = _treeView->getItemDisplayName(parent);

		HTREEITEM temp = _treeView->getParent(parent);
		if (temp == nullptr)
		{
			folderName = _treeView->getItemParam(parent); // the root path
		}
		parent = temp;
		fullPathArray.push_back(folderName);
	}

	for (int i = int(fullPathArray.size()) - 1; i >= 0; --i)
	{
		fullPath += fullPathArray[i];
		if (i != 0 && fullPath.back() != pathSep)
			fullPath += pathSep;
	}

	return fullPath;
}

wstring FileBrowser::getNodeName(HTREEITEM node) const
{
	return node ? _treeView->getItemDisplayName(node) : L"";
}

void FileBrowser::openSelectFile()
{
	// Get the selected item
	HTREEITEM selectedNode = _treeView->getSelection();
	if (!selectedNode) return;

	const wstring selectedNodeFullPath = getNodePath(selectedNode);

	// test the path - if it's a file, open it, otherwise just fold or unfold it
	if (!doesFileExist(selectedNodeFullPath.c_str()))
		return;

	// upstream ::PostMessage(_hParent, NPPM_DOOPEN, ...)
	if (_host._open)
		QMetaObject::invokeMethod(this, [this, selectedNodeFullPath]() { _host._open(selectedNodeFullPath); }, Qt::QueuedConnection);
}

bool FileBrowser::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == _treeView && event->type() == QEvent::KeyPress)
	{
		// upstream TVN_KEYDOWN
		const int key = static_cast<QKeyEvent*>(event)->key();
		HTREEITEM hItem = _treeView->getSelection();
		if (key == Qt::Key_Return || key == Qt::Key_Enter)
		{
			BrowserNodeType nType = getNodeType(hItem);
			if (nType == browserNodeType_file)
				openSelectFile();
			else
				_treeView->toggleExpandCollapse(hItem);
			return true;
		}
		else if (key == Qt::Key_Delete)
		{
			BrowserNodeType nType = getNodeType(hItem);
			if (nType == browserNodeType_root)
				popupMenuCmd(IDM_FILEBROWSER_REMOVEROOTFOLDER);
			return true;
		}
	}
	return DockingPanel::eventFilter(watched, event);
}

BrowserNodeType FileBrowser::getNodeType(HTREEITEM hItem) const
{
	if (_treeView->getItemImage(hItem) == INDEX_LEAF)
	{
		return browserNodeType_file;
	}
	else if (!_treeView->getItemParam(hItem).empty())
	{
		return browserNodeType_root;
	}
	else
	{
		return browserNodeType_folder;
	}
}

void FileBrowser::showContextMenu(const QPoint& pos)
{
	HTREEITEM hItem = _treeView->itemAt(pos);
	const QPoint globalPos = _treeView->viewport()->mapToGlobal(pos);

	if (hItem == nullptr)
	{
		_hGlobalMenu->popup(globalPos);
	}
	else
	{
		_treeView->selectItem(hItem);

		BrowserNodeType nodeType = getNodeType(hItem);
		QMenu* hMenu = nullptr;
		if (nodeType == browserNodeType_root)
			hMenu = _hRootMenu;
		else if (nodeType == browserNodeType_folder)
			hMenu = _hFolderMenu;
		else //nodeType_file
			hMenu = _hFileMenu;

		hMenu->popup(globalPos);
	}
}

void FileBrowser::popupMenuCmd(int cmdID)
{
	// get selected item handle
	HTREEITEM selectedNode = _treeView->getSelection();

	switch (cmdID)
	{
		//
		// FileBrowser menu commands
		//
		case IDM_FILEBROWSER_REMOVEROOTFOLDER:
		{
			if (!selectedNode) return;

			const wstring rootPath = _treeView->getItemParam(selectedNode);
			if (_treeView->getParent(selectedNode) != nullptr || rootPath.empty())
				return;

			size_t nbFolderUpdaters = _folderUpdaters.size();
			for (size_t i = 0; i < nbFolderUpdaters; ++i)
			{
				if (_folderUpdaters[i]->_rootFolder._rootPath == rootPath)
				{
					delete _folderUpdaters[i]; // stops the watcher
					_folderUpdaters.erase(_folderUpdaters.begin() + i);
					_treeView->removeItem(selectedNode);
					break;
				}
			}
		}
		break;

		case IDM_FILEBROWSER_EXPLORERHERE:
		{
			if (!selectedNode) return;

			wstring selPath = getNodePath(selectedNode);
			if (doesPathExist(selPath.c_str()))
			{
				// NPLX: was ShellExecute "explore" (folder) / openInExplorerAndSelect (file)
				if (QFileInfo(QString::fromStdWString(selPath)).isDir())
					nplx::desktop::openFolder(QString::fromStdWString(selPath));
				else
					nplx::desktop::showFileInFileManager(QString::fromStdWString(selPath));
			}
		}
		break;

		case IDM_FILEBROWSER_CMDHERE:
		case IDM_FILEBROWSER_POWERSHELLHERE:
		{
			if (!selectedNode) return;

			if (getNodeType(selectedNode) == browserNodeType_file)
				selectedNode = _treeView->getParent(selectedNode);

			wstring path = getNodePath(selectedNode);
			if (doesPathExist(path.c_str()))
			{
				// NPLX: a terminal in the folder (was cmd.exe / PowerShell)
				nplx::desktop::openTerminal(QString::fromStdWString(path));
			}
		}
		break;

		case IDM_FILEBROWSER_COPYPATH:
		{
			if (!selectedNode) return;
			wstring path = getNodePath(selectedNode);
			QApplication::clipboard()->setText(QString::fromStdWString(path)); // NPLX: was str2Clipboard
		}
		break;

		case IDM_FILEBROWSER_COPYFILENAME:
		{
			if (!selectedNode) return;
			wstring fileName = getNodeName(selectedNode);
			QApplication::clipboard()->setText(QString::fromStdWString(fileName)); // NPLX: was str2Clipboard
		}
		break;

		case IDM_FILEBROWSER_FINDINFILES:
		{
			if (!selectedNode) return;
			wstring path = getNodePath(selectedNode);
			if (_host._findInFiles)
				_host._findInFiles(path); // upstream NPPM_LAUNCHFINDINFILESDLG
		}
		break;

		case IDM_FILEBROWSER_OPENINNPP:
		{
			openSelectFile();
		}
		break;

		case IDM_FILEBROWSER_REMOVEALLROOTS:
		{
			for (int i = static_cast<int>(_folderUpdaters.size()) - 1; i >= 0; --i)
			{
				HTREEITEM root = getRootFromFullPath(_folderUpdaters[i]->_rootFolder._rootPath);
				if (root)
					_treeView->removeItem(root);
				delete _folderUpdaters[i]; // stops the watcher
				_folderUpdaters.erase(_folderUpdaters.begin() + i);
			}
		}
		break;

		case IDM_FILEBROWSER_ADDROOT:
		{
			NativeLangSpeaker *pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();
			wstring openWorkspaceStr = L"Select a folder to add in Folder as Workspace panel";
			if (pNativeSpeaker)
				openWorkspaceStr = pNativeSpeaker->getAttrNameStr(openWorkspaceStr.c_str(), FOLDERASWORKSPACE_NODE, "SelectFolderFromBrowserString");
			// NPLX: was folderBrowser
			wstring folderPath = nplx::uiServices() ? nplx::uiServices()->getExistingDirectory(openWorkspaceStr) : wstring();
			if (!folderPath.empty())
			{
				addRootFolder(folderPath);
			}
		}
		break;

		case IDM_FILEBROWSER_SHELLEXECUTE:
		{
			if (!selectedNode) return;
			wstring path = getNodePath(selectedNode);

			if (doesPathExist(path.c_str()))
			{
				nplx::desktop::openWithSystem(QString::fromStdWString(path)); // NPLX: was ShellExecuteEx "open"
			}
		}
		break;
	}
}

void FileBrowser::getDirectoryStructure(const wchar_t *dir, const std::vector<wstring> & patterns, FolderInfo & directoryStructure, bool isRecursive, bool isInHiddenDir)
{
	if (directoryStructure._parent == nullptr) // Root!
		directoryStructure.setRootPath(dir);

	const DirEntries entries = readDir(dir, isInHiddenDir);
	if (isRecursive)
	{
		for (const std::wstring& folder : entries._folders)
		{
			const wstring pathDir = joinPath(dir, folder);
			FolderInfo subDirectoryStructure(folder, &directoryStructure);
			getDirectoryStructure(pathDir.c_str(), patterns, subDirectoryStructure, isRecursive, isInHiddenDir);
			directoryStructure.addSubFolder(subDirectoryStructure);
		}
	}

	for (const std::wstring& file : entries._files)
	{
		// upstream matchInList(file, patterns): FaW always uses "*.*", every file
		bool isMatched = false;
		for (const wstring& pattern : patterns)
			isMatched = isMatched || pattern == L"*.*" || QDir::match(QString::fromStdWString(pattern), QString::fromStdWString(file));
		if (isMatched)
			directoryStructure.addFile(file);
	}
}

void FileBrowser::addRootFolder(wstring rootFolderPath, std::unordered_set<std::wstring>* pExpandedPaths)
{
	if (!doesDirectoryExist(rootFolderPath.c_str()))
		return;

	if (rootFolderPath.size() > 1 && rootFolderPath[rootFolderPath.length() - 1] == pathSep)
	{
		rootFolderPath = rootFolderPath.substr(0, rootFolderPath.length() - 1);
	}

	for (const auto f : _folderUpdaters)
	{
		if (f->_rootFolder._rootPath == rootFolderPath)
			return;
		else
		{
			if (isRelatedRootFolder(f->_rootFolder._rootPath, rootFolderPath))
			{
				//do nothing, go out
				wstring rootPath = f->_rootFolder._rootPath;
				const size_t skip = (rootPath.back() == pathSep) ? 0 : 1;
				wstring pathSuffix = rootFolderPath.substr(rootPath.size() + skip, rootFolderPath.size() - rootPath.size());
				vector<wstring> linarPathArray = split(pathSuffix, pathSep);

				HTREEITEM foundItem = findInTree(rootPath, nullptr, linarPathArray);
				if (foundItem)
					_treeView->selectItem(foundItem);
				return;
			}

			if (isRelatedRootFolder(rootFolderPath, f->_rootFolder._rootPath))
			{
				NppParameters::getInstance().getNativeLangSpeaker()->messageBox("FolderAsWorspaceSubfolderExists",
					L"A sub-folder of the folder you want to add exists.\rPlease remove its root from the panel before you add folder \"$STR_REPLACE$\".",
					L"Folder as Workspace adding folder problem",
					MB_OK,
					0, // not used
					rootFolderPath.c_str());
				return;
			}
		}
	}

	std::vector<wstring> patterns2Match;
	patterns2Match.push_back(L"*.*");

	wstring rootLabel = QFileInfo(QString::fromStdWString(rootFolderPath)).fileName().toStdWString(); // NPLX: was PathFindFileName
	if (rootLabel.empty())
		rootLabel = rootFolderPath; // "/"

	FolderInfo directoryStructure(rootLabel, nullptr);
	getDirectoryStructure(rootFolderPath.c_str(), patterns2Match, directoryStructure, true, false);
	HTREEITEM hRootItem = createFolderItemsFromDirStruct(nullptr, directoryStructure);
	sortChildren(hRootItem, true); // needed here for possible *nix like storages (Samba, WebDAV, WSL, ...)

	if (pExpandedPaths)
	{
		applyExpandState(hRootItem, pExpandedPaths);
	}

	_folderUpdaters.push_back(new FolderUpdater(directoryStructure, this));
	_folderUpdaters[_folderUpdaters.size() - 1]->startWatcher();
}

HTREEITEM FileBrowser::createFolderItemsFromDirStruct(HTREEITEM hParentItem, const FolderInfo & directoryStructure)
{
	HTREEITEM hFolderItem = nullptr;
	if (directoryStructure._parent == nullptr && hParentItem == nullptr)
	{
		hFolderItem = _treeView->addItem(directoryStructure._name.c_str(), nullptr, INDEX_CLOSE_ROOT, directoryStructure._rootPath);
	}
	else
	{
		hFolderItem = _treeView->addItem(directoryStructure._name.c_str(), hParentItem, INDEX_CLOSE_NODE);
	}

	for (const auto& folder : directoryStructure._subFolders)
	{
		createFolderItemsFromDirStruct(hFolderItem, folder);
	}

	for (const auto& file : directoryStructure._files)
	{
		_treeView->addItem(file._name.c_str(), hFolderItem, INDEX_LEAF);
	}

	_treeView->fold(hParentItem);

	return hFolderItem;
}

HTREEITEM FileBrowser::getRootFromFullPath(const wstring & rootPath) const
{
	for (HTREEITEM hItemNode = _treeView->getRoot(); hItemNode != nullptr; hItemNode = _treeView->getNextSibling(hItemNode))
	{
		if (rootPath == _treeView->getItemParam(hItemNode))
			return hItemNode;
	}
	return nullptr;
}

HTREEITEM FileBrowser::findChildNodeFromName(HTREEITEM parent, const wstring& label) const
{
	for (HTREEITEM hItemNode = _treeView->getChildFrom(parent); hItemNode != nullptr; hItemNode = _treeView->getNextSibling(hItemNode))
	{
		if (label == _treeView->getItemDisplayName(hItemNode))
			return hItemNode;
	}
	return nullptr;
}

vector<wstring> FileBrowser::getRoots() const
{
	vector<wstring> roots;
	for (HTREEITEM hItemNode = _treeView->getRoot(); hItemNode != nullptr; hItemNode = _treeView->getNextSibling(hItemNode))
		roots.push_back(_treeView->getItemParam(hItemNode));
	return roots;
}

wstring FileBrowser::getSelectedItemPath() const
{
	wstring itemPath;
	HTREEITEM hItemNode = _treeView->getSelection();
	if (hItemNode)
	{
		itemPath = getNodePath(hItemNode);
	}
	return itemPath;
}

bool FileBrowser::addToTree(FilesToChange & group, HTREEITEM node)
{
	if (node == nullptr) // it's a root. Search the right root with rootPath
	{
		// Search
		if ((node = getRootFromFullPath(group._rootPath)) == nullptr)
			return false;
	}

	if (group._linarWithoutLastPathElement.size() == 0)
	{
		// Items to add should exist on the disk
		group._files.erase(std::remove_if(group._files.begin(), group._files.end(),
			[&group](const auto & file)
			{
				return !doesPathExist((group._commonPath + file).c_str());
			}),
			group._files.end());

		if (group._files.empty())
		{
			return false;
		}

		// Search: if not found, add
		removeNamesAlreadyInNode(node, group._files);
		if (group._files.empty())
		{
			return false;
		}

		// Not found, good - Action
		for (auto & file : group._files) {
			if (doesDirectoryExist((group._commonPath + file).c_str()))
			{
				// NPLX: the new folder with its content (upstream adds it empty; its files come with later notifications)
				FolderInfo structure(file, nullptr); // added under node: createFolderItemsFromDirStruct makes it a folder item
				const std::wstring folderPath = group._commonPath + file;
				std::vector<std::wstring> patterns{ L"*.*" };
				getDirectoryStructure(folderPath.c_str(), patterns, structure, true, false);
				createFolderItemsFromDirStruct(node, structure);
				std::function<void(const FolderInfo&, const std::wstring&)> watchAll = [this, &group, &watchAll](const FolderInfo& fi, const std::wstring& path) {
					if (FolderUpdater* updater = updaterOf(group._rootPath))
						updater->watchFolder(path);
					for (const FolderInfo& sub : fi._subFolders)
						watchAll(sub, joinPath(path, sub._name));
				};
				watchAll(structure, folderPath);
			}
			else
			{
				_treeView->addItem(file.c_str(), node, INDEX_LEAF);
			}
		}
		sortChildren(node, false);
		return true;
	}
	else
	{
		for (HTREEITEM hItemNode = _treeView->getChildFrom(node); hItemNode != nullptr; hItemNode = _treeView->getNextSibling(hItemNode))
		{
			if (group._linarWithoutLastPathElement[0] == _treeView->getItemDisplayName(hItemNode))
			{
				// search recursively the node for an action
				group._linarWithoutLastPathElement.erase(group._linarWithoutLastPathElement.begin());
				return addToTree(group, hItemNode);
			}
		}
		return false;
	}
}

bool FileBrowser::deleteFromTree(FilesToChange & group)
{
	std::vector<HTREEITEM> foundItems = findInTree(group, nullptr);

	if (foundItems.empty() == true)
	{
		return false;
	}

	for (auto & item : foundItems)
	{
		_treeView->removeItem(item);
	}
	return true;
}

HTREEITEM FileBrowser::findInTree(const wstring& rootPath, HTREEITEM node, std::vector<wstring> linarPathArray) const
{
	if (node == nullptr) // it's a root. Search the right root with rootPath
	{
		// Search
		if ((node = getRootFromFullPath(rootPath)) == nullptr)
			return nullptr;
	}

	if (linarPathArray.empty()) // nothing to search, return node
	{
		return node;
	}
	else if (linarPathArray.size() == 1)
	{
		// Of course, it's a leaf (file or folder)
		return findChildNodeFromName(node, linarPathArray[0]);
	}
	else
	{
		for (HTREEITEM hItemNode = _treeView->getChildFrom(node); hItemNode != nullptr; hItemNode = _treeView->getNextSibling(hItemNode))
		{
			if (linarPathArray[0] == _treeView->getItemDisplayName(hItemNode))
			{
				// search recursively the node for an action
				linarPathArray.erase(linarPathArray.begin());
				return findInTree(rootPath, hItemNode, linarPathArray);
			}
		}
		return nullptr;
	}
}

std::vector<HTREEITEM> FileBrowser::findInTree(FilesToChange & group, HTREEITEM node) const
{
	if (node == nullptr) // it's a root. Search the right root with rootPath
	{
		// Search
		if ((node = getRootFromFullPath(group._rootPath)) == nullptr)
		{
			return {};
		}
	}

	if (group._linarWithoutLastPathElement.empty())
	{
		// Items to delete should not exist on the disk
		group._files.erase(std::remove_if(group._files.begin(), group._files.end(),
			[&group](const auto& file)
			{
				return doesPathExist((group._commonPath + file).c_str());
			}),
			group._files.end());

		if (group._files.empty())
		{
			return {};
		}

		return findChildNodesFromNames(node, group._files);
	}
	else
	{
		for (HTREEITEM hItemNode = _treeView->getChildFrom(node); hItemNode != nullptr; hItemNode = _treeView->getNextSibling(hItemNode))
		{
			if (group._linarWithoutLastPathElement[0] == _treeView->getItemDisplayName(hItemNode))
			{
				// search recursively the node for an action
				group._linarWithoutLastPathElement.erase(group._linarWithoutLastPathElement.begin());
				return findInTree(group, hItemNode);
			}
		}
		return {};
	}
}

std::vector<HTREEITEM> FileBrowser::findChildNodesFromNames(HTREEITEM parent, std::vector<wstring> & labels) const
{
	std::vector<HTREEITEM> itemNodes;

	for (HTREEITEM hItemNode = _treeView->getChildFrom(parent); hItemNode != nullptr && !labels.empty(); hItemNode = _treeView->getNextSibling(hItemNode))
	{
		auto it = std::find(labels.begin(), labels.end(), _treeView->getItemDisplayName(hItemNode));
		if (it != labels.end())
		{
			labels.erase(it); // remove, as it was already found
			itemNodes.push_back(hItemNode);
		}
	}
	return itemNodes;
}

void FileBrowser::removeNamesAlreadyInNode(HTREEITEM parent, std::vector<wstring> & labels) const
{
	// We have to search for the labels in the child nodes of parent, and remove the ones already present
	for (HTREEITEM hItemNode = _treeView->getChildFrom(parent); hItemNode != nullptr && !labels.empty(); hItemNode = _treeView->getNextSibling(hItemNode))
	{
		auto it = std::find(labels.begin(), labels.end(), _treeView->getItemDisplayName(hItemNode));
		if (it != labels.end())
		{
			labels.erase(it);
		}
	}
}

bool FileBrowser::renameInTree(const wstring& rootPath, HTREEITEM node, const std::vector<wstring>& linarPathArrayFrom, const wstring & renameTo)
{
	HTREEITEM foundItem = findInTree(rootPath, node, linarPathArrayFrom);
	if (foundItem == nullptr)
			return false;

	_treeView->renameItem(foundItem, renameTo.c_str());
	sortChildren(_treeView->getParent(foundItem), false);
	return true;
}

std::vector<std::wstring> FileBrowser::getExpandedPathsFromFaW() const
{
	std::vector<std::wstring> expandedList;
	std::function<void(HTREEITEM)> walk = [&](HTREEITEM hItem) {
		while (hItem != nullptr)
		{
			if (hItem->isExpanded())
			{
				std::wstring path = getNodePath(hItem);
				if (!path.empty())
				{
					expandedList.push_back(path);
				}
			}

			HTREEITEM hChild = _treeView->getChildFrom(hItem);
			if (hChild)
			{
				walk(hChild);
			}
			hItem = _treeView->getNextSibling(hItem);
		}
	};

	walk(_treeView->getRoot());
	return expandedList;
}

void FileBrowser::applyExpandState(HTREEITEM rootHItem, std::unordered_set<std::wstring>* pExpandedPaths)
{
	if (!rootHItem || !pExpandedPaths || pExpandedPaths->empty()) return;

	std::wstring rootPath = getNodePath(rootHItem);
	if (rootPath.empty()) return;

	std::function<void(HTREEITEM)> walkAndExpand = [&](HTREEITEM hItem) {
		if (!hItem || pExpandedPaths->empty()) return;

		std::wstring path = getNodePath(hItem);

		auto it = pExpandedPaths->find(path);
		if (it != pExpandedPaths->end())
		{
			_treeView->expand(hItem);
			pExpandedPaths->erase(it);
		}

		if (pExpandedPaths->empty()) return;

		for (HTREEITEM hChild = _treeView->getChildFrom(hItem); hChild != nullptr; hChild = _treeView->getNextSibling(hChild))
		{
			walkAndExpand(hChild);
		}
	};

	walkAndExpand(rootHItem);
}

int FileBrowser::categorySortFunc(const TreeView& tree, HTREEITEM item1, HTREEITEM item2)
{
	// folders first, then the labels, case-insensitively (upstream lstrcmpi)
	const bool isFolder1 = tree.getItemImage(item1) != INDEX_LEAF;
	const bool isFolder2 = tree.getItemImage(item2) != INDEX_LEAF;
	if (isFolder1 && !isFolder2)
		return -1;
	else if (!isFolder1 && isFolder2)
		return 1;
	else
		return item1->text(0).compare(item2->text(0), Qt::CaseInsensitive);
}

void FileBrowser::sortChildren(HTREEITEM node, bool isRecursive)
{
	if (!node)
		return;
	_treeView->customSortingItems(node, [this](HTREEITEM a, HTREEITEM b) { return categorySortFunc(*_treeView, a, b); }, isRecursive);
}

FolderUpdater* FileBrowser::updaterOf(const std::wstring& rootPath) const
{
	for (FolderUpdater* f : _folderUpdaters)
	{
		if (f->_rootFolder._rootPath == rootPath)
			return f;
	}
	return nullptr;
}

void FileBrowser::folderChanged(const std::wstring& rootPath, const std::wstring& folderPath)
{
	// NPLX: upstream gets FILE_ACTION_ADDED / REMOVED / RENAMED for each file; QFileSystemWatcher only says that a
	// folder changed, so the folder is compared with its node and the differences go through the upstream code
	// (addToTree / deleteFromTree)
	vector<wstring> linarPathArray;
	if (folderPath.size() > rootPath.size())
	{
		const size_t skip = (rootPath.back() == pathSep) ? 0 : 1;
		linarPathArray = split(folderPath.substr(rootPath.size() + skip), pathSep);
	}
	HTREEITEM node = findInTree(rootPath, nullptr, linarPathArray);
	if (!node)
		return;

	const DirEntries entries = readDir(folderPath, false);
	std::vector<std::wstring> onDisk = entries._folders;
	onDisk.insert(onDisk.end(), entries._files.begin(), entries._files.end());

	std::vector<std::wstring> inTree;
	for (HTREEITEM child = _treeView->getChildFrom(node); child; child = _treeView->getNextSibling(child))
		inTree.push_back(_treeView->getItemDisplayName(child));

	FilesToChange added;
	FilesToChange removed;
	added._rootPath = removed._rootPath = rootPath;
	added._commonPath = removed._commonPath = joinPath(folderPath, L"");
	added._linarWithoutLastPathElement = removed._linarWithoutLastPathElement = linarPathArray;
	for (const std::wstring& name : onDisk)
	{
		if (std::find(inTree.begin(), inTree.end(), name) == inTree.end())
			added._files.push_back(name);
	}
	for (const std::wstring& name : inTree)
	{
		if (std::find(onDisk.begin(), onDisk.end(), name) == onDisk.end())
			removed._files.push_back(name);
	}

	if (!removed._files.empty())
		deleteFromTree(removed);
	if (!added._files.empty())
		addToTree(added, nullptr);
}

void FileBrowser::setBackgroundColor(COLORREF bgColour)
{
	_bgColour = QColor(GetRValue(bgColour), GetGValue(bgColour), GetBValue(bgColour));
	updateTreeColours();
}

void FileBrowser::setForegroundColor(COLORREF fgColour)
{
	_fgColour = QColor(GetRValue(fgColour), GetGValue(fgColour), GetBValue(fgColour));
	updateTreeColours();
}

void FileBrowser::updateTreeColours()
{
	// upstream TreeView_SetBkColor / SetTextColor: the editor theme's default colours
	QString qss;
	if (_bgColour.isValid())
		qss += QStringLiteral("background: %1; ").arg(_bgColour.name());
	if (_fgColour.isValid())
		qss += QStringLiteral("color: %1; ").arg(_fgColour.name());
	_treeView->setStyleSheet(qss.isEmpty() ? QString() : QStringLiteral("QTreeView { %1}").arg(qss));
}
