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

// NPLX: port of upstream ScintillaComponent/FindReplaceDlg.h.
// The search engine (Searching, processFindNext, processReplace, processAll, processRange) and the
// Finder logic are kept as upstream. The Win32 dialog becomes a QDialog whose controls are looked up
// by their upstream control IDs (FindReplaceDlg_rc.h), and the Finder is a QDockWidget.
// Messages upstream sends to its parent (::SendMessage(_hParent, ...)) go through a callback set by Notepad_plus.

#pragma once

#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <QDialog>
#include <QDockWidget>

#include "Scintilla.h"
#include "BoostRegexSearch.h"

#include "Common.h"
#include "FindReplaceDlg_rc.h"
#include "Notepad_plus_msgs.h"
#include "Parameters.h"
#include "ParentMessenger.h"
#include "ScintillaEditView.h"
#include "resource.h"

class QAbstractButton;
class QComboBox;
class QLabel;
class QTabBar;

#define FIND_RECURSIVE 1
#define FIND_INHIDDENDIR 2

#define FIND_INVALID_REGULAR_EXPRESSION -2

#define FINDREPLACE_MAXLENGTH2SAVE 2048  // the maximum length of the string (decrease 1 for '\0') to save in the config.xml file

#define FINDTEMPSTRING_MAXSIZE 1024*1024

enum DIALOG_TYPE {FIND_DLG, REPLACE_DLG, FINDINFILES_DLG, FINDINPROJECTS_DLG, MARK_DLG};

#define DIR_DOWN true
#define DIR_UP false

#define FIND_STATUS_END_REACHED_TEXT L"Find: Reached document end, first occurrence from the top found."
#define FIND_STATUS_TOP_REACHED_TEXT L"Find:  Reached document beginning, first occurrence from the bottom found."
#define FIND_STATUS_REPLACE_END_REACHED_TEXT L"Replace: Reached document end, started from top."
#define FIND_STATUS_REPLACE_TOP_REACHED_TEXT L"Replace: Reached document beginning, started from bottom."

enum InWhat{ALL_OPEN_DOCS, FILES_IN_DIR, CURRENT_DOC, CURR_DOC_SELECTION, FILES_IN_PROJECTS};

struct FoundInfo {
	FoundInfo(intptr_t start, intptr_t end, size_t lineNumber, const wchar_t *fullPath)
		: _lineNumber(lineNumber), _fullPath(fullPath) {
		_ranges.push_back(std::pair<intptr_t, intptr_t>(start, end));
	}
	std::vector<std::pair<intptr_t, intptr_t>> _ranges;
	size_t _lineNumber = 0;
	std::wstring _fullPath;
};

struct TargetRange {
	int targetStart;
	int targetEnd;
};

enum SearchIncrementalType { NotIncremental, FirstIncremental, NextIncremental };
enum SearchType { FindNormal, FindExtended, FindRegex };
enum ProcessOperation { ProcessFindAll, ProcessReplaceAll, ProcessCountAll, ProcessMarkAll, ProcessHighLightAll, ProcessMarkAll_IncSearch, ProcessMarkAllExt, ProcessFindInFinder };

struct FindOption
{
	// In
	bool _isWholeWord = true;
	bool _isMatchCase = true;
	bool _isWrapAround = true;
	bool _whichDirection = DIR_DOWN;
	SearchIncrementalType _incrementalType = NotIncremental;
	SearchType _searchType = FindNormal;
	bool _doPurge = false;
	bool _doMarkLine = false;
	bool _isInSelection = false;
	std::wstring _str2Search;
	std::wstring _str4Replace;
	std::wstring _filters;
	std::wstring _directory;
	bool _isRecursive = true;
	bool _isInHiddenDir = false;
	bool _isProjectPanel_1 = false;
	bool _isProjectPanel_2 = false;
	bool _isProjectPanel_3 = false;
	bool _dotMatchesNewline = false;
	bool _isMatchLineNumber = false; // always false for main search

	// Out
	bool _regexEmptyStringFound = false; // used only for status bar tooltip display on countAll & markAll operations
};

struct MatchPosition
{
	intptr_t start = -1;
	intptr_t end = -1;
};

