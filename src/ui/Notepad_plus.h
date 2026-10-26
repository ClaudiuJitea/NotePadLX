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

// NPLX: Qt port of upstream Notepad_plus (Notepad_plus.cpp, NppIO.cpp, NppCommands.cpp,
// NppNotification.cpp) and Notepad_plus_Window. Upstream member and function names are kept;
// Win32 window management is replaced by Qt widgets (QMainWindow, QSplitter, QTabBar, QMenuBar).

#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <QList>
#include <QMainWindow>
#include <QPointer>
#include <QRect>
#include <QStringList>

#include "Buffer.h"
#include "ContextMenuQt.h"
#include "NppBufferHost.h"
#include "LastRecentFileList.h"
#include "Notepad_plus_msgs.h"
#include "localization.h"
#include "ScintillaTypes.h"
#include "ScintillaStructures.h"
#include "shortcut.h"

class QAction;
class QDragEnterEvent;
class QDropEvent;

class QLabel;
class QMenu;
class QSplitter;
class QSystemTrayIcon;
class DocTabView;
class FindReplaceDlg;
class FindIncrementDlg;
class PreferenceDlg;
class GoToLineDlg;
class UserDefineDialog;
class DockingPanel;
class FunctionListPanel;
class VerticalFileSwitcher;
class AnsiCharPanel;
class DocumentMap;
class FileBrowser;
class ProjectPanel;
struct FileBrowserRootsInfo;
class ClipboardHistoryPanel;
struct TaskListInfo;
class AutoCompletion;
class WordStyleDlg;
class QtNppMenu;
class NppMenu;
class RunDlg;
class RunMacroDlg;
class HashFromFilesDlg;
class ColumnEditorDlg;
class FindCharsInRangeDlg;
class AboutDlg;
class DebugInfoDlg;
class CmdLineArgsDlg;
class UpdateChecker;
class HashFromTextDlg;
struct SCNotification;
class SmartHighlighter;
class MatchedFileNameProgress;
class ScintillaEditView;
struct SCNotification;

// Status bar parts: STATUSBAR_DOC_TYPE ... STATUSBAR_TYPING_MODE are defined in Notepad_plus_msgs.h (plugin API)
inline constexpr int STATUSBAR_NB_SECTIONS = STATUSBAR_TYPING_MODE + 1;

// upstream Notepad_plus.h: targets of enableCommand
#define MENU 0x01
#define TOOLBAR 0x02

enum trimOp {
	lineHeader = 0,
	lineTail = 1,
	lineBoth = 2,
	lineEol = 3
};

enum spaceTab {
	tab2Space = 0,
	space2TabLeading = 1,
	space2TabAll = 2
};

// Main menu definition entry, generated from upstream Notepad_plus.rc (see MainMenuDef.inc)
struct NppMenuDef
{
	enum Kind { PopupBegin, PopupEnd, Item, Separator };
	enum Flags { Grayed = 1, Checked = 2, RightAligned = 4 };
	Kind _kind;
	const wchar_t* _text;
	int _id;
	int _flags;
};

enum FileTransferMode {
	TransferClone		= 0x01,
	TransferMove		= 0x02
};

// upstream VisibleGUIConf: the state of the window before full screen, post-it or distraction free
struct VisibleGUIConf final
{
	bool _isPostIt = false;
	bool _isFullScreen = false;
	bool _isDistractionFree = false;

	//Used by postit & fullscreen
	bool _isMenuShown = true;
	bool _isToolbarShown = true; // NPLX: upstream hides the rebars

	//used by postit
	bool _isTabbarShown = true;
	bool _isAlwaysOnTop = false;
	bool _isStatusbarShown = true;

	//used by fullscreen (NPLX: was WINDOWPLACEMENT)
	bool _wasMaximized = false;
	QRect _normalGeometry;

	//used by distractionFree
	bool _was2ViewModeOn = false;
	std::vector<class QDockWidget*> _pVisibleDockingContainers;
};

class Notepad_plus : public QMainWindow, public NppBufferHost
{
	Q_OBJECT

public:
	Notepad_plus();
	~Notepad_plus() override;

