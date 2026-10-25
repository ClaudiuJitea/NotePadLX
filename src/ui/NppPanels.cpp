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

// NPLX: the docking panels of Notepad_plus (upstream Notepad_plus.cpp launch* functions, the docking part of
// Notepad_plus::init and saveGUIParams). QMainWindow is the docking manager: it docks, tabs and floats the panels.
// The open panels are saved like upstream (config.xml, DockingManager/PluginDlg, so the file stays compatible);
// the exact Qt layout (sizes, tab order, floating geometry) goes to dockingLayout.dat next to config.xml.

#include "Notepad_plus.h"

#include <algorithm>

#include <QFile>
#include <QDockWidget>
#include <QMenu>
#include <QScrollArea>
#include <QSaveFile>

#include "DockingCont.h"
#include "DockingPanel.h"
#include "AnsiCharPanel.h"
#include "DocumentMap.h"
#include "FileBrowser.h"
#include "ProjectPanel.h"
#include "UserDefineDialog.h"
#include "FindReplaceDlg.h"
#include "ClipboardHistoryPanel.h"
#include "DocTabView.h"
#include "FunctionListPanel.h"
#include "VerticalFileSwitcher.h"
#include "WindowsDlg.h"
#include "NppConstants.h"
#include "Parameters.h"
#include "ScintillaEditView.h"
#include "menuCmdID.h"

namespace
{
	constexpr int DockLayoutVersion = 1;

	QString dockLayoutPath()
	{
		std::wstring path = NppParameters::getInstance().getUserPath();
		pathAppend(path, L"dockingLayout.dat");
		return QString::fromStdWString(path);
	}

	int containerOf(Qt::DockWidgetArea area)
	{
		switch (area)
		{
			case Qt::LeftDockWidgetArea:   return CONT_LEFT;
			case Qt::RightDockWidgetArea:  return CONT_RIGHT;
			case Qt::TopDockWidgetArea:    return CONT_TOP;
			case Qt::BottomDockWidgetArea: return CONT_BOTTOM;
			default:                       return CONT_RIGHT;
		}
	}

	Qt::DockWidgetArea areaOf(int cont)
	{
		switch (cont)
		{
			case CONT_LEFT:   return Qt::LeftDockWidgetArea;
			case CONT_TOP:    return Qt::TopDockWidgetArea;
			case CONT_BOTTOM: return Qt::BottomDockWidgetArea;
			default:          return Qt::RightDockWidgetArea;
		}
	}
}

void Notepad_plus::registerPanel(DockingPanel* panel, Qt::DockWidgetArea defaultArea)
{
	// upstream NPPM_DMMREGASDCKDLG + getIntegralDockingData: the container saved in config.xml, else the default
	const DockingManagerData& dockingData = NppParameters::getInstance().getNppGUI()._dockingData;
	Qt::DockWidgetArea area = defaultArea;
	bool isFloating = false;
	for (const PluginDlgDockingInfo& pddi : dockingData._pluginDockInfo)
	{
		if (pddi._name == NPP_INTERNAL_FUNCTION_STR && pddi._internalID == panel->dlgID())
		{
			isFloating = pddi._currContainer >= DOCKCONT_MAX;
			area = areaOf(isFloating ? pddi._prevContainer : pddi._currContainer);
			break;
		}
	}

	// the panels of a container share it as tabs, like upstream
	DockingPanel* sameArea = nullptr;
	for (DockingPanel* other : _panels)
	{
		if (!other->isClosed() && !other->isFloating() && dockWidgetArea(other) == area)
			sameArea = other;
	}

	addDockWidget(area, panel);
	if (sameArea)
		tabifyDockWidget(sameArea, panel);
	panel->setFloating(isFloating);
	panel->setCloseHandler([this](int dlgID) { command(dlgID); }); // upstream DMN_CLOSE
	_panels.push_back(panel);
}