//This class contains generic search functions as static functions for easy access
class Searching {
public:
	static int convertExtendedToString(const wchar_t * query, wchar_t * result, int length);
	static TargetRange t;
	static int buildSearchFlags(const FindOption * option) {
		return	(option->_isWholeWord ? SCFIND_WHOLEWORD : 0) |
				(option->_isMatchCase ? SCFIND_MATCHCASE : 0) |
				(option->_searchType == FindRegex ? SCFIND_REGEXP|SCFIND_POSIX : 0) |
				((option->_searchType == FindRegex && option->_dotMatchesNewline) ? SCFIND_REGEXP_DOTMATCHESNL : 0);
	}
	static void displaySectionCentered(size_t posStart, size_t posEnd, ScintillaEditView* pEditView, bool isDownwards = true);

private:
	static bool readBase(const wchar_t * str, int * value, int base, int size);

};

// NPLX: upstream calls ::EnableMenuItem(::GetMenu(_hParent), id, ...) directly; here it asks Notepad_plus
#define NPPM_INTERNAL_ENABLECOMMAND (NOTEPADPLUS_USER_INTERNAL + 900) // wParam: IDM_*, lParam: TRUE/FALSE
#define NPPM_INTERNAL_ISMENUITEMCHECKED (NOTEPADPLUS_USER_INTERNAL + 901) // NPLX: wParam: IDM_* (upstream: GetMenuState on NPPM_INTERNAL_GETMENU)


//Finder: Dockable window that contains search results
class Finder : public QDockWidget {
	Q_OBJECT
friend class FindReplaceDlg;
public:
	explicit Finder(QWidget* parent = nullptr);
	~Finder() override = default;

	void init(ScintillaEditView** ppEditView, ParentMessenger sendToParent);

	void addSearchLine(const wchar_t *searchName);
	void addFileNameTitle(const wchar_t * fileName);
	void addFileHitCount(int count);
	void addSearchResultInfo(int count, int countSearched, bool searchedEntireNotSelection, const FindOption *pFindOpt);
	std::string foundLine(FoundInfo fi, SearchResultMarkingLine miLine, const wchar_t* foundline, size_t foundLineLen, size_t totalLineNumber);
	void setFinderStyle();
	void setFinderStyleForNpc(bool onlyColor = false);
	void removeAll();
	void openAll();
	void wrapLongLinesToggle();
	void purgeToggle();
	void copy();
	void copyPathnames();
	void beginNewFilesSearch();
	void finishFilesSearch(int count, int searchedCount, bool searchedEntireNotSelection, const FindOption *pFindOpt);

	void gotoNextFoundResult(int direction);
	std::pair<intptr_t, intptr_t> gotoFoundLine(size_t nOccurrence = 0); // value 0 means this argument is not used
	void deleteResult();
	std::vector<std::wstring> getResultFilePaths(bool onlyInSelectedText) const;
	bool canFind(const wchar_t *fileName, size_t lineNumber, size_t* indexToStartFrom) const;
	void setVolatiled(bool val) { _canBeVolatiled = val; }
	std::wstring getHitsString(int count) const;

	LRESULT scintillaExecute(UINT msg, WPARAM wParam = 0, LPARAM lParam = 0) const {
		return _scintView->execute(msg, wParam, lParam);
	}

	ScintillaEditView* getScintView() const { return _scintView; } // NPLX

	void display(bool toShow = true); // upstream DockingDlgInterface::display

protected:
	bool notify(SCNotification *notification);
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	enum { searchHeaderLevel = SC_FOLDLEVELBASE, fileHeaderLevel, resultLevel };

	enum CurrentPosInLineStatus { pos_infront, pos_between, pos_inside, pos_behind };

	struct CurrentPosInLineInfo {
		CurrentPosInLineStatus _status = pos_infront;
		intptr_t auxiliaryInfo = -1; // according the status - see getCurrentPosInLineInfo member function definition's above comment
	};

	ScintillaEditView** _ppEditView = nullptr;
	ParentMessenger _sendToParent;
	std::vector<FoundInfo> _foundInfos1;
	std::vector<FoundInfo> _foundInfos2;
	std::vector<FoundInfo>* _pMainFoundInfos = &_foundInfos1;
	std::vector<SearchResultMarkingLine> _markings1;
	std::vector<SearchResultMarkingLine> _markings2;
	std::vector<SearchResultMarkingLine>* _pMainMarkings = &_markings1;
	SearchResultMarkings _markingsStruct{};
	intptr_t _previousLineNumber = -1;

	ScintillaEditView* _scintView = nullptr;
	unsigned int _nbFoundFiles = 0;