	// Upstream Notepad_plus::init (edit views, doc tabs, menus, status bar)
	void init();
	void loadLocalizationsAndThemes(); // upstream Notepad_plus_Window::init
	std::vector<std::wstring> addNppComponents(const wchar_t *destDir, const wchar_t *extFilterName, const wchar_t *extFilter);

	// --- NppBufferHost (called by FileManager / Buffer)
	Buffer* getCurrentBuffer() override;
	bool viewVisible(int whichOne) override;
	int otherView() override { return (_activeView == MAIN_VIEW ? SUB_VIEW : MAIN_VIEW); }
	ScintillaEditView* getInactiveScintView() override { return _pNonEditView; }
	ScintillaEditView* getActiveEditView() override { return _pEditView; }
	void notifyBufferChanged(Buffer* buffer, int mask) override;
	void command(int id) override;
	void loadBufferIntoView(BufferID id, int whichOne) override { loadBufferIntoView(id, whichOne, false); }
	void saveCurrentSession() override;

	// --- views
	int currentView() const { return _activeView; }
	DocTabView* getDocTab(int whichOne) const { return whichOne == MAIN_VIEW ? _mainDocTab : _subDocTab; } // NPLX
	int otherFromView(int whichOne) const { return (whichOne == MAIN_VIEW ? SUB_VIEW : MAIN_VIEW); }
	bool bothActive() const { return viewVisibleConst(MAIN_VIEW) && viewVisibleConst(SUB_VIEW); }
	int switchEditViewTo(int gid);
	void showView(int whichOne);
	void hideView(int whichOne);
	bool canHideView(int whichOne);
	bool isEmpty();

	// --- documents (NppIO.cpp)
	BufferID doOpen(const std::wstring& fileName, bool isRecursive = false, bool isReadOnly = false, int encoding = -1, const wchar_t* backupFileName = nullptr, FILETIME fileNameTimestamp = {});
	bool doSave(BufferID bufferID, const wchar_t* filename, bool isSaveCopy = false);
	void doClose(BufferID bufferID, int whichOne, bool doDeleteBackup = false);
	bool doReload(BufferID id, bool alert = true);
	void fileNew();
	void fileOpen();
	void filePrint(bool showDialog); // File > Print... (with the dialog) and Print Now
	bool fileLoadSession(const wchar_t* fn = nullptr);
	const wchar_t* fileSaveSession(size_t nbFile, wchar_t** fileNames, const wchar_t* sessionFile2save, bool includeFileBrowser = false);
	const wchar_t* fileSaveSession(size_t nbFile = 0, wchar_t** fileNames = nullptr);
	bool fileReload();
	bool fileClose(BufferID id = BUFFER_INVALID, int curView = -1);
	bool fileCloseAll(bool doDeleteBackup, bool isSnapshotMode = false);
	bool fileSave(BufferID id = BUFFER_INVALID);
	bool fileSaveAll();
	bool fileSaveAs(BufferID id = BUFFER_INVALID, bool isSaveCopy = false);
	bool fileRename(BufferID id = BUFFER_INVALID);
	bool useFirstLineAsTabName(BufferID id);
	bool fileDelete(BufferID id = BUFFER_INVALID);
	void dropFiles(const QStringList& paths, int view = -1); // upstream dropFiles(HDROP)
	void monitoringStartOrStopAndUpdateUI(Buffer* pBuf, bool isStarting);
	void createMonitoringThread(Buffer* pBuf);

	// --- sessions (NppSession.cpp)
	void loadLastSession();
	// upstream loadCommandlineParams; NPLX: takes the file names instead of a quoted command line string
	std::vector<std::wstring> loadCommandlineParams(const std::vector<std::wstring>& fileNames, const CmdLineParamsDTO* pCmdParams);
	bool loadSession(Session& session, bool isSnapshotMode = false, const wchar_t* userCreatedSessionName = nullptr);
	void saveSession(const Session& session);
	void getCurrentOpenedFiles(Session& session, bool includeUntitledDoc = false);
	bool isFileSession(const wchar_t* filename);
	int getHtmlXmlEncoding(const wchar_t* fileName) const; // upstream: the encoding an XML or HTML header declares
	bool isFileWorkspace(const wchar_t* filename);
	std::wstring getLangFromMenu(const Buffer* buf);
	int getLangFromMenuName(const wchar_t* langName);