void Notepad_plus::launchFunctionList()
{
	if (!_pFuncList)
	{
		_pFuncList = new FunctionListPanel(this);
		_pFuncList->init(&_pEditView);

		// define the default docking behaviour (upstream DWS_DF_CONT_RIGHT)
		registerPanel(_pFuncList, Qt::RightDockWidgetArea);

		NppParameters& nppParam = NppParameters::getInstance();
		COLORREF fgColor = nppParam.getCurrentDefaultFgColor();
		COLORREF bgColor = nppParam.getCurrentDefaultBgColor();

		_pFuncList->setBackgroundColor(bgColor);
		_pFuncList->setForegroundColor(fgColor);
	}

	_pFuncList->display();
	_pFuncList->reload(); // NPLX: upstream gets DMN_SWITCHIN when the panel is shown
}

void Notepad_plus::launchDocumentListPanel()
{
	if (!_pDocumentListPanel)
	{
		_pDocumentListPanel = new VerticalFileSwitcher(this);

		// NPLX: what upstream VerticalFileSwitcher sends to its parent
		VerticalFileSwitcher::Host host;
		host._getTaskListInfo = [this](TaskListInfo& tli) { taskListInfo(&tli, true); };
		host._activateDoc = [this](BufferID bufferID, int view) {
			// upstream activateDoc: NPPM_GETPOSFROMBUFFERID + NPPM_ACTIVATEDOC
			if (bufferID == _pEditView->getCurrentBufferID() && view == currentView())
				return;
			switchEditViewTo(view);
			activateBuffer(bufferID, view);
		};
		host._closeDoc = [this](BufferID bufferID, int view) {
			// upstream closeDoc: NPPM_INTERNAL_CLOSEDOC (closes a document without switching to it)
			switchEditViewTo(view);
			fileClose(bufferID, view);
		};
		host._showTabContextMenu = [this](const QPoint& globalPos) { showTabContextMenu(currentView(), globalPos); };
		host._command = [this](int cmdID) { command(cmdID); };
		_pDocumentListPanel->init(std::move(host));

		// define the default docking behaviour (upstream DWS_DF_CONT_LEFT)
		registerPanel(_pDocumentListPanel, Qt::LeftDockWidgetArea);

		NppParameters& nppParam = NppParameters::getInstance();
		_pDocumentListPanel->setBackgroundColor(nppParam.getCurrentDefaultBgColor());
		_pDocumentListPanel->setForegroundColor(nppParam.getCurrentDefaultFgColor());
	}

	_pDocumentListPanel->display();
}

void Notepad_plus::launchAnsiCharPanel()
{
	if (!_pAnsiCharPanel)
	{
		_pAnsiCharPanel = new AnsiCharPanel(this);
		_pAnsiCharPanel->init(&_pEditView);

		// define the default docking behaviour (upstream DWS_DF_CONT_RIGHT)
		registerPanel(_pAnsiCharPanel, Qt::RightDockWidgetArea);

		NppParameters& nppParam = NppParameters::getInstance();
		_pAnsiCharPanel->setBackgroundColor(nppParam.getCurrentDefaultBgColor());
		_pAnsiCharPanel->setForegroundColor(nppParam.getCurrentDefaultFgColor());
	}

	_pAnsiCharPanel->display();
}

void Notepad_plus::launchDocMap()
{
	// NPLX: upstream refuses without window transparency (the view zone is a translucent window); here it is painted
	if (!_pDocMap)
	{
		_pDocMap = new DocumentMap(this);
		_pDocMap->init(&_pEditView);

		// define the default docking behaviour (upstream DWS_DF_CONT_RIGHT)
		registerPanel(_pDocMap, Qt::RightDockWidgetArea);
	}

	_pDocMap->initWrapMap();
	_pDocMap->wrapMap();
	_pDocMap->display();

	_pEditView->setFocus();
}