	intptr_t _lastFileHeaderPos = 0;
	intptr_t _lastSearchHeaderPos = 0;

	bool _canBeVolatiled = true;
	bool _longLinesAreWrapped = false;
	bool _purgeBeforeEverySearch = false;
	bool _isDoubleClicked = false;

	std::wstring _prefixLineStr;

	void setFinderReadOnly(bool isReadOnly) {
		_scintView->execute(SCI_SETREADONLY, isReadOnly);
	}

	void showContextMenu(const QPoint& globalPos); // upstream WM_CONTEXTMENU
	void runCommand(int cmd); // upstream WM_COMMAND (NPPM_INTERNAL_SCINTILLAFINDER*)

	static bool isLineActualSearchResult(const std::wstring& s);
	std::wstring & prepareStringForClipboard(std::wstring & s) const;

	static FoundInfo EmptyFoundInfo;
	static SearchResultMarkingLine EmptySearchResultMarking;

	CurrentPosInLineInfo getCurrentPosInLineInfo(intptr_t currentPosInLine, const SearchResultMarkingLine& markingLine) const;
	void anchorWithNoHeaderLines(intptr_t& currentL, intptr_t initL, intptr_t minL, intptr_t maxL, int direction);
};


enum FindStatus { FSFound, FSNotFound, FSTopReached, FSEndReached, FSMessage, FSNoMessage, FSWarning, FSResetCount };

enum FindNextType {
	FINDNEXTTYPE_FINDNEXT,
	FINDNEXTTYPE_REPLACENEXT,
	FINDNEXTTYPE_FINDNEXTFORREPLACE
};

struct FindReplaceInfo
{
	const wchar_t *_txt2find = nullptr;
	const wchar_t *_txt2replace = nullptr;
	intptr_t _startRange = -1;
	intptr_t _endRange = -1;
};

struct FindersInfo
{
	Finder *_pSourceFinder = nullptr;
	Finder *_pDestFinder = nullptr;
	const wchar_t *_pFileName = nullptr;

	FindOption _findOption;
};

class FindReplaceDlg : public QDialog
{
	Q_OBJECT
public :
	static FindOption _options;
	static FindOption* _env;
	FindReplaceDlg();
	~FindReplaceDlg() override;

	// NPLX: upstream init(hInst, hPere, ppEditView); the parent is the QWidget, its messages go to sendToParent
	void init(QWidget* parent, ScintillaEditView** ppEditView, ParentMessenger sendToParent);

	bool isCreated() const { return _isCreated; }
	void create(); // NPLX: builds the Qt widgets (upstream: CreateDialog + WM_INITDIALOG)

	void initOptionsFromDlg();

	void doDialog(DIALOG_TYPE whichType, bool isRTL = false, bool toShow = true);
	bool processFindNext(const wchar_t* txt2find,
		const FindOption* options = nullptr, FindStatus* oFindStatus = nullptr, FindNextType findNextType = FINDNEXTTYPE_FINDNEXT, MatchPosition* pMatch = nullptr);
	bool processReplace(const wchar_t* txt2find, const wchar_t* txt2replace, const FindOption* options = nullptr);

	int markAll(const wchar_t* txt2find, int styleID);
	int markAllInc(FindOption* opt);

	int processAll(ProcessOperation op, FindOption* opt,
		bool isEntire = false, const FindersInfo *pFindersInfo = nullptr, int colourStyleID = -1, std::vector<MatchPosition>* pMatches = nullptr);
	int processRange(ProcessOperation op, FindReplaceInfo& findReplaceInfo, const FindersInfo* pFindersInfo,
		FindOption* opt = nullptr, int colourStyleID = -1, ScintillaEditView* view2Process = nullptr, std::vector<MatchPosition>* pMatches = nullptr);

	void replaceAllInOpenedDocs();
	void findAllIn(InWhat op);
	void setSearchText(const wchar_t * txt2find);

	void gotoNextFoundResult(int direction = 0) const {
		if (_pFinder) _pFinder->gotoNextFoundResult(direction);
	}

	void putFindResult(int result) {
		_findAllResult = result;
	}
	const wchar_t * getDir2Search() const { return _env->_directory.c_str(); }

	void getPatterns(std::vector<std::wstring> & patternVect);
	void getAndValidatePatterns(std::vector<std::wstring> & patternVect);