	void loadBufferIntoView(BufferID id, int whichOne, bool dontClose);
	bool removeBufferFromView(BufferID id, int whichOne);
	bool activateBuffer(BufferID id, int whichOne, bool forceApplyHilite = false);
	bool switchToFile(BufferID buffer);
	void notifyBufferActivated(BufferID bufid, int view);
	void docGotoAnotherEditView(FileTransferMode mode);	//TransferMode
	void docOpenInNewInstance(FileTransferMode mode, int x = 0, int y = 0);
	void tabDroppedOutside(const QPoint& globalPos, bool isInCtrlStat); // upstream TCN_TABDROPPEDOUTSIDE
	bool noOpenedDoc() const;
	void tabPinned(DocTabView* docTab, int index); // upstream TCN_TABPINNED: pins or unpins the tab
	void systemColorSchemeChanged(); // upstream WM_SETTINGCHANGE
	LangType menuID2LangType(int cmdID);
	void setLanguage(LangType langType);

	// --- UI state (Notepad_plus.cpp)
	void setTitle();
	void updateStatusBar();
	void setLangStatus(LangType langType);
	void setDisplayFormat(EolType format);
	void setUniModeText();
	void checkDocState();
	void checkModifiedDocument(bool bCheckOnlyCurrentBuffer);
	bool checkDocStatus(); // upstream NPPM_INTERNAL_CHECKDOCSTATUS
	void checkUnicodeMenuItems();
	void checkLangsMenu(int id);
	void checkMenuItem(int itemID, bool willBeChecked);
	bool isMenuItemChecked(int itemID) const; // NPLX: upstream ::GetMenuState(_mainMenuHandle, id, MF_BYCOMMAND) == MF_CHECKED
	void addUserDefineLangMenuItem(int cmdID, const wchar_t* name); // NPLX: upstream InsertMenu in the Language menu
	void renameUserDefineLangMenuItem(int cmdID, const wchar_t* name); // NPLX: upstream ModifyMenu
	void removeUserDefineLangMenuItem(int cmdID); // NPLX: upstream RemoveMenu
	QMenu* languageMenu() const;
	void enableCommand(int cmdID, bool doEnable);
	// upstream enableCommand(id, enable, MENU | TOOLBAR); NPLX: the menu and the toolbar share one QAction,
	// so a TOOLBAR-only change is ignored
	void enableCommand(int cmdID, bool doEnable, int which) { if (which & MENU) enableCommand(cmdID, doEnable); }
	void checkClipboard();
	void checkUndoState();
	std::wstring getLangDesc(LangType langType, bool shortDesc = false);

	// --- search (NppSearch.cpp; upstream Notepad_plus.cpp)
	// NPLX: upstream Notepad_plus::process() for the messages its children send (::SendMessage(_hParent, ...))
	intptr_t nppMessage(unsigned int message, uptr_t wParam, sptr_t lParam);
	// NPLX: the messages of the Preferences dialog (upstream NppBigSwitch cases), in NppPreferences.cpp
	bool preferenceMessage(unsigned int message, uptr_t wParam, sptr_t lParam, intptr_t& result);
	PreferenceDlg* getPreferenceDlg() const { return _preference; } // NPLX: tests
	FindReplaceDlg* getFindReplaceDlg() const { return _findReplaceDlg; } // NPLX
	FindIncrementDlg* getIncrementFindDlg() const { return _incrementFindDlg; } // NPLX: tests
	GoToLineDlg* getGoToLineDlg() const { return _goToLineDlg; } // NPLX
	UserDefineDialog* getUserDefineDlg() const { return _userDefineDlg; } // NPLX: upstream ScintillaEditView::getUserDefineDlg()

