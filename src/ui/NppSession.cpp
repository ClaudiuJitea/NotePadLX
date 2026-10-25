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

// NPLX: sessions, ported from upstream NppIO.cpp (loadSession, saveSession, isFileSession, isFileWorkspace),
// Notepad_plus.cpp (getCurrentOpenedFiles, getLangFromMenu, getLangFromMenuName) and NppBigSwitch.cpp
// (NPPM_INTERNAL_SAVECURRENTSESSION).

#include "Notepad_plus.h"
#include "VerticalFileSwitcher.h"
#include "FileBrowser.h"

#include <QAction>

#include "DocTabView.h"
#include "Parameters.h"
#include "ScintillaEditView.h"
#include "menuCmdID.h"

using std::wstring;
using std::vector;

bool Notepad_plus::isFileSession(const wchar_t* filename)
{
	// if file2open matches the ext of user defined session file ext, then it'll be opened as a session
	const wchar_t* definedSessionExt = NppParameters::getInstance().getNppGUI()._definedSessionExt.c_str();
	if (*definedSessionExt != '\0')
	{
		wstring fncp = filename;
		const wchar_t* pExt = PathFindExtension(fncp.c_str());

		wstring usrSessionExt = L"";
		if (*definedSessionExt != '.')
		{
			usrSessionExt += L".";
		}
		usrSessionExt += definedSessionExt;

		if (!_wcsicmp(pExt, usrSessionExt.c_str()))
		{
			return true;
		}
	}
	return false;
}

bool Notepad_plus::isFileWorkspace(const wchar_t* filename)
{
	// if filename matches the ext of user defined workspace file ext, then it'll be opened as a workspace
	const wchar_t* definedWorkspaceExt = NppParameters::getInstance().getNppGUI()._definedWorkspaceExt.c_str();
	if (*definedWorkspaceExt != '\0')
	{
		wstring fncp = filename;
		const wchar_t* pExt = PathFindExtension(fncp.c_str());

		wstring usrWorkspaceExt = L"";
		if (*definedWorkspaceExt != '.')
		{
			usrWorkspaceExt += L".";
		}
		usrWorkspaceExt += definedWorkspaceExt;

		if (!_wcsicmp(pExt, usrWorkspaceExt.c_str()))
		{
			return true;
		}
	}
	return false;
}

// NPLX: upstream reads the menu item texts with GetMenuString; here the Language menu actions
wstring Notepad_plus::getLangFromMenu(const Buffer* buf)
{
	int	id;
	wstring userLangName;

	id = (NppParameters::getInstance()).langTypeToCommandID(buf->getLangType());
	if ((id != IDM_LANG_USER) || !(buf->isUserDefineLangExt()))
	{
		auto it = _langActions.find(id);
		if (it != _langActions.end())
			userLangName = it->second->text().toStdWString();
	}
	else
	{
		userLangName = buf->getUserDefineLangName();
	}
	return	userLangName;
}

int Notepad_plus::getLangFromMenuName(const wchar_t* langName)
{
	for (const auto& [id, action] : _langActions)
	{
		if (action->text().toStdWString() == langName)
			return id;
	}
	return 0;
}

void Notepad_plus::loadLastSession()
{
	NppParameters& nppParams = NppParameters::getInstance();
	const NppGUI& nppGui = nppParams.getNppGUI();
	Session lastSession = nppParams.getSession();
	bool isSnapshotMode = nppGui.isSnapshotMode();
	_isFolding = true;
	loadSession(lastSession, isSnapshotMode);
	_isFolding = false;
}

void Notepad_plus::saveSession(const Session& session)
{
	(NppParameters::getInstance()).writeSession(session);
}

void Notepad_plus::saveCurrentSession()
{
	// upstream: NPPM_INTERNAL_SAVECURRENTSESSION
	const NppGUI& nppGui = NppParameters::getInstance().getNppGUI();

	if (nppGui._rememberLastSession && !nppGui._isCmdlineNosessionActivated)
	{
		Session currentSession;
		getCurrentOpenedFiles(currentSession, true);
		NppParameters::getInstance().writeSession(currentSession);
	}
}