	void setFindInFilesDirFilter(const wchar_t *dir, const wchar_t *filters);
	void setProjectCheckmarks(FindHistory *findHistory, int msk);
	void execSavedCommand(int cmd, uptr_t intValue, const std::wstring& stringValue);

	const std::wstring& getText2search() const {
		return _env->_str2Search;
	}

	const std::wstring& getFilters() const { return _env->_filters; }
	const std::wstring& getDirectory() const { return _env->_directory; }
	const FindOption& getCurrentOptions() const { return *_env; }
	bool isRecursive() const { return _env->_isRecursive; }
	bool isInHiddenDir() const { return _env->_isInHiddenDir; }
	bool isProjectPanel_1() const { return _env->_isProjectPanel_1; }
	bool isProjectPanel_2() const { return _env->_isProjectPanel_2; }
	bool isProjectPanel_3() const { return _env->_isProjectPanel_3; }
	void saveFindHistory();

	void beginNewFilesSearch() {
		_pFinder->beginNewFilesSearch();
		_pFinder->addSearchLine(getText2search().c_str());
	}

	void finishFilesSearch(int count, int searchedCount, bool searchedEntireNotSelection) {
		_pFinder->finishFilesSearch(count, searchedCount, searchedEntireNotSelection, _env);
	}

	void focusOnFinder() {
		// Show finder and set focus
		if (_pFinder)
		{
			_pFinder->display();
			_pFinder->_scintView->setFocus();
		}
	}

	bool allowCopyAction() {
		// NPLX: upstream checks the focused HWND against the finders
		if (_pFinder && _pFinder->_scintView->hasFocus())
			return _pFinder->_scintView->hasSelection();
		return false;
	}

	ScintillaEditView* getHFindResults() {
		if (_pFinder)
			return _pFinder->_scintView;
		return nullptr;
	}

	void updateFinderScintilla() {
		if (_pFinder)
		{
			_pFinder->setFinderStyle();

			if (!_findersOfFinder.empty())
			{
				for (const auto& finder : _findersOfFinder)
				{
					finder->setFinderStyle();
				}
			}
		}
	}

	void updateFinderScintillaForNpc(bool onlyColor = false) {
		if (_pFinder)
		{
			_pFinder->setFinderStyleForNpc(onlyColor);

			if (!_findersOfFinder.empty())
			{
				for (const auto& finder : _findersOfFinder)
				{
					finder->setFinderStyleForNpc();
				}
			}
		}
	}

	Finder* getMainFinder() const {
		return _pFinder;
	}

	const std::vector<Finder*>& getFindersOfFinder() const {
		return _findersOfFinder;
	}

	void clearMarks(const FindOption& opt);
	void setStatusbarMessage(const std::wstring & msg, FindStatus status, const std::wstring& tooltipMsg = L"");
	void setStatusbarMessageWithRegExprErr(ScintillaEditView* pEditView);
	void setStatusMessageNotFound(const std::wstring& textNotFound, const std::wstring& tooltipMsg = L"");

	std::wstring getScopeInfoForStatusBar(FindOption const *pFindOpt) const;
	Finder * createFinder();
	bool removeFinder(Finder *finder2remove);
	DIALOG_TYPE getCurrentStatus() const { return _currentStatus; }
	int regexBackwardMsgBox();
	std::wstring setSearchText();
	std::wstring setSearchTextWithSettings();

	// NPLX: test/automation helpers (upstream drives the dialog through Win32 control messages)
	QComboBox* comboBox(int id) const;
	QWidget* dlgItem(int id) const { return ctrl(id); } // upstream ::GetDlgItem(_hSelf, id)
	void command(int id); // upstream WM_COMMAND, LOWORD(wParam) == id
	std::wstring statusbarMessage() const;

protected:
	void changeEvent(QEvent* event) override; // upstream WM_ACTIVATE
	void showEvent(QShowEvent* event) override;
	void reject() override; // Esc (upstream IDCANCEL)
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	DIALOG_TYPE _currentStatus = DIALOG_TYPE::FIND_DLG;

	ScintillaEditView** _ppEditView = nullptr;
	ParentMessenger _sendToParent;
	Finder  *_pFinder = nullptr;
	std::wstring _findResTitle;

	std::vector<Finder*> _findersOfFinder{};

	bool _isCreated = false;
	bool _isRTL = false;
	bool _shiftForOk = false; // NPLX: Shift+Enter already reversed the direction