	// --- docking panels (NppPanels.cpp; upstream Notepad_plus.cpp launch* functions)
	static bool isCommandPorted(int id); // NPLX: the command is handled by command()
	void launchFunctionList();
	FunctionListPanel* getFunctionList() const { return _pFuncList; } // NPLX
	void launchDocumentListPanel();
	void launchAnsiCharPanel();
	void launchDocMap();
	void dockUserDlg();
	void undockUserDlg();
	void applyUserDefineDlgStatus(); // NPLX: the UDL part of upstream Notepad_plus::init (nppGUI._userDefineDlgStatus)
	class QDockWidget* udlDock() const { return _udlDock; } // NPLX
	void launchFileBrowser(const std::vector<std::wstring>& folders, const std::wstring& selectedItemPath, bool fromScratch = false, std::vector<FileBrowserRootsInfo>* pFileBrowserRoots = nullptr);
	bool saveFileBrowserParam();
	void launchProjectPanel(int cmdID, ProjectPanel** pProjPanel, int panelID);
	void checkProjectMenuItem();
	bool saveProjectPanelsParams();
	bool findInProjects();
	bool replaceInProjects();
	bool createFilelistForProjects(std::vector<std::wstring>& fileNames);
	ProjectPanel* getProjectPanel(int i) const { return i == 0 ? _pProjectPanel_1 : (i == 1 ? _pProjectPanel_2 : _pProjectPanel_3); } // NPLX
	FileBrowser* getFileBrowser() const { return _pFileBrowser; } // NPLX
	void drawTabbarColoursFromStylerArray();
	void drawDocumentMapColoursFromStylerArray();
	DocumentMap* getDocumentMap() const { return _pDocMap; } // NPLX
	void launchClipboardHistoryPanel();
	AnsiCharPanel* getAnsiCharPanel() const { return _pAnsiCharPanel; } // NPLX
	ClipboardHistoryPanel* getClipboardHistoryPanel() const { return _pClipboardHistoryPanel; } // NPLX
	VerticalFileSwitcher* getDocumentListPanel() const { return _pDocumentListPanel; } // NPLX
	void getTaskListInfo(TaskListInfo* tli);
	void taskListInfo(TaskListInfo* tli, bool forDocList); // upstream WM_GETTASKLISTINFO
	void activateNextDoc(bool direction);

	// --- macros and the Run menu (NppMacro.cpp; upstream NppCommands.cpp, Notepad_plus.cpp, NppBigSwitch.cpp)
	void macroPlayback(Macro macro, std::vector<Document>* pDocs4EndUAIn = nullptr);
	void playBackMacroStep(const recordedMacroStep& step, ScintillaEditView* pEditView); // upstream recordedMacroStep::PlayBack
	void recordMacroStep(const SCNotification* notification); // upstream SCN_MACRORECORD
	void recordMenuCommand(int id); // upstream: the recording switch at the end of command()
	void toggleMacroRecording();
	void checkMacroState();
	bool addCurrentMacro();
	void addUserCommand(const UserCommand& uc); // upstream RunDlg IDC_BUTTON_SAVE
	intptr_t macroStatus() const; // upstream NPPM_GETCURRENTMACROSTATUS
	void runMacroFromDlg(); // upstream WM_MACRODLGRUNMACRO
	bool isShortcutsXmlSafe(); // upstream shortcuts.xml security validation
	void validateShortcutsXml(); // upstream IDM_EXECUTE_VALIDATE_SHORTCUTSXML
	void runShortcutsXmlCommand(int id); // ID_MACRO / ID_USER_CMD
	bool findKeyConflicts(const KeyCombo& combo) const; // upstream NPPM_INTERNAL_FINDKEYCONFLICTS
	void saveShortcuts();
	void initDynamicMenus(); // upstream Notepad_plus::init: the Macro and Run menus
	void updateDynamicMenuShortcuts(); // upstream _accelerator.updateShortcuts() for the macros and Run commands

