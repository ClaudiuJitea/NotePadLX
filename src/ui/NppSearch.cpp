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

// NPLX: the search parts of upstream Notepad_plus.cpp (findInFiles, findInOpenedFiles, replaceInOpenedFiles...,
// braceMatch, goToNextIndicator) and the messages of NppBigSwitch.cpp that the Find dialog and the Finder send.

#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
#include <QProgressDialog>

#include "Notepad_plus.h"

#include "FindReplaceDlg.h"
#include "RunDlg.h"
#include "AnsiCharPanel.h"
#include "DocumentMap.h"
#include "FileBrowser.h"
#include "ProjectPanel.h"
#include "ClipboardHistoryPanel.h"
#include "FunctionListPanel.h"
#include "VerticalFileSwitcher.h"
#include "AutoCompletion.h"
#include "WordStyleDlg.h"
#include "Parameters.h"
#include "ScintillaEditView.h"
#include "DocTabView.h"
#include "menuCmdID.h"

using namespace std;

// NPLX: upstream Progress (FindReplaceDlg.cpp) runs its own window in a thread; here a QProgressDialog
// shown by the searching (GUI) thread, which processes events at each update so Cancel works.
class Progress
{
public:
	explicit Progress(QWidget* parent) : _parent(parent) {}
	~Progress() { close(); }

	Progress(const Progress&) = delete;
	const Progress& operator=(const Progress&) = delete;

	void open(QWidget* hCallerWnd, const wchar_t* header = nullptr)
	{
		_dialog = new QProgressDialog(hCallerWnd ? hCallerWnd : _parent);
		_dialog->setWindowTitle(QString::fromWCharArray(header ? header : L"Operation progress..."));
		_dialog->setWindowModality(Qt::WindowModal);
		_dialog->setMinimumDuration(500);
		_dialog->setRange(0, 100);
		_dialog->setAutoClose(false);
		_dialog->setAutoReset(false);
		_dialog->setMinimumWidth(500);
		_dialog->setValue(0);
	}

	void close()
	{
		if (_dialog)
		{
			_dialog->close();
			delete _dialog;
			_dialog = nullptr;
		}
	}

	bool isCancelled() const
	{
		return _dialog && _dialog->wasCanceled();
	}

	void setPercent(unsigned percent, const wchar_t* fileName, int nbHitsSoFar) const
	{
		if (!_dialog)
			return;
		_dialog->setValue(static_cast<int>(percent));
		setInfo(fileName, nbHitsSoFar);
	}

	void setInfo(const wchar_t* info, int nbHitsSoFar = -1) const
	{
		if (!_dialog)
			return;
		QString text = QString::fromWCharArray(info);
		if (nbHitsSoFar >= 0)
			text = QStringLiteral("%1\nHits: %2").arg(text).arg(nbHitsSoFar);
		_dialog->setLabelText(text);
		QCoreApplication::processEvents();
	}

private:
	QWidget* _parent = nullptr;
	QProgressDialog* _dialog = nullptr;
};

class MatchedFileNameProgress final
{
public:
	static constexpr std::chrono::milliseconds updateInterval{ 50 }; // 20 Hz; enough for the progress dialog

	explicit MatchedFileNameProgress(Progress & progress) : _pProgress(&progress) {}

	bool due() const
	{
		return std::chrono::steady_clock::now() - _pLastUpdateTime >= updateInterval;
	}

	void noteUpdated()
	{
		_pLastUpdateTime = std::chrono::steady_clock::now();
	}

	// Updates progress if enough time has passed and returns whether search should continue
	bool report(const wchar_t * currentPath, int nbHitsSoFar)
	{
		if (!due())
			return true;
		if (_pProgress->isCancelled())
			return false;
		_pProgress->setInfo(currentPath, nbHitsSoFar);
		noteUpdated();
		return true;
	}
private:
	std::chrono::steady_clock::time_point _pLastUpdateTime = std::chrono::steady_clock::now();
	Progress* _pProgress;
};

// ============================================================================
// Messages from the children (upstream NppBigSwitch.cpp)
// ============================================================================