// if pFileBrowserRoots is not NULL, then pFileBrowserRoots is used, otherwise folders is used.
void Notepad_plus::launchFileBrowser(const std::vector<std::wstring>& folders, const std::wstring& selectedItemPath, bool fromScratch, std::vector<FileBrowserRootsInfo>* pFileBrowserRoots)
{
	if (!_pFileBrowser)
	{
		_pFileBrowser = new FileBrowser(this);

		// NPLX: what upstream FileBrowser sends to its parent
		FileBrowser::Host host;
		host._currentFilePath = [this]() { return std::wstring(_pEditView->getCurrentBuffer()->getFullPathName()); }; // NPPM_GETFULLCURRENTPATH
		host._open = [this](const std::wstring& path) { nppMessage(NPPM_DOOPEN, 0, reinterpret_cast<sptr_t>(path.c_str())); };
		host._findInFiles = [this](const std::wstring& dir) {
			// upstream NPPM_LAUNCHFINDINFILESDLG
			_findReplaceDlg->doDialog(FINDINFILES_DLG, _nativeLangSpeaker.isRTL());
			_findReplaceDlg->setSearchTextWithSettings();
			setFindReplaceFolderFilter(dir.c_str(), nullptr);
		};
		_pFileBrowser->init(std::move(host));

		// define the default docking behaviour (upstream DWS_DF_CONT_LEFT)
		registerPanel(_pFileBrowser, Qt::LeftDockWidgetArea);

		NppParameters& nppParams = NppParameters::getInstance();
		_pFileBrowser->setBackgroundColor(nppParams.getCurrentDefaultBgColor());
		_pFileBrowser->setForegroundColor(nppParams.getCurrentDefaultFgColor());
	}

	if (fromScratch)
	{
		_pFileBrowser->deleteAllFromTree();
	}

	if (pFileBrowserRoots)
	{
		for (auto i : *pFileBrowserRoots)
		{
			_pFileBrowser->addRootFolder(i._root, &(i._expandedPaths));
		}
	}
	else
	{
		for (const auto& i : folders)
		{
			_pFileBrowser->addRootFolder(i);
		}
	}

	_pFileBrowser->display();
	_pFileBrowser->selectItemFromPath(selectedItemPath);

	checkMenuItem(IDM_VIEW_FILEBROWSER, true);
	_pFileBrowser->setClosed(false);
}

bool Notepad_plus::saveFileBrowserParam()
{
	if (_pFileBrowser)
	{
		std::vector<std::wstring> rootPaths = _pFileBrowser->getRoots();
		std::wstring selectedItemPath = _pFileBrowser->getSelectedItemPath();
		auto expandedPaths = _pFileBrowser->getExpandedPathsFromFaW();
		std::unordered_set<std::wstring> transportPaths(expandedPaths.begin(), expandedPaths.end());
		return (NppParameters::getInstance()).writeFileBrowserSettings(rootPaths, selectedItemPath, transportPaths);
	}
	return true; // nothing to save so true is returned
}

void Notepad_plus::launchProjectPanel(int cmdID, ProjectPanel ** pProjPanel, int panelID)
{
	NppParameters& nppParam = NppParameters::getInstance();
	if (!(*pProjPanel))
	{
		(*pProjPanel) = new ProjectPanel(panelID, this);
		(*pProjPanel)->init(
			[this](const std::wstring& path) { nppMessage(NPPM_DOOPEN, 0, reinterpret_cast<sptr_t>(path.c_str())); },
			[this](int panelMask) {
				// upstream NPPM_INTERNAL_FINDINPROJECTS
				_findReplaceDlg->doDialog(FINDINPROJECTS_DLG, _nativeLangSpeaker.isRTL());
				_findReplaceDlg->setSearchTextWithSettings();
				_findReplaceDlg->setProjectCheckmarks(nullptr, panelMask);
			});
		(*pProjPanel)->setWorkSpaceFilePath(nppParam.getWorkSpaceFilePath(panelID));

		std::wstring title_no = std::to_wstring(panelID + 1);
		NativeLangSpeaker *pNativeSpeaker = nppParam.getNativeLangSpeaker();
		std::wstring title_temp = (pNativeSpeaker ? pNativeSpeaker->getAttrNameStr(PM_PROJECTPANELTITLE, "ProjectManager", "PanelTitle") : std::wstring(PM_PROJECTPANELTITLE)) + L" " + title_no;
		(*pProjPanel)->setPanelTitle(title_temp);

		// define the default docking behaviour (upstream DWS_DF_CONT_LEFT)
		registerPanel(*pProjPanel, Qt::LeftDockWidgetArea);

		(*pProjPanel)->setBackgroundColor(nppParam.getCurrentDefaultBgColor());
		(*pProjPanel)->setForegroundColor(nppParam.getCurrentDefaultFgColor());
		(*pProjPanel)->startWorkspace(); // upstream WM_INITDIALOG
	}
	else
	{
		(*pProjPanel)->openWorkSpace(nppParam.getWorkSpaceFilePath(panelID));
	}
	(*pProjPanel)->display();

	checkMenuItem(cmdID, true);
	checkProjectMenuItem();
	(*pProjPanel)->setClosed(false);
}

