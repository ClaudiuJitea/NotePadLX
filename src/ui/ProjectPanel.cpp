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

#include "ProjectPanel.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMenu>
#include <QToolButton>
#include <QVBoxLayout>

#include "Common.h"
#include "NplxStyle.h"
#include "Parameters.h"
#include "UiServices.h"
#include "localization.h"
#include "menuCmdID.h"

using namespace std;

enum TvIndex : int
{
	INDEX_CLEAN_ROOT,
	INDEX_DIRTY_ROOT,
	INDEX_PROJECT,
	INDEX_OPEN_NODE,
	INDEX_CLOSED_NODE,
	INDEX_LEAF,
	INDEX_LEAF_INVALID,
};

namespace
{
	constexpr wchar_t pathSep = L'/'; // NPLX: was '\\'

	std::wstring fileNameOf(const std::wstring& path) // upstream PathFindFileName
	{
		const size_t slash = path.find_last_of(pathSep);
		return slash == std::wstring::npos ? path : path.substr(slash + 1);
	}

	std::wstring dirOf(const std::wstring& path) // upstream PathRemoveFileSpec
	{
		const size_t slash = path.find_last_of(pathSep);
		return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash == 0 ? 1 : slash);
	}
}

ProjectPanel::ProjectPanel(int panelID, QWidget* parent)
	: DockingPanel(QString::fromWCharArray(PM_PROJECTPANELTITLE), IDM_VIEW_PROJECT_PANEL_1 + panelID,
		QStringLiteral("ProjectPanel%1").arg(panelID + 1), parent),
	  _panelID(panelID)
{
	// upstream WM_INITDIALOG: the "Workspace" and "Edit" menu buttons above the tree view
	NppParameters& nppParam = NppParameters::getInstance();
	NativeLangSpeaker *pNativeSpeaker = nppParam.getNativeLangSpeaker();
	const wstring workspace_entry = pNativeSpeaker ? pNativeSpeaker->getProjectPanelLangMenuStr("Entries", 0, PM_WORKSPACEMENUENTRY) : PM_WORKSPACEMENUENTRY;
	const wstring edit_entry = pNativeSpeaker ? pNativeSpeaker->getProjectPanelLangMenuStr("Entries", 1, PM_EDITMENUENTRY) : PM_EDITMENUENTRY;

	auto* content = new QWidget(this);
	auto* layout = new QVBoxLayout(content);
	layout->setContentsMargins(4, 4, 4, 4);
	layout->setSpacing(4);

	auto* toolbar = new QHBoxLayout();
	toolbar->setSpacing(2);
	_workspaceButton = new QToolButton(content);
	_workspaceButton->setText(QString::fromStdWString(workspace_entry));
	_workspaceButton->setAutoRaise(true);
	_editButton = new QToolButton(content);
	_editButton->setText(QString::fromStdWString(edit_entry));
	_editButton->setAutoRaise(true);
	toolbar->addWidget(_workspaceButton);
	toolbar->addWidget(_editButton);
	toolbar->addStretch(1);
	layout->addLayout(toolbar);

	_treeView = new TreeView(content);
	_treeView->setImageList({ nplx::style::icon(QStringLiteral("workspace")), nplx::style::icon(QStringLiteral("workspace_dirty")),
		nplx::style::icon(QStringLiteral("project")), nplx::style::icon(QStringLiteral("folder_open")), nplx::style::icon(QStringLiteral("folder")),
		nplx::style::icon(QStringLiteral("file_plain")), nplx::style::icon(QStringLiteral("file_invalid")) });
	_treeView->addCanNotDropInList(INDEX_LEAF);
	_treeView->addCanNotDropInList(INDEX_LEAF_INVALID);
	_treeView->addCanNotDragOutList(INDEX_CLEAN_ROOT);
	_treeView->addCanNotDragOutList(INDEX_DIRTY_ROOT);
	_treeView->addCanNotDragOutList(INDEX_PROJECT);
	_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
	_treeView->installEventFilter(this);
	layout->addWidget(_treeView, 1);
	setWidget(content);

	initMenus();

	// upstream IDB_PROJECT_BTN / IDB_EDIT_BTN (getMenuDisplayPoint: below the button)
	connect(_workspaceButton, &QToolButton::clicked, this, [this]() { popupMenuCmd(IDB_PROJECT_BTN); });
	connect(_editButton, &QToolButton::clicked, this, [this]() { popupMenuCmd(IDB_EDIT_BTN); });
	connect(_treeView, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) { showContextMenu(pos); });
	connect(_treeView, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
		// upstream NM_DBLCLK: open a file (a folder toggles)
		if (getNodeType(item) == nodeType_file)
			openSelectFile();
		else
			_treeView->toggleExpandCollapse(item);
	});
	connect(_treeView, &TreeView::labelEdited, this, [this](QTreeWidgetItem* item, const QString& oldLabel) { onLabelEdited(item, oldLabel); });
	connect(_treeView, &TreeView::itemDropped, this, [this]() { setWorkSpaceDirty(true); }); // upstream dropItem
	// upstream TVN_ITEMEXPANDED: the open / closed folder images
	connect(_treeView, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* item) {
		if (getNodeType(item) == nodeType_folder)
			_treeView->setItemImage(item, INDEX_OPEN_NODE);
	});
	connect(_treeView, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* item) {
		if (getNodeType(item) == nodeType_folder)
			_treeView->setItemImage(item, INDEX_CLOSED_NODE);
	});
}