	int _findAllResult = 0;
	wchar_t _findAllResultStr[1024] = {'\0'};

	QTabBar* _tab = nullptr;
	QLabel* _statusBar = nullptr;
	FindStatus _statusbarFindStatus = FSNoMessage;

	std::wstring _statusbarTooltipMsg;

	std::map<int, QWidget*> _ctrls; // NPLX: upstream ::GetDlgItem(_hSelf, id)

	QWidget* ctrl(int id) const;
	bool isCheckedOrNot(int checkControlID) const;
	void setChecked(int checkControlID, bool checkOrNot = true) const;

	void enableFindDlgItem(int dlgItemID, bool isEnable = true);
	void showFindDlgItem(int dlgItemID, bool isShow = true);

	void enableReplaceFunc(bool isEnable);
	void enableFindInFilesControls(bool isEnable, bool projectPanels);
	void enableFindInFilesFunc();
	void enableFindInProjectsFunc();
	void enableMarkAllControls(bool isEnable);
	void enableMarkFunc();
	void updateTitle();

	void gotoCorrectTab();

	FindStatus getFindStatus() const {
		return _statusbarFindStatus;
	}

	void updateCombos();
	void updateCombo(int comboID);
	void fillFindHistory();
	void fillComboHistory(int id, const std::vector<std::wstring> & strings);
	int saveComboHistory(int id, int maxcount, std::vector<std::wstring> & strings, bool saveEmpty);
	bool replaceInFilesConfirmCheck(const std::wstring& directory, const std::wstring& fileTypes);
	bool replaceInProjectsConfirmCheck();
	static const int FR_OP_FIND = 1;
	static const int FR_OP_REPLACE = 2;
	static const int FR_OP_FIF = 4;
	static const int FR_OP_GLOBAL = 8;
	static const int FR_OP_FIP = 16;
	void saveInMacro(size_t cmd, int cmdType);
	void enableProjectCheckmarks();
	bool replaceInOpenDocsConfirmCheck();

	bool isSearchUnicodeCharOnAnsi(const wchar_t* text) const;
	void updateInSelectionCheck(); // upstream WM_ACTIVATE (WA_ACTIVE)

	enum SwapButtonStatus {swap, down, up} _swapButtonStatus = swap;

	friend class FindIncrementDlg; // upstream: FindIncrementDlg reads _pFRDlg->_ppEditView
};

//FindIncrementDlg: incremental search dialog, docked in rebar
// NPLX: a QWidget bar (upstream IDD_INCREMENT_FIND: the same controls and IDs) in a toolbar row of the main window,
// at the bottom or the top (upstream: a band of the bottom or top rebar)
class FindIncrementDlg : public QWidget
{
	Q_OBJECT
public :
	explicit FindIncrementDlg(QWidget* parent = nullptr);

	void init(FindReplaceDlg* pFRDlg, bool isRTL = false);
	void display(bool toShow = true);
	bool isVisibleBar() const; // upstream isVisible: the band is shown

	void setSearchText(const wchar_t* txt2find, bool);

	void setFindStatus(FindStatus iStatus, size_t nbCounted, int nth);

	FindStatus getFindStatus() const { return _findStatus; }

	void reInitCount() {
		if (_matches.size() > 0)
		{
			_matches.clear();
			setFindStatus(FindStatus::FSResetCount, 0, 0);
		}
	}

	ScintillaEditView* getAssociatedScintillaView() const {
		if (_pFRDlg)
			return *(_pFRDlg->_ppEditView);
		else
			return nullptr;
	}

	// upstream WM_COMMAND (also the accelerators IDM_SEARCH_FINDINCREMENT, FINDNEXT, FINDPREV while the bar has the focus)
	void command(int id, bool isShiftDown = false);
	bool hasFocusWithin() const;
	QWidget* dlgItem(int id) const; // NPLX: tests
	void setHost(QWidget* host) { _host = host; } // the toolbar row that holds the bar

signals:
	void positionToggled(); // upstream NPPM_INTERNAL_INCSEARCHBARPOSITION

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private :
	void markSelectedTextInc(bool enable, FindOption* opt);
	bool isCheckedOrNot(int id) const;

	bool _isRTL = false;
	FindReplaceDlg *_pFRDlg = nullptr;
	FindStatus _findStatus = FSFound;
	QWidget* _host = nullptr;
	std::map<int, QWidget*> _ctrls;

	std::vector<MatchPosition> _matches;
};