void Notepad_plus::checkProjectMenuItem()
{
	// upstream: the "Project Panels" sub-menu of View is checked when one of its panels is
	const QAction* panel1 = actionFor(IDM_VIEW_PROJECT_PANEL_1);
	if (!panel1)
		return;
	auto* subMenu = qobject_cast<QMenu*>(panel1->parent());
	if (!subMenu || !subMenu->menuAction())
		return;
	const bool checked = isMenuItemChecked(IDM_VIEW_PROJECT_PANEL_1) || isMenuItemChecked(IDM_VIEW_PROJECT_PANEL_2) || isMenuItemChecked(IDM_VIEW_PROJECT_PANEL_3);
	subMenu->menuAction()->setCheckable(true);
	subMenu->menuAction()->setChecked(checked);
}

bool Notepad_plus::saveProjectPanelsParams()
{
	NppParameters& nppParams = NppParameters::getInstance();

	if (_pProjectPanel_1)
	{
		if (!_pProjectPanel_1->checkIfNeedSave()) return false;
		nppParams.setWorkSpaceFilePath(0, _pProjectPanel_1->getWorkSpaceFilePath());
	}
	if (_pProjectPanel_2)
	{
		if (!_pProjectPanel_2->checkIfNeedSave()) return false;
		nppParams.setWorkSpaceFilePath(1, _pProjectPanel_2->getWorkSpaceFilePath());
	}
	if (_pProjectPanel_3)
	{
		if (!_pProjectPanel_3->checkIfNeedSave()) return false;
		nppParams.setWorkSpaceFilePath(2, _pProjectPanel_3->getWorkSpaceFilePath());
	}
	return nppParams.writeProjectPanelsSettings();
}

bool Notepad_plus::createFilelistForProjects(std::vector<std::wstring> & fileNames)
{
	std::vector<std::wstring> patterns2Match;
	_findReplaceDlg->getAndValidatePatterns(patterns2Match);
	bool somethingIsSelected = false; // at least one Project Panel is open and checked

	if (_findReplaceDlg->isProjectPanel_1() && _pProjectPanel_1 && !_pProjectPanel_1->isClosed())
	{
		_pProjectPanel_1->enumWorkSpaceFiles (NULL, patterns2Match, fileNames);
		somethingIsSelected = true;
	}
	if (_findReplaceDlg->isProjectPanel_2() && _pProjectPanel_2 && !_pProjectPanel_2->isClosed())
	{
		_pProjectPanel_2->enumWorkSpaceFiles (NULL, patterns2Match, fileNames);
		somethingIsSelected = true;
	}
	if (_findReplaceDlg->isProjectPanel_3() && _pProjectPanel_3 && !_pProjectPanel_3->isClosed())
	{
		_pProjectPanel_3->enumWorkSpaceFiles (NULL, patterns2Match, fileNames);
		somethingIsSelected = true;
	}
	return somethingIsSelected;
}

bool Notepad_plus::findInProjects()
{
	std::vector<std::wstring> projectFiles;
	if (! createFilelistForProjects(projectFiles))
		return false;

	std::vector<SearchCandidate> fileNames;
	fileNames.reserve(projectFiles.size());
	for (auto&& path : projectFiles)
		fileNames.push_back(SearchCandidate{ std::move(path), -1 });

	return findInFilelist(fileNames);
}

bool Notepad_plus::replaceInProjects()
{
	std::vector<std::wstring> fileNames;
	if (!createFilelistForProjects(fileNames))
		return false;

	return replaceInFilelist(fileNames);
}