void ProjectPanel::startWorkspace()
{
	if (!openWorkSpace(_workSpaceFilePath.c_str(), true))
		newWorkSpace();
}

bool ProjectPanel::checkIfNeedSave()
{
	if (_isDirty)
	{
		const wstring title = _workSpaceFilePath.length() > 0 ? fileNameOf(_workSpaceFilePath) : _panelTitle;
		NativeLangSpeaker *pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();
		int res = pNativeSpeaker->messageBox("ProjectPanelChanged",
			L"The workspace was modified. Do you want to save it?",
			L"$STR_REPLACE$",
			MB_YESNOCANCEL | MB_ICONQUESTION,
			0,
			title.c_str());

		if (res == IDYES)
		{
			if (!saveWorkSpace())
				return false;
		}
		else if (res == IDNO)
		{
			// Don't save so do nothing here
		}
		else
		//else if (res == IDCANCEL)
		{
			// User cancels action "New Workspace" so we interrupt here
			return false;
		}
	}
	return true;
}

void ProjectPanel::initMenus()
{
	NativeLangSpeaker *pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();
	auto str = [pNativeSpeaker](const char* node, int id, const wchar_t* defaultStr) {
		QString s = QString::fromStdWString(pNativeSpeaker ? pNativeSpeaker->getProjectPanelLangMenuStr(node, id, defaultStr) : defaultStr);
		return s.replace(QLatin1Char('\t'), QStringLiteral("\t")); // the shortcut after the tab, like Win32 menus
	};
	auto add = [this](QMenu* menu, int id, const QString& text) {
		if (id == -1)
		{
			menu->addSeparator();
			return;
		}
		QAction* a = menu->addAction(text);
		a->setData(id);
		connect(a, &QAction::triggered, this, [this, id]() { popupMenuCmd(id); });
	};

	// upstream inserts each item at position 0: the menus read in the reverse order of the InsertMenu calls
	_hWorkSpaceMenu = new QMenu(this);
	add(_hWorkSpaceMenu, IDM_PROJECT_FINDINPROJECTSWS, str("WorkspaceMenu", IDM_PROJECT_FINDINPROJECTSWS, PM_FINDINFILESWORKSPACE));
	add(_hWorkSpaceMenu, -1, QString());
	add(_hWorkSpaceMenu, IDM_PROJECT_NEWPROJECT, str("WorkspaceMenu", IDM_PROJECT_NEWPROJECT, PM_NEWPROJECTWORKSPACE));
	add(_hWorkSpaceMenu, -1, QString());
	add(_hWorkSpaceMenu, IDM_PROJECT_SAVEACOPYASWS, str("WorkspaceMenu", IDM_PROJECT_SAVEACOPYASWS, PM_SAVEACOPYASWORKSPACE));
	add(_hWorkSpaceMenu, IDM_PROJECT_SAVEASWS, str("WorkspaceMenu", IDM_PROJECT_SAVEASWS, PM_SAVEASWORKSPACE));
	add(_hWorkSpaceMenu, IDM_PROJECT_SAVEWS, str("WorkspaceMenu", IDM_PROJECT_SAVEWS, PM_SAVEWORKSPACE));
	add(_hWorkSpaceMenu, IDM_PROJECT_RELOADWS, str("WorkspaceMenu", IDM_PROJECT_RELOADWS, PM_RELOADWORKSPACE));
	add(_hWorkSpaceMenu, IDM_PROJECT_OPENWS, str("WorkspaceMenu", IDM_PROJECT_OPENWS, PM_OPENWORKSPACE));
	add(_hWorkSpaceMenu, IDM_PROJECT_NEWWS, str("WorkspaceMenu", IDM_PROJECT_NEWWS, PM_NEWWORKSPACE));
	{
		// NPLX: the menu order upstream shows (the reverse of the calls above)
		QList<QAction*> actions = _hWorkSpaceMenu->actions();
		for (QAction* a : actions)
			_hWorkSpaceMenu->removeAction(a);
		for (auto it = actions.rbegin(); it != actions.rend(); ++it)
			_hWorkSpaceMenu->addAction(*it);
	}

	auto buildEditMenu = [&](QMenu* menu, const char* node, bool isFile) {
		if (isFile)
		{
			add(menu, IDM_PROJECT_MODIFYFILEPATH, str(node, IDM_PROJECT_MODIFYFILEPATH, PM_EDITMODIFYFILE));
			add(menu, IDM_PROJECT_DELETEFILE, str(node, IDM_PROJECT_DELETEFILE, PM_EDITREMOVE));
			add(menu, IDM_PROJECT_RENAME, str(node, IDM_PROJECT_RENAME, PM_EDITRENAME));
		}
		else
		{
			add(menu, IDM_PROJECT_DELETEFOLDER, str(node, IDM_PROJECT_DELETEFOLDER, PM_EDITREMOVE));
			add(menu, IDM_PROJECT_ADDFILESRECUSIVELY, str(node, IDM_PROJECT_ADDFILESRECUSIVELY, PM_EDITADDFILESRECUSIVELY));
			add(menu, IDM_PROJECT_ADDFILES, str(node, IDM_PROJECT_ADDFILES, PM_EDITADDFILES));
			add(menu, IDM_PROJECT_NEWFOLDER, str(node, IDM_PROJECT_NEWFOLDER, PM_EDITNEWFOLDER));
			add(menu, IDM_PROJECT_RENAME, str(node, IDM_PROJECT_RENAME, PM_EDITRENAME));
		}
		add(menu, -1, QString());
		add(menu, IDM_PROJECT_MOVEDOWN, str(node, IDM_PROJECT_MOVEDOWN, PM_MOVEDOWNENTRY));
		add(menu, IDM_PROJECT_MOVEUP, str(node, IDM_PROJECT_MOVEUP, PM_MOVEUPENTRY));
		QList<QAction*> actions = menu->actions();
		for (QAction* a : actions)
			menu->removeAction(a);
		for (auto it = actions.rbegin(); it != actions.rend(); ++it)
			menu->addAction(*it);
	};
	_hProjectMenu = new QMenu(this);
	buildEditMenu(_hProjectMenu, "ProjectMenu", false);
	_hFolderMenu = new QMenu(this);
	buildEditMenu(_hFolderMenu, "FolderMenu", false);
	_hFileMenu = new QMenu(this);
	buildEditMenu(_hFileMenu, "FileMenu", true);
}