	// --- shortcuts (NppShortcuts.cpp; upstream Accelerator / ScintillaAccelerator in shortcut.cpp)
	void updateShortcuts();     // upstream Accelerator::updateShortcuts + updateFullMenu
	void updateScintillaKeys(); // upstream ScintillaAccelerator::updateKeys
	void updateCommandShortcuts(); // upstream: the names (from the menu) and the categories of the menu commands
	void setShortcutCategories(); // upstream CommandShortcut::setCategoryFromMenu for each menu command
	static bool isRemovedOnLinux(int id); // NPLX: a Windows-only command, not in the menus nor the Shortcut Mapper
	class ShortcutMapper* activeShortcutMapper() const { return _pShortcutMapper; } // NPLX: tests
	const Macro& currentMacro() const { return _macro; } // NPLX: tests
	bool isRecordingMacro() const { return _recordingMacro; } // NPLX: tests
	RunDlg* getRunDlg() const { return _runDlg; } // NPLX: tests
	RunMacroDlg* getRunMacroDlg() const { return _runMacroDlg; } // NPLX: tests
	ColumnEditorDlg* getColumnEditorDlg() const { return _colEditorDlg; } // NPLX: tests
	FindCharsInRangeDlg* getFindCharsInRangeDlg() const { return _findCharsInRangeDlg; } // NPLX: tests
	AboutDlg* getAboutDlg() const { return _aboutDlg; } // NPLX: tests
	DebugInfoDlg* getDebugInfoDlg() const { return _debugInfoDlg; } // NPLX: tests
	CmdLineArgsDlg* getCmdLineArgsDlg() const { return _cmdLineArgsDlg; } // NPLX: tests
	void checkForUpdates(); // NPLX: ? > Check for Updates... (replaces the WinGUp updater)

	// --- full screen, post-it, distraction free (NppSpecialView.cpp; upstream Notepad_plus.cpp)
	void fullScreenToggle();
	void postItToggle();
	void distractionFreeToggle();
	const VisibleGUIConf& specialView() const { return _beforeSpecialView; } // NPLX: tests
	class QToolButton* restoreButton() const { return _restoreButton; } // NPLX: tests
	QWidget* getHashDlg(int cmdID) const; // NPLX: tests; the hash dialog of a Tools > MD5/SHA command

	void activateDoc(size_t pos);
	void windowsDlgNotify(struct NMWINDLG& nmdlg); // upstream WDN_NOTIFY
	void initWindowsMenu(class QMenu* hMenu);      // upstream WindowsMenu::initPopupMenu (WM_INITMENUPOPUP)
	bool exportFunctionList(); // upstream NPPM_INTERNAL_EXPORTFUNCLISTANDQUIT (without the quit)
	void saveDockingParams();  // NPLX: the docking part of upstream saveGUIParams (+ the Qt dock layout)
	void loadDockingParams();  // NPLX: upstream Notepad_plus::init (_internalFuncIDs) + the Qt dock layout
	void saveUserDefineLangs();
	bool findInCurrentFile(bool isEntireDoc);
	bool findInOpenedFiles();
	bool findInFiles();
	bool findInFilelist(std::vector<SearchCandidate>& fileNames);
	bool replaceInOpenedFiles();
	bool replaceInFiles();
	bool replaceInFilelist(std::vector<std::wstring>& fileNames);
	bool createFilelistForFiles(std::vector<SearchCandidate>& fileNames);
	void getMatchedFileNames(const wchar_t* dir, size_t level, const std::vector<std::wstring>& patterns, std::vector<std::wstring>& fileNames, bool isRecursive, bool isInHiddenDir, MatchedFileNameProgress* progress = nullptr);
	void getMatchedFileNames(const wchar_t* dir, size_t level, const std::vector<std::wstring>& patterns, std::vector<SearchCandidate>& fileNames, bool isRecursive, bool isInHiddenDir, MatchedFileNameProgress* progress = nullptr);
	void setCodePageForInvisibleView(Buffer const* pBuffer);
	void setCodePageForInvisibleView(UniMode unicodeMode);
	void setFindReplaceFolderFilter(const wchar_t* dir, const wchar_t* filter);
	bool goToPreviousIndicator(int indicID2Search, bool isWrap = true) const;
	bool goToNextIndicator(int indicID2Search, bool isWrap = true) const;
	void findMatchingBracePos(intptr_t& braceAtCaret, intptr_t& braceOpposite);
	bool braceMatch();