void Notepad_plus::getCurrentOpenedFiles(Session & session, bool includeUntitledDoc)
{
	_mainEditView->saveCurrentPos();	//save position so itll be correct in the session
	_subEditView->saveCurrentPos();	//both views
	session._activeView = currentView();
	session._activeMainIndex = _mainDocTab->getCurrentTabIndex();
	session._activeSubIndex = _subDocTab->getCurrentTabIndex();

	//Use _invisibleEditView to temporarily open documents to retrieve markers
	Document oldDoc = _invisibleEditView->execute(SCI_GETDOCPOINTER);
	const int nbElem = 2;
	DocTabView* docTab[nbElem]{};
	docTab[0] = _mainDocTab;
	docTab[1] = _subDocTab;
	for (size_t k = 0; k < nbElem; ++k)
	{
		for (size_t i = 0, len = docTab[k]->nbItem(); i < len ; ++i)
		{
			BufferID bufID = docTab[k]->getBufferByIndex(i);
			ScintillaEditView *editView = k == 0 ? _mainEditView : _subEditView;
			size_t activeIndex = k == 0 ? session._activeMainIndex : session._activeSubIndex;
			vector<sessionFileInfo> *viewFiles = (vector<sessionFileInfo> *)(k == 0?&(session._mainViewFiles):&(session._subViewFiles));

			Buffer * buf = MainFileManager.getBufferByID(bufID);

			if (buf->isUntitled() && buf->docLength() == 0)
				continue;

			if (!includeUntitledDoc)
				if (!doesFileExist(buf->getFullPathName()))
					continue;


			wstring	languageName = getLangFromMenu(buf);

			if (languageName.empty()) // The lang menu item in question is hidden
			{
				NppParameters& nppParam = NppParameters::getInstance();
				const NppGUI& nppGUI = nppParam.getNppGUI();

				for (size_t j = 0; j < nppGUI._excludedLangList.size(); ++j) // try to find it in exclude lang list
				{
					if (buf->getLangType() == nppGUI._excludedLangList[j]._langType)
					{
						languageName = nppGUI._excludedLangList[j]._langName;
						break;
					}
				}
			}

			const wchar_t* langName = languageName.c_str();
			sessionFileInfo sfi(buf->getFullPathName(), langName, buf->getEncoding(), buf->getUserReadOnly(), buf->isPinned(), buf->isUntitledTabRenamed(), buf->getPosition(editView), buf->getBackupFileName().c_str(), buf->getLastModifiedFileTimestamp(), buf->getMapPosition());

			sfi._isMonitoring = buf->isMonitoringOn();
			sfi._individualTabColour = -1; // NPLX_TODO(Phase 3): individual tab colours
			sfi._isRTL = buf->isRTL();

			_invisibleEditView->execute(SCI_SETDOCPOINTER, 0, buf->getDocument());

			auto nextMarkedLine = _invisibleEditView->execute(SCI_MARKERNEXT, 0, (1 << MARK_BOOKMARK));
			while (nextMarkedLine != -1)
			{
				sfi._marks.push_back(nextMarkedLine);
				nextMarkedLine = _invisibleEditView->execute(SCI_MARKERNEXT, nextMarkedLine + 1, (1 << MARK_BOOKMARK));
			}

			if (i == activeIndex)
			{
				editView->getCurrentFoldStates(sfi._foldStates);
			}
			else
			{
				sfi._foldStates = buf->getHeaderLineState(editView);
			}
			viewFiles->push_back(sfi);
		}
	}
	_invisibleEditView->execute(SCI_SETDOCPOINTER, 0, oldDoc);
}