bool ProjectPanel::openWorkSpace(const wchar_t* projectFileName, bool force)
{
	if (!projectFileName)
		return false;

	if ((!force) && (_workSpaceFilePath.length() > 0))
	{ // Return if it is better to keep the current workspace tree
		wstring newWorkspace = projectFileName;
		if (newWorkspace == _workSpaceFilePath)
			return true;
		if (!saveWorkspaceRequest())
			return true;
	}

	if (!doesFileExist(projectFileName))
		return false;

	NppXml::NewDocument xmlDocProject{};
	const bool loadOkay = NppXml::loadFile(&xmlDocProject, projectFileName);
	if (!loadOkay)
		return false;

	NppXml::Element root = NppXml::firstChildElement(&xmlDocProject, "NotepadPlus");
	if (!root)
		return false;

	NppXml::Element childNode = NppXml::firstChildElement(root, "Project");
	if (!childNode)
		return false;

	_treeView->removeAllItems();
	_workSpaceFilePath = projectFileName;

	const wstring fileName = fileNameOf(projectFileName);
	HTREEITEM rootItem = _treeView->addItem(fileName.c_str(), nullptr, INDEX_CLEAN_ROOT);

	for (;
		childNode;
		childNode = NppXml::nextSiblingElement(childNode, "Project"))
	{
		const char* itemName = NppXml::attribute(childNode, "name");
		HTREEITEM projectItem = _treeView->addItem(string2wstring(itemName).c_str(), rootItem, INDEX_PROJECT);
		buildTreeFrom(childNode, projectItem);
	}
	setWorkSpaceDirty(false);
	_treeView->expand(rootItem);

	return true;
}

void ProjectPanel::newWorkSpace()
{
	NativeLangSpeaker *pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();
	wstring workspace = pNativeSpeaker ? pNativeSpeaker->getAttrNameStr(PM_WORKSPACEROOTNAME, "ProjectManager", "WorkspaceRootName") : PM_WORKSPACEROOTNAME;
	_treeView->addItem(workspace.c_str(), nullptr, INDEX_CLEAN_ROOT);
	setWorkSpaceDirty(false);
	_workSpaceFilePath = L"";
}

bool ProjectPanel::saveWorkSpace()
{
	if (_workSpaceFilePath == L"")
	{
		return saveWorkSpaceAs(false);
	}
	else
	{
		if (!writeWorkSpace())
			return false;
		setWorkSpaceDirty(false);
		return true;
	}
}