void Notepad_plus::dockUserDlg()
{
	// upstream: the UDL dialog in a splitter on the right of the documents (SplitterMode::RIGHT_FIX).
	// NPLX: a dock widget on the right; it scrolls when the window is lower than the dialog
	if (!_udlDock)
	{
		_udlDock = new QDockWidget(_userDefineDlg->windowTitle(), this);
		_udlDock->setObjectName(QStringLiteral("UserDefineDialog"));
		_udlDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
		_udlDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
		auto* scroll = new QScrollArea(_udlDock);
		scroll->setWidgetResizable(false);
		scroll->setFrameShape(QFrame::NoFrame);
		_udlDock->setWidget(scroll);
		addDockWidget(Qt::RightDockWidgetArea, _udlDock);
		connect(_udlDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
			if (!visible && _udlDock->isHidden() && _userDefineDlg->isDocked())
				checkMenuItem(IDM_LANG_USER_DLG, false); // its close button
		});
	}

	auto* scroll = qobject_cast<QScrollArea*>(_udlDock->widget());
	scroll->takeWidget();
	scroll->setWidget(_userDefineDlg); // a child widget now (setParent strips the window type)
	_userDefineDlg->show();
	_udlDock->setWindowTitle(_userDefineDlg->windowTitle());
	_udlDock->show();
	_udlDock->raise();
}

void Notepad_plus::undockUserDlg()
{
	// upstream: the splitter is hidden and the dialog is a window again
	if (_udlDock)
	{
		if (auto* scroll = qobject_cast<QScrollArea*>(_udlDock->widget()))
			scroll->takeWidget();
		_udlDock->hide();
	}
	_userDefineDlg->setParent(this, Qt::Dialog);
	_userDefineDlg->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
	_userDefineDlg->doDialog();
}

void Notepad_plus::applyUserDefineDlgStatus()
{
	// upstream Notepad_plus::init, "User Define Dialog Section"
	const int uddStatus = NppParameters::getInstance().getNppGUI()._userDefineDlgStatus;
	UserDefineDialog *udd = _userDefineDlg;

	bool uddShow = false;
	switch (uddStatus)
	{
		case UDD_SHOW: // show & undocked
		{
			udd->doDialog(true, _nativeLangSpeaker.isRTL());
			uddShow = true;
			break;
		}
		case UDD_DOCKED: // hide & docked
		{
			_isUDDocked = true;
			break;
		}
		case (UDD_SHOW | UDD_DOCKED): // show & docked
		{
			udd->doDialog(true, _nativeLangSpeaker.isRTL());
			udd->command(IDC_DOCK_BUTTON);
			uddShow = true;
			break;
		}
		default: // hide & undocked
			break;
	}
	checkMenuItem(IDM_LANG_USER_DLG, uddShow);
}

void Notepad_plus::drawDocumentMapColoursFromStylerArray()
{
	Style* docMap = NppParameters::getInstance().getMiscStylerArray().findByName(VIEWZONE_DOCUMENTMAP);
	// NPLX: upstream compares static_cast<long>(colour) != -1, never false where long is 64-bit
	if (docMap && docMap->_fgColor != static_cast<COLORREF>(-1))
		ViewZoneDlg::setColour(docMap->_fgColor, ViewZoneDlg::ViewZoneColorIndex::focus);
	if (docMap && docMap->_bgColor != static_cast<COLORREF>(-1))
		ViewZoneDlg::setColour(docMap->_bgColor, ViewZoneDlg::ViewZoneColorIndex::frost);
}

void Notepad_plus::launchClipboardHistoryPanel()
{
	if (!_pClipboardHistoryPanel)
	{
		_pClipboardHistoryPanel = new ClipboardHistoryPanel(this);
		_pClipboardHistoryPanel->init(&_pEditView);

		// define the default docking behaviour (upstream DWS_DF_CONT_RIGHT)
		registerPanel(_pClipboardHistoryPanel, Qt::RightDockWidgetArea);

		NppParameters& nppParam = NppParameters::getInstance();
		_pClipboardHistoryPanel->setBackgroundColor(nppParam.getCurrentDefaultBgColor());
		_pClipboardHistoryPanel->setForegroundColor(nppParam.getCurrentDefaultFgColor());
	}

	_pClipboardHistoryPanel->display();
}