	// --- editing helpers (NppEdit.cpp; upstream Notepad_plus.cpp)
	enum comment_mode { cm_comment, cm_uncomment, cm_toggle };
	bool doBlockComment(comment_mode currCommentMode);
	bool doStreamComment();
	bool undoStreamComment(bool tryBlockComment = true);
	bool isConditionExprLine(intptr_t lineNumber);
	size_t getSelectedCharNumber(UniMode u);
	size_t getCurrentDocCharCount(UniMode u);
	size_t getSelectedAreas();
	size_t getSelectedBytes();
	int wordCount();
	void checkSyncState();
	void doSynScroll(ScintillaEditView* whichView);
	void showAutoComp();
	void showPathCompletion();
	void autoCompFromCurrentFile(bool autoInsert = true);
	void showFunctionComp();
	void showFunctionNextHint(bool isNext = true);
	intptr_t findMachedBracePos(size_t startPos, size_t endPos, char targetSymbol, char matchedSymbol);
	void maintainIndentation(wchar_t ch);
	void addHotSpot(ScintillaEditView* view = nullptr);
	void drawAutocompleteColoursFromTheme(COLORREF fgColor, COLORREF bgColor);
	void updateScintillas(bool doChangePanel); // upstream WM_UPDATESCINTILLAS
	WordStyleDlg* getConfigStyleDlg() const { return _configStyleDlg; } // NPLX
	void wsTabConvert(spaceTab whichWay);
	void doTrim(trimOp whichPart);
	void eol2ws();
	void removeEmptyLine(bool isBlankContained);
	void removeDuplicateLines();
	void copyMarkedLines();
	void cutMarkedLines();
	void deleteMarkedLines(bool isMarked);
	void pasteToMarkedLines();
	void deleteMarkedline(size_t ln);
	void inverseMarks();
	void replaceMarkedline(size_t ln, const wchar_t* str);
	std::wstring getMarkedLine(size_t ln);
	void clearChangesHistory(int iView = -1);
	void changedHistoryGoTo(int idGoTo);
	void changeReadOnlyUserModeForAllOpenedTabs(const bool ro);
	bool fileCloseAllGiven(const std::vector<BufferViewInfo>& fileInfos);
	bool fileCloseAllToLeft();
	bool fileCloseAllToRight();
	void fileCloseAllButPinned();
	bool fileCloseAllUnchanged();
	bool fileCloseAllButCurrent();

	// Bookmarks (upstream Notepad_plus.h)
	void bookmarkAdd(intptr_t lineno) const;
	void bookmarkDelete(size_t lineno) const;
	bool bookmarkPresent(intptr_t lineno) const;
	void bookmarkToggle(intptr_t lineno) const;
	void bookmarkNext(bool forwardScan);
	void bookmarkClearAll() const;

	// Context menus (upstream WM_CONTEXTMENU in NppBigSwitch.cpp, NM_RCLICK in NppNotification.cpp)
	void showEditorContextMenu(ScintillaEditView* view, const QPoint& globalPos);
	void showTabContextMenu(int whichOne, const QPoint& globalPos);
	const ContextMenu& getTabPopupMenu() const { return _tabPopupMenu; } // NPLX: tests