bool ProjectPanel::writeWorkSpace(const wchar_t* projectFileName, bool doUpdateGUI)
{
	//write <NotepadPlus>: use the default file name if new file name is not given
	const wchar_t* fn2write = projectFileName ? projectFileName : _workSpaceFilePath.c_str();

	NppXml::NewDocument projDoc{};
	NppXml::Element root = NppXml::createChildElement(projDoc, "NotepadPlus");

	HTREEITEM tvRoot = _treeView->getRoot();
	if (!tvRoot)
		return false;

	for (HTREEITEM tvProj = _treeView->getChildFrom(tvRoot);
		tvProj != nullptr;
		tvProj = _treeView->getNextSibling(tvProj))
	{
		NppXml::Element projRoot = NppXml::createChildElement(root, "Project");
		NppXml::setAttribute(projRoot, "name", wstring2string(_treeView->getItemDisplayName(tvProj)));

		buildProjectXml(projRoot, tvProj, fn2write);
	}

	if (!NppXml::saveFileProject(&projDoc, fn2write))
	{
		const wstring title = _workSpaceFilePath.length() > 0 ? fileNameOf(_workSpaceFilePath) : _panelTitle;
		NativeLangSpeaker *pNativeSpeaker = NppParameters::getInstance().getNativeLangSpeaker();
		pNativeSpeaker->messageBox("ProjectPanelSaveError",
		L"An error occurred while writing your workspace file.\nYour workspace has not been saved.",
		L"$STR_REPLACE$",
		MB_OK | MB_ICONERROR,
		0,
		title.c_str());
		return false;
	}

	if (doUpdateGUI)
	{
		_treeView->renameItem(tvRoot, fileNameOf(fn2write).c_str());
	}
	return true;
}

void ProjectPanel::buildProjectXml(NppXml::Element& root, HTREEITEM hItem, const wchar_t* fn2write)
{
	for (HTREEITEM hItemNode = _treeView->getChildFrom(hItem);
		hItemNode != nullptr;
		hItemNode = _treeView->getNextSibling(hItemNode))
	{
		const wstring fn = _treeView->getItemParam(hItemNode);
		if (!fn.empty())
		{
			std::string newFn = wstring2string(getRelativePath(fn, fn2write));
			NppXml::Element fileLeaf = NppXml::createChildElement(root, "File");
			NppXml::setAttribute(fileLeaf, "name", newFn);
		}
		else
		{
			NppXml::Element folderNode = NppXml::createChildElement(root, "Folder");
			NppXml::setAttribute(folderNode, "name", wstring2string(_treeView->getItemDisplayName(hItemNode)));
			buildProjectXml(folderNode, hItemNode, fn2write);
		}
	}
}

bool ProjectPanel::enumWorkSpaceFiles(HTREEITEM tvFrom, const std::vector<wstring> & patterns, std::vector<wstring> & fileNames)
{
	HTREEITEM tvRoot = tvFrom ? tvFrom : _treeView->getRoot();
	if (!tvRoot) return false;

	for (HTREEITEM tvProj = _treeView->getChildFrom(tvRoot);
		tvProj != nullptr;
		tvProj = _treeView->getNextSibling(tvProj))
	{
		const wstring fn = _treeView->getItemParam(tvProj);
		if (!fn.empty())
		{
			if (matchInList(_treeView->getItemDisplayName(tvProj).c_str(), patterns))
			{
				fileNames.push_back(fn);
			}
		}
		else
		{
			if (!enumWorkSpaceFiles (tvProj, patterns, fileNames)) return false;
		}
	}
	return true;
}

wstring ProjectPanel::getRelativePath(const wstring& filePath, const wchar_t *workSpaceFileName)
{
	const wstring wsfn = dirOf(workSpaceFileName);

	size_t pos_found = filePath.find(wsfn);
	if (pos_found == wstring::npos)
		return filePath;
	const wchar_t *relativeFile = filePath.c_str() + wsfn.size();
	if (relativeFile[0] == pathSep)
		++relativeFile;
	return relativeFile;
}

bool ProjectPanel::buildTreeFrom(const NppXml::Element& projectRoot, HTREEITEM hParentItem)
{
	for (NppXml::Element childNode = NppXml::firstChildElement(projectRoot);
		childNode;
		childNode = NppXml::nextSibling(childNode))
	{
		const char* nodeName = NppXml::getName(childNode);
		if (std::strcmp(nodeName, "Folder") == 0)
		{
			const char* itemName = NppXml::attribute(childNode, "name");
			HTREEITEM addedItem = _treeView->addItem(string2wstring(itemName).c_str(), hParentItem, INDEX_CLOSED_NODE);
			if (NppXml::firstChild(childNode))
			{
				bool isOK = buildTreeFrom(childNode, addedItem);
				if (!isOK)
					return false;
			}
		}
		else if (std::strcmp(nodeName, "File") == 0)
		{
			std::wstring strValue = string2wstring(NppXml::attribute(childNode, "name"));
			// NPLX: workspaces written on Windows use '\\'
			for (wchar_t& c : strValue)
				if (c == L'\\')
					c = pathSep;
			const std::wstring fullPath = getAbsoluteFilePath(strValue.c_str());
			// NPLX: upstream asks before loading UNC paths (network); Linux network mounts are plain paths

			const wstring strValueLabel = fileNameOf(strValue);
			int iImage = doesFileExist(fullPath.c_str()) ? INDEX_LEAF : INDEX_LEAF_INVALID;
			_treeView->addItem(strValueLabel.c_str(), hParentItem, iImage, fullPath);
		}
	}
	return true;
}

wstring ProjectPanel::getAbsoluteFilePath(const wchar_t * relativePath)
{
	if (relativePath[0] == pathSep) // NPLX: was !::PathIsRelative
		return relativePath;

	wstring absolutePath = dirOf(_workSpaceFilePath);
	pathAppend(absolutePath, relativePath);
	return QDir::cleanPath(QString::fromStdWString(absolutePath)).toStdWString(); // NPLX: resolves "../" like PathAppend
}