intptr_t Notepad_plus::nppMessage(unsigned int message, uptr_t wParam, sptr_t lParam)
{
	switch (message)
	{
		case WM_MOUSEWHEEL:
		{
			if (0 != (LOWORD(wParam) & MK_RBUTTON))
			{
				// redirect to the IDC_PREV_DOC or IDC_NEXT_DOC so that we have the unified process

				NppParameters::getInstance()._isTaskListRBUTTONUP_Active = true;
				short zDelta = (short) HIWORD(wParam);
				command(zDelta > 0 ? IDC_PREV_DOC : IDC_NEXT_DOC);
			}
			return TRUE;
		}

		case WM_REPLACEALL_INOPENEDDOC:
		{
			replaceInOpenedFiles();
			return TRUE;
		}

		case WM_FINDALL_INOPENEDDOC:
		{
			findInOpenedFiles();
			return TRUE;
		}

		case WM_FINDALL_INCURRENTDOC:
		{
			const bool isEntireDoc = wParam == 0;
			return findInCurrentFile(isEntireDoc);
		}

		case WM_FINDINFILES:
		{
			return findInFiles();
		}

		case WM_FINDINPROJECTS:
		{
			return findInProjects();
		}

		case WM_REPLACEINPROJECTS:
		{
			return replaceInProjects();
		}

		case NPPM_INTERNAL_ISMENUITEMCHECKED:
		{
			return isMenuItemChecked(static_cast<int>(wParam));
		}

		case WM_REPLACEINFILES:
		{
			replaceInFiles();
			return TRUE;
		}

		case NPPM_DOOPEN:
		case WM_DOOPEN:
		{
			BufferID id = doOpen(reinterpret_cast<const wchar_t *>(lParam));
			if (id != BUFFER_INVALID)
				return switchToFile(id);
			break;
		}

		case WM_REMOVE_USERLANG:
		{
			wchar_t *userLangName = reinterpret_cast<wchar_t *>(lParam);
			if (!userLangName || !userLangName[0])
				return FALSE;

			wstring name{userLangName};

			//loop through buffers and reset the language (L_USER, L"")) if (L_USER, name)
			for (size_t i = 0; i < MainFileManager.getNbBuffers(); ++i)
			{
				Buffer* buf = MainFileManager.getBufferByIndex(i);
				if (buf->getLangType() == L_USER && name == buf->getUserDefineLangName())
					buf->setLangType(L_USER, L"");
			}
			return TRUE;
		}

		case WM_RENAME_USERLANG:
		{
			if (!lParam || !((reinterpret_cast<wchar_t *>(lParam))[0]) || !wParam || !((reinterpret_cast<wchar_t *>(wParam))[0]))
				return FALSE;

			wstring oldName{ reinterpret_cast<wchar_t *>(lParam) };
			wstring newName{ reinterpret_cast<wchar_t *>(wParam) };

			//loop through buffers and reset the language (L_USER, newName) if (L_USER, oldName)
			for (size_t i = 0; i < MainFileManager.getNbBuffers(); ++i)
			{
				Buffer* buf = MainFileManager.getBufferByIndex(i);
				if (buf->getLangType() == L_USER && oldName == buf->getUserDefineLangName())
					buf->setLangType(L_USER, newName.c_str());
			}
			return TRUE;
		}

		case NPPM_INTERNAL_DOCMODIFIEDBYREPLACEALL:
		{
			Buffer* currentBuf = _pEditView->getCurrentBuffer();
			if (wParam == reinterpret_cast<uptr_t>(currentBuf))
			{
				int urlAction = NppParameters::getInstance().getNppGUI()._styleURL;
				if (urlAction != urlDisable && currentBuf->allowClickableLink())
				{
					addHotSpot(_pEditView);
				}
			}
			// NPLX_TODO(Phase 9): NPPN_GLOBALMODIFIED for plugins
			return TRUE;
		}

		case WM_UPDATESCINTILLAS:
		{
			updateScintillas(wParam == TRUE);
			return TRUE;
		}

		case WM_UPDATEMAINMENUBITMAPS:
		{
			setupColorSampleBitmapsOnMainMenuItems();
			return TRUE;
		}

		case NPPM_GETCURRENTMACROSTATUS:
		{
			return macroStatus();
		}

		case WM_FRSAVE_INT:
		{
			_macro.push_back(recordedMacroStep(static_cast<int32_t>(wParam), 0, lParam, NULL, recordedMacroStep::mtSavedSnR));
			break;
		}

		case WM_FRSAVE_STR:
		{
			_macro.push_back(recordedMacroStep(static_cast<int32_t>(wParam), 0, 0, reinterpret_cast<const char *>(lParam), recordedMacroStep::mtSavedSnR));
			break;
		}

		case WM_MACRODLGRUNMACRO:
		{
			runMacroFromDlg(); // NPLX: upstream body in NppMacro.cpp
			break;
		}

		case NPPM_INTERNAL_FINDKEYCONFLICTS:
		{
			// NPLX: wParam = const KeyCombo*, lParam = bool* (upstream ShortcutMapper session handling not needed)
			if (!wParam || !lParam)
				return TRUE;
			*reinterpret_cast<bool*>(lParam) = findKeyConflicts(*reinterpret_cast<const KeyCombo*>(wParam));
			return TRUE;
		}

		case NPPM_GETFULLCURRENTPATH:
		case NPPM_GETCURRENTDIRECTORY:
		case NPPM_GETFILENAME:
		case NPPM_GETNAMEPART:
		case NPPM_GETEXTPART:
		{
			// NPLX: std::filesystem in place of the Shlwapi Path* functions
			const std::filesystem::path fullPath(_pEditView->getCurrentBuffer()->getFullPathName());
			std::wstring fileStr = fullPath.wstring();
			// par defaut : NPPM_GETCURRENTDIRECTORY

			if (message == NPPM_GETCURRENTDIRECTORY)
				fileStr = fullPath.parent_path().wstring();
			else if (message == NPPM_GETFILENAME)
				fileStr = fullPath.filename().wstring();
			else if (message == NPPM_GETNAMEPART)
				fileStr = fullPath.stem().wstring();
			else if (message == NPPM_GETEXTPART)
				fileStr = fullPath.extension().wstring();

			// For the compatibility reasons, if wParam is 0, then we assume the size of wstring buffer (lParam) is large enough.
			// otherwise we check if the wstring buffer size is enough for the wstring to copy.
			if (wParam != 0)
			{
				if (fileStr.length() >= wParam)
				{
					return FALSE;
				}
			}

			wcscpy(reinterpret_cast<wchar_t *>(lParam), fileStr.c_str());
			return TRUE;
		}

		case NPPM_GETCURRENTWORD:
		case NPPM_GETCURRENTLINESTR:
		{
			const int strSize = CURRENTWORD_MAXLENGTH; // "If you are using the ShellExecute/Ex function, then you become subject to the INTERNET_MAX_URL_LENGTH (around 2048)
			                                           // command line length limit imposed by the ShellExecute/Ex functions."
			                                           // https://devblogs.microsoft.com/oldnewthing/20031210-00/?p=41553

			auto str = std::make_unique<wchar_t[]>(strSize);
			std::fill_n(str.get(), strSize, L'\0');

			wchar_t *pTchar = reinterpret_cast<wchar_t *>(lParam);

			if (message == NPPM_GETCURRENTWORD)
			{
				auto txtW = _pEditView->getSelectedTextToWChar();
				wcsncpy(str.get(), txtW.c_str(), strSize - 1);
			}
			else if (message == NPPM_GETCURRENTLINESTR)
			{
				_pEditView->getLine(_pEditView->getCurrentLineNumber(), str.get(), strSize);
			}

			// For the compatibility reason, if wParam is 0, then we assume the size of wstring buffer (lParam) is large enough.
			// otherwise we check if the wstring buffer size is enough for the wstring to copy.
			BOOL result = FALSE;
			if (wParam == 0)
			{
				wcscpy(pTchar, str.get()); // compatibility mode - don't check the size
				result = TRUE;
			}
			else
			{
				if (wcslen(str.get()) < wParam)	// buffer large enough, perform safe copy
				{
					wcsncpy(pTchar, str.get(), wParam);
					result = TRUE;
				}
			}

			return result;
		}

		case NPPM_GETFILENAMEATCURSOR: // wParam = buffer length, lParam = (wchar_t*)buffer
		{
			constexpr int strSize = CURRENTWORD_MAXLENGTH;
			auto str = std::make_unique<wchar_t[]>(strSize);
			std::fill_n(str.get(), strSize, L'\0');

			auto CharacterIs = [](wchar_t c, const wchar_t* any) { return c != 0 && std::wcschr(any, c) != nullptr; };

			int hasSlash = 0;

			auto txtW = _pEditView->getSelectedTextToWChar(); // this is either the selected text, or the word under the cursor if there is no selection
			wcsncpy(str.get(), txtW.c_str(), strSize - 1);

			hasSlash = FALSE;
			for (int i = 0; str[i] != 0; i++)
				if (CharacterIs(str[i], L"\\/"))
					hasSlash = TRUE;

			if (hasSlash == FALSE)
			{
				// it's not a full file name so try to find the beginning and ending of it
				intptr_t start = 0;
				intptr_t end = 0;
				const wchar_t *delimiters;
				auto strLine = std::make_unique<wchar_t[]>(strSize);
				std::fill_n(strLine.get(), strSize, L'\0');

				size_t lineNumber = 0;
				intptr_t col = 0;

				lineNumber = _pEditView->getCurrentLineNumber();
				col = _pEditView->execute(SCI_GETCURRENTPOS) - _pEditView->execute(SCI_POSITIONFROMLINE, lineNumber);
				_pEditView->getLine(lineNumber, strLine.get(), strSize);

				// find the start
				start = col;
				delimiters = L" \t[(\"<>";
				while ((start > 0) && (CharacterIs(strLine[start], delimiters) == FALSE))
					start--;

				if (CharacterIs(strLine[start], delimiters)) start++;

				// find the end
				end = col;
				delimiters = L" \t:()[]<>\"\r\n";
				while ((strLine[end] != 0) && (CharacterIs(strLine[end], delimiters) == FALSE)) end++;

				wcsncpy(str.get(), &strLine[start], static_cast<size_t>(end - start));
				str[end - start] = L'\0';
			}

			if (static_cast<int>(wcslen(str.get())) >= int(wParam))	//buffer too small
			{
				return FALSE;
			}
			else //buffer large enough, perform safe copy
			{
				wchar_t* pTchar = reinterpret_cast<wchar_t*>(lParam);
				wcsncpy(pTchar, str.get(), wParam);
				return TRUE;
			}
		}

		case NPPM_GETNPPFULLFILEPATH:
		case NPPM_GETNPPDIRECTORY:
		{
			// NPLX: ::GetModuleFileName(NULL, ...)
			std::wstring str = QCoreApplication::applicationFilePath().toStdWString();

			if (message == NPPM_GETNPPDIRECTORY)
				str = QCoreApplication::applicationDirPath().toStdWString();

			// For the compatibility reason, if wParam is 0, then we assume the size of wstring buffer (lParam) is large enough.
			// otherwise we check if the wstring buffer size is enough for the wstring to copy.
			if (wParam != 0)
			{
				if (str.length() >= wParam)
				{
					return FALSE;
				}
			}

			wcscpy(reinterpret_cast<wchar_t *>(lParam), str.c_str());
			return TRUE;
		}

		case NPPM_GETCURRENTLINE:
		{
			return _pEditView->getCurrentLineNumber();
		}

		case NPPM_GETCURRENTCOLUMN:
		{
			return _pEditView->getCurrentColumnNumber();
		}

		case NPPM_DMMREGASDCKDLG:
		{
			// NPLX: upstream registers a DockedWidgetData with the docking manager; the Finder is a QDockWidget
			auto* dock = reinterpret_cast<QDockWidget*>(lParam);
			addDockWidget(Qt::BottomDockWidgetArea, dock);
			return TRUE;
		}

		case NPPM_INTERNAL_ENABLECOMMAND:
		{
			enableCommand(static_cast<int>(wParam), lParam != 0);
			return TRUE;
		}

		case WM_COMMAND:
		{
			command(static_cast<int>(wParam));
			return TRUE;
		}

		default:
		{
			intptr_t result = FALSE;
			if (preferenceMessage(message, wParam, lParam, result))
				return result;
			break;
		}
	}
	return FALSE;
}

