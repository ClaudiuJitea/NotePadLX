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

// NPLX: port of upstream WinControls/FileBrowser ("Folder as Workspace"). Paths use '/'. The tree items keep the
// root path in their param (upstream SortingData4lParam::_rootPath; empty for the other nodes) and their kind in the
// image index. FolderUpdater watches the folders with QFileSystemWatcher (upstream: a ReadDirectoryChanges thread
// posting FB_ADDFILE / FB_RMFILE / FB_RNFILE); a changed folder is compared with its node, which gives the same
// add / remove updates (a rename is a remove and an add).

#pragma once

#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

#include <QColor>

#include "DockingPanel.h"
#include "TreeView.h"
#include "fileBrowser_rc.h"

class QFileSystemWatcher;
class QMenu;
class QTimer;

#define FB_PANELTITLE         L"Folder as Workspace"
#define FB_ADDROOT            L"Add"
#define FB_REMOVEALLROOTS     L"Remove All"
#define FB_REMOVEROOTFOLDER   L"Remove"
#define FB_COPYPATH           L"Copy path"
#define FB_COPYFILENAME       L"Copy file name"
#define FB_FINDINFILES        L"Find in Files..."
#define FB_EXPLORERHERE       L"Explorer here"
#define FB_CMDHERE            L"CMD here"
#define FB_POWERSHELLHERE     L"PowerShell here"
#define FB_OPENINNPP          L"Open"
#define FB_SHELLEXECUTE       L"Run by system"

#define FOLDERASWORKSPACE_NODE "FolderAsWorkspace"

class FileBrowser;
class FolderInfo;

class FileInfo final
{
friend class FileBrowser;
friend class FolderInfo;

public:
	FileInfo() = delete; // constructor by default is forbidden
	explicit FileInfo(const std::wstring& name) noexcept : _name(name) {}
	const std::wstring& getName() const { return _name; }
	void setName(const std::wstring& name) { _name = name; }

private:
	std::wstring _name;
};

class FolderInfo final
{
friend class FileBrowser;
friend class FolderUpdater;

public:
	FolderInfo() = delete; // constructor by default is forbidden
	FolderInfo(const std::wstring& name, FolderInfo* parent) : _name(name), _parent(parent) {}
	void setRootPath(const std::wstring& rootPath) { _rootPath = rootPath; }
	const std::wstring& getRootPath() const { return _rootPath; }
	void setName(const std::wstring& name) { _name = name; }
	const std::wstring& getName() const { return _name; }
	void addFile(const std::wstring& fn) { _files.push_back(FileInfo(fn)); }
	void addSubFolder(FolderInfo subDirectoryStructure) { _subFolders.push_back(subDirectoryStructure); }

private:
	std::vector<FolderInfo> _subFolders;
	std::vector<FileInfo> _files;
	std::wstring _name;
	FolderInfo* _parent = nullptr;
	std::wstring _rootPath; // set only for root folder; empty for normal folder
};

enum BrowserNodeType {
	browserNodeType_root = 0, browserNodeType_folder = 2, browserNodeType_file = 3
};

class FolderUpdater final
{
friend class FileBrowser;

public:
	FolderUpdater(const FolderInfo& fi, FileBrowser *pFileBrowser) : _rootFolder(fi), _pFileBrowser(pFileBrowser) {}
	~FolderUpdater();

	void startWatcher();
	void stopWatcher();
	void watchFolder(const std::wstring& path); // NPLX: QFileSystemWatcher is not recursive

private:
	FolderInfo _rootFolder;
	FileBrowser* _pFileBrowser = nullptr;
	QFileSystemWatcher* _watcher = nullptr;
	QTimer* _timer = nullptr;            // NPLX: batches the change notifications (upstream: MAX_BATCH_SIZE)
	std::unordered_set<std::wstring> _changedFolders;
};

class FileBrowser : public DockingPanel
{
	Q_OBJECT

public:
	explicit FileBrowser(QWidget* parent = nullptr);
	~FileBrowser() override;