void ProjectPanel::openSelectFile()
{
	HTREEITEM hItem = _treeView->getSelection();
	NodeType nType = getNodeType(hItem);
	const wstring fn = _treeView->getItemParam(hItem);
	if (nType == nodeType_file && !fn.empty())
	{
		if (doesFileExist(fn.c_str()))
		{
			// upstream ::PostMessage(_hParent, NPPM_DOOPEN, ...)
			if (_open)
				QMetaObject::invokeMethod(this, [this, fn]() { _open(fn); }, Qt::QueuedConnection);
			_treeView->setItemImage(hItem, INDEX_LEAF);
		}
		else
		{
			_treeView->setItemImage(hItem, INDEX_LEAF_INVALID);
		}
	}
}

void ProjectPanel::onLabelEdited(HTREEITEM item, const QString& oldLabel)
{
	// upstream TVN_ENDLABELEDIT
	if (getNodeType(item) == nodeType_root)
	{
		_treeView->renameItem(item, oldLabel.toStdWString().c_str()); // the root is the workspace file name
		return;
	}

	// For File, modify the lParam and its label
	wstring filePath = _treeView->getItemParam(item);
	if (!filePath.empty())
	{
		// Find the position of old label in File path
		const wstring oldName = oldLabel.toStdWString();
		size_t found = filePath.rfind(oldName);

		// If found the old label, replace it with the modified one
		if (found != wstring::npos)
			filePath.replace(found, oldName.size(), _treeView->getItemDisplayName(item));
		_treeView->setItemParam(item, filePath);

		// Check the validity of modified file path
		_treeView->setItemImage(item, doesFileExist(filePath.c_str()) ? INDEX_LEAF : INDEX_LEAF_INVALID);
	}

	setWorkSpaceDirty(true);
}

bool ProjectPanel::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == _treeView && event->type() == QEvent::KeyPress && !_treeView->isEditing())
	{
		// upstream TVN_KEYDOWN
		auto* keyEvent = static_cast<QKeyEvent*>(event);
		HTREEITEM hItem = _treeView->getSelection();
		const bool ctrl = keyEvent->modifiers() & Qt::ControlModifier;
		switch (keyEvent->key())
		{
			case Qt::Key_Delete:
			{
				NodeType nType = getNodeType(hItem);
				if (nType == nodeType_project || nType == nodeType_folder)
					popupMenuCmd(IDM_PROJECT_DELETEFOLDER);
				else if (nType == nodeType_file)
					popupMenuCmd(IDM_PROJECT_DELETEFILE);
				return true;
			}
			case Qt::Key_Return:
			case Qt::Key_Enter:
			{
				NodeType nType = getNodeType(hItem);
				if (nType == nodeType_file)
					openSelectFile();
				else
					_treeView->toggleExpandCollapse(hItem);
				return true;
			}
			case Qt::Key_Up:
				if (ctrl)
				{
					popupMenuCmd(IDM_PROJECT_MOVEUP);
					return true;
				}
				break;
			case Qt::Key_Down:
				if (ctrl)
				{
					popupMenuCmd(IDM_PROJECT_MOVEDOWN);
					return true;
				}
				break;
			case Qt::Key_F2:
				popupMenuCmd(IDM_PROJECT_RENAME);
				return true;
			default:
				break;
		}
	}
	return DockingPanel::eventFilter(watched, event);
}

void ProjectPanel::setWorkSpaceDirty(bool isDirty)
{
	_isDirty = isDirty;
	int iImg = _isDirty?INDEX_DIRTY_ROOT:INDEX_CLEAN_ROOT;
	_treeView->setItemImage(_treeView->getRoot(), iImg);
}

NodeType ProjectPanel::getNodeType(HTREEITEM hItem) const
{
	const int iImage = _treeView->getItemImage(hItem);

	// Root
	if (iImage == INDEX_CLEAN_ROOT || iImage == INDEX_DIRTY_ROOT)
	{
		return nodeType_root;
	}
	// Project
	else if (iImage == INDEX_PROJECT)
	{
		return nodeType_project;
	}
	// Folder
	else if (_treeView->getItemParam(hItem).empty())
	{
		return nodeType_folder;
	}
	// File
	else
	{
		return nodeType_file;
	}
}

void ProjectPanel::showContextMenu(const QPoint& pos)
{
	HTREEITEM hItem = _treeView->itemAt(pos);
	if (hItem != nullptr)
	{
		// Make item selected
		_treeView->selectItem(hItem);
		getMenuHandler(hItem)->popup(_treeView->viewport()->mapToGlobal(pos));
	}
}

QMenu* ProjectPanel::getMenuHandler(HTREEITEM selectedItem)
{
	// get clicked item type
	NodeType nodeType = getNodeType(selectedItem);
	if (nodeType == nodeType_root)
		return _hWorkSpaceMenu;
	else if (nodeType == nodeType_project)
		return _hProjectMenu;
	else if (nodeType == nodeType_folder)
		return _hFolderMenu;
	else //nodeType_file
		return _hFileMenu;
}