// ============================================================================
// Search in documents and files (upstream Notepad_plus.cpp)
// ============================================================================

void Notepad_plus::setCodePageForInvisibleView(Buffer const *pBuffer)
{
	setCodePageForInvisibleView(pBuffer->getUnicodeMode());
}

void Notepad_plus::setCodePageForInvisibleView(UniMode unicodeMode)
{
	intptr_t detectedCp = _invisibleEditView->execute(SCI_GETCODEPAGE);
	intptr_t cp2set = SC_CP_UTF8;
	if (unicodeMode == uni8Bit)
	{
		cp2set = (detectedCp == SC_CP_UTF8 ? CP_ACP : detectedCp);
	}
	_invisibleEditView->execute(SCI_SETCODEPAGE, cp2set);
}

bool Notepad_plus::replaceInOpenedFiles()
{
	ScintillaEditView *pOldView = _pEditView;
	_pEditView = _invisibleEditView;
	Document oldDoc = _invisibleEditView->execute(SCI_GETDOCPOINTER);
	Buffer * oldBuf = _invisibleEditView->getCurrentBuffer();	//for manually setting the buffer, so notifications can be handled properly

	Buffer * pBuf = NULL;

	int nbTotal = 0;
	const bool isEntireDoc = true;
	bool hasInvalidRegExpr = false;

	if (viewVisible(MAIN_VIEW))
	{
		for (size_t i = 0, len = _mainDocTab->nbItem(); i < len ; ++i)
		{
			pBuf = MainFileManager.getBufferByID(_mainDocTab->getBufferByIndex(i));
			if (pBuf->isReadOnly())
				continue;
			_invisibleEditView->execute(SCI_SETDOCPOINTER, 0, pBuf->getDocument());

			setCodePageForInvisibleView(pBuf);

			_invisibleEditView->setCurrentBuffer(pBuf);

			_invisibleEditView->execute(SCI_BEGINUNDOACTION);
			int nb = _findReplaceDlg->processAll(ProcessReplaceAll, FindReplaceDlg::_env, isEntireDoc);
			_invisibleEditView->execute(SCI_ENDUNDOACTION);
			if (nb == FIND_INVALID_REGULAR_EXPRESSION)
			{
				hasInvalidRegExpr = true;
				break;
			}
			else
			{
				nbTotal += nb;
			}
		}
	}

	if (!hasInvalidRegExpr && viewVisible(SUB_VIEW))
	{
		for (size_t i = 0, len = _subDocTab->nbItem(); i < len; ++i)
		{
			BufferID bufId = _subDocTab->getBufferByIndex(i);

			if (_mainDocTab->getIndexByBuffer(bufId) != -1)
			{
				// cloned doc, replacements already done in main doc
				continue;
			}

			pBuf = MainFileManager.getBufferByID(bufId);
			if (pBuf->isReadOnly())
				continue;
			_invisibleEditView->execute(SCI_SETDOCPOINTER, 0, pBuf->getDocument());

			setCodePageForInvisibleView(pBuf);

			_invisibleEditView->setCurrentBuffer(pBuf);

			_invisibleEditView->execute(SCI_BEGINUNDOACTION);
			int nb = _findReplaceDlg->processAll(ProcessReplaceAll, FindReplaceDlg::_env, isEntireDoc);
			_invisibleEditView->execute(SCI_ENDUNDOACTION);
			if (nb == FIND_INVALID_REGULAR_EXPRESSION)
			{
				hasInvalidRegExpr = true;
				break;
			}
			else
			{
				nbTotal += nb;
			}
		}
	}

	_invisibleEditView->execute(SCI_SETDOCPOINTER, 0, oldDoc);

	_invisibleEditView->setCurrentBuffer(oldBuf);
	_pEditView = pOldView;

	if (hasInvalidRegExpr)
	{
		_findReplaceDlg->setStatusbarMessageWithRegExprErr(_invisibleEditView);
	}
	else
	{
		if (nbTotal)
			enableCommand(IDM_FILE_SAVEALL, true);

		wstring result;
		if (nbTotal == 1)
		{
			result = _nativeLangSpeaker.getLocalizedStrFromID("find-status-replaceinopenedfiles-1-replaced", L"Replace in Opened Files: 1 occurrence was replaced.");
		}
		else
		{
			result = _nativeLangSpeaker.getLocalizedStrFromID("find-status-replaceinopenedfiles-nb-replaced", L"Replace in Opened Files: $INT_REPLACE$ occurrences were replaced.");
			result = stringReplace(result, L"$INT_REPLACE$", std::to_wstring(nbTotal));
		}
		_findReplaceDlg->setStatusbarMessage(result, FSMessage);
	}

	return true;
}