	// upstream trayIconControler (Preferences > MISC.: "Minimize / Close to the system tray")
	void minimizeToTray();
	bool reloadLang(); // upstream: the menus and the dialogs in the language of the new nativeLang.xml
	void restoreFromTray(); // upstream NPPM_INTERNAL_MINIMIZED_TRAY WM_LBUTTONUP
	bool isInTray() const;
	QSystemTrayIcon* getTrayIcon() const { return _pTrayIco; } // NPLX: tests

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
	void closeEvent(QCloseEvent* event) override;
	void changeEvent(QEvent* event) override; // upstream WM_SYSCOMMAND SC_MINIMIZE
	void resizeEvent(QResizeEvent* event) override; // NPLX: keeps the restore button in the top-right corner
	void dragEnterEvent(QDragEnterEvent* event) override;
	void dropEvent(QDropEvent* event) override;

private slots:
	void onScintillaNotify(Scintilla::NotificationData* scn);

private:
	bool viewVisibleConst(int whichOne) const;
	void buildMainMenu();
	void changeMenuLang(); // upstream NativeLangSpeaker::changeMenuLang(_mainMenuHandle)
	void updateTrayIconMenu();
	void buildToolBar(); // upstream toolBarIcons[] (Notepad_plus.cpp)
	// upstream ToolBar::reduce / enlarge / reduceToSet2 / enlargeToSet2 / setToBmpIcons and the Fluent colours:
	// the icon size and style of nppGUI._tbIconInfo
	void applyToolbarIconSet();
	void launchDocumentBackupTask(); // upstream: starts the backupDocument thread
	void placeIncrementalSearchBar(); // upstream addToRebar of the incremental search bar
	QString fileDialogDir() const; // Preferences > Default Directory
	void rememberFileDialogDir(const QString& filePath);
	void refreshDarkMode(bool resetStyle);
	void removeAllHotSpot();
	void hideExcludedLanguages(); // NPLX: upstream removes the excluded languages from the menu at init
	void setupColorSampleBitmapsOnMainMenuItems(); // the Mark style and tab colour samples of the menu items
	void showLanguageMenuItem(int cmdID, bool show);
	void buildLanguageMenu(QMenu* langMenu);
	void setupEditView(ScintillaEditView* view);
	void syncZoom();
	int doSaveOrNot(const wchar_t* fn, bool isMulti = false);
	int doReloadOrNot(const wchar_t* fn, bool dirty);
	int doCloseOrNot(const wchar_t* fn);
	int doDeleteOrNot(const wchar_t* fn);
	void setLangFromMenu(int id);
	QAction* actionFor(int id) const;
	QMenu* menuOfCommand(int cmdID) const;      // NPLX: the QMenu holding the command
	NppMenu* nppMenuOfCommand(int cmdID) const; // NPLX: upstream ::GetSubMenu(_mainMenuHandle, MENUINDEX_*)

	NativeLangSpeaker& _nativeLangSpeaker;

	ScintillaEditView* _mainEditView = nullptr;
	ScintillaEditView* _subEditView = nullptr;
	ScintillaEditView* _fileEditView = nullptr; // hidden, used by FileManager (upstream _fileEditView)
	ScintillaEditView* _invisibleEditView = nullptr; // hidden, used to read markers of any document
	ScintillaEditView* _pEditView = nullptr;
	ScintillaEditView* _pNonEditView = nullptr;

	DocTabView* _mainDocTab = nullptr;
	DocTabView* _subDocTab = nullptr;
	DocTabView* _pDocTab = nullptr;
	DocTabView* _pNonDocTab = nullptr;

	QSplitter* _splitter = nullptr;
	QWidget* _mainContainer = nullptr;
	QWidget* _subContainer = nullptr;