HTREEITEM ProjectPanel::addFolder(HTREEITEM hTreeItem, const wchar_t *folderName, bool editLabel)
{
	HTREEITEM addedItem = _treeView->addItem(folderName, hTreeItem, INDEX_CLOSED_NODE);

	_treeView->expand(hTreeItem);
	if (editLabel)
		_treeView->editLabel(addedItem);
	if (getNodeType(hTreeItem) == nodeType_folder)
		_treeView->setItemImage(hTreeItem, INDEX_OPEN_NODE);

	return addedItem;
}

bool ProjectPanel::saveWorkspaceRequest()
{ // returns true for continue and false for break
	if (_isDirty)
	{
		NativeLangSpeaker *pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();
		int res = pNativeSpeaker->messageBox("ProjectPanelOpenDoSaveDirtyWsOrNot",
					L"The current workspace was modified. Do you want to save the current project?",
					L"Open Workspace",
					MB_YESNOCANCEL | MB_ICONQUESTION | MB_APPLMODAL);

		if (res == IDYES)
		{
			if (!saveWorkSpace())
				return false;
		}
		else if (res == IDNO)
		{
			// Don't save so do nothing here
		}
		else if (res == IDCANCEL)
		{
			// User cancels action "New Workspace" so we interrupt here
			return false;
		}
	}
	return true;
}