void Notepad_plus::getMatchedFileNames(const wchar_t *dir, size_t level, const vector<wstring> & patterns, vector<wstring> & fileNames, bool isRecursive, bool isInHiddenDir, MatchedFileNameProgress* progress)
{
	vector<SearchCandidate> files;
	getMatchedFileNames(dir, level, patterns, files, isRecursive, isInHiddenDir, progress);
	fileNames.reserve(fileNames.size() + files.size());
	for (auto&& file : files)
	{
		fileNames.push_back(std::move(file._path));
	}
}

void Notepad_plus::getMatchedFileNames(const wchar_t *dir, size_t level, const vector<wstring> & patterns, vector<SearchCandidate> & fileNames, bool isRecursive, bool isInHiddenDir, MatchedFileNameProgress* progress)
{
	level++;

	// NPLX: QDir in place of FindFirstFile/FindNextFile; hidden = dot files; symlinked folders are not followed
	// (FindFirstFile doesn't enter junction loops either)
	const QDir qdir(QString::fromWCharArray(dir));
	const QFileInfoList entries = qdir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDir::Name);
	for (const QFileInfo& foundData : entries)
	{
		const std::wstring fileName = foundData.fileName().toStdWString();
		if (foundData.isDir())
		{
			if (!isInHiddenDir && foundData.isHidden())
			{
				// do nothing
			}
			else if (isRecursive && !foundData.isSymLink())
			{
				if (!matchInExcludeDirList(fileName.c_str(), patterns, level))
				{
					wstring pathDir(dir);
					pathDir += fileName;
					pathDir += L"/";
					getMatchedFileNames(pathDir.c_str(), level, patterns, fileNames, isRecursive, isInHiddenDir, progress);
				}
			}
		}
		else if (foundData.isFile())
		{
			if (matchInList(fileName.c_str(), patterns))
			{
				wstring pathFile(dir);
				pathFile += fileName;
				fileNames.push_back(SearchCandidate{ std::move(pathFile), static_cast<int64_t>(foundData.size()) });
			}
		}

		if (progress && !progress->report(dir, static_cast<int>(fileNames.size())))
		{
			break;
		}
	}
}

bool Notepad_plus::createFilelistForFiles(vector<SearchCandidate> & fileNames)
{
	const wchar_t *dir2Search = _findReplaceDlg->getDir2Search();
	if (!dir2Search[0] || !doesDirectoryExist(dir2Search))
	{
		return false;
	}

	vector<wstring> patterns2Match;
	_findReplaceDlg->getAndValidatePatterns(patterns2Match);

	bool isRecursive = _findReplaceDlg->isRecursive();
	bool isInHiddenDir = _findReplaceDlg->isInHiddenDir();

	Progress progress(this);
	wstring msg = _nativeLangSpeaker.getLocalizedStrFromID("discover-file-candidates-title", L"Discovering file candidates...");
	progress.open(_findReplaceDlg, msg.c_str());
	progress.setInfo(L"", 0);

	MatchedFileNameProgress matchedFileNameProgress{ progress };
	getMatchedFileNames(dir2Search, 0, patterns2Match, fileNames, isRecursive, isInHiddenDir, &matchedFileNameProgress);

	return !progress.isCancelled();
}

namespace
{
	std::mutex replaceInFiles_mutex;
}

bool Notepad_plus::replaceInFiles()
{
	std::lock_guard<std::mutex> lock(replaceInFiles_mutex);

	std::vector<SearchCandidate> fileList;
	if (!createFilelistForFiles(fileList))
		return false;

	std::vector<wstring> fileNames;
	fileNames.reserve(fileList.size());
	for (const auto& file : fileList)
		fileNames.push_back(file._path);

	return replaceInFilelist(fileNames);
}

