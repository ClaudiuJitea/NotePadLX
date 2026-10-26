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

// NPLX: port of upstream NppIO.cpp (open / save / close). The Win32 file dialogs (CustomFileDialog)
// are QFileDialog; NPPN_* plugin notifications come with the plugin host (Phase 9).

#include "Notepad_plus.h"
#include "VerticalFileSwitcher.h"

#include "FunctionListPanel.h"

#include <cerrno>
#include <cstring>
#include <unordered_set>

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDir>
#include <QFileDialog>
#include <QInputDialog>
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QMessageBox>
#include <QPushButton>

#include "DocTabView.h"
#include "Parameters.h"
#include "Printer.h"
#include "ScintillaEditView.h"
#include "UiServices.h"
#include "menuCmdID.h"

using std::wstring;
using std::vector;

namespace
{
	// File type filters of the Open / Save As dialogs, from langs.xml (upstream setFileOpenSaveDlgFilters)
	QString buildFileDialogFilters(LangType langType, QString* selectedFilter)
	{
		NppParameters& nppParam = NppParameters::getInstance();
		QStringList filters;
		filters << QObject::tr("All types (*)");

		for (int i = 0; i < nppParam.getNbLang(); ++i)
		{
			Lang* l = nppParam.getLangFromIndex(i);
			if (!l)
				continue;

			const wchar_t* extList = l->getDefaultExtList();
			if (!extList || !extList[0])
				continue;

			QStringList patterns;
			for (const wstring& ext : tokenizeString(wstring(extList), ' '))
				patterns << (QStringLiteral("*.") + QString::fromStdWString(ext));
			const QString filter = QStringLiteral("%1 (%2)")
				.arg(QString::fromWCharArray(ScintillaEditView::_langNameInfoArray[l->getLangID()]._longName), patterns.join(QLatin1Char(' ')));
			filters << filter;
			if (selectedFilter && l->getLangID() == langType)
				*selectedFilter = filter;
		}
		return filters.join(QStringLiteral(";;"));
	}
}

// ============================================================================
// Open
// ============================================================================

BufferID Notepad_plus::doOpen(const wstring& fileName, bool isRecursive, bool isReadOnly, int encoding, const wchar_t* backupFileName, FILETIME fileNameTimestamp)
{
	const size_t longFileNameBufferSize = MAX_PATH;
	if (fileName.size() >= longFileNameBufferSize - 1)
		return BUFFER_INVALID;

	// NPLX: no Windows .lnk shortcuts to resolve; symlinks are opened as the path given (like other Linux editors)
	wstring targetFileName = fileName;

	NppParameters& nppParam = NppParameters::getInstance();
	wchar_t longFileName[longFileNameBufferSize] = { 0 };
	wcscpy_s(longFileName, longFileNameBufferSize, nplx::fullPathName(targetFileName).c_str()); // NPLX: was GetFullPathName

	bool isSnapshotMode = (backupFileName != NULL) && doesFileExist(backupFileName);
	bool longFileNameExists = doesFileExist(longFileName);
	if (isSnapshotMode && !longFileNameExists) // UNTITLED
	{
		wcscpy_s(longFileName, targetFileName.c_str());
	}
	_lastRecentFileList.remove(longFileName);

	// "fileName" could be:
	// 1. full file path to open or create
	// 2. "new N" or whatever renamed from "new N" to switch to (if user double-clicks the found entries of untitled documents, which is the results of running commands "Find in current doc" & "Find in opened docs")
	// 3. a file name with relative path to open or create

	// Search case 1 & 2 firstly
	BufferID foundBufID = MainFileManager.getBufferFromName(targetFileName.c_str());

	// if case 1 & 2 not found, search case 3
	if (foundBufID == BUFFER_INVALID)
	{
		wstring fileName2Find;
		fileName2Find = longFileName;
		foundBufID = MainFileManager.getBufferFromName(fileName2Find.c_str());
	}

	// If we found the document, then we don't open the existing doc. We return the found buffer ID instead.
	if (foundBufID != BUFFER_INVALID && !isSnapshotMode)
	{
		if (isInTray())
			restoreFromTray();
		return foundBufID;
	}

	// NPLX_TODO(Phase 3): session files (fileLoadSession) and workspace files (Project Panel, Phase 6)

	bool globbing = (wcsrchr(longFileName, wchar_t('*')) || wcsrchr(longFileName, wchar_t('?')));

	if (!isSnapshotMode) // if not backup mode, or backupfile path is invalid
	{
		if (!doesPathExist(longFileName) && !globbing)
		{
			wstring longFileDir(longFileName);
			pathRemoveFileSpec(longFileDir);

			bool isCreateFileSuccessful = false;
			if (doesDirectoryExist(longFileDir.c_str()))
			{
				int res = _nativeLangSpeaker.messageBox("CreateNewFileOrNot",
					L"\"$STR_REPLACE$\" doesn't exist. Create it?",
					L"Create new file",
					MB_YESNO,
					0,
					longFileName);

				if (res == IDYES)
				{
					bool isOK = MainFileManager.createEmptyFile(longFileName);
					if (isOK)
					{
						isCreateFileSuccessful = true;
					}
					else
					{
						_nativeLangSpeaker.messageBox("CreateNewFileError",
							L"Cannot create the file \"$STR_REPLACE$\".",
							L"Create new file",
							MB_OK,
							0,
							longFileName);
					}
				}
			}
			else
			{
				wstring msg, title;
				if (!_nativeLangSpeaker.getMsgBoxLang("OpenFileNoFolderError", title, msg))
				{
					title = L"Cannot open file";
					msg = L"\"";
					msg += longFileName;
					msg += L"\" cannot be opened:\nFolder \"";
					msg += longFileDir;
					msg += L"\" doesn't exist.";
				}
				else
				{
					msg = stringReplace(msg, L"$STR_REPLACE1$", longFileName);
					msg = stringReplace(msg, L"$STR_REPLACE2$", longFileDir);
				}
				NppDarkMode::darkMessageBoxW(msg.c_str(), title.c_str(), MB_OK);
			}

			if (!isCreateFileSuccessful)
			{
				return BUFFER_INVALID;
			}
		}
	}

	// NPLX_TODO(Phase 3): encoding = getHtmlXmlEncoding(longFileName) when encoding == -1

	BufferID buffer;
	if (isSnapshotMode)
	{
		buffer = MainFileManager.loadFile(longFileName, static_cast<Document>(NULL), encoding, backupFileName, fileNameTimestamp);

		if (buffer != BUFFER_INVALID)
		{
			buffer->setLoadedDirty(true);
		}
	}
	else
	{
		buffer = MainFileManager.loadFile(longFileName, static_cast<Document>(NULL), encoding);
		if (buffer != BUFFER_INVALID && nppParam.isMonitoringMode())
		{
			monitoringStartOrStopAndUpdateUI(MainFileManager.getBufferByID(buffer), true);
			createMonitoringThread(MainFileManager.getBufferByID(buffer));
		}
	}

	if (buffer != BUFFER_INVALID)
	{
		Buffer* buf = MainFileManager.getBufferByID(buffer);

		// if file is read only, we set the view read only
		if (isReadOnly)
			buf->setUserReadOnly(true);

		loadBufferIntoView(buffer, currentView());

		if (_pDocumentListPanel)
			_pDocumentListPanel->newItem(buffer, currentView());
	}
	else
	{
		if (globbing || doesDirectoryExist(targetFileName.c_str()))
		{
			vector<wstring> fileNames;
			vector<wstring> patterns;
			if (globbing)
			{
				const wchar_t* substring = wcsrchr(targetFileName.c_str(), NPLX_PATH_SEP);
				if (substring)
				{
					size_t pos = substring - targetFileName.c_str();

					patterns.push_back(substring + 1);
					wstring dir(targetFileName.c_str(), pos + 1);

					getMatchedFileNames(dir.c_str(), 0, patterns, fileNames, isRecursive, false);
				}
			}
			else
			{
				wstring fileNameStr = targetFileName;
				if (targetFileName[targetFileName.size() - 1] != NPLX_PATH_SEP)
					fileNameStr += NPLX_PATH_SEP;

				patterns.push_back(L"*");
				getMatchedFileNames(fileNameStr.c_str(), 0, patterns, fileNames, true, false);
			}

			bool ok2Open = true;
			size_t nbFiles2Open = fileNames.size();

			if (nbFiles2Open > 200)
			{
				ok2Open = IDYES == _nativeLangSpeaker.messageBox("NbFileToOpenImportantWarning",
					L"$INT_REPLACE$ files are about to be opened.\rAre you sure to open them?",
					L"Amount of files to open is too large",
					MB_YESNO | MB_APPLMODAL,
					static_cast<int>(nbFiles2Open));
			}

			if (ok2Open)
			{
				for (size_t i = 0; i < nbFiles2Open; ++i)
					doOpen(fileNames[i]);
			}
		}
		else
		{
			_nativeLangSpeaker.messageBox("OpenFileError",
				L"Cannot open file \"$STR_REPLACE$\".",
				L"ERROR",
				MB_OK,
				0,
				longFileName);
		}
	}
	return buffer;
}