void ProjectPanel::popupMenuCmd(int cmdID)
{
	// get selected item handle
	HTREEITEM hTreeItem = _treeView->getSelection();
	if (!hTreeItem && cmdID != IDB_PROJECT_BTN) // NPLX: the Workspace button works without a selection
		hTreeItem = _treeView->getRoot();
	if (!hTreeItem && cmdID != IDB_PROJECT_BTN)
		return;

	switch (cmdID)
	{
		//
		// Toolbar menu buttons
		//
		case IDB_PROJECT_BTN:
		{
			_hWorkSpaceMenu->popup(_workspaceButton->mapToGlobal(QPoint(0, _workspaceButton->height())));
		}
		break;

		case IDB_EDIT_BTN:
		{
			QMenu* hMenu = nullptr;
			NodeType nodeType = getNodeType(hTreeItem);
			if (nodeType == nodeType_project)
				hMenu = _hProjectMenu;
			else if (nodeType == nodeType_folder)
				hMenu = _hFolderMenu;
			else if (nodeType == nodeType_file)
				hMenu = _hFileMenu;
			if (hMenu)
				hMenu->popup(_editButton->mapToGlobal(QPoint(0, _editButton->height())));
		}
		break;

		//
		// Toolbar menu commands
		//
		case IDM_PROJECT_NEWPROJECT :
		{
			HTREEITEM root = _treeView->getRoot();

			NativeLangSpeaker *pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();
			wstring newProjectLabel = pNativeSpeaker ? pNativeSpeaker->getAttrNameStr(PM_NEWPROJECTNAME, "ProjectManager", "NewProjectName") : PM_NEWPROJECTNAME;
			HTREEITEM addedItem = _treeView->addItem(newProjectLabel.c_str(),  root, INDEX_PROJECT);
			setWorkSpaceDirty(true);
			_treeView->expand(hTreeItem);
			_treeView->editLabel(addedItem);
		}
		break;

		case IDM_PROJECT_NEWWS :
		{
			if (_isDirty)
			{
				NativeLangSpeaker *pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();
				int res = pNativeSpeaker->messageBox("ProjectPanelNewDoSaveDirtyWsOrNot",
					L"The current workspace was modified. Do you want to save the current project?",
					L"New Workspace",
					MB_YESNOCANCEL | MB_ICONQUESTION | MB_APPLMODAL);
				if (res == IDYES)
				{
					if (!saveWorkSpace())
						return;
				}
				else if (res == IDNO)
				{
					// Don't save so do nothing here
				}
				else if (res == IDCANCEL)
				{
					// User cancels action "New Workspace" so we interrupt here
					return;
				}
			}
			_treeView->removeAllItems();
			newWorkSpace();
		}
		break;

		case IDM_PROJECT_RENAME :
			_treeView->editLabel(hTreeItem);
		break;

		case IDM_PROJECT_NEWFOLDER :
		{
			NativeLangSpeaker *pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();
			wstring newFolderLabel = pNativeSpeaker ? pNativeSpeaker->getAttrNameStr(PM_NEWFOLDERNAME, "ProjectManager", "NewFolderName") : PM_NEWFOLDERNAME;
			addFolder(hTreeItem, newFolderLabel.c_str());
			setWorkSpaceDirty(true);
		}
		break;

		case IDM_PROJECT_MOVEDOWN :
		{
			if (_treeView->moveDown(hTreeItem))
				setWorkSpaceDirty(true);
		}
		break;

		case IDM_PROJECT_MOVEUP :
		{
			if (_treeView->moveUp(hTreeItem))
				setWorkSpaceDirty(true);
		}
		break;

		case IDM_PROJECT_ADDFILES :
		{
			addFiles(hTreeItem);
			if (getNodeType(hTreeItem) == nodeType_folder)
				_treeView->setItemImage(hTreeItem, INDEX_OPEN_NODE);
		}
		break;

		case IDM_PROJECT_ADDFILESRECUSIVELY :
		{
			addFilesFromDirectory(hTreeItem);
			if (getNodeType(hTreeItem) == nodeType_folder)
				_treeView->setItemImage(hTreeItem, INDEX_OPEN_NODE);
		}
		break;

		case IDM_PROJECT_OPENWS:
		{
			if (!saveWorkspaceRequest())
				break;

			// NPLX: was CustomFileDialog::doOpenSingleFileDlg
			const wstring fn = nplx::uiServices() ? nplx::uiServices()->getOpenFileName(L"Open Workspace", fileExtFilter(), dirOf(_workSpaceFilePath)) : wstring();
			if (!fn.empty())
			{
				if (!openWorkSpace(fn.c_str(), true))
				{
					NativeLangSpeaker *pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();
					pNativeSpeaker->messageBox("ProjectPanelOpenFailed",
						L"The workspace could not be opened.\rIt seems the file to open is not a valid project file.",
						L"Open Workspace",
						MB_OK);
					return;
				}
			}
		}
		break;

		case IDM_PROJECT_RELOADWS:
		{
			NativeLangSpeaker *pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();
			if (_isDirty)
			{
				int res = pNativeSpeaker->messageBox("ProjectPanelReloadDirty",
					L"The current workspace was modified. Reloading will discard all modifications.\rDo you want to continue?",
					L"Reload Workspace",
					MB_YESNO | MB_ICONQUESTION | MB_APPLMODAL);

				if (res == IDNO)
				{
					return;
				}
			}

			if (doesFileExist(_workSpaceFilePath.c_str()))
			{
				openWorkSpace(_workSpaceFilePath.c_str(), true);
			}
			else
			{
				pNativeSpeaker->messageBox("ProjectPanelReloadError",
					L"Cannot find the file to reload.",
					L"Reload Workspace",
					MB_OK);
			}
		}
		break;

		case IDM_PROJECT_SAVEWS:
			saveWorkSpace();
		break;

		case IDM_PROJECT_SAVEACOPYASWS:
		case IDM_PROJECT_SAVEASWS:
		{
			saveWorkSpaceAs(cmdID == IDM_PROJECT_SAVEACOPYASWS);
		}
		break;

		case IDM_PROJECT_FINDINPROJECTSWS:
		{
			if (_findInProjects)
				_findInProjects(1 << _panelID); // upstream NPPM_INTERNAL_FINDINPROJECTS
		}
		break;

		//
		// Context menu commands
		//
		case IDM_PROJECT_DELETEFOLDER :
		{
			HTREEITEM parent = _treeView->getParent(hTreeItem);

			if (_treeView->getChildFrom(hTreeItem) != nullptr)
			{
				NativeLangSpeaker *pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();
				int res = pNativeSpeaker->messageBox("ProjectPanelRemoveFolderFromProject",
					L"All the sub-items will be removed.\rAre you sure you want to remove this folder from the project?",
					L"Remove folder from project",
					MB_YESNO);
				if (res == IDYES)
				{
					_treeView->removeItem(hTreeItem);
					setWorkSpaceDirty(true);
				}
			}
			else
			{
				_treeView->removeItem(hTreeItem);
				setWorkSpaceDirty(true);
			}
			if (getNodeType(parent) == nodeType_folder && !_treeView->getChildFrom(parent))
				_treeView->setItemImage(parent, INDEX_CLOSED_NODE);
		}
		break;

		case IDM_PROJECT_DELETEFILE :
		{
			HTREEITEM parent = _treeView->getParent(hTreeItem);

			NativeLangSpeaker *pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();
			int res = pNativeSpeaker->messageBox("ProjectPanelRemoveFileFromProject",
				L"Are you sure you want to remove this file from the project?",
				L"Remove file from project",
				MB_YESNO);
			if (res == IDYES)
			{
				_treeView->removeItem(hTreeItem);
				setWorkSpaceDirty(true);
				if (getNodeType(parent) == nodeType_folder && !_treeView->getChildFrom(parent))
					_treeView->setItemImage(parent, INDEX_CLOSED_NODE);
			}
		}
		break;

		case IDM_PROJECT_MODIFYFILEPATH :
		{
			// upstream FileRelocalizerDlg
			wstring fn = _treeView->getItemParam(hTreeItem);
			if (fn.empty())
				return;

			wstring newValue = fn;
			if (nplx::uiServices() && nplx::uiServices()->getText(L"Change file full path name", L"New full path name:", newValue))
			{
				if (fn == newValue)
					return;

				_treeView->setItemParam(hTreeItem, newValue);
				_treeView->renameItem(hTreeItem, fileNameOf(newValue).c_str());
				_treeView->setItemImage(hTreeItem, doesFileExist(newValue.c_str()) ? INDEX_LEAF : INDEX_LEAF_INVALID);
				setWorkSpaceDirty(true);
			}
		}
		break;
	}
}