bool Notepad_plus::replaceInFilelist(std::vector<wstring> & fileNames)
{
	int nbTotal = 0;

	ScintillaEditView *pOldView = _pEditView;
	_pEditView = _invisibleEditView;
	Document oldDoc = _invisibleEditView->execute(SCI_GETDOCPOINTER);
	Buffer * oldBuf = _invisibleEditView->getCurrentBuffer();	//for manually setting the buffer, so notifications can be handled properly

	Progress progress(this);
	size_t filesCount = fileNames.size();
	size_t filesPerPercent = 1;

	if (filesCount > 1)
	{
		if (filesCount >= 200)
			filesPerPercent = filesCount / 100;

		wstring msg = _nativeLangSpeaker.getLocalizedStrFromID("replace-in-files-progress-title", L"Replace In Files progress...");
		progress.open(_findReplaceDlg, msg.c_str());
	}

	bool hasInvalidRegExpr = false;

	NppGUI& nppGUI = (NppParameters::getInstance()).getNppGUI();

	for (size_t i = 0, updateOnCount = filesPerPercent; i < filesCount; ++i)
	{
		if (progress.isCancelled()) break;

		bool closeBuf = false;

		BufferID id = nppGUI._fif_ignoreunsavedChangesInOpenedFiles ? BUFFER_INVALID : MainFileManager.getBufferFromName(fileNames.at(i).c_str());

		if (id == BUFFER_INVALID)
		{
			id = MainFileManager.loadFile(fileNames.at(i).c_str());
			closeBuf = true;
		}

		if (id != BUFFER_INVALID)
		{
			Buffer * pBuf = MainFileManager.getBufferByID(id);
			_invisibleEditView->execute(SCI_SETDOCPOINTER, 0, pBuf->getDocument());

			setCodePageForInvisibleView(pBuf);

			_invisibleEditView->setCurrentBuffer(pBuf);

			FindersInfo findersInfo;
			findersInfo._pFileName = fileNames.at(i).c_str();
			int nbReplaced = _findReplaceDlg->processAll(ProcessReplaceAll, FindReplaceDlg::_env, true, &findersInfo);

			if (nbReplaced == FIND_INVALID_REGULAR_EXPRESSION)
			{
				hasInvalidRegExpr = true;
				break;
			}
			else
			{
				nbTotal += nbReplaced;
				if (nbReplaced)
				{
					MainFileManager.saveBuffer(id, pBuf->getFullPathName());
				}
			}
			if (closeBuf)
				MainFileManager.closeBuffer(id, _pEditView);
		}
		if (i == updateOnCount)
		{
			updateOnCount += filesPerPercent;
			progress.setPercent(int32_t((i * 100) / filesCount), fileNames.at(i).c_str(), nbTotal);
		}
		else
		{
			progress.setInfo(fileNames.at(i).c_str(), nbTotal);
		}
	}

	progress.close();

	_invisibleEditView->execute(SCI_SETDOCPOINTER, 0, oldDoc);
	_invisibleEditView->setCurrentBuffer(oldBuf);
	_pEditView = pOldView;

	wstring result;
	if (nbTotal == 1)
	{
		result = _nativeLangSpeaker.getLocalizedStrFromID("find-status-replaceinfiles-1-replaced", L"Replace in Files: 1 occurrence was replaced.");
	}
	else
	{
		result = _nativeLangSpeaker.getLocalizedStrFromID("find-status-replaceinfiles-nb-replaced", L"Replace in Files: $INT_REPLACE$ occurrences were replaced.");
		result = stringReplace(result, L"$INT_REPLACE$", std::to_wstring(nbTotal));
	}

	if (hasInvalidRegExpr)
		_findReplaceDlg->setStatusbarMessageWithRegExprErr(_invisibleEditView);
	else
		_findReplaceDlg->setStatusbarMessage(result, FSMessage);

	return true;
}

bool Notepad_plus::findInFiles()
{
	std::vector<SearchCandidate> fileNames;
	if (! createFilelistForFiles(fileNames))
		return false;

	return findInFilelist(fileNames);
}

bool Notepad_plus::findInFilelist(std::vector<SearchCandidate> & fileNames)
{
	int nbTotal = 0;
	ScintillaEditView *pOldView = _pEditView;
	_pEditView = _invisibleEditView;
	Document oldDoc = _invisibleEditView->execute(SCI_GETDOCPOINTER);

	_findReplaceDlg->beginNewFilesSearch();

	Progress progress(this);

	size_t filesCount = fileNames.size();

	if (filesCount > 1)
	{
		wstring msg = _nativeLangSpeaker.getLocalizedStrFromID("find-in-files-progress-title", L"Find In Files progress...");
		progress.open(_findReplaceDlg, msg.c_str());
	}

	const bool isEntireDoc = true;
	bool hasInvalidRegExpr = false;

	NppParameters& nppParam = NppParameters::getInstance();
	const NppGUI& nppGui = nppParam.getNppGUI();

	Document scratchDoc = MainFileManager.createSearchDocument();
	MatchedFileNameProgress progressUi(progress);

	for (size_t i = 0; i < filesCount; ++i)
	{
		if (progress.isCancelled()) break;

		const wchar_t* filePath = fileNames.at(i)._path.c_str();

		BufferID id = nppGui._fif_ignoreunsavedChangesInOpenedFiles ? BUFFER_INVALID : MainFileManager.getBufferFromName(filePath);

		if (id != BUFFER_INVALID)
		{
			Buffer * pBuf = MainFileManager.getBufferByID(id);
			_invisibleEditView->execute(SCI_SETDOCPOINTER, 0, pBuf->getDocument());

			setCodePageForInvisibleView(pBuf);

			FindersInfo findersInfo;
			findersInfo._pFileName = filePath;

			int nb = _findReplaceDlg->processAll(ProcessFindAll, FindReplaceDlg::_env, isEntireDoc, &findersInfo);

			if (nb == FIND_INVALID_REGULAR_EXPRESSION)
			{
				hasInvalidRegExpr = true;
				break;
			}

			nbTotal += nb;
		}
		else if (scratchDoc)
		{
			if (_invisibleEditView->execute(SCI_GETDOCPOINTER) != scratchDoc)
				_invisibleEditView->execute(SCI_SETDOCPOINTER, 0, scratchDoc);

			SearchLoadResult loaded = MainFileManager.loadFileContentForSearch({ scratchDoc, filePath, fileNames.at(i)._size, _invisibleEditView });
			if (loaded._ok)
			{
				// New documents start at code page 0; a reused doc may still have the previous file's code page
				_invisibleEditView->execute(SCI_SETCODEPAGE, 0);
				setCodePageForInvisibleView(loaded._unicodeMode);

				FindersInfo findersInfo;
				findersInfo._pFileName = filePath;

				int nb = _findReplaceDlg->processAll(ProcessFindAll, FindReplaceDlg::_env, isEntireDoc, &findersInfo);

				if (nb == FIND_INVALID_REGULAR_EXPRESSION)
				{
					hasInvalidRegExpr = true;
					break;
				}

				nbTotal += nb;
			}
		}
		if (progressUi.due())
		{
			progress.setPercent(int32_t((i * 100) / filesCount), filePath, nbTotal);
			progressUi.noteUpdated();
		}
	}

	progress.close();

	_findReplaceDlg->finishFilesSearch(nbTotal, int(filesCount), isEntireDoc);

	_invisibleEditView->execute(SCI_SETDOCPOINTER, 0, oldDoc);
	MainFileManager.releaseSearchDocument(scratchDoc);
	_pEditView = pOldView;

	_findReplaceDlg->putFindResult(nbTotal);

	if (hasInvalidRegExpr)
	{
		_findReplaceDlg->setStatusbarMessageWithRegExprErr(_invisibleEditView);
		return false;
	}

	if (nbTotal > 0 && !nppGui._findDlgAlwaysVisible)
	{
		_findReplaceDlg->hide();
	}

	return true;
}