QString Notepad_plus::fileDialogDir() const
{
	// upstream: the dialogs open in NppParameters::getWorkingDir(), which Preferences > Default Directory sets: the
	// folder of the current file (Notepad_plus::setWorkingDir on activation), the last folder used, or the user's
	NppParameters& nppParam = NppParameters::getInstance();
	const NppGUI& nppGUI = nppParam.getNppGUI();
	switch (nppGUI._openSaveDir)
	{
		case dir_userDef:
			return QString::fromWCharArray(nppGUI._defaultDirExp);
		case dir_last:
			return QString::fromWCharArray(nppParam.getWorkingDir());
		default:
		{
			const Buffer* buf = _pEditView->getCurrentBuffer();
			if (!buf->isUntitled())
				return QFileInfo(QString::fromWCharArray(buf->getFullPathName())).absolutePath();
			return QString::fromWCharArray(nppParam.getWorkingDir());
		}
	}
}

void Notepad_plus::rememberFileDialogDir(const QString& filePath)
{
	// upstream dir_last: the dialog remembers its folder
	NppParameters& nppParam = NppParameters::getInstance();
	if (nppParam.getNppGUI()._openSaveDir == dir_last && !filePath.isEmpty())
		nppParam.setWorkingDir(QFileInfo(filePath).absolutePath().toStdWString().c_str());
}

void Notepad_plus::fileOpen()
{
	// upstream: CustomFileDialog with the langs.xml filters
	const QString filters = buildFileDialogFilters(L_TEXT, nullptr);
	const QStringList fileNames = QFileDialog::getOpenFileNames(this,
		QString::fromStdWString(_nativeLangSpeaker.getNativeLangMenuString(IDM_FILE_OPEN, L"Open", true)), fileDialogDir(), filters);
	if (!fileNames.isEmpty())
		rememberFileDialogDir(fileNames.first());

	BufferID lastOpened = BUFFER_INVALID;
	for (const QString& fn : fileNames)
	{
		BufferID test = doOpen(fn.toStdWString());
		if (test != BUFFER_INVALID)
			lastOpened = test;
	}

	if (lastOpened != BUFFER_INVALID)
	{
		switchToFile(lastOpened);
	}
}

void Notepad_plus::filePrint(bool showDialog)
{
	Printer printer;

	intptr_t startPos = _pEditView->execute(SCI_GETSELECTIONSTART);
	intptr_t endPos = _pEditView->execute(SCI_GETSELECTIONEND);

	printer.init(this, [this](unsigned int message, uptr_t wParam, sptr_t lParam) { return nppMessage(message, wParam, lParam); },
		_pEditView, showDialog, startPos, endPos, _nativeLangSpeaker.isRTL());
	printer.doPrint();
}

bool Notepad_plus::fileReload()
{
	BufferID buf = _pEditView->getCurrentBufferID();
	return doReload(buf, buf->isDirty());
}

bool Notepad_plus::doReload(BufferID id, bool alert)
{
	if (alert)
	{
		int answer = _nativeLangSpeaker.messageBox("DocReloadWarning",
			L"Are you sure you want to reload the current file and lose the changes made in Notepad++?",
			L"Reload",
			MB_YESNO | MB_ICONEXCLAMATION | MB_APPLMODAL);
		if (answer != IDYES)
			return false;
	}

	//In order to prevent Scintilla from restyling the entire document,
	//an empty Document is inserted during reload if needed.
	bool mainVisible = (_mainEditView->getCurrentBufferID() == id);
	bool subVisible = (_subEditView->getCurrentBufferID() == id);
	unsigned long MODEVENTMASK_ON = NppParameters::getInstance().getScintillaModEventMask();
	if (mainVisible)
	{
		_mainEditView->saveCurrentPos();
		_mainEditView->execute(SCI_SETMODEVENTMASK, MODEVENTMASK_OFF);
		_mainEditView->execute(SCI_SETDOCPOINTER, 0, 0);
		_mainEditView->execute(SCI_SETMODEVENTMASK, MODEVENTMASK_ON);
	}

	if (subVisible)
	{
		_subEditView->saveCurrentPos();
		_subEditView->execute(SCI_SETMODEVENTMASK, MODEVENTMASK_OFF);
		_subEditView->execute(SCI_SETDOCPOINTER, 0, 0);
		_subEditView->execute(SCI_SETMODEVENTMASK, MODEVENTMASK_ON);
	}

	if (!mainVisible && !subVisible)
	{
		return MainFileManager.reloadBufferDeferred(id);
	}

	bool res = MainFileManager.reloadBuffer(id);
	Buffer* pBuf = MainFileManager.getBufferByID(id);
	if (mainVisible)
	{
		_mainEditView->execute(SCI_SETMODEVENTMASK, MODEVENTMASK_OFF);
		_mainEditView->execute(SCI_SETDOCPOINTER, 0, pBuf->getDocument());
		_mainEditView->execute(SCI_SETMODEVENTMASK, MODEVENTMASK_ON);
		_mainEditView->restoreCurrentPosPreStep();
	}

	if (subVisible)
	{
		_subEditView->execute(SCI_SETMODEVENTMASK, MODEVENTMASK_OFF);
		_subEditView->execute(SCI_SETDOCPOINTER, 0, pBuf->getDocument());
		_subEditView->execute(SCI_SETMODEVENTMASK, MODEVENTMASK_ON);
		_subEditView->restoreCurrentPosPreStep();
	}

	const auto& svp = NppParameters::getInstance().getSVP();

	// Once reload is complete, activate buffer which will take care of
	// many settings such as update status bar, clickable link etc.
	if (((currentView() == MAIN_VIEW) && mainVisible) || ((currentView() == SUB_VIEW) && subVisible))
	{
		activateBuffer(id, currentView(), true);

		if (svp._isChangeHistoryMarginEnabled || svp._isChangeHistoryIndicatorEnabled)
			clearChangesHistory(currentView());
	}
	else
	{
		// handle also the less usual case when the reloaded buffer is not in the current active view

		int originalActiveView = currentView();
		BufferID originalActiveBufferID = nullptr;
		if (mainVisible)
		{
			originalActiveBufferID = _subEditView->getCurrentBufferID();
			activateBuffer(id, MAIN_VIEW, true);
		}
		else
		{
			originalActiveBufferID = _mainEditView->getCurrentBufferID();
			activateBuffer(id, SUB_VIEW, true);
		}
		activateBuffer(originalActiveBufferID, originalActiveView, true); // set back the original

		if (svp._isChangeHistoryMarginEnabled || svp._isChangeHistoryIndicatorEnabled)
			clearChangesHistory(otherView());
	}

	return res;
}