void Notepad_plus::getTaskListInfo(TaskListInfo *tli)
{
	int currentNbDoc = static_cast<int32_t>(_pDocTab->nbItem());
	int nonCurrentNbDoc = static_cast<int32_t>(_pNonDocTab->nbItem());

	tli->_currentIndex = 0;

	if (!viewVisible(otherView()))
		nonCurrentNbDoc = 0;

	for (int i = 0 ; i < currentNbDoc ; ++i)
	{
		BufferID bufID = _pDocTab->getBufferByIndex(i);
		Buffer * b = MainFileManager.getBufferByID(bufID);
		int status = b->isMonitoringOn()?tb_monitored:(b->isReadOnly()?tb_ro:(b->isDirty()?tb_unsaved:tb_saved));
		tli->_tlfsLst.push_back(TaskLstFnStatus(currentView(), i, b->getFullPathName(), status, (void *)bufID, b->getDocColorId()));
	}
	for (int i = 0 ; i < nonCurrentNbDoc ; ++i)
	{
		BufferID bufID = _pNonDocTab->getBufferByIndex(i);
		Buffer * b = MainFileManager.getBufferByID(bufID);
		int status = b->isMonitoringOn()?tb_monitored:(b->isReadOnly()?tb_ro:(b->isDirty()?tb_unsaved:tb_saved));
		tli->_tlfsLst.push_back(TaskLstFnStatus(otherView(), i, b->getFullPathName(), status, (void *)bufID, b->getDocColorId()));
	}
}

void Notepad_plus::taskListInfo(TaskListInfo* tli, bool forDocList)
{
	// upstream WM_GETTASKLISTINFO (lParam: forDocList)
	getTaskListInfo(tli);

	if (forDocList)
	{
		for (size_t idx = 0; idx < tli->_tlfsLst.size(); ++idx)
		{
			if (tli->_tlfsLst[idx]._iView == currentView() &&
				tli->_tlfsLst[idx]._docIndex == _pDocTab->getCurrentTabIndex())
			{
				tli->_currentIndex = static_cast<int>(idx);
				break;
			}
		}
		return;
	}

	if (NppParameters::getInstance().getNppGUI()._styleMRU)
	{
		tli->_currentIndex = 0;
		// upstream SortTaskListPred: the most recently used first
		DocTabView* views[2] = { _mainDocTab, _subDocTab };
		std::sort(tli->_tlfsLst.begin(), tli->_tlfsLst.end(), [&views](const TaskLstFnStatus &l, const TaskLstFnStatus &r) {
			BufferID lID = views[l._iView]->getBufferByIndex(l._docIndex);
			BufferID rID = views[r._iView]->getBufferByIndex(r._docIndex);
			Buffer * bufL = MainFileManager.getBufferByID(lID);
			Buffer * bufR = MainFileManager.getBufferByID(rID);
			return bufL->getRecentTag() > bufR->getRecentTag();
		});
	}
	else
	{
		for (size_t idx = 0; idx < tli->_tlfsLst.size(); ++idx)
		{
			if (tli->_tlfsLst[idx]._iView == currentView() &&
			    tli->_tlfsLst[idx]._docIndex == _pDocTab->getCurrentTabIndex())
			{
				tli->_currentIndex = static_cast<int>(idx);
				break;
			}
		}
	}
}

void Notepad_plus::activateNextDoc(bool direction)
{
	int nbDoc = static_cast<int32_t>(_pDocTab->nbItem());

	int curIndex = _pDocTab->getCurrentTabIndex();
	curIndex += (direction == dirUp)?-1:1;

	if (curIndex >= nbDoc)
	{
		if (viewVisible(otherView()))
			switchEditViewTo(otherView());
		curIndex = 0;
	}
	else if (curIndex < 0)
	{
		if (viewVisible(otherView()))
		{
			switchEditViewTo(otherView());
			nbDoc = static_cast<int32_t>(_pDocTab->nbItem());
		}
		curIndex = nbDoc - 1;
	}

	BufferID id = _pDocTab->getBufferByIndex(curIndex);
	activateBuffer(id, currentView());
}