bool Notepad_plus::loadSession(Session & session, bool isSnapshotMode, const wchar_t* userCreatedSessionName)
{
	NppParameters& nppParam = NppParameters::getInstance();
	const NppGUI& nppGUI = nppParam.getNppGUI();

	nppParam.setTheWarningHasBeenGiven(false);

	bool allSessionFilesLoaded = true;
	BufferID lastOpened = BUFFER_INVALID;
	//size_t i = 0;
	showView(MAIN_VIEW);
	switchEditViewTo(MAIN_VIEW);	//open files in main

	int mainIndex2Update = -1;

	// no session
	if (!session.nbMainFiles() && !session.nbSubFiles())
	{
		Buffer* buf = getCurrentBuffer();
		if (nppParam.getNativeLangSpeaker()->isRTL() && nppParam.getNativeLangSpeaker()->isEditZoneRTL())
			buf->setRTL(true);

		_mainEditView->changeTextDirection(buf->isRTL());
		return true;
	}

	for (size_t i = 0; i < session.nbMainFiles() ; )
	{
		const wchar_t *pFn = session._mainViewFiles[i]._fileName.c_str();

		if (isFileSession(pFn) || isFileWorkspace(pFn))
		{
			vector<sessionFileInfo>::iterator posIt = session._mainViewFiles.begin() + i;
			session._mainViewFiles.erase(posIt);
			continue;	//skip session files, not supporting recursive sessions or embedded workspace files
		}
		if (doesFileExist(pFn))
		{
			if (isSnapshotMode && !session._mainViewFiles[i]._backupFilePath.empty())
				lastOpened = doOpen(pFn, false, false, session._mainViewFiles[i]._encoding, session._mainViewFiles[i]._backupFilePath.c_str(), session._mainViewFiles[i]._originalFileLastModifTimestamp);
			else
				lastOpened = doOpen(pFn, false, false, session._mainViewFiles[i]._encoding);
		}
		else if (isSnapshotMode && doesFileExist(session._mainViewFiles[i]._backupFilePath.c_str()))
		{
			lastOpened = doOpen(pFn, false, false, session._mainViewFiles[i]._encoding, session._mainViewFiles[i]._backupFilePath.c_str(), session._mainViewFiles[i]._originalFileLastModifTimestamp);
		}
		else
		{
			BufferID foundBufID = MainFileManager.getBufferFromName(pFn);
			if (foundBufID == BUFFER_INVALID)
				lastOpened = nppGUI._keepSessionAbsentFileEntries ? MainFileManager.newPlaceholderDocument(pFn, MAIN_VIEW, userCreatedSessionName) : BUFFER_INVALID;
		}
		if (lastOpened != BUFFER_INVALID)
		{
			showView(MAIN_VIEW);
			const wchar_t* pLn = nullptr;
			LangType langTypeToSet = L_TEXT;
			Buffer* buf = MainFileManager.getBufferByID(lastOpened);

			if (!buf->isLargeFile())
			{
				pLn = session._mainViewFiles[i]._langName.c_str();

				int id = getLangFromMenuName(pLn);
				
				if (!id) // it could be due to the hidden language from the sub-menu "Languages"
				{
					for (size_t k = 0; k < nppGUI._excludedLangList.size(); ++k) // try to find it in exclude lang list
					{
						if (nppGUI._excludedLangList[k]._langName == pLn)
						{
							langTypeToSet = nppGUI._excludedLangList[k]._langType;
							break;
						}
					}
				}
				else if (id != IDM_LANG_USER)
					langTypeToSet = menuID2LangType(id);

				if (langTypeToSet == L_EXTERNAL)
					langTypeToSet = (LangType)(id - IDM_LANG_EXTERNAL + L_EXTERNAL);
			}
			

			if (session._mainViewFiles[i]._foldStates.size() > 0)
			{
				if (buf == _mainEditView->getCurrentBuffer()) // current document
					// Set folding state in the current document
					mainIndex2Update = static_cast<int>(i);
				else
					// Set fold states in the buffer
					buf->setHeaderLineState(session._mainViewFiles[i]._foldStates, _mainEditView);
			}

			buf->setPosition(session._mainViewFiles[i], _mainEditView);
			buf->setMapPosition(session._mainViewFiles[i]._mapPos);
			buf->setLangType(langTypeToSet, pLn);
			if (session._mainViewFiles[i]._encoding != -1)
				buf->setEncoding(session._mainViewFiles[i]._encoding);

			buf->setUserReadOnly(session._mainViewFiles[i]._isUserReadOnly || nppGUI._isFullReadOnly || nppGUI._isFullReadOnlySavingForbidden);

			buf->setPinned(session._mainViewFiles[i]._isPinned);

			buf->setUntitledTabRenamedStatus(session._mainViewFiles[i]._isUntitledTabRenamed);

			if (isSnapshotMode && !session._mainViewFiles[i]._backupFilePath.empty() && doesFileExist(session._mainViewFiles[i]._backupFilePath.c_str()))
				buf->setDirty(true);

			buf->setRTL(session._mainViewFiles[i]._isRTL);
			if (i == 0 && session._activeMainIndex == 0)
				_mainEditView->changeTextDirection(buf->isRTL());

			// NPLX_TODO(Phase 3): individual tab colours (_mainDocTab->setIndividualTabColour)

			//Force in the document so we can add the markers
			//Don't use default methods because of performance
			Document prevDoc = _mainEditView->execute(SCI_GETDOCPOINTER);
			unsigned long MODEVENTMASK_ON = nppParam.getScintillaModEventMask();
			_mainEditView->execute(SCI_SETMODEVENTMASK, MODEVENTMASK_OFF);
			_mainEditView->execute(SCI_SETDOCPOINTER, 0, buf->getDocument());
			_mainEditView->execute(SCI_SETMODEVENTMASK, MODEVENTMASK_ON);
			for (size_t j = 0, len = session._mainViewFiles[i]._marks.size(); j < len ; ++j)
			{
				_mainEditView->execute(SCI_MARKERADD, session._mainViewFiles[i]._marks[j], MARK_BOOKMARK);
			}
			_mainEditView->execute(SCI_SETMODEVENTMASK, MODEVENTMASK_OFF);
			_mainEditView->execute(SCI_SETDOCPOINTER, 0, prevDoc);
			_mainEditView->execute(SCI_SETMODEVENTMASK, MODEVENTMASK_ON);
			++i;
		}
		else
		{
			vector<sessionFileInfo>::iterator posIt = session._mainViewFiles.begin() + i;
			session._mainViewFiles.erase(posIt);
			allSessionFilesLoaded = false;
		}
	}

	if (mainIndex2Update != -1)
	{
		_isFolding = true;
		_mainEditView->syncFoldStateWith(session._mainViewFiles[mainIndex2Update]._foldStates);
		_isFolding = false;
	}


	showView(SUB_VIEW);
	switchEditViewTo(SUB_VIEW);	//open files in sub
	int subIndex2Update = -1;

	bool shouldQueryMainViewForClones = (session.nbMainFiles() > 0); // only if it has real docs/tabs (a view has also its implicit doc/tab otherwise)

	for (size_t k = 0 ; k < session.nbSubFiles() ; )
	{
		const wchar_t *pFn = session._subViewFiles[k]._fileName.c_str();

		if (isFileSession(pFn) || isFileWorkspace(pFn))
		{
			vector<sessionFileInfo>::iterator posIt = session._subViewFiles.begin() + k;
			session._subViewFiles.erase(posIt);
			continue;	//skip session files, not supporting recursive sessions or embedded workspace files
		}
		if (doesFileExist(pFn) || (isSnapshotMode && doesFileExist(session._subViewFiles[k]._backupFilePath.c_str())))
		{
			//check if already open in main. If so, clone
			BufferID clonedBuf = BUFFER_INVALID;
			if (shouldQueryMainViewForClones)
				clonedBuf = _mainDocTab->findBufferByName(pFn);

			if (clonedBuf != BUFFER_INVALID)
			{
				loadBufferIntoView(clonedBuf, SUB_VIEW);
				lastOpened = clonedBuf;
			}
			else
			{
				if (isSnapshotMode && !session._subViewFiles[k]._backupFilePath.empty())
					lastOpened = doOpen(pFn, false, false, session._subViewFiles[k]._encoding, session._subViewFiles[k]._backupFilePath.c_str(), session._subViewFiles[k]._originalFileLastModifTimestamp);
				else
					lastOpened = doOpen(pFn, false, false, session._subViewFiles[k]._encoding);
			}
		}
		else
		{
			BufferID foundBufID = MainFileManager.getBufferFromName(pFn);
			if (foundBufID == BUFFER_INVALID)
				lastOpened = nppGUI._keepSessionAbsentFileEntries ? MainFileManager.newPlaceholderDocument(pFn, SUB_VIEW, userCreatedSessionName) : BUFFER_INVALID;
		}

		if (lastOpened != BUFFER_INVALID)
		{
			showView(SUB_VIEW);
			if (canHideView(MAIN_VIEW))
				hideView(MAIN_VIEW);
			const wchar_t *pLn = session._subViewFiles[k]._langName.c_str();
			int id = getLangFromMenuName(pLn);
			LangType typeToSet = L_TEXT;

			if (id != 0)
				typeToSet = menuID2LangType(id);
			if (typeToSet == L_EXTERNAL )
				typeToSet = static_cast<LangType>(id - IDM_LANG_EXTERNAL + L_EXTERNAL);

			Buffer * buf = MainFileManager.getBufferByID(lastOpened);

			// Set fold states
			if (session._subViewFiles[k]._foldStates.size() > 0)
			{
				if (buf == _subEditView->getCurrentBuffer()) // current document
					// Set folding state in the current document
					subIndex2Update = static_cast<int>(k);
				else
					// Set fold states in the buffer
					buf->setHeaderLineState(session._subViewFiles[k]._foldStates, _subEditView);
			}

			buf->setPosition(session._subViewFiles[k], _subEditView);
			buf->setMapPosition(session._subViewFiles[k]._mapPos);
			if (typeToSet == L_USER)
			{
				if (!lstrcmp(pLn, L"User Defined"))
				{
					pLn = L"";	//default user defined
				}
			}
			buf->setLangType(typeToSet, pLn);
			buf->setEncoding(session._subViewFiles[k]._encoding);
			buf->setUserReadOnly(session._subViewFiles[k]._isUserReadOnly || nppGUI._isFullReadOnly || nppGUI._isFullReadOnlySavingForbidden);
			buf->setPinned(session._subViewFiles[k]._isPinned);

			buf->setUntitledTabRenamedStatus(session._subViewFiles[k]._isUntitledTabRenamed);

			if (isSnapshotMode && !session._subViewFiles[k]._backupFilePath.empty() && doesFileExist(session._subViewFiles[k]._backupFilePath.c_str()))
				buf->setDirty(true);

			buf->setRTL(session._subViewFiles[k]._isRTL);

			// NPLX_TODO(Phase 3): individual tab colours (_subDocTab->setIndividualTabColour)

			//Force in the document so we can add the markers
			//Don't use default methods because of performance
			Document prevDoc = _subEditView->execute(SCI_GETDOCPOINTER);
			unsigned long MODEVENTMASK_ON = nppParam.getScintillaModEventMask();
			_subEditView->execute(SCI_SETMODEVENTMASK, MODEVENTMASK_OFF);
			_subEditView->execute(SCI_SETDOCPOINTER, 0, buf->getDocument());
			_subEditView->execute(SCI_SETMODEVENTMASK, MODEVENTMASK_ON);
			for (size_t j = 0, len = session._subViewFiles[k]._marks.size(); j < len ; ++j)
			{
				_subEditView->execute(SCI_MARKERADD, session._subViewFiles[k]._marks[j], MARK_BOOKMARK);
			}
			_subEditView->execute(SCI_SETMODEVENTMASK, MODEVENTMASK_OFF);
			_subEditView->execute(SCI_SETDOCPOINTER, 0, prevDoc);
			_subEditView->execute(SCI_SETMODEVENTMASK, MODEVENTMASK_ON);

			++k;
		}
		else
		{
			vector<sessionFileInfo>::iterator posIt = session._subViewFiles.begin() + k;
			session._subViewFiles.erase(posIt);
			allSessionFilesLoaded = false;
		}
	}
	if (subIndex2Update != -1)
	{
		_isFolding = true;
		_subEditView->syncFoldStateWith(session._subViewFiles[subIndex2Update]._foldStates);
		_isFolding = false;
	}

	_mainEditView->restoreCurrentPosPreStep();
	_subEditView->restoreCurrentPosPreStep();

	if (session._activeMainIndex < session._mainViewFiles.size())
	{
		const wchar_t* fileName = session._mainViewFiles[session._activeMainIndex]._fileName.c_str();
		BufferID buf = _mainDocTab->findBufferByName(fileName);
		if (buf != BUFFER_INVALID)
			activateBuffer(buf, MAIN_VIEW);
	}

	if (session._activeSubIndex < session._subViewFiles.size())
	{
		const wchar_t* fileName = session._subViewFiles[session._activeSubIndex]._fileName.c_str();
		BufferID buf = _subDocTab->findBufferByName(fileName);
		if (buf != BUFFER_INVALID)
			activateBuffer(buf, SUB_VIEW);
	}

	if ((session.nbSubFiles() > 0) && (session._activeView == MAIN_VIEW || session._activeView == SUB_VIEW))
		switchEditViewTo(static_cast<int>(session._activeView));
	else
		switchEditViewTo(MAIN_VIEW);

	if (canHideView(otherView()))
		hideView(otherView());
	else if (canHideView(currentView()))
		hideView(currentView());

	checkSyncState();

	if (_pDocumentListPanel)
		_pDocumentListPanel->reload();

	if (userCreatedSessionName && !session._fileBrowserRoots.empty())
	{
		// If the session is user's created session but not session.xml, we force to launch Folder as Workspace and add roots
		std::vector<std::wstring> dummy; // use nppParam.getFileBrowserRoots() instead

		launchFileBrowser(dummy, session._fileBrowserSelectedItem, true, &(session._fileBrowserRoots));
	}

	// Especially File status auto-detection set on "Enable for all opened files":  nppGUI._fileAutoDetection & cdEnabled4AllFiles
	// when "Remember inaccessible files from past session" is enabled:             nppGUI._keepSessionAbsentFileEntries
	// there are some (or 1) absent files:                                          nppParam.theWarningHasBeenGiven()
	// and user want to create the placeholders for these files:                    nppParam.isPlaceHolderEnabled()
	//
	// When above conditions are true, the created placeholders are not read-only, due to the lack of file-detection on them.
	if (nppGUI._keepSessionAbsentFileEntries && nppParam.theWarningHasBeenGiven() && nppParam.isPlaceHolderEnabled() && (nppGUI._fileAutoDetection & cdEnabled4AllFiles))
	{
		MainFileManager.checkFilesystemChanges(false); // NPLX: was checkModifiedDocument(false) - launch file-detection for all placeholders manually
	}

	return allSessionFilesLoaded;
}