// ============================================================================
// Save
// ============================================================================

bool Notepad_plus::doSave(BufferID id, const wchar_t* filename, bool isCopy)
{
	const int index = MainFileManager.getBufferIndexByID(id);
	if (index == -1)
	{
		_nativeLangSpeaker.messageBox("BufferInvalidWarning",
			L"Cannot save: Buffer is invalid.",
			L"Save failed",
			MB_OK | MB_ICONWARNING);

		return false;
	}

	SavingStatus res = MainFileManager.saveBuffer(id, filename, isCopy);

	if (res == SavingStatus::FullReadOnlySavingForbidden)
	{
		_nativeLangSpeaker.messageBox("FullReadOnlySavingForbidden",
			L"Cannot save file.\nThe Notepad++ full read-only saving forbidden mode prevented the file from being saved.",
			L"Save failed",
			MB_OK);
	}
	else if (res == SavingStatus::NotEnoughRoom)
	{
		_nativeLangSpeaker.messageBox("NotEnoughRoom4Saving",
			L"Failed to save file.\nIt seems there's not enough space on disk to save file. Your file is not saved.",
			L"Save failed",
			MB_OK);
	}
	else if (res == SavingStatus::SaveWritingFailed)
	{
		if (!(NppParameters::getInstance()).isEndSessionCritical()) // can we report to the user?
		{
			wstring errorMessage = string2wstring(std::strerror(errno)); // NPLX: was GetLastErrorAsString(::GetLastError())
			NppDarkMode::darkMessageBoxW(errorMessage.c_str(), L"Save failed", MB_OK | MB_ICONWARNING);
		}
	}
	else if (res == SavingStatus::SaveOpenFailed)
	{
		Buffer* buf = MainFileManager.getBufferByID(id);
		if (buf->isFromNetwork())
		{
			_nativeLangSpeaker.messageBox("FileLockedWarning",
				L"Please check whether the network where the file is located is connected.",
				L"Save failed",
				MB_OK | MB_ICONWARNING);
		}
		else
		{
			// NPLX: upstream offers to restart Notepad++ as Administrator (UAC).
			// NPLX_TODO(Phase 3): elevated save through pkexec; for now explain why it failed.
			const wstring reason = string2wstring(std::strerror(errno));
			const wstring msg = L"Cannot save \"" + wstring(filename) + L"\":\n" + reason;
			NppDarkMode::darkMessageBoxW(msg.c_str(), L"Save failed", MB_OK | MB_ICONWARNING);
		}
	}

	if (res == SavingStatus::SaveOK && _pFuncList && (!_pFuncList->isClosed()) && _pFuncList->isVisible())
	{
		_pFuncList->reload();
	}

	return res == SavingStatus::SaveOK;
}

bool Notepad_plus::fileSave(BufferID bufferID)
{
	if (bufferID == BUFFER_INVALID)
		bufferID = _pEditView->getCurrentBufferID();

	Buffer* buf = MainFileManager.getBufferByID(bufferID);

	if (buf->isDirty())
	{
		if (buf->isReadOnly()) //cannot save if readonly in Notepad++ or in Windows
		{
			_nativeLangSpeaker.messageBox("ReadOnlyFileCannotBeSaved",
				L"\"$STR_REPLACE$\"\rThe file is read-only and cannot be saved.\rPlease remove read-only then save your file.",
				L"Save Failed - File is Read-Only",
				MB_OK | MB_ICONWARNING,
				0,
				buf->getFullPathName());

			return false;
		}

		if (buf->isUntitled())
		{
			return fileSaveAs(bufferID);
		}

		const NppGUI& nppgui = (NppParameters::getInstance()).getNppGUI();
		BackupFeature backup = nppgui._backup;

		if (backup != bak_none && !buf->isLargeFile())
		{
			const wchar_t* fn = buf->getFullPathName();
			const wchar_t* name = ::PathFindFileName(fn);
			wstring fn_bak;

			if (nppgui._useDir && !nppgui._backupDir.empty())
			{
				// Get the custom directory, make sure it has a trailing slash
				fn_bak = nppgui._backupDir;
				if (fn_bak.back() != NPLX_PATH_SEP)
					fn_bak += NPLX_PATH_SEP;
			}
			else
			{
				// Get the current file's directory
				wstring path = fn;
				::pathRemoveFileSpec(path);
				fn_bak = path;
				fn_bak += NPLX_PATH_SEP;

				// If verbose, save it in a sub folder
				if (backup == bak_verbose)
				{
					fn_bak += L"nppBackup/";
				}
			}

			// Expand any environment variables
			expandEnv(fn_bak); // NPLX: was ExpandEnvironmentStrings

			// Make sure the directory exists
			if (!doesDirectoryExist(fn_bak.c_str()))
			{
				nplx::createDirectories(fn_bak); // NPLX: was SHCreateDirectory
			}

			// Determine what to name the backed-up file
			if (backup == bak_simple)
			{
				fn_bak += name;
				fn_bak += L".bak";
			}
			else if (backup == bak_verbose)
			{
				time_t ltime = time(0);
				const struct tm* today;

				today = localtime(&ltime);
				if (today)
				{
					constexpr int temBufLen = 32;
					wchar_t tmpbuf[temBufLen]{};
					wcsftime(tmpbuf, temBufLen, L"%Y-%m-%d_%H%M%S", today);

					fn_bak += name;
					fn_bak += L".";
					fn_bak += tmpbuf;
					fn_bak += L".bak";
				}
			}

			if (!nplx::copyFile(fn, fn_bak, false)) // NPLX: was CopyFileEx
			{
				int res = _nativeLangSpeaker.messageBox("FileBackupFailed",
					L"The previous version of the file could not be saved into the backup directory at \"$STR_REPLACE$\".\r\rDo you want to save the current file anyway?",
					L"File Backup Failed",
					MB_YESNO | MB_ICONERROR,
					0,
					fn_bak.c_str());

				if (res == IDNO)
				{
					return false;
				}
			}
		}

		return doSave(bufferID, buf->getFullPathName(), false);
	}

	return false;
}