	// NPLX: what upstream asks its parent (NPPM_GETFULLCURRENTPATH, NPPM_DOOPEN, NPPM_LAUNCHFINDINFILESDLG)
	struct Host
	{
		std::function<std::wstring()> _currentFilePath;
		std::function<void(const std::wstring&)> _open;
		std::function<void(const std::wstring&)> _findInFiles;
	};
	void init(Host host) { _host = std::move(host); }

	void setBackgroundColor(COLORREF bgColour);
	void setForegroundColor(COLORREF fgColour);

	std::wstring getNodePath(HTREEITEM node) const;
	std::wstring getNodeName(HTREEITEM node) const;
	void addRootFolder(std::wstring rootFolderPath, std::unordered_set<std::wstring>* pExpandedPaths = nullptr);
	void applyExpandState(HTREEITEM rootHItem, std::unordered_set<std::wstring>* pExpandedPaths);
	std::vector<std::wstring> getExpandedPathsFromFaW() const;

	HTREEITEM getRootFromFullPath(const std::wstring & rootPath) const;
	HTREEITEM findChildNodeFromName(HTREEITEM parent, const std::wstring& label) const;

	HTREEITEM findInTree(const std::wstring& rootPath, HTREEITEM node, std::vector<std::wstring> linarPathArray) const;

	void deleteAllFromTree() {
		popupMenuCmd(IDM_FILEBROWSER_REMOVEALLROOTS);
	}

	bool renameInTree(const std::wstring& rootPath, HTREEITEM node, const std::vector<std::wstring>& linarPathArrayFrom, const std::wstring & renameTo);

	std::vector<std::wstring> getRoots() const;
	std::wstring getSelectedItemPath() const;
	bool selectItemFromPath(const std::wstring& itemPath) const;

	void popupMenuCmd(int cmdID);
	bool selectCurrentEditingFile() const;
	void openSelectFile();
	void folderChanged(const std::wstring& rootPath, const std::wstring& folderPath); // NPLX: FolderUpdater

	TreeView* treeView() const { return _treeView; } // NPLX: tests
	BrowserNodeType getNodeType(HTREEITEM hItem) const;

protected:
	bool eventFilter(QObject* watched, QEvent* event) override; // upstream TVN_KEYDOWN

private:
	Host _host;
	TreeView* _treeView = nullptr;
	QMenu* _hGlobalMenu = nullptr;
	QMenu* _hRootMenu = nullptr;
	QMenu* _hFolderMenu = nullptr;
	QMenu* _hFileMenu = nullptr;
	std::vector<FolderUpdater *> _folderUpdaters;
	QColor _bgColour;
	QColor _fgColour;

	std::wstring _expandAllFolders = L"Unfold all";
	std::wstring _collapseAllFolders = L"Fold all";
	std::wstring _locateCurrentFile = L"Locate current file";

	void initPopupMenus();
	void showContextMenu(const QPoint& pos);

	struct FilesToChange {
		std::wstring _commonPath; // Common path between all the files. _rootPath + _linarWithoutLastPathElement
		std::wstring _rootPath;
		std::vector<std::wstring> _linarWithoutLastPathElement;
		std::vector<std::wstring> _files; // file/folder names
	};

	bool addToTree(FilesToChange & group, HTREEITEM node);
	bool deleteFromTree(FilesToChange & group);
	std::vector<HTREEITEM> findInTree(FilesToChange & group, HTREEITEM node) const;
	std::vector<HTREEITEM> findChildNodesFromNames(HTREEITEM parent, std::vector<std::wstring> & labels) const;
	void removeNamesAlreadyInNode(HTREEITEM parent, std::vector<std::wstring> & labels) const;
	void getDirectoryStructure(const wchar_t *dir, const std::vector<std::wstring> & patterns, FolderInfo & directoryStructure, bool isRecursive, bool isInHiddenDir);
	HTREEITEM createFolderItemsFromDirStruct(HTREEITEM hParentItem, const FolderInfo & directoryStructure);
	static int categorySortFunc(const TreeView& tree, HTREEITEM item1, HTREEITEM item2);
	void sortChildren(HTREEITEM node, bool isRecursive);
	void updateTreeColours();
	FolderUpdater* updaterOf(const std::wstring& rootPath) const;
};