bool Notepad_plus::findInOpenedFiles()
{
	int nbTotal = 0;
	ScintillaEditView *pOldView = _pEditView;
	_pEditView = _invisibleEditView;
	Document oldDoc = _invisibleEditView->execute(SCI_GETDOCPOINTER);

	Buffer * pBuf = NULL;

	const bool isEntireDoc = true;
	bool hasInvalidRegExpr = false;

	_findReplaceDlg->beginNewFilesSearch();

	if (viewVisible(MAIN_VIEW))
	{
		for (size_t i = 0, len = _mainDocTab->nbItem(); i < len ; ++i)
		{
			pBuf = MainFileManager.getBufferByID(_mainDocTab->getBufferByIndex(i));
			_invisibleEditView->execute(SCI_SETDOCPOINTER, 0, pBuf->getDocument());

			setCodePageForInvisibleView(pBuf);

			FindersInfo findersInfo;
			findersInfo._pFileName = pBuf->getFullPathName();

			int nb = _findReplaceDlg->processAll(ProcessFindAll, FindReplaceDlg::_env, isEntireDoc, &findersInfo);
			if (nb == FIND_INVALID_REGULAR_EXPRESSION)
			{
				hasInvalidRegExpr = true;
				break;
			}
			else
			{
				nbTotal += nb;
			}
		}
	}

	size_t nbUniqueBuffers = _mainDocTab->nbItem();

	if (!hasInvalidRegExpr && viewVisible(SUB_VIEW))
	{
		for (size_t i = 0, len2 = _subDocTab->nbItem(); i < len2 ; ++i)
		{
			pBuf = MainFileManager.getBufferByID(_subDocTab->getBufferByIndex(i));
			if (_mainDocTab->getIndexByBuffer(pBuf) != -1)
			{
				continue;  // clone was already searched in main; skip re-searching in sub
			}
			_invisibleEditView->execute(SCI_SETDOCPOINTER, 0, pBuf->getDocument());

			setCodePageForInvisibleView(pBuf);

			FindersInfo findersInfo;
			findersInfo._pFileName = pBuf->getFullPathName();

			int nb = _findReplaceDlg->processAll(ProcessFindAll, FindReplaceDlg::_env, isEntireDoc, &findersInfo);
			if (nb == FIND_INVALID_REGULAR_EXPRESSION)
			{
				hasInvalidRegExpr = true;
				break;
			}
			else
			{
				nbTotal += nb;
			}

			++nbUniqueBuffers;
		}
	}

	_findReplaceDlg->finishFilesSearch(nbTotal, int(nbUniqueBuffers), isEntireDoc);

	_invisibleEditView->execute(SCI_SETDOCPOINTER, 0, oldDoc);
	_pEditView = pOldView;

	_findReplaceDlg->putFindResult(nbTotal);

	if (hasInvalidRegExpr)
	{
		_findReplaceDlg->setStatusbarMessageWithRegExprErr(_invisibleEditView);
		return false;
	}

	if (nbTotal > 0)
	{
		NppParameters& nppParam = NppParameters::getInstance();
		const NppGUI& nppGui = nppParam.getNppGUI();
		if (!nppGui._findDlgAlwaysVisible)
		{
			_findReplaceDlg->hide();
		}
	}

	return true;
}


bool Notepad_plus::findInCurrentFile(bool isEntireDoc)
{
	int nbTotal = 0;
	Buffer * pBuf = _pEditView->getCurrentBuffer();

	Sci_CharacterRangeFull mainSelection = _pEditView->getSelection();  // remember selection before switching view

	ScintillaEditView *pOldView = _pEditView;
	_pEditView = _invisibleEditView;
	Document oldDoc = _invisibleEditView->execute(SCI_GETDOCPOINTER);

	_findReplaceDlg->beginNewFilesSearch();

	_invisibleEditView->execute(SCI_SETDOCPOINTER, 0, pBuf->getDocument());

	setCodePageForInvisibleView(pBuf);

	if (!isEntireDoc)
	{
		auto docLength = _invisibleEditView->execute(SCI_GETLENGTH);

		if ((mainSelection.cpMin > 0) || (mainSelection.cpMax < docLength))
		{
			_invisibleEditView->execute(SCI_SETSELECTIONSTART, mainSelection.cpMin);
			_invisibleEditView->execute(SCI_SETSELECTIONEND, mainSelection.cpMax);
		}
		else
		{
			isEntireDoc = true;
		}
	}

	FindersInfo findersInfo;
	findersInfo._pFileName = pBuf->getFullPathName();
	bool hasInvalidRegExpr = false;

	int nb = _findReplaceDlg->processAll(ProcessFindAll, FindReplaceDlg::_env, isEntireDoc, &findersInfo);
	if (nb == FIND_INVALID_REGULAR_EXPRESSION)
	{
		hasInvalidRegExpr = true;
	}
	else
	{
		nbTotal += nb;
	}

	_findReplaceDlg->finishFilesSearch(nbTotal, 1, isEntireDoc);

	_invisibleEditView->execute(SCI_SETDOCPOINTER, 0, oldDoc);
	_pEditView = pOldView;

	_findReplaceDlg->putFindResult(nbTotal);

	if (hasInvalidRegExpr)
	{
		_findReplaceDlg->setStatusbarMessageWithRegExprErr(_invisibleEditView);
		return false;
	}

	if (nbTotal > 0)
	{
		NppParameters& nppParam = NppParameters::getInstance();
		const NppGUI& nppGui = nppParam.getNppGUI();
		if (!nppGui._findDlgAlwaysVisible)
		{
			_findReplaceDlg->hide();
		}
	}

	return true;
}