bool Notepad_plus::fileSaveAll()
{
	if (viewVisible(MAIN_VIEW))
	{
		for (size_t i = 0; i < _mainDocTab->nbItem(); ++i)
		{
			BufferID idToSave = _mainDocTab->getBufferByIndex(i);
			fileSave(idToSave);
		}
	}

	if (viewVisible(SUB_VIEW))
	{
		for (size_t i = 0; i < _subDocTab->nbItem(); ++i)
		{
			BufferID idToSave = _subDocTab->getBufferByIndex(i);
			fileSave(idToSave);
		}
	}
	checkDocState();
	return true;
}

bool Notepad_plus::fileSaveAs(BufferID bufferID, bool isSaveCopy)
{
	if (bufferID == BUFFER_INVALID)
		bufferID = _pEditView->getCurrentBufferID();

	Buffer* buf = MainFileManager.getBufferByID(bufferID);

	wstring origPathname = buf->getFullPathName();
	bool wasUntitled = buf->isUntitled();

	wstring localizedTitle;
	if (isSaveCopy)
		localizedTitle = _nativeLangSpeaker.getNativeLangMenuString(IDM_FILE_SAVECOPYAS, L"Save a Copy As", true);
	else
		localizedTitle = _nativeLangSpeaker.getNativeLangMenuString(IDM_FILE_SAVEAS, L"Save As", true);

	// Disable file autodetection before opening save dialog to prevent use-after-delete bug.
	NppParameters& nppParam = NppParameters::getInstance();
	auto cdBefore = nppParam.getNppGUI()._fileAutoDetection;
	(nppParam.getNppGUI())._fileAutoDetection = cdDisabled;

	QString initialPath = QString::fromWCharArray(buf->getFileName());
	if (!wasUntitled)
		initialPath = QString::fromStdWString(origPathname);
	if (wasUntitled || nppParam.getNppGUI()._openSaveDir != dir_followCurrent)
		initialPath = QDir(fileDialogDir()).filePath(QString::fromWCharArray(buf->getFileName()));
	QString selectedFilter;
	const QString filters = buildFileDialogFilters(buf->getLangType(), &selectedFilter);
	const QString chosen = QFileDialog::getSaveFileName(this, QString::fromStdWString(localizedTitle), initialPath, filters, &selectedFilter);
	rememberFileDialogDir(chosen);
	wstring fn = chosen.toStdWString();

	// Enable file autodetection again.
	(nppParam.getNppGUI())._fileAutoDetection = cdBefore;

	if (!fn.empty())
	{
		BufferID other = MainFileManager.getBufferFromName(fn.c_str());
		if (other != BUFFER_INVALID && _pDocTab->getIndexByBuffer(other) == -1 && _pNonDocTab->getIndexByBuffer(other) == -1)
			other = BUFFER_INVALID;

		if (other == BUFFER_INVALID // both (current and other) views don't contain the same file (path), "Save As" action is allowed.
			|| other->getID() == bufferID->getID()) // or if user saves the file by using "Save As" instead of "Save" (ie. in "Save As" dialog to chooses the same file), then it's still legit.
		{
			bool res = doSave(bufferID, fn.c_str(), isSaveCopy);
			//Changing lexer after save seems to work properly
			if (!wasUntitled && !isSaveCopy)
			{
				_lastRecentFileList.add(origPathname.c_str());
			}

			// If file is replaced then remove it from recent list
			if (res && !isSaveCopy)
			{
				_lastRecentFileList.remove(fn.c_str());
			}
			return res;
		}
		else		//cannot save, other view has buffer already open, activate it
		{
			_nativeLangSpeaker.messageBox("FileAlreadyOpenedInNpp",
				L"The file is already opened in Notepad++.",
				L"ERROR",
				MB_OK | MB_ICONSTOP);
			switchToFile(other);
			return false;
		}
	}
	else // cancel button is pressed
	{
		return false;
	}
}

// ============================================================================
// New / Close
// ============================================================================

void Notepad_plus::fileNew()
{
	BufferID newBufID = MainFileManager.newEmptyDocument();
	if (newBufID != BUFFER_INVALID)
	{
		loadBufferIntoView(newBufID, currentView(), true); // true, because we want multiple new files if possible
		switchToFile(newBufID);
	}
}

void Notepad_plus::doClose(BufferID id, int whichOne, bool doDeleteBackup)
{
	DocTabView* tabToClose = (whichOne == MAIN_VIEW) ? _mainDocTab : _subDocTab;
	int i = tabToClose->getIndexByBuffer(id);
	if (i == -1)
		return;

	size_t numInitialOpenBuffers =
		(viewVisible(MAIN_VIEW) ? _mainDocTab->nbItem() : 0) +
		(viewVisible(SUB_VIEW) ? _subDocTab->nbItem() : 0);

	if (doDeleteBackup)
		MainFileManager.deleteBufferBackup(id);

	Buffer* buf = MainFileManager.getBufferByID(id);

	// Add to recent file history only if file is removed from all the views
	// There might be cases when file is cloned/moved to view.
	// Don't add to recent list unless file is removed from all the views
	wstring fileFullPath;
	if (!buf->isUntitled())
	{
		const wchar_t* fn = buf->getFullPathName();
		bool fileExists = doesFileExist(fn);

		if (fileExists)
			fileFullPath = fn;
	}

	size_t nbDocs = whichOne == MAIN_VIEW ? (_mainDocTab->nbItem()) : (_subDocTab->nbItem());

	if (buf->isMonitoringOn())
	{
		// turn off monitoring
		monitoringStartOrStopAndUpdateUI(buf, false);
	}

	//Do all the works
	bool isBufRemoved = removeBufferFromView(id, whichOne);
	BufferID hiddenBufferID = BUFFER_INVALID;
	if (nbDocs == 1 && canHideView(whichOne))
	{	//close the view if both visible
		hideView(whichOne);

		// if the current activated buffer is in this view,
		// then get buffer ID to remove the entry from File Switcher Pannel
		DocTabView* hiddenTab = (whichOne == MAIN_VIEW) ? _mainDocTab : _subDocTab; // NPLX: was NPPM_GETBUFFERIDFROMPOS
		hiddenBufferID = hiddenTab->nbItem() ? hiddenTab->getBufferByIndex(0) : BUFFER_INVALID;
		if (!isBufRemoved && hiddenBufferID != BUFFER_INVALID && _pDocumentListPanel)
			_pDocumentListPanel->closeItem(hiddenBufferID, whichOne);
	}
	checkSyncState();

	if (isBufRemoved)
	{
		// The document could be cloned.
		// if the same buffer ID is not found then remove the entry from File Switcher Panel
		if (_pDocumentListPanel)
		{
			_pDocumentListPanel->closeItem(id, whichOne);

			if (hiddenBufferID != BUFFER_INVALID)
				_pDocumentListPanel->closeItem(hiddenBufferID, whichOne);
		}

		// Add to recent file only if file is removed and does not exist in any of the views
		BufferID buffID = MainFileManager.getBufferFromName(fileFullPath.c_str());
		if (buffID == BUFFER_INVALID && fileFullPath.length() > 0)
			_lastRecentFileList.add(fileFullPath.c_str());
	}

	if (NppParameters::getInstance().getNppGUI()._tabStatus & TAB_QUITONEMPTY)
	{
		// the user closed the last open tab
		if (numInitialOpenBuffers == 1 && isEmpty() && !_isAttemptingCloseOnQuit)
		{
			command(IDM_FILE_EXIT);
		}
	}
}