void Notepad_plus::activateDoc(size_t pos)
{
	size_t nbDoc = _pDocTab->nbItem();
	if (pos == static_cast<size_t>(_pDocTab->getCurrentTabIndex()))
	{
		Buffer * buf = _pEditView->getCurrentBuffer();
		buf->increaseRecentTag();
		return;
	}

	if (pos < nbDoc)
	{
		BufferID id = _pDocTab->getBufferByIndex(pos);
		activateBuffer(id, currentView());
	}
}

void Notepad_plus::windowsDlgNotify(NMWINDLG& nmdlg)
{
	// upstream NppBigSwitch WDN_NOTIFY
	switch (nmdlg.type)
	{
		case WDT_ACTIVATE:
		{
			activateDoc(nmdlg.curSel);
			nmdlg.processed = true;
			break;
		}

		case WDT_SAVE:
		{
			//loop through nmdlg->nItems, get index and save it
			for (unsigned int i = 0; i < nmdlg.nItems; ++i)
			{
				fileSave(_pDocTab->getBufferByIndex(nmdlg.Items[i]));
			}
			nmdlg.processed = true;
			break;
		}

		case WDT_CLOSE:
		{
			//loop through nmdlg->nItems, get index and close it
			for (unsigned int i = 0; i < nmdlg.nItems; ++i)
			{
				bool closed = fileClose(_pDocTab->getBufferByIndex(nmdlg.Items[i]), currentView());
				unsigned int pos = nmdlg.Items[i];
				// The window list only needs to be rearranged when the file was actually closed
				if (closed)
				{
					nmdlg.Items[i] = 0xFFFFFFFF; // indicate file was closed

					// Shift the remaining items downward to fill the gap
					for (unsigned int j = i + 1; j < nmdlg.nItems; ++j)
					{
						if (nmdlg.Items[j] > pos)
							nmdlg.Items[j]--;
					}
				}
			}
			nmdlg.processed = true;
			break;
		}

		case WDT_SORT:
		{
			if (nmdlg.nItems != _pDocTab->nbItem())	//sanity check, if mismatch just abort
				break;

			//Collect all buffers
			std::vector<BufferID> tempBufs;
			for (unsigned int i = 0; i < nmdlg.nItems; ++i)
			{
				tempBufs.push_back(_pDocTab->getBufferByIndex(i));
			}
			//Reset buffers
			for (unsigned int i = 0; i < nmdlg.nItems; ++i)
			{
				_pDocTab->setBuffer(i, tempBufs[nmdlg.Items[i]]);
			}
			activateBuffer(_pDocTab->getBufferByIndex(_pDocTab->getCurrentTabIndex()), currentView());

			// upstream NPPM_INTERNAL_DOCORDERCHANGED
			if (_pDocumentListPanel)
				_pDocumentListPanel->updateTabOrder();
			break;
		}
	}
}

void Notepad_plus::initWindowsMenu(QMenu* hMenu)
{
	// upstream WindowsMenu::initPopupMenu for the Window menu: the documents of the current view at its end
	// (IDM_WINDOW_MRU_FIRST + index), the current one checked
	for (QAction* a : hMenu->actions())
	{
		const int id = a->data().toInt();
		if (id >= IDM_WINDOW_MRU_FIRST && id <= IDM_WINDOW_MRU_LIMIT)
			delete a;
	}

	const auto curDoc = _pDocTab->getCurrentTabIndex();
	size_t nMaxDoc = static_cast<size_t>(IDM_WINDOW_MRU_LIMIT) - IDM_WINDOW_MRU_FIRST + 1;
	size_t nDoc = std::min<size_t>(_pDocTab->nbItem(), nMaxDoc);

	for (size_t pos = 0; pos < nDoc; ++pos)
	{
		BufferID bufID = _pDocTab->getBufferByIndex(pos);
		Buffer* buf = MainFileManager.getBufferByID(bufID);

		std::wstring strBuffer(BuildMenuFileName(60, static_cast<int>(pos), buf->getFileName(), true));
		if (buf->isDirty())
			strBuffer += L'*';

		const int id = IDM_WINDOW_MRU_FIRST + static_cast<int>(pos);
		QAction* action = hMenu->addAction(QString::fromStdWString(strBuffer));
		action->setData(id);
		action->setCheckable(true);
		action->setChecked(static_cast<int>(pos) == curDoc);
		connect(action, &QAction::triggered, this, [this, id]() { command(id); });
	}
}