void Notepad_plus::setFindReplaceFolderFilter(const wchar_t *dir, const wchar_t *filter)
{
	wstring fltr;
	NppParameters& nppParam = NppParameters::getInstance();
	NppGUI& nppGui = nppParam.getNppGUI();
	FindHistory & findHistory = nppParam.getFindHistory();

	// get current directory in case it's not provided.
	if (!dir && nppGui._fillDirFieldFromActiveDoc)
	{
		dir = nppParam.getWorkingDir();
	}

	// get current language file extensions in case it's not provided.
	if (!filter && findHistory._isFilterFollowDoc)
	{
		// Get current language file extensions
		const wchar_t *ext = NULL;
		LangType lt = _pEditView->getCurrentBuffer()->getLangType();

		if (lt == L_USER)
		{
			Buffer * buf = _pEditView->getCurrentBuffer();
			const UserLangContainer* userLangContainer = nppParam.getULCFromName(buf->getUserDefineLangName());
			if (userLangContainer)
				ext = userLangContainer->getExtention();
		}
		else
		{
			ext = nppParam.getLangExtFromLangType(lt);
		}

		if (ext && ext[0])
		{
			fltr = L"";
			vector<wstring> vStr;
			cutString(ext, vStr);
			for (size_t i = 0 ,len = vStr.size(); i < len; ++i)
			{
				fltr += L"*.";
				fltr += vStr[i] + L" ";
			}
		}
		else
		{
			fltr = L"*.*";
		}
		filter = fltr.c_str();
	}
	_findReplaceDlg->setFindInFilesDirFilter(dir, filter);
}

// ============================================================================
// Indicators and braces (upstream Notepad_plus.cpp)
// ============================================================================

bool Notepad_plus::goToPreviousIndicator(int indicID2Search, bool isWrap) const
{
    auto position = _pEditView->execute(SCI_GETCURRENTPOS);
	auto docLen = _pEditView->getCurrentDocLen();

    bool isInIndicator = _pEditView->execute(SCI_INDICATORVALUEAT, indicID2Search,  position) != 0;
    size_t posStart = _pEditView->execute(SCI_INDICATORSTART, indicID2Search,  position);
    size_t posEnd = _pEditView->execute(SCI_INDICATOREND, indicID2Search,  position);

	// pre-condition
	if ((posStart == 0) && (posEnd == docLen - 1))
		return false;

    if (posStart <= 0)
	{
		if (!isWrap)
			return false;

		isInIndicator = _pEditView->execute(SCI_INDICATORVALUEAT, indicID2Search,  docLen - 1) != 0;
		posStart = _pEditView->execute(SCI_INDICATORSTART, indicID2Search,  docLen - 1);
	}

    if (isInIndicator) // try to get out of indicator
    {
        posStart = _pEditView->execute(SCI_INDICATORSTART, indicID2Search, posStart - 1);
        if (posStart <= 0)
		{
			if (!isWrap)
				return false;
			posStart = _pEditView->execute(SCI_INDICATORSTART, indicID2Search,  docLen - 1);
		}
	}

    auto newPos = posStart - 1;
    posStart = _pEditView->execute(SCI_INDICATORSTART, indicID2Search, newPos);
    posEnd = _pEditView->execute(SCI_INDICATOREND, indicID2Search, newPos);

	// found
	if (_pEditView->execute(SCI_INDICATORVALUEAT, indicID2Search, posStart))
	{
		NppGUI & nppGUI = (NppParameters::getInstance()).getNppGUI();
		nppGUI._disableSmartHiliteTmp = true;

        auto currentline = _pEditView->execute(SCI_LINEFROMPOSITION, posEnd);
	    _pEditView->execute(SCI_ENSUREVISIBLE, currentline);	// make sure target line is unfolded

		_pEditView->execute(SCI_SETSEL, posEnd, posStart);
		_pEditView->execute(SCI_SCROLLCARET);
		return true;
	}
	return false;
}

bool Notepad_plus::goToNextIndicator(int indicID2Search, bool isWrap) const
{
    size_t position = _pEditView->execute(SCI_GETCURRENTPOS);
	size_t docLen = _pEditView->getCurrentDocLen();

    bool isInIndicator = _pEditView->execute(SCI_INDICATORVALUEAT, indicID2Search,  position) != 0;
    size_t posStart = _pEditView->execute(SCI_INDICATORSTART, indicID2Search,  position);
    size_t posEnd = _pEditView->execute(SCI_INDICATOREND, indicID2Search,  position);

	// pre-condition
	if ((posStart == 0) && (posEnd == docLen - 1))
		return false;

    if (posEnd >= docLen)
	{
		if (!isWrap)
			return false;

		isInIndicator = _pEditView->execute(SCI_INDICATORVALUEAT, indicID2Search,  0) != 0;
		posEnd = _pEditView->execute(SCI_INDICATOREND, indicID2Search, 0);
	}

    if (isInIndicator) // try to get out of indicator
    {
        posEnd = _pEditView->execute(SCI_INDICATOREND, indicID2Search, posEnd);

        if (posEnd >= docLen)
		{
			if (!isWrap)
				return false;
			posEnd = _pEditView->execute(SCI_INDICATOREND, indicID2Search, 0);
		}
    }
    auto newPos = posEnd;
    posStart = _pEditView->execute(SCI_INDICATORSTART, indicID2Search, newPos);
    posEnd = _pEditView->execute(SCI_INDICATOREND, indicID2Search, newPos);

	// found
	if (_pEditView->execute(SCI_INDICATORVALUEAT, indicID2Search, posStart))
	{
		NppGUI & nppGUI = (NppParameters::getInstance()).getNppGUI();
		nppGUI._disableSmartHiliteTmp = true;

        auto currentline = _pEditView->execute(SCI_LINEFROMPOSITION, posEnd);
	    _pEditView->execute(SCI_ENSUREVISIBLE, currentline);	// make sure target line is unfolded

		_pEditView->execute(SCI_SETSEL, posStart, posEnd);
		_pEditView->execute(SCI_SCROLLCARET);
		return true;
	}
	return false;
}