bool Notepad_plus::fileClose(BufferID id, int curView)
{
	BufferID bufferID = id;
	if (id == BUFFER_INVALID)
		bufferID = _pEditView->getCurrentBufferID();
	Buffer* buf = MainFileManager.getBufferByID(bufferID);

	int viewToClose = currentView();
	if (curView != -1)
		viewToClose = curView;

	// Determine if it's a cloned buffer
	DocTabView* nonCurrentTab = (viewToClose == MAIN_VIEW) ? _subDocTab : _mainDocTab;
	bool isCloned = nonCurrentTab->getIndexByBuffer(bufferID) != -1;

	if ((buf->isUntitled() && buf->docLength() == 0) || isCloned)
	{
		// Do nothing
	}
	else if (buf->isDirty())
	{
		const wchar_t* fileNamePath = buf->getFullPathName();
		int res = doSaveOrNot(fileNamePath);

		if (res == IDYES)
		{
			if (!fileSave(id)) // the cancel button of savedialog is pressed, aborts closing
				return false;
		}
		else if (res == IDCANCEL)
		{
			return false;	//cancel aborts closing
		}
		else
		{
			// else IDNO we continue
		}
	}

	bool isSnapshotMode = NppParameters::getInstance().getNppGUI().isSnapshotMode();
	bool doDeleteBackup = isSnapshotMode;
	if (isSnapshotMode && isCloned) // if Buffer is cloned then we don't delete backup file
		doDeleteBackup = false;

	doClose(bufferID, viewToClose, doDeleteBackup);
	return true;
}

bool Notepad_plus::fileCloseAll(bool doDeleteBackup, bool isSnapshotMode)
{
	bool noSaveToAll = false;
	bool saveToAll = false;

	//closes all documents, makes the current view the only one visible

	//first check if we need to save any file
	//check in the both view
	std::unordered_set<BufferID> uniqueBuffers;

	auto checkView = [&](DocTabView* docTab, int view) -> bool {
		for (size_t i = 0; i < docTab->nbItem() && !noSaveToAll; ++i)
		{
			BufferID id = docTab->getBufferByIndex(i);
			Buffer* buf = MainFileManager.getBufferByID(id);

			// Is this buffer already included? (the main view puts all its buffers in the hash table)
			if (!uniqueBuffers.insert(id).second)
				continue;

			if (buf->isUntitled() && buf->docLength() == 0)
			{
				// Do nothing
			}
			else if (buf->isDirty())
			{
				if (isSnapshotMode)
				{
					if (buf->getBackupFileName() == L"" || !doesFileExist(buf->getBackupFileName().c_str())) //backup file has been deleted from outside
					{
						// warning user and save it if user want it.
						activateBuffer(id, view);
						switchEditViewTo(view);

						int res = _nativeLangSpeaker.messageBox("NoBackupDoSaveFile",
							L"Your backup file cannot be found (deleted from outside).\rSave it otherwise your data will be lost\rDo you want to save file \"$STR_REPLACE$\" ?",
							L"Save",
							MB_YESNOCANCEL | MB_ICONQUESTION | MB_APPLMODAL,
							0, // not used
							buf->getFullPathName());

						if (res == IDYES)
						{
							if (!fileSave(id))
								return false;	//abort entire procedure
						}
						else if (res == IDCANCEL)
						{
							return false;
						}
					}
				}
				else
				{
					activateBuffer(id, view);
					switchEditViewTo(view);

					int res = -1;
					if (saveToAll)
					{
						res = IDYES;
					}
					else
					{
						size_t nbDirtyFiles = MainFileManager.getNbDirtyBuffers();
						res = doSaveOrNot(buf->getFullPathName(), nbDirtyFiles > 1);
					}

					if (res == IDYES)
					{
						if (!fileSave(id))
							return false;	//abort entire procedure
					}
					else if (res == IDCANCEL)
					{
						return false;
					}
					else if (res == IDIGNORE)
					{
						noSaveToAll = true;
					}
					else if (res == IDRETRY)
					{
						if (!fileSave(id))
							return false;	// Abort entire procedure.

						saveToAll = true;
					}
				}
			}
		}
		return true;
	};

	if (!checkView(_mainDocTab, MAIN_VIEW))
		return false;
	if (!checkView(_subDocTab, SUB_VIEW))
		return false;

	//Then start closing, inactive view first so the active is left open
	if (bothActive())
	{
		//first close all docs in non-current view, which gets closed automatically
		//Set active tab to the last one closed.
		activateBuffer(_pNonDocTab->getBufferByIndex(0), otherView());
		for (auto i = static_cast<int>(_pNonDocTab->nbItem()) - 1; i >= 0; --i) //close all from right to left
		{
			doClose(_pNonDocTab->getBufferByIndex(i), otherView(), doDeleteBackup);
		}
	}

	activateBuffer(_pDocTab->getBufferByIndex(0), currentView());
	for (auto i = static_cast<int>(_pDocTab->nbItem()) - 1; i >= 0; --i)
	{	//close all from right to left
		doClose(_pDocTab->getBufferByIndex(i), currentView(), doDeleteBackup);
	}
	return true;
}

// ============================================================================
// Questions (Notepad_plus.cpp)
// ============================================================================

int Notepad_plus::doSaveOrNot(const wchar_t* fn, bool isMulti)
{
	if ((NppParameters::getInstance()).isEndSessionCritical())
		return IDCANCEL; // simulate Esc-key or Cancel-button as there should not be any big delay / code-flow block

	if (isMinimized())
		showNormal();

	wstring title, msg;

	if (!_nativeLangSpeaker.getDoSaveOrNotStrings(title, msg))
	{
		title = L"Save";
		msg = L"Save file \"$STR_REPLACE$\" ?";
	}

	msg = stringReplace(msg, L"$STR_REPLACE$", fn);

	if (!isMulti)
		return NppDarkMode::darkMessageBoxW(msg.c_str(), title.c_str(), MB_YESNOCANCEL | MB_ICONQUESTION | MB_APPLMODAL);

	// upstream DoSaveOrNotBox: Yes / No / Yes to all / No to all / Cancel
	return nplx::uiServices()->saveOrNotMultiBox(msg, title);
}

int Notepad_plus::doReloadOrNot(const wchar_t* fn, bool dirty)
{
	if (dirty)
		return _nativeLangSpeaker.messageBox("DoReloadOrNotAndLooseChange",
			L"\"$STR_REPLACE$\"\r\rThis file has been modified by another program.\rDo you want to reload it and lose the changes made in Notepad++?",
			L"Reload",
			MB_YESNO | MB_APPLMODAL | MB_ICONEXCLAMATION,
			0, // not used
			fn);
	else
		return _nativeLangSpeaker.messageBox("DoReloadOrNot",
			L"\"$STR_REPLACE$\"\r\rThis file has been modified by another program.\rDo you want to reload it?",
			L"Reload",
			MB_YESNO | MB_APPLMODAL | MB_ICONQUESTION,
			0, // not used
			fn);
}

int Notepad_plus::doCloseOrNot(const wchar_t* fn)
{
	return _nativeLangSpeaker.messageBox("DoCloseOrNot",
		L"The file \"$STR_REPLACE$\" doesn't exist anymore.\rKeep this file in editor?",
		L"Keep non existing file",
		MB_YESNO | MB_ICONQUESTION | MB_APPLMODAL,
		0, // not used
		fn);
}