	std::unique_ptr<QtNppMenu> _mainMenuHandle; // NppMenu view of the menu bar (upstream _mainMenuHandle)
	ContextMenu _tabPopupMenu;
	ScintillaEditView* _pendingContextMenuView = nullptr; // a right press waits for the release (eventFilter)
	QPoint _pendingContextMenuPos;
	FindReplaceDlg* _findReplaceDlg = nullptr;
	class FindIncrementDlg* _incrementFindDlg = nullptr; // upstream _incrementFindDlg
	class QToolBar* _incrementFindBar = nullptr;           // NPLX: the row of the incremental search bar
	PreferenceDlg* _preference = nullptr; // created on first use (upstream: created by IDM_SETTING_PREFERENCE)
	bool _altAloneDown = false;           // Alt pressed alone, to show a hidden menu bar
	RunDlg* _runDlg = nullptr;
	RunMacroDlg* _runMacroDlg = nullptr;
	class ShortcutMapper* _pShortcutMapper = nullptr; // upstream: the open Shortcut Mapper (NPPM_INTERNAL_FINDKEYCONFLICTS)
	// upstream _md5FromFilesDlg, _md5FromTextDlg, _sha1From..., _sha2From..., _sha512From... (created on first use)
	ColumnEditorDlg* _colEditorDlg = nullptr; // upstream _colEditorDlg (created on first use)
	FindCharsInRangeDlg* _findCharsInRangeDlg = nullptr; // upstream _findCharsInRangeDlg (created on first use)
	AboutDlg* _aboutDlg = nullptr;
	DebugInfoDlg* _debugInfoDlg = nullptr;
	CmdLineArgsDlg* _cmdLineArgsDlg = nullptr;
	UpdateChecker* _updateChecker = nullptr;
	HashFromFilesDlg* _md5FromFilesDlg = nullptr;
	HashFromTextDlg* _md5FromTextDlg = nullptr;
	HashFromFilesDlg* _sha1FromFilesDlg = nullptr;
	HashFromTextDlg* _sha1FromTextDlg = nullptr;
	HashFromFilesDlg* _sha2FromFilesDlg = nullptr;
	HashFromTextDlg* _sha2FromTextDlg = nullptr;
	HashFromFilesDlg* _sha512FromFilesDlg = nullptr;
	HashFromTextDlg* _sha512FromTextDlg = nullptr;
	Macro _macro;
	bool _recordingMacro = false;
	bool _playingBackMacro = false;
	bool _recordingSaved = false;
	GoToLineDlg* _goToLineDlg = nullptr;
	UserDefineDialog* _userDefineDlg = nullptr; // upstream static ScintillaEditView::_userDefineDlg
	FunctionListPanel* _pFuncList = nullptr;
	VerticalFileSwitcher* _pDocumentListPanel = nullptr;
	AnsiCharPanel* _pAnsiCharPanel = nullptr;
	DocumentMap* _pDocMap = nullptr;
	class QDockWidget* _udlDock = nullptr; // NPLX: the docked UDL dialog (upstream _pMainSplitter, SplitterMode::RIGHT_FIX)
	bool _isUDDocked = false;
	FileBrowser* _pFileBrowser = nullptr;
	ProjectPanel* _pProjectPanel_1 = nullptr;
	ProjectPanel* _pProjectPanel_2 = nullptr;
	ProjectPanel* _pProjectPanel_3 = nullptr;
	ClipboardHistoryPanel* _pClipboardHistoryPanel = nullptr;
	std::vector<DockingPanel*> _panels; // NPLX: the launched panels (upstream: _dockingManager's dialogs)
	void registerPanel(DockingPanel* panel, Qt::DockWidgetArea defaultArea); // upstream NPPM_DMMREGASDCKDLG
	WordStyleDlg* _configStyleDlg = nullptr;
	std::unique_ptr<AutoCompletion> _autoCompleteMain;
	std::unique_ptr<AutoCompletion> _autoCompleteSub;
	SmartHighlighter* _smartHighlighter = nullptr;
	class QToolBar* _toolBar = nullptr;
	VisibleGUIConf _beforeSpecialView;
	class QToolButton* _restoreButton = nullptr; // upstream ButtonDlg _restoreButton
	void placeRestoreButton();
	QLabel* _statusBarParts[STATUSBAR_NB_SECTIONS] = {};

	int _activeView = MAIN_VIEW;
	int _multiSelectFlag = 0;
	bool _linkTriggered = true;

	struct SyncInfo final
	{
		intptr_t _line = 0;
		intptr_t _column = 0;
		bool _isSynScrollV = false;
		bool _isSynScrollH = false;

		bool doSync() const {return (_isSynScrollV || _isSynScrollH); }
	}
	_syncInfo;
	std::map<Buffer*, class QFileSystemWatcher*> _monitors; // NPLX: "tail -f" watchers (upstream: one thread per buffer)
	bool _isFolding = false;
	bool _isAttemptingCloseOnQuit = false;
	QSystemTrayIcon* _pTrayIco = nullptr;
	QList<QPointer<QWidget>> _hiddenInTray; // the floating panels and dialogs hidden with the window (upstream minimizeDialogs)
	void showFromTray(); // upstream: ShowWindow, showFloatingContainers(true), restoreMinimizeDialogs()
	bool _isNppSessionSavedAtExit = false;
	class QTimer* _backupTimer = nullptr; // snapshot mode (upstream: backupDocument thread)

	LastRecentFileList _lastRecentFileList;
	std::multimap<int, QAction*> _actions; // IDM_* -> menu actions (an id can appear in several menus)
	std::map<int, QAction*> _hiddenShortcutActions; // NPLX: the shortcuts of the commands without a menu item
	std::map<int, QAction*> _langActions;
};
