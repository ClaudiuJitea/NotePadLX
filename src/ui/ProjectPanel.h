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

// NPLX: port of upstream WinControls/ProjectPanel (Project Panel 1, 2, 3): a workspace (a file in the upstream
// workspace XML format) of projects, folders and files. The tree items keep the file's full path in their param
// (upstream: a std::wstring* LPARAM; empty for the other nodes). The file dialogs and the File Relocalizer go
// through nplx::uiServices().

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <QColor>

#include "DockingPanel.h"
#include "NppXml.h"
#include "ProjectPanel_rc.h"
#include "TreeView.h"

class QMenu;
class QToolButton;

#define PM_PROJECTPANELTITLE       L"Project Panel"
#define PM_WORKSPACEROOTNAME       L"Workspace"
#define PM_NEWFOLDERNAME           L"Folder Name"
#define PM_NEWPROJECTNAME          L"Project Name"

#define PM_NEWWORKSPACE            L"New Workspace"
#define PM_OPENWORKSPACE           L"Open Workspace"
#define PM_RELOADWORKSPACE         L"Reload Workspace"
#define PM_SAVEWORKSPACE           L"Save"
#define PM_SAVEASWORKSPACE         L"Save As..."
#define PM_SAVEACOPYASWORKSPACE    L"Save a Copy As..."
#define PM_NEWPROJECTWORKSPACE     L"Add New Project"
#define PM_FINDINFILESWORKSPACE    L"Find in Projects..."

#define PM_EDITRENAME              L"Rename"
#define PM_EDITNEWFOLDER           L"Add Folder"
#define PM_EDITADDFILES            L"Add Files..."
#define PM_EDITADDFILESRECUSIVELY  L"Add Files from Directory..."
#define PM_EDITREMOVE              L"Remove\tDEL"
#define PM_EDITMODIFYFILE          L"Modify File Path"

#define PM_WORKSPACEMENUENTRY      L"Workspace"
#define PM_EDITMENUENTRY           L"Edit"

#define PM_MOVEUPENTRY             L"Move Up\tCtrl+Up"
#define PM_MOVEDOWNENTRY           L"Move Down\tCtrl+Down"

enum NodeType {
	nodeType_root = 0, nodeType_project = 1, nodeType_folder = 2, nodeType_file = 3
};

class ProjectPanel : public DockingPanel
{
	Q_OBJECT

public:
	ProjectPanel(int panelID, QWidget* parent = nullptr);

	// NPLX: what upstream sends to its parent: NPPM_DOOPEN, NPPM_INTERNAL_FINDINPROJECTS
	void init(std::function<void(const std::wstring&)> open, std::function<void(int panelMask)> findInProjects) {
		_open = std::move(open);
		_findInProjects = std::move(findInProjects);
	}

	void setPanelTitle(const std::wstring& title) {
		_panelTitle = title;
		setWindowTitle(QString::fromStdWString(title));
	}
	const wchar_t* getPanelTitle() const {
		return _panelTitle.c_str();
	}

	void newWorkSpace();
	bool saveWorkspaceRequest();
	bool openWorkSpace(const wchar_t* projectFileName, bool force = false);
	bool saveWorkSpace();
	bool saveWorkSpaceAs(bool saveCopyAs);
	void setWorkSpaceFilePath(const wchar_t* projectFileName) {
		_workSpaceFilePath = projectFileName;
	}
	const wchar_t* getWorkSpaceFilePath() const {
		return _workSpaceFilePath.c_str();
	}
	bool isDirty() const {
		return _isDirty;
	}
	bool checkIfNeedSave();

	void setBackgroundColor(COLORREF bgColour);
	void setForegroundColor(COLORREF fgColour);
	bool enumWorkSpaceFiles(HTREEITEM tvFrom, const std::vector<std::wstring>& patterns, std::vector<std::wstring>& fileNames);

	void popupMenuCmd(int cmdID);
	void startWorkspace(); // NPLX: upstream WM_INITDIALOG: open the saved workspace, else a new one
	TreeView* treeView() const { return _treeView; } // NPLX: tests
	NodeType getNodeType(HTREEITEM hItem) const;
	void recursiveAddFilesFrom(const wchar_t* folderPath, HTREEITEM hTreeItem);
	void openSelectFile();

protected:
	bool eventFilter(QObject* watched, QEvent* event) override; // upstream TVN_KEYDOWN

	TreeView* _treeView = nullptr;
	QToolButton* _workspaceButton = nullptr;
	QToolButton* _editButton = nullptr;
	QMenu* _hWorkSpaceMenu = nullptr;
	QMenu* _hProjectMenu = nullptr;
	QMenu* _hFolderMenu = nullptr;
	QMenu* _hFileMenu = nullptr;
	std::wstring _panelTitle;
	std::wstring _workSpaceFilePath;
	std::wstring _selDirOfFilesFromDirDlg;
	bool _isDirty = false;
	int _panelID = 0;
	std::function<void(const std::wstring&)> _open;
	std::function<void(int)> _findInProjects;
	QColor _bgColour;
	QColor _fgColour;

	void initMenus();
	void addFiles(HTREEITEM hTreeItem);
	void addFilesFromDirectory(HTREEITEM hTreeItem);
	HTREEITEM addFolder(HTREEITEM hTreeItem, const wchar_t* folderName, bool editLabel = true);
	bool writeWorkSpace(const wchar_t* projectFileName = nullptr, bool doUpdateGUI = true);
	std::wstring getRelativePath(const std::wstring& filePath, const wchar_t* workSpaceFileName);
	void buildProjectXml(NppXml::Element& root, HTREEITEM hItem, const wchar_t* fn2write);
	void setWorkSpaceDirty(bool isDirty);
	bool buildTreeFrom(const NppXml::Element& projectRoot, HTREEITEM hParentItem);
	void showContextMenu(const QPoint& pos);
	QMenu* getMenuHandler(HTREEITEM selectedItem);
	std::wstring getAbsoluteFilePath(const wchar_t* relativePath);
	std::wstring fileExtFilter() const; // upstream setFileExtFilter
	void onLabelEdited(HTREEITEM item, const QString& oldLabel); // upstream TVN_ENDLABELEDIT
	void updateTreeColours();
};