// ============================================================================
// Rename / Delete / Drop (NppIO.cpp, Notepad_plus.cpp)
// ============================================================================

bool Notepad_plus::fileRename(BufferID bufferID)
{
	if (bufferID == BUFFER_INVALID)
		bufferID = _pEditView->getCurrentBufferID();

	Buffer* buf = MainFileManager.getBufferByID(bufferID);

	bool success = false;
	wstring oldFileNamePath = buf->getFullPathName();
	bool isFileExisting = doesFileExist(oldFileNamePath.c_str());
	if (isFileExisting)
	{
		wstring localizedRename = _nativeLangSpeaker.getNativeLangMenuString(IDM_FILE_RENAME, L"Rename", true);
		const QString fn = QFileDialog::getSaveFileName(this, QString::fromStdWString(localizedRename),
			QString::fromStdWString(oldFileNamePath), buildFileDialogFilters(buf->getLangType(), nullptr));

		if (!fn.isEmpty())
		{
			success = MainFileManager.moveFile(bufferID, fn.toStdWString().c_str());
		}
	}
	else
	{
		// We are just going to rename the tab nothing else
		// So just rename the tab and rename the backup file too if applicable

		wstring staticName = _nativeLangSpeaker.getLocalizedStrFromID("tabrename-newname", L"New name");
		wstring title = _nativeLangSpeaker.getLocalizedStrFromID("tabrename-title", L"Rename Current Tab");

		bool ok = false;
		const QString newName = QInputDialog::getText(this, QString::fromStdWString(title), QString::fromStdWString(staticName),
			QLineEdit::Normal, QString::fromWCharArray(buf->getFileName()), &ok); // NPLX: upstream StringDlg
		if (ok)
		{
			wstring tabNewNameStr = newName.left(langNameLenMax - 1).toStdWString();
			if (oldFileNamePath == tabNewNameStr) // No change but user clicks on OK
				return false;

			trim(tabNewNameStr); // No leading and trailing space allowed

			BufferID sameNamedBufferId = _pDocTab->findBufferByName(tabNewNameStr.c_str());
			if (sameNamedBufferId == BUFFER_INVALID)
			{
				sameNamedBufferId = _pNonDocTab->findBufferByName(tabNewNameStr.c_str());
			}

			if (sameNamedBufferId != BUFFER_INVALID)
			{
				_nativeLangSpeaker.messageBox("RenameTabTemporaryNameAlreadyInUse",
					L"The specified name is already in use on another tab.",
					L"Rename failed",
					MB_OK | MB_ICONSTOP);
			}
			else if (tabNewNameStr.empty())
			{
				_nativeLangSpeaker.messageBox("RenameTabTemporaryNameIsEmpty",
					L"The specified name cannot be empty, or it cannot contain only space(s) or TAB(s).",
					L"Rename failed",
					MB_OK | MB_ICONSTOP);
			}
			else // The change will be done here
			{
				buf->setFileName(tabNewNameStr.c_str());

				success = true;
				buf->setUntitledTabRenamedStatus(true);

				bool isSnapshotMode = NppParameters::getInstance().getNppGUI().isSnapshotMode();
				if (isSnapshotMode)
				{
					wstring oldBackUpFileName = buf->getBackupFileName();
					if (oldBackUpFileName.empty())
						return success;

					wstring newBackUpFileName = oldBackUpFileName;
					newBackUpFileName.replace(newBackUpFileName.rfind(oldFileNamePath), oldFileNamePath.length(), tabNewNameStr);

					nplx::moveFileReplace(oldBackUpFileName, newBackUpFileName); // NPLX: was ReplaceFile / MoveFileEx

					buf->setBackupFileName(newBackUpFileName);
				}
			}
		}
	}

	return success;
}

bool Notepad_plus::fileDelete(BufferID id)
{
	BufferID bufferID = id;
	if (id == BUFFER_INVALID)
		bufferID = _pEditView->getCurrentBufferID();

	Buffer* buf = MainFileManager.getBufferByID(bufferID);
	const wchar_t* fileNamePath = buf->getFullPathName();

	bool goAhead = (doDeleteOrNot(fileNamePath) == IDOK);

	if (goAhead)
	{
		if (!MainFileManager.deleteFile(bufferID))
		{
			_nativeLangSpeaker.messageBox("DeleteFileFailed",
				L"Delete File failed",
				L"Delete File",
				MB_OK);

			return false;
		}
		bool isSnapshotMode = NppParameters::getInstance().getNppGUI().isSnapshotMode();
		doClose(bufferID, MAIN_VIEW, isSnapshotMode);
		doClose(bufferID, SUB_VIEW, isSnapshotMode);

		return true;
	}
	return false;
}

int Notepad_plus::doDeleteOrNot(const wchar_t* fn)
{
	// NPLX: "Trash" instead of "Recycle Bin"
	return _nativeLangSpeaker.messageBox("DoDeleteOrNot",
		L"The file \"$STR_REPLACE$\"\rwill be moved to your Recycle Bin and this document will be closed.\rContinue?",
		L"Delete file",
		MB_OKCANCEL | MB_ICONQUESTION | MB_APPLMODAL,
		0, // not used
		fn);
}

void Notepad_plus::dropFiles(const QStringList& paths, int view)
{
	// Determine in which view the file(s) is (are) dropped
	if (view == MAIN_VIEW || view == SUB_VIEW)
		switchEditViewTo(view);

	vector<wstring> folderPaths;
	vector<wstring> filePaths;
	for (const QString& p : paths)
	{
		const wstring pathDropped = p.toStdWString();
		if (doesDirectoryExist(pathDropped.c_str()))
			folderPaths.push_back(pathDropped);
		else
			filePaths.push_back(pathDropped);
	}

	NppParameters& nppParam = NppParameters::getInstance();
	bool isOldMode = nppParam.getNppGUI()._isFolderDroppedOpenFiles;

	if ((filePaths.size() == 0) && (folderPaths.size() == 0))
	{
		// invalid paths, do nothing
	}
	else if (isOldMode || folderPaths.size() == 0) // old mode or new mode + only files
	{
		BufferID lastOpened = BUFFER_INVALID;
		for (const QString& p : paths)
		{
			BufferID test = doOpen(p.toStdWString());
			if (test != BUFFER_INVALID)
				lastOpened = test;
		}

		if (lastOpened != BUFFER_INVALID)
		{
			switchToFile(lastOpened);
		}
	}
	else if (!isOldMode && (folderPaths.size() != 0 && filePaths.size() != 0)) // new mode && both folders & files
	{
		// display error & do nothing
		_nativeLangSpeaker.messageBox("DroppingFolderAsProjectModeWarning",
			L"You can only drop files or folders but not both, because you're in dropping Folder as Project mode.\rYou have to enable \"Open all files of folder instead of launching Folder as Workspace on folder dropping\" in \"Default Directory\" section of Preferences dialog to make this operation work.",
			L"Invalid action",
			MB_OK | MB_APPLMODAL);
	}
	else if (!isOldMode && (folderPaths.size() != 0 && filePaths.size() == 0)) // new mode && only folders
	{
		// process new mode
		wstring emptyStr;
		launchFileBrowser(folderPaths, emptyStr);
	}
}

void Notepad_plus::dragEnterEvent(QDragEnterEvent* event)
{
	if (event->mimeData()->hasUrls())
		event->acceptProposedAction();
}