void Notepad_plus::findMatchingBracePos(intptr_t& braceAtCaret, intptr_t& braceOpposite)
{
	intptr_t caretPos = _pEditView->execute(SCI_GETCURRENTPOS);
	braceAtCaret = -1;
	braceOpposite = -1;
	wchar_t charBefore = '\0';

	intptr_t lengthDoc = _pEditView->execute(SCI_GETLENGTH);

	if ((lengthDoc > 0) && (caretPos > 0))
    {
		charBefore = wchar_t(_pEditView->execute(SCI_GETCHARAT, caretPos - 1, 0));
	}
	// Priority goes to character before caret
	if (charBefore && wcschr(L"[](){}", charBefore))
    {
		braceAtCaret = caretPos - 1;
	}

	if (lengthDoc > 0  && (braceAtCaret < 0))
    {
		// No brace found so check other side
		wchar_t charAfter = wchar_t(_pEditView->execute(SCI_GETCHARAT, caretPos, 0));
		if (charAfter && wcschr(L"[](){}", charAfter))
        {
			braceAtCaret = caretPos;
		}
	}
	if (braceAtCaret >= 0)
		braceOpposite = _pEditView->execute(SCI_BRACEMATCH, braceAtCaret, 0);
}

// return true if 1 or 2 (matched) brace(s) is found
bool Notepad_plus::braceMatch()
{
	Buffer* currentBuf = _pEditView->getCurrentBuffer();
	if (!currentBuf->allowBraceMach())
		return false;

	intptr_t braceAtCaret = -1;
	intptr_t braceOpposite = -1;
	findMatchingBracePos(braceAtCaret, braceOpposite);

	if ((braceAtCaret != -1) && (braceOpposite == -1))
    {
		_pEditView->execute(SCI_BRACEBADLIGHT, braceAtCaret);
		_pEditView->execute(SCI_SETHIGHLIGHTGUIDE, 0);
	}
    else
    {
		_pEditView->execute(SCI_BRACEHIGHLIGHT, braceAtCaret, braceOpposite);

		if (_pEditView->isShownIndentGuide())
        {
            intptr_t columnAtCaret = _pEditView->execute(SCI_GETCOLUMN, braceAtCaret);
		    intptr_t columnOpposite = _pEditView->execute(SCI_GETCOLUMN, braceOpposite);
			_pEditView->execute(SCI_SETHIGHLIGHTGUIDE, (columnAtCaret < columnOpposite)?columnAtCaret:columnOpposite);
        }
    }

	const bool enable = (braceAtCaret != -1) && (braceOpposite != -1);
    enableCommand(IDM_SEARCH_GOTOMATCHINGBRACE, enable);
	enableCommand(IDM_SEARCH_SELECTMATCHINGBRACES, enable);
    return (braceAtCaret != -1);
}

// ============================================================================
// Styles changed (upstream NppBigSwitch.cpp WM_UPDATESCINTILLAS)
// ============================================================================

void Notepad_plus::updateScintillas(bool doChangePanel)
{
	NppParameters& nppParam = NppParameters::getInstance();

	//reset styler for change in Stylers.xml
	_mainEditView->defineDocType(_mainEditView->getCurrentBuffer()->getLangType());
	_mainEditView->performGlobalStyles();

	_subEditView->defineDocType(_subEditView->getCurrentBuffer()->getLangType());
	_subEditView->performGlobalStyles();

	int urlAction = nppParam.getNppGUI()._styleURL;
	if (urlAction != urlDisable)
	{
		if (_mainEditView->getCurrentBuffer()->allowClickableLink())
			addHotSpot(_mainEditView);

		if (_subEditView->getCurrentBuffer()->allowClickableLink())
			addHotSpot(_subEditView);
	}

	_findReplaceDlg->updateFinderScintilla();

	drawTabbarColoursFromStylerArray();
	drawDocumentMapColoursFromStylerArray();

	// Update default fg/bg colors in Parameters for both internal/plugins docking dialog
	const Style* pStyle = nppParam.getGlobalStylers().findByID(STYLE_DEFAULT);
	if (pStyle)
	{
		nppParam.setCurrentDefaultFgColor(pStyle->_fgColor);
		nppParam.setCurrentDefaultBgColor(pStyle->_bgColor);
		drawAutocompleteColoursFromTheme(pStyle->_fgColor, pStyle->_bgColor);
	}

	AutoCompletion::drawAutocomplete(_pEditView);
	AutoCompletion::drawAutocomplete(_pNonEditView);

	if (_pDocMap)
		_pDocMap->setSyntaxHiliting();

	if (doChangePanel) // Theme change
	{
		// Set default fg/bg colors on internal docking dialog
		if (pStyle && _pFuncList)
		{
			_pFuncList->setBackgroundColor(pStyle->_bgColor);
			_pFuncList->setForegroundColor(pStyle->_fgColor);
		}
		if (pStyle && _pDocumentListPanel)
		{
			_pDocumentListPanel->setBackgroundColor(pStyle->_bgColor);
			_pDocumentListPanel->setForegroundColor(pStyle->_fgColor);
		}
		if (pStyle && _pAnsiCharPanel)
		{
			_pAnsiCharPanel->setBackgroundColor(pStyle->_bgColor);
			_pAnsiCharPanel->setForegroundColor(pStyle->_fgColor);
		}

		if (pStyle && _pClipboardHistoryPanel)
		{
			_pClipboardHistoryPanel->setBackgroundColor(pStyle->_bgColor);
			_pClipboardHistoryPanel->setForegroundColor(pStyle->_fgColor);
		}
		if (pStyle && _pFileBrowser)
		{
			_pFileBrowser->setBackgroundColor(pStyle->_bgColor);
			_pFileBrowser->setForegroundColor(pStyle->_fgColor);
		}
		ProjectPanel* projectPanels[] = { _pProjectPanel_1, _pProjectPanel_2, _pProjectPanel_3 };
		for (ProjectPanel* projectPanel : projectPanels)
		{
			if (pStyle && projectPanel)
			{
				projectPanel->setBackgroundColor(pStyle->_bgColor);
				projectPanel->setForegroundColor(pStyle->_fgColor);
			}
		}
	}
}