bool ProjectPanel::saveWorkSpaceAs(bool saveCopyAs)
{
	// NPLX: was CustomFileDialog::doSaveDlg
	wstring fn = nplx::uiServices() ? nplx::uiServices()->getSaveFileName(L"Save Workspace", fileExtFilter(), getWorkSpaceFilePath()) : wstring();
	if (fn.empty())
		return false;

	// upstream setDefExt: the defined workspace extension when none was given
	const wstring& ext = NppParameters::getInstance().getNppGUI()._definedWorkspaceExt;
	if (!ext.empty() && fileNameOf(fn).find(L'.') == wstring::npos)
		fn += (ext[0] == L'.' ? L"" : L".") + ext;

	if (!writeWorkSpace(fn.c_str(), !saveCopyAs))
		return false;

	if (!saveCopyAs)
	{
		_workSpaceFilePath = fn;
		setWorkSpaceDirty(false);
	}

	return true;
}

wstring ProjectPanel::fileExtFilter() const
{
	// upstream setFileExtFilter
	const wstring& ext = NppParameters::getInstance().getNppGUI()._definedWorkspaceExt;
	wstring filter;
	if (!ext.empty())
	{
		wstring workspaceExt = (ext[0] != L'.') ? L"." + ext : ext;
		filter = L"Workspace file (*" + workspaceExt + L");;";
	}
	filter += L"All types (*)";
	return filter;
}

void ProjectPanel::addFiles(HTREEITEM hTreeItem)
{
	// NPLX: was CustomFileDialog::doOpenMultiFilesDlg
	const std::vector<wstring> fns = nplx::uiServices() ? nplx::uiServices()->getOpenFileNames(L"Add Files", L"All types (*)", dirOf(_workSpaceFilePath)) : std::vector<wstring>();
	if (!fns.empty())
	{
		for (const wstring& fn : fns)
		{
			_treeView->addItem(fileNameOf(fn).c_str(), hTreeItem, INDEX_LEAF, fn);
		}
		_treeView->expand(hTreeItem);
		setWorkSpaceDirty(true);
	}
}

void ProjectPanel::recursiveAddFilesFrom(const wchar_t *folderPath, HTREEITEM hTreeItem)
{
	// NPLX: QDir in place of FindFirstFile; hidden (dot) folders are ignored like upstream's hidden folders
	const QFileInfoList infos = QDir(QString::fromWCharArray(folderPath)).entryInfoList(
		QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
	std::vector<wstring> files;

	for (const QFileInfo& fi : infos)
	{
		if (fi.isDir())
		{
			if (fi.isHidden()) // Hidden directories are ignored
				continue;
			// Always recursive
			HTREEITEM addedItem = addFolder(hTreeItem, fi.fileName().toStdWString().c_str(), false);
			recursiveAddFilesFrom(fi.absoluteFilePath().toStdWString().c_str(), addedItem);
		}
		else
		{
			files.push_back(fi.fileName().toStdWString());
		}
	}

	for (size_t i = 0, len = files.size() ; i < len ; ++i)
	{
		wstring pathFile(folderPath);
		if (pathFile.back() != pathSep)
			pathFile += pathSep;
		pathFile += files[i];
		_treeView->addItem(files[i].c_str(), hTreeItem, INDEX_LEAF, pathFile);
	}
}

void ProjectPanel::addFilesFromDirectory(HTREEITEM hTreeItem)
{
	if (_selDirOfFilesFromDirDlg == L"" && _workSpaceFilePath != L"")
		_selDirOfFilesFromDirDlg = dirOf(_workSpaceFilePath);

	// NPLX: was getFolderName (a folder dialog)
	wstring dirPath = nplx::uiServices() ? nplx::uiServices()->getExistingDirectory(L"Add Files from Directory") : wstring();
	if (dirPath != L"")
	{
		recursiveAddFilesFrom(dirPath.c_str(), hTreeItem);
		_treeView->expand(hTreeItem);
		setWorkSpaceDirty(true);
		_selDirOfFilesFromDirDlg = dirPath;
	}
}

void ProjectPanel::setBackgroundColor(COLORREF bgColour)
{
	_bgColour = QColor(GetRValue(bgColour), GetGValue(bgColour), GetBValue(bgColour));
	updateTreeColours();
}

void ProjectPanel::setForegroundColor(COLORREF fgColour)
{
	_fgColour = QColor(GetRValue(fgColour), GetGValue(fgColour), GetBValue(fgColour));
	updateTreeColours();
}

void ProjectPanel::updateTreeColours()
{
	QString qss;
	if (_bgColour.isValid())
		qss += QStringLiteral("background: %1; ").arg(_bgColour.name());
	if (_fgColour.isValid())
		qss += QStringLiteral("color: %1; ").arg(_fgColour.name());
	_treeView->setStyleSheet(qss.isEmpty() ? QString() : QStringLiteral("QTreeView { %1}").arg(qss));
}