void Notepad_plus::dropEvent(QDropEvent* event)
{
	QStringList paths;
	for (const QUrl& url : event->mimeData()->urls())
	{
		if (url.isLocalFile())
			paths << url.toLocalFile();
	}
	if (paths.isEmpty())
		return;

	int view = -1;
	QWidget* target = childAt(event->position().toPoint());
	for (QWidget* w = target; w; w = w->parentWidget())
	{
		if (w == _mainContainer) { view = MAIN_VIEW; break; }
		if (w == _subContainer) { view = SUB_VIEW; break; }
	}
	dropFiles(paths, view);
	event->acceptProposedAction();
}

// ============================================================================
// Close multiple tabs (upstream NppIO.cpp)
// ============================================================================

bool Notepad_plus::fileCloseAllGiven(const std::vector<BufferViewInfo>& fileInfos)
{
	// First check if we need to save any file.

	bool noSaveToAll = false;
	bool saveToAll = false;
	std::vector<BufferViewInfo> buffersToClose;

	// Count the number of dirty file
	size_t nbDirtyFiles = 0;
	for (const auto& i : fileInfos)
	{
		Buffer* buf = MainFileManager.getBufferByID(i._bufID);

		if (buf->isDirty())
			++nbDirtyFiles;
	}

	for (const auto& i : fileInfos)
	{
		Buffer* buf = MainFileManager.getBufferByID(i._bufID);

		if ((buf->isUntitled() && buf->docLength() == 0) || noSaveToAll || !buf->isDirty())
		{
			// Do nothing, these documents are already ready to close
			buffersToClose.push_back(i);
		}
		else if (buf->isDirty() && !noSaveToAll)
		{
			if (_activeView == MAIN_VIEW)
			{
				activateBuffer(i._bufID, MAIN_VIEW);
				if (!activateBuffer(i._bufID, SUB_VIEW))
					switchEditViewTo(MAIN_VIEW);
			}
			else
			{
				activateBuffer(i._bufID, SUB_VIEW);
				switchEditViewTo(SUB_VIEW);
			}

			/* Do you want to save
			*	IDYES		: Yes
			*	IDRETRY		: Yes to All
			*	IDNO		: No
			*	IDIGNORE	: No To All
			*	IDCANCEL	: Cancel Operation
			*/			

			int res = saveToAll ? IDYES : doSaveOrNot(buf->getFullPathName(), nbDirtyFiles > 1);

			if (res == IDYES || res == IDRETRY)
			{
				if (!fileSave(i._bufID))
					break;	// Abort entire procedure, but close whatever processed so far

				buffersToClose.push_back(i);

				if (res == IDRETRY)
					saveToAll = true;
			}
			else if (res == IDNO || res == IDIGNORE)
			{
				buffersToClose.push_back(i);

				if (res == IDIGNORE)
					noSaveToAll = true;
			}
			else if (res == IDCANCEL)
			{
				break;
			}
		}
	}

	// Now we close.
	bool isSnapshotMode = NppParameters::getInstance().getNppGUI().isSnapshotMode();
	for (const auto& i : buffersToClose)
	{
		doClose(i._bufID, i._iView, isSnapshotMode);
	}

	return true;
}

bool Notepad_plus::fileCloseAllToLeft()
{
	// Indexes must go from high to low to deal with the fact that when one index is closed, any remaining
	// indexes (smaller than the one just closed) will point to the wrong tab.
	std::vector<BufferViewInfo> bufsToClose;
	for (int i = _pDocTab->getCurrentTabIndex() - 1; i >= 0; i--)
	{
		bufsToClose.push_back(BufferViewInfo(_pDocTab->getBufferByIndex(i), currentView()));
	}
	return fileCloseAllGiven(bufsToClose);
}

bool Notepad_plus::fileCloseAllToRight()
{
	// Indexes must go from high to low to deal with the fact that when one index is closed, any remaining
	// indexes (smaller than the one just closed) will point to the wrong tab.
	const int iActive = _pDocTab->getCurrentTabIndex();
	std::vector<BufferViewInfo> bufsToClose;
	for (int i = int(_pDocTab->nbItem()) - 1; i > iActive; i--)
	{
		bufsToClose.push_back(BufferViewInfo(_pDocTab->getBufferByIndex(i), currentView()));
	}
	return fileCloseAllGiven(bufsToClose);
}

void Notepad_plus::fileCloseAllButPinned()
{
	std::vector<BufferViewInfo> bufsToClose;

	int iPinned = -1;
	for (int j = 0; j < int(_mainDocTab->nbItem()); ++j)
	{
		if (_mainDocTab->getBufferByIndex(j)->isPinned())
			iPinned++;
		else
			break;
	}
	
	for (int i = int(_mainDocTab->nbItem()) - 1; i > iPinned; i--)
	{
		bufsToClose.push_back(BufferViewInfo(_mainDocTab->getBufferByIndex(i), MAIN_VIEW));
	}


	iPinned = -1;
	for (int j = 0; j < int(_subDocTab->nbItem()); ++j)
	{
		if (_subDocTab->getBufferByIndex(j)->isPinned())
			iPinned++;
		else
			break;
	}
	for (int i = int(_subDocTab->nbItem()) - 1; i > iPinned; i--)
	{
		bufsToClose.push_back(BufferViewInfo(_subDocTab->getBufferByIndex(i), SUB_VIEW));
	}
	
	fileCloseAllGiven(bufsToClose);
}

bool Notepad_plus::fileCloseAllUnchanged()
{
	// Indexes must go from high to low to deal with the fact that when one index is closed, any remaining
	// indexes (smaller than the one just closed) will point to the wrong tab.
	std::vector<BufferViewInfo> bufsToClose;

	for (int i = int(_pDocTab->nbItem()) - 1; i >= 0; i--)
	{
		BufferID id = _pDocTab->getBufferByIndex(i);
		Buffer* buf = MainFileManager.getBufferByID(id);
		if ((buf->isUntitled() && buf->docLength() == 0) || !buf->isDirty())
		{
			bufsToClose.push_back(BufferViewInfo(_pDocTab->getBufferByIndex(i), currentView()));
		}
	}

	return fileCloseAllGiven(bufsToClose);
}