std::vector<wstring> Notepad_plus::loadCommandlineParams(const std::vector<wstring>& fnss, const CmdLineParamsDTO* pCmdParams)
{
	if (!pCmdParams)
		return std::vector<wstring>();

	NppParameters& nppParams = NppParameters::getInstance();
	const NppGUI& nppGUI = nppParams.getNppGUI();

	// loading file as session file is allowed only when there is only one file
	if (pCmdParams->_isSessionFile && fnss.size() == 1)
	{
		Session session2Load;
		const wchar_t* sessionFileName = fnss[0].c_str();
		if (nppParams.loadSession(session2Load, sessionFileName))
		{
			const bool isSnapshotMode = false;

			if (nppGUI._multiInstSetting == multiInstOnSession)
				nppParams.setLoadedSessionFilePath(sessionFileName);
			loadSession(session2Load, isSnapshotMode, sessionFileName);
		}
		return std::vector<wstring>();
	}

	LangType lt = pCmdParams->_langType;
	wstring udl = pCmdParams->_udlName;
	intptr_t lineNumber = pCmdParams->_line2go;
	intptr_t columnNumber = pCmdParams->_column2go;
	intptr_t positionNumber = pCmdParams->_pos2go;
	bool recursive = pCmdParams->_isRecursive;
	bool readOnly = pCmdParams->_isReadOnly;
	bool openFoldersAsWorkspace = pCmdParams->_openFoldersAsWorkspace;

	if (openFoldersAsWorkspace)
	{
		// All the filepath in argument will be used as folder in workspace
		// call launchFileBrowser later with fnss
		return fnss;
	}

	BufferID lastOpened = BUFFER_INVALID;
	for (size_t i = 0, len = fnss.size(); i < len; ++i)
	{
		const wchar_t* pFn = fnss[i].c_str();

		BufferID bufID = doOpen(pFn, recursive, readOnly);
		if (bufID == BUFFER_INVALID)	//cannot open file
			continue;

		lastOpened = bufID;
		Buffer* pBuf = MainFileManager.getBufferByID(bufID);

		if (!udl.empty())
		{
			pBuf->setLangType(L_USER, udl.c_str());
		}
		else if (lt != L_EXTERNAL && lt < nppParams.L_END)
		{
			pBuf->setLangType(lt);
		}

		if (lineNumber >= 0 || positionNumber >= 0)
		{
			//we have to move the cursor manually
			int iView = currentView();	//store view since fileswitch can cause it to change
			switchToFile(bufID);	//switch to the file. No deferred loading, but this way we can easily move the cursor to the right position

			if (positionNumber >= 0)
			{
				if (positionNumber > 0)
				{
					// make sure not jumping into the middle of a multibyte character
					// or into the middle of a CR/LF pair for Windows files
					auto before = _pEditView->execute(SCI_POSITIONBEFORE, positionNumber);
					positionNumber = _pEditView->execute(SCI_POSITIONAFTER, before);
				}
				_pEditView->execute(SCI_GOTOPOS, positionNumber);
			}
			else if (columnNumber < 0)
			{
				_pEditView->execute(SCI_GOTOLINE, lineNumber - 1);
			}
			else
			{
				auto pos = _pEditView->execute(SCI_FINDCOLUMN, lineNumber - 1, columnNumber - 1);
				_pEditView->execute(SCI_GOTOPOS, pos);
			}

			_pEditView->scrollPosToCenter(_pEditView->execute(SCI_GETCURRENTPOS));

			switchEditViewTo(iView);	//restore view
		}

		// NPLX_TODO(Phase 3): monitorFiles (monitoringStartOrStopAndUpdateUI + createMonitoringThread)
	}
	if (lastOpened != BUFFER_INVALID)
	{
		switchToFile(lastOpened);
	}

	return fnss;
}