bool Notepad_plus::exportFunctionList()
{
	// upstream NPPM_INTERNAL_EXPORTFUNCLISTANDQUIT (the caller quits)
	checkMenuItem(IDM_VIEW_FUNC_LIST, true);
	launchFunctionList();
	_pFuncList->setClosed(false);
	return _pFuncList->serialize();
}

void Notepad_plus::saveDockingParams()
{
	// upstream saveGUIParams / _dockingManager: the internal panels and their container (config.xml)
	NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();
	std::vector<PluginDlgDockingInfo> infos;
	for (const PluginDlgDockingInfo& pddi : nppGUI._dockingData._pluginDockInfo)
	{
		if (pddi._name != NPP_INTERNAL_FUNCTION_STR) // plugin panels: kept as they are (Phase 9)
			infos.push_back(pddi);
	}
	for (DockingPanel* panel : _panels)
	{
		const int dockedCont = containerOf(dockWidgetArea(panel));
		const int curr = panel->isFloating() ? DOCKCONT_MAX : dockedCont;
		const int prev = panel->isFloating() ? dockedCont : -1;
		infos.emplace_back(NPP_INTERNAL_FUNCTION_STR, panel->dlgID(), curr, prev, !panel->isClosed() && panel->isVisible());
	}
	nppGUI._dockingData._pluginDockInfo = infos;

	// NPLX: the exact Qt layout
	QSaveFile f(dockLayoutPath());
	if (f.open(QIODevice::WriteOnly))
	{
		f.write(saveState(DockLayoutVersion));
		f.commit();
	}
}

void Notepad_plus::loadDockingParams()
{
	// upstream Notepad_plus::init: launch the internal panels that were visible (_internalFuncIDs)
	const NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();
	const std::vector<PluginDlgDockingInfo> infos = nppGUI._dockingData._pluginDockInfo;
	for (const PluginDlgDockingInfo& pdi : infos)
	{
		const bool isInternalFunc = pdi._name == NPP_INTERNAL_FUNCTION_STR;
		if (!isInternalFunc || !pdi._isVisible)
			continue;

		bool showPanel = true;
		if (nppGUI._isCmdlineNosessionActivated)
		{
			showPanel = (pdi._internalID == IDM_EDIT_CLIPBOARDHISTORY_PANEL && nppGUI._clipboardHistoryPanelKeepState)
				|| (pdi._internalID == IDM_VIEW_DOCLIST && nppGUI._docListKeepState)
				|| (pdi._internalID == IDM_EDIT_CHAR_PANEL && nppGUI._charPanelKeepState)
				|| (pdi._internalID == IDM_VIEW_FILEBROWSER && nppGUI._fileBrowserKeepState)
				|| (pdi._internalID == IDM_VIEW_DOC_MAP && nppGUI._docMapKeepState)
				|| (pdi._internalID == IDM_VIEW_FUNC_LIST && nppGUI._funcListKeepState);
		}

		if (showPanel && isCommandPorted(pdi._internalID) && !isMenuItemChecked(pdi._internalID))
			command(pdi._internalID);
	}

	// NPLX: the exact Qt layout of the panels just launched
	QFile f(dockLayoutPath());
	if (f.open(QIODevice::ReadOnly))
		restoreState(f.readAll(), DockLayoutVersion);
}

void Notepad_plus::saveUserDefineLangs()
{
	(NppParameters::getInstance()).writeNeed2SaveUDL();
}