bool Notepad_plus::fileCloseAllButCurrent()
{
	BufferID current = _pEditView->getCurrentBufferID();
	const int activeViewID = currentView();
	int active = _pDocTab->getCurrentTabIndex();
	bool noSaveToAll = false;
	bool saveToAll = false;
	std::vector<unsigned int> mainSaveOpIndex, subSaveOpIndex;

	bool isSnapshotMode = NppParameters::getInstance().getNppGUI().isSnapshotMode();

	//closes all documents, makes the current view the only one visible

	//first check if we need to save any file
	for (size_t i = 0; i < _mainDocTab->nbItem() && !noSaveToAll; ++i)
	{
		BufferID id = _mainDocTab->getBufferByIndex(i);
		Buffer* buf = MainFileManager.getBufferByID(id);
		if (id == current)
			continue;

		if (buf->isUntitled() && buf->docLength() == 0)
		{
			// Do nothing
		}
		else if (buf->isDirty())
		{
			activateBuffer(id, MAIN_VIEW);
			if (!activateBuffer(id, SUB_VIEW))
				switchEditViewTo(MAIN_VIEW);

			int res = -1;
			if (saveToAll)
			{
				res = IDYES;
			}
			else
			{
				size_t nbDirtyFiles = MainFileManager.getNbDirtyBuffers();

				// if current file is dirty, if should be removed from dirty files to make nbDirtyFiles accurate
				Buffer* currentBuf = MainFileManager.getBufferByID(current);
				nbDirtyFiles -= currentBuf->isDirty() ? 1 : 0;

				res = doSaveOrNot(buf->getFullPathName(), nbDirtyFiles > 1);
			}

			if (res == IDYES)
			{
				bool isSaved = fileSave(id);
				if (isSaved)
					mainSaveOpIndex.push_back((unsigned int)i);
				else
					res = IDCANCEL;	//about to abort entire procedure
			}
			else if (res == IDNO)
			{
				mainSaveOpIndex.push_back((unsigned int)i);
			}
			else if (res == IDIGNORE)
			{
				noSaveToAll = true;
			}
			else if (res == IDRETRY)
			{
				if (!fileSave(id))
					return false;	// Abort entire procedure.

				saveToAll = true;
			}


			if (res == IDCANCEL)
			{
				for (auto j = static_cast<int>(mainSaveOpIndex.size()) - 1; j >= 0; --j) //close all from right to left
				{
					doClose(_mainDocTab->getBufferByIndex(mainSaveOpIndex[j]), MAIN_VIEW, isSnapshotMode);
				}

				return false;
			}

		}
	}

	for (size_t i = 0; i < _subDocTab->nbItem() && !noSaveToAll; ++i)
	{
		BufferID id = _subDocTab->getBufferByIndex(i);
		Buffer * buf = MainFileManager.getBufferByID(id);
		if (id == current)
			continue;

		if (buf->isUntitled() && buf->docLength() == 0)
		{
			// Do nothing
		}
		else if (buf->isDirty())
		{
			activateBuffer(id, SUB_VIEW);
			switchEditViewTo(SUB_VIEW);

			int res = -1;
			if (saveToAll)
			{
				res = IDYES;
			}
			else
			{
				size_t nbDirtyFiles = MainFileManager.getNbDirtyBuffers();

				// if current file is dirty, if should be removed from dirty files to make nbDirtyFiles accurate
				Buffer* currentBuf = MainFileManager.getBufferByID(current);
				nbDirtyFiles -= currentBuf->isDirty() ? 1 : 0;

				res = doSaveOrNot(buf->getFullPathName(), nbDirtyFiles > 1);
			}


			if (res == IDYES)
			{
				bool isSaved = fileSave(id);
				if (isSaved)
					subSaveOpIndex.push_back((unsigned int)i);
				else
					res = IDCANCEL;	//about to abort entire procedure
			}
			else if (res == IDNO)
			{
				subSaveOpIndex.push_back((unsigned int)i);
			}
			else if (res == IDIGNORE)
			{
				noSaveToAll = true;
			}
			else if (res == IDRETRY)
			{
				if (!fileSave(id))
					return false;	// Abort entire procedure.

				saveToAll = true;
			}

			
			if (res == IDCANCEL)
			{
				for (auto j = static_cast<int>(mainSaveOpIndex.size()) - 1; j >= 0; --j) //close all from right to left
				{
					doClose(_mainDocTab->getBufferByIndex(mainSaveOpIndex[j]), MAIN_VIEW, isSnapshotMode);
				}

				for (auto j = static_cast<int>(subSaveOpIndex.size()) - 1; j >= 0; --j) //close all from right to left
				{
					doClose(_subDocTab->getBufferByIndex(subSaveOpIndex[j]), SUB_VIEW, isSnapshotMode);
				}
				return false;
			}

		}
	}

	// We may have to restore previous view after saving new files
	switchEditViewTo(activeViewID);

	//Then start closing, inactive view first so the active is left open
    if (bothActive())
    {
		//first close all docs in non-current view, which gets closed automatically
		//Set active tab to the last one closed.
		const int viewNo = otherView();
		activateBuffer(_pNonDocTab->getBufferByIndex(0), viewNo);

		for (auto i = static_cast<int>(_pNonDocTab->nbItem()) - 1; i >= 0; --i) //close all from right to left
		{
			doClose(_pNonDocTab->getBufferByIndex(i), viewNo, isSnapshotMode);
		}
    }

	const int viewNo = currentView();
	size_t nbItems = _pDocTab->nbItem();
	activateBuffer(_pDocTab->getBufferByIndex(0), viewNo);
	
	// After activateBuffer() call, if file is deleted, user will decide to keep or not the tab
	// So here we check if the 1st tab is closed or not
	size_t newNbItems = _pDocTab->nbItem();

	if (nbItems > newNbItems) // the 1st tab has been removed
	{
		// active tab move 1 position forward
		active -= 1;
	}

	for (auto i = static_cast<int>(newNbItems) - 1; i >= 0; --i) //close all from right to left
	{
		if (i == active)	//don't close active index
		{
			continue;
		}
		doClose(_pDocTab->getBufferByIndex(i), viewNo, isSnapshotMode);
	}
	return true;
}

// ============================================================================
// Monitoring, "tail -f" (upstream Notepad_plus.cpp / NppIO.cpp monitorFileOnChange)
// ============================================================================

void Notepad_plus::monitoringStartOrStopAndUpdateUI(Buffer* pBuf, bool isStarting)
{
	if (pBuf)
	{
		if (isStarting)
			pBuf->startMonitoring();
		else
			pBuf->stopMonitoring();

		checkMenuItem(IDM_VIEW_MONITORING, isStarting);
		pBuf->setUserReadOnly(isStarting);

		// NPLX: the watcher replaces the thread, which upstream stops through the buffer's monitoring event
		if (!isStarting)
		{
			auto it = _monitors.find(pBuf);
			if (it != _monitors.end())
			{
				it->second->deleteLater();
				_monitors.erase(it);
			}
		}
	}
}

void Notepad_plus::createMonitoringThread(Buffer* pBuf)
{
	// NPLX: QFileSystemWatcher (inotify) in place of the monitorFileOnChange thread
	// (ReadDirectoryChanges + 250 ms polling). Changes are coalesced to one reload per 250 ms, like upstream.
	if (_monitors.count(pBuf))
		return;

	auto* watcher = new QFileSystemWatcher(this);
	const QString path = QString::fromWCharArray(pBuf->getFullPathName());
	watcher->addPath(path);
	auto* rateLimit = new QTimer(watcher);
	rateLimit->setSingleShot(true);
	rateLimit->setInterval(250);	// Limit refresh rate

	connect(rateLimit, &QTimer::timeout, this, [this, pBuf, path, watcher]() {
		if (!pBuf->isMonitoringOn())
			return;
		if (!QFileInfo::exists(path))
		{
			// File is deleted or renamed - quit monitoring (upstream NPPM_INTERNAL_STOPMONITORING)
			monitoringStartOrStopAndUpdateUI(pBuf, false);
			return;
		}
		// a file replaced by rename drops out of the inotify watch: watch it again
		if (!watcher->files().contains(path))
			watcher->addPath(path);
		pBuf->reload(); // upstream NPPM_INTERNAL_RELOADSCROLLTOEND
	});
	connect(watcher, &QFileSystemWatcher::fileChanged, rateLimit, [rateLimit]() {
		if (!rateLimit->isActive())
			rateLimit->start();
	});
	_monitors[pBuf] = watcher;
}
