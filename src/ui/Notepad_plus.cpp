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

#include "Notepad_plus.h"
#include "VerticalFileSwitcher.h"
#include "TaskListInfo.h"

#include <cassert>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <set>
#include <stack>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

#include "DocTabView.h"
#include "EncodingMapper.h"
#include "FindReplaceDlg.h"
#include "AnsiCharPanel.h"
#include "DocumentMap.h"
#include "FunctionListPanel.h"
#include "GoToLineDlg.h"
#include "NplxStyle.h"
#include "UserDefineDialog.h"
#include "WordStyleDlg.h"
#include "AutoCompletion.h"
#include "QtNppMenu.h"
#include "RunDlg.h"
#include "RunMacroDlg.h"
#include "SmartHighlighter.h"
#include "xmlMatchedTagsHighlighter.h"
#include "KeyMap.h"
#include "Parameters.h"
#include "ParametersCompat.h"
#include "ScintillaEditView.h"
#include "menuCmdID.h"
#include "resource.h"

using std::wstring;

namespace
{
	std::mutex command_mutex;

	// Generated from upstream Notepad_plus.rc (tools/gen_mainmenu.py)
	const NppMenuDef g_mainMenuDef[] = {
#include "MainMenuDef.inc"
	};

	// NPLX: the Windows-only commands that NotePadLX removes from the menu (no Linux equivalent, see PORTING_STATUS.md
	// "Not ported"). Upstream also deletes the updater items at init when there is no updater.
	bool isRemovedOnLinuxImpl(int id)
	{
		switch (id)
		{
			case IDM_FILE_OPEN_POWERSHELL: // "Open Containing Folder > PowerShell": the Terminal item covers it
			case IDM_UPDATE_NPP:           // WinGUp auto-updater: updates come from the package manager
			case IDM_CONFUPDATERPROXY:
			case IDM_ONLINEDOCUMENT:       // the Notepad++ user manual and forum: NotePadLX links to its own repository
			case IDM_FORUM:
				return true;
			default:
				return false;
		}
	}

	// NPLX: Linux and NotePadLX names for menu entries whose upstream text names Windows or Notepad++
	// (a translation from nativeLang.xml still wins)
	const wchar_t* linuxMenuText(int id)
	{
		switch (id)
		{
			case IDM_FILE_OPEN_FOLDER:                     return L"File Manager";           // was "Explorer"
			case IDM_FILE_OPEN_CMD:                        return L"Terminal";               // was "cmd"
			case IDM_FILE_DELETE:                          return L"Move to &Trash";         // was "Move to Recycle Bin"
			case IDM_EDIT_OPENSELECTEDFILEFOLDERINEXPLORER: return L"Open Containing Folder in File Manager";
			case IDM_EDIT_TOGGLESYSTEMREADONLY:            return L"Read-Only Attribute in File System";
			case IDM_HOMESWEETHOME:                        return L"NotePadLX on GitHub";
			case IDM_PROJECTPAGE:                          return L"Report an Issue";
			case IDM_ABOUT:                                return L"About NotePadLX";
			default:                                       return nullptr;
		}
	}

	const wchar_t* linuxPopupText(const wchar_t* text)
	{
		return std::wcscmp(text, L"Read-&Only in Notepad++") == 0 ? L"Read-&Only in NotePadLX" : text;
	}

}

// Commands handled by Notepad_plus::command. Every other IDM_* is shown disabled ("not yet ported").
bool Notepad_plus::isCommandPorted(int id)
{
	if (id >= IDM_LANG && id < IDM_LANG + 400)
		return true;
	switch (id)
	{
#include "PortedCommands.inc"
			return true;
		default:
			return false;
	}
}

// ============================================================================
// Construction / init (upstream Notepad_plus::init)
// ============================================================================

Notepad_plus::Notepad_plus()
	: _nativeLangSpeaker(*NppParameters::getInstance().getNativeLangSpeaker())
{
	setWindowIcon(QIcon(QStringLiteral(":/icons/notepadlx-256.png")));
	setAcceptDrops(true);
}

Notepad_plus::~Notepad_plus()
{
	delete _smartHighlighter;
}

void Notepad_plus::setupEditView(ScintillaEditView* view)
{
	const ScintillaViewParams& svp = NppParameters::getInstance().getSVP();

	view->showMargin(ScintillaEditView::_SC_MARGE_LINENUMBER, svp._lineNumberMarginShow);
	view->showMargin(ScintillaEditView::_SC_MARGE_SYMBOL, svp._bookMarkMarginShow);
	view->showChangeHistoryMargin(svp._isChangeHistoryMarginEnabled); // upstream: NppBigSwitch (otherwise markers draw as line backgrounds)
	view->showIndentGuideLine(svp._indentGuideLineShow);
	view->setMakerStyle(svp._folderStyle);
	view->defineDocType(view->getCurrentBuffer()->getLangType());
	view->setWrapMode(svp._lineWrapMethod);
	view->execute(SCI_SETENDATLASTLINE, !svp._scrollBeyondLastLine);
	view->setBorderEdge(svp._showBorderEdge);
	view->execute(SCI_SETCARETLINEVISIBLEALWAYS, true);
	view->wrap(svp._doWrap);
	view->showEOL(svp._eolShow);
	view->showWSAndTab(svp._whiteSpaceShow);
	view->showWrapSymbol(svp._wrapSymbolShow);
	view->performGlobalStyles();

	view->execute(SCI_SETMULTIPLESELECTION, svp._multiSelection); // upstream NPPM_INTERNAL_SETMULTISELECTION
	view->execute(SCI_SETADDITIONALSELECTIONTYPING, true);
	int virtualSpaceOptions = SCVS_RECTANGULARSELECTION;
	if (svp._virtualSpace)
		virtualSpaceOptions |= SCVS_USERACCESSIBLE | SCVS_NOWRAPLINESTART;
	view->execute(SCI_SETVIRTUALSPACEOPTIONS, virtualSpaceOptions);
	view->execute(SCI_SETMULTIPASTE, SC_MULTIPASTE_EACH);
	view->execute(SCI_AUTOCSETMULTI, SC_MULTIAUTOC_EACH);
	view->execute(SCI_SETMOUSESELECTIONRECTANGULARSWITCH, true);
	view->execute(SCI_SETAUTOMATICFOLD, SC_AUTOMATICFOLD_SHOW | SC_AUTOMATICFOLD_CHANGE);
	view->execute(SCI_SETMARGINLEFT, 0, svp._paddingLeft);
	view->execute(SCI_SETMARGINRIGHT, 0, svp._paddingRight);
	view->execute(SCI_STYLESETCHECKMONOSPACED, STYLE_DEFAULT, true);
	view->execute(SCI_SETUNDOSELECTIONHISTORY, SC_UNDO_SELECTION_HISTORY_ENABLED | SC_UNDO_SELECTION_HISTORY_SCROLL);

	connect(view, &ScintillaEditBase::notify, this, &Notepad_plus::onScintillaNotify);
	view->viewport()->installEventFilter(this); // context menu (upstream WM_CONTEXTMENU)
	// Files dropped on the text (upstream WM_DROPFILES); Scintilla's Qt widget reports them as URIs
	connect(view, &ScintillaEditBase::uriDropped, this, [this, view](const QString& uri) {
		const QUrl url(uri);
		if (url.isLocalFile())
			dropFiles({ url.toLocalFile() }, view == _mainEditView ? MAIN_VIEW : SUB_VIEW);
	});
	connect(view, &ScintillaEditBase::focusChanged, this, [this, view](bool focused) {
		if (focused && view != _pEditView)
			switchEditViewTo(view == _mainEditView ? MAIN_VIEW : SUB_VIEW);
	});
}

// upstream Notepad_plus_Window::init: the localization files and the themes (install dir + user dir)
void Notepad_plus::loadLocalizationsAndThemes()
{
	NppParameters& nppParams = NppParameters::getInstance();

	std::vector<std::wstring> fileNames;
	std::vector<std::wstring> patterns;
	patterns.push_back(L"*.xml");

	std::wstring nppDir = nppParams.getNppPath();

	LocalizationSwitcher & localizationSwitcher = nppParams.getLocalizationSwitcher();
	std::wstring localizationDir = nppDir;
	pathAppend(localizationDir, L"localization/"); // NPLX: '/' (upstream '\\')

	getMatchedFileNames(localizationDir.c_str(), 0, patterns, fileNames, false, false);
	for (const auto& fileName : fileNames)
		localizationSwitcher.addLanguageFromXml(fileName);

	fileNames.clear();
	ThemeSwitcher & themeSwitcher = nppParams.getThemeSwitcher();

	//  Get themes from both npp install themes dir and user data themes dir (AppData, settingsDir, or cloud) with the per user
	//  overriding default themes of the same name.

	std::wstring userDataThemeDir = nppParams.getUserPath(); // getUserPath will always pick the right settingsDir/Cloud/AppData location
	if (!userDataThemeDir.empty() && userDataThemeDir != nppDir)
	{
		// append files from userDataThemeDir, unless it matches nppDir (which means the themes are already in the internal structure)
		pathAppend(userDataThemeDir, L"themes/");
		getMatchedFileNames(userDataThemeDir.c_str(), 0, patterns, fileNames, false, false);
		for (const auto& fileName: fileNames)
		{
			themeSwitcher.addThemeFromXml(fileName);
		}
	}

	fileNames.clear();

	std::wstring nppThemeDir = nppDir; // <- should use the pointer to avoid the constructor of copy
	pathAppend(nppThemeDir, L"themes/");

	// Set theme directory to their installation directory
	themeSwitcher.setThemeDirPath(nppThemeDir);

	getMatchedFileNames(nppThemeDir.c_str(), 0, patterns, fileNames, false, false);
	for (const auto& fileName : fileNames)
	{
		std::wstring themeName( themeSwitcher.getThemeFromXmlFileName(fileName.c_str()) );
		if (!themeSwitcher.themeNameExists(themeName.c_str()))
		{
			themeSwitcher.addThemeFromXml(fileName);

			if (!userDataThemeDir.empty() && userDataThemeDir != nppDir)
			{
				std::wstring userDataThemePath = userDataThemeDir;

				if (!doesDirectoryExist(userDataThemePath.c_str()))
				{
					nplx::createDirectories(userDataThemePath); // NPLX: was ::CreateDirectory
				}
				const wchar_t* fn = PathFindFileName(fileName.c_str());
				pathAppend(userDataThemePath, fn);
				themeSwitcher.addThemeStylerSavePath(fileName, userDataThemePath);
			}
		}
	}
	// NPLX_TODO(Phase 8): follow the desktop dark mode (NppDarkMode::isWindowsModeEnabled) with its theme
}

void Notepad_plus::init()
{
	NppParameters& nppParam = NppParameters::getInstance();
	loadLocalizationsAndThemes();
	const ScintillaViewParams& svp = nppParam.getSVP();

	// Upstream order: the file edit view and the FileManager first, then the edit views
	_fileEditView = new ScintillaEditView(false, this);
	_fileEditView->hide();
	_fileEditView->init();
	_fileEditView->execute(SCI_SETMODEVENTMASK, MODEVENTMASK_OFF); // Turn off the modification event
	MainFileManager.init(this, _fileEditView); //get it up and running asap.

	_invisibleEditView = new ScintillaEditView(false, this);
	_invisibleEditView->hide();
	_invisibleEditView->init();
	_invisibleEditView->execute(SCI_SETUNDOCOLLECTION);
	_invisibleEditView->execute(SCI_EMPTYUNDOBUFFER);
	_invisibleEditView->execute(SCI_SETMODEVENTMASK, MODEVENTMASK_OFF); // Turn off the modification event
	_invisibleEditView->wrap(false); // Make sure no slow down

	_mainEditView = new ScintillaEditView(true, this);
	_subEditView = new ScintillaEditView(true, this);
	_pEditView = _mainEditView;
	_pNonEditView = _subEditView;
	_mainEditView->init();
	_subEditView->init();

	_mainDocTab = new DocTabView(_mainEditView, this);
	_subDocTab = new DocTabView(_subEditView, this);
	_pDocTab = _mainDocTab;
	_pNonDocTab = _subDocTab;

	auto makeContainer = [this](DocTabView* tab, ScintillaEditView* view) {
		QWidget* w = new QWidget(this);
		auto* layout = new QVBoxLayout(w);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setSpacing(0);
		layout->addWidget(tab);
		layout->addWidget(view, 1);
		return w;
	};
	_mainContainer = makeContainer(_mainDocTab, _mainEditView);
	_subContainer = makeContainer(_subDocTab, _subEditView);

	_splitter = new QSplitter(Qt::Horizontal, this);
	_splitter->addWidget(_mainContainer);
	_splitter->addWidget(_subContainer);
	_splitter->setChildrenCollapsible(false);
	_subContainer->hide();
	setCentralWidget(_splitter);

	// Each view starts with its default ("new 1") document, shown in its tab bar
	_mainDocTab->addBuffer(_mainEditView->getCurrentBufferID());
	_subDocTab->addBuffer(_subEditView->getCurrentBufferID());

	for (ScintillaEditView* view : { _mainEditView, _subEditView })
		setupEditView(view);
	_mainEditView->execute(SCI_SETZOOM, svp._zoom);
	_subEditView->execute(SCI_SETZOOM, svp._zoomSync ? svp._zoom : svp._zoom2);

	for (DocTabView* tab : { _mainDocTab, _subDocTab })
	{
		const int view = (tab == _mainDocTab) ? MAIN_VIEW : SUB_VIEW;
		connect(tab, &DocTabView::bufferSelected, this, [this, view](BufferID id) {
			switchEditViewTo(view);
			activateBuffer(id, view);
		});
		connect(tab, &DocTabView::bufferCloseRequested, this, [this, view](BufferID id) {
			fileClose(id, view);
		});
		tab->setContextMenuPolicy(Qt::CustomContextMenu);
		connect(tab, &QWidget::customContextMenuRequested, this, [this, tab, view](const QPoint& pos) {
			// upstream: a right click activates the tab, then NM_RCLICK shows the tab context menu
			const int index = tab->tabAt(pos);
			if (index < 0)
				return;
			switchEditViewTo(view);
			activateBuffer(tab->getBufferByIndex(static_cast<size_t>(index)), view);
			showTabContextMenu(view, tab->mapToGlobal(pos));
		});
		connect(tab, &QTabBar::tabMoved, this, [this]() {
			// upstream TabBarPlus drag and drop: NPPM_INTERNAL_DOCORDERCHANGED
			if (_pDocumentListPanel)
				_pDocumentListPanel->updateTabOrder();
		});
	}

	// upstream Notepad_plus::init: _findReplaceDlg.init(_pPublicInterface->getHinst(), hwnd, &_pEditView)
	_findReplaceDlg = new FindReplaceDlg();
	_findReplaceDlg->init(this, &_pEditView, [this](unsigned int message, uptr_t wParam, sptr_t lParam) {
		return nppMessage(message, wParam, lParam);
	});
	// upstream Notepad_plus::init: _runMacroDlg.init(...), _runDlg.init(...)
	const ParentMessenger sendToNpp = [this](unsigned int message, uptr_t wParam, sptr_t lParam) {
		return nppMessage(message, wParam, lParam);
	};
	_runMacroDlg = new RunMacroDlg(this);
	_runMacroDlg->init(sendToNpp);
	_runDlg = new RunDlg(this);
	_runDlg->init(sendToNpp, [this](const UserCommand& uc) { addUserCommand(uc); },
		[this](const KeyCombo& kc) { return findKeyConflicts(kc); });
	_smartHighlighter = new SmartHighlighter(_findReplaceDlg);
	// upstream constructor: _autoCompleteMain(&_mainEditView), _autoCompleteSub(&_subEditView)
	_autoCompleteMain = std::make_unique<AutoCompletion>(_mainEditView);
	_autoCompleteSub = std::make_unique<AutoCompletion>(_subEditView);
	_goToLineDlg = new GoToLineDlg(this); // upstream _goToLineDlg.init(_pPublicInterface->getHinst(), hwnd, &_pEditView)
	_goToLineDlg->init(&_pEditView);
	_goToLineDlg->setLinkTriggered([this]() {
		// upstream SCN_PAINTED with wParam == LINKTRIGGERED: the next paint re-detects the links
		_linkTriggered = true;
		_pEditView->viewport()->update();
	});
	// upstream ScintillaEditView::_userDefineDlg.init(_hInst, _hParent, this) (the first view created)
	_userDefineDlg = new UserDefineDialog(this);
	UserDefineDialog::MenuHandler udlMenu;
	udlMenu._insert = [this](int cmdID, const wchar_t* name) { addUserDefineLangMenuItem(cmdID, name); };
	udlMenu._rename = [this](int cmdID, const wchar_t* name) { renameUserDefineLangMenuItem(cmdID, name); };
	udlMenu._remove = [this](int cmdID) { removeUserDefineLangMenuItem(cmdID); };
	udlMenu._renamed = [this](const wchar_t* newName, const wchar_t* oldName) {
		nppMessage(WM_RENAME_USERLANG, reinterpret_cast<uptr_t>(newName), reinterpret_cast<sptr_t>(oldName));
	};
	udlMenu._removed = [this](const wchar_t* name) { nppMessage(WM_REMOVE_USERLANG, 0, reinterpret_cast<sptr_t>(name)); };
	udlMenu._closed = [this]() {
		// upstream WM_CLOSE_USERDEFINE_DLG (when docked, the main splitter is hidden too)
		checkMenuItem(IDM_LANG_USER_DLG, false);
		if (_udlDock && _userDefineDlg->isDocked())
			_udlDock->hide();
	};
	udlMenu._dock = [this]() { dockUserDlg(); };     // upstream WM_DOCK_USERDEFINE_DLG
	udlMenu._undock = [this]() { undockUserDlg(); }; // upstream WM_UNDOCK_USERDEFINE_DLG
	_userDefineDlg->init(_mainEditView, std::move(udlMenu));
	_configStyleDlg = new WordStyleDlg(this); // upstream _configStyleDlg.init(_pPublicInterface->getHinst(), hwnd)
	_configStyleDlg->init([this](unsigned int message, uptr_t wParam, sptr_t lParam) {
		return nppMessage(message, wParam, lParam);
	});

	// Status bar: the same parts as upstream
	for (int i = 0; i < STATUSBAR_NB_SECTIONS; ++i)
	{
		_statusBarParts[i] = new QLabel(this);
		_statusBarParts[i]->setContentsMargins(6, 0, 6, 0);
		if (i == STATUSBAR_DOC_TYPE)
			statusBar()->addWidget(_statusBarParts[i], 1);
		else
			statusBar()->addPermanentWidget(_statusBarParts[i]);
	}

	buildMainMenu();
	buildToolBar();

	_mainMenuHandle = std::make_unique<QtNppMenu>(menuBar());

	// Add User Defined Languages Entry (upstream Notepad_plus::init)
	for (int i = 0, len = nppParam.getNbUserLang(); i < len; ++i)
	{
		const UserLangContainer* userLangContainer = nppParam.getULCFromIndex(i);
		addUserDefineLangMenuItem(IDM_LANG_USER + i + 1, userLangContainer->getName());
	}

	//
	// Initialize the default foreground & background color
	//
	if (const Style* pStyle = nppParam.getGlobalStylers().findByID(STYLE_DEFAULT))
	{
		nppParam.setCurrentDefaultFgColor(pStyle->_fgColor);
		nppParam.setCurrentDefaultBgColor(pStyle->_bgColor);
		drawAutocompleteColoursFromTheme(pStyle->_fgColor, pStyle->_bgColor);
	}

	// upstream Notepad_plus::init: the categories of the Shortcut Mapper, then the Scintilla keys
	// (Notepad_plus_Window::init: _scintaccelerator.updateKeys())
	updateCommandShortcuts();
	updateScintillaKeys();

	// Macro Menu, Run Menu
	initDynamicMenus();
	checkMacroState();

	//Main menu is loaded, now load editor context menu items
	nppParam.getContextMenuFromXmlTree(_mainMenuHandle.get(), nullptr); // NPLX_TODO(Phase 9): plugins menu

	//Main menu is loaded, now load tab context menu items
	nppParam.getContextMenuFromXmlTree(_mainMenuHandle.get(), nullptr, false);

	if (nppParam.hasCustomContextMenu())
	{
		_mainEditView->execute(SCI_USEPOPUP, SC_POPUP_NEVER);
		_subEditView->execute(SCI_USEPOPUP, SC_POPUP_NEVER);
	}

	// Update Scintilla context menu strings (translated)
	for (std::vector<MenuItemUnit>* items : { &nppParam.getContextMenuItems(), &nppParam.getTabContextMenuItems() })
	{
		for (MenuItemUnit& item : *items)
		{
			if (item._itemName.empty())
			{
				if (QAction* mainAction = actionFor(static_cast<int>(item._cmdID)))
					item._itemName = purgeMenuItemString(mainAction->text().toStdWString().c_str());
			}
		}
	}

	//Add recent files (upstream Notepad_plus::init)
	if (QAction* exitAction = actionFor(IDM_FILE_EXIT))
	{
		QMenu* hFileMenu = menuBar()->actions().at(0)->menu();
		_lastRecentFileList.initMenu(hFileMenu, IDM_FILEMENU_LASTONE + 1, exitAction, [this](int id) { command(id); }, nppParam.putRecentFileInSubMenu());
		const NppGUI& nppGUIForLrf = nppParam.getNppGUI();
		for (int i = 0, nbLRFile = nppParam.getNbLRFile(); i < nbLRFile; ++i)
		{
			std::wstring* stdStr = nppParam.getLRFile(i);
			if (!nppGUIForLrf._checkHistoryFiles || doesFileExist(stdStr->c_str()))
			{
				_lastRecentFileList.add(stdStr->c_str());
			}
		}
	}

	// Initial state of the checkable menu items (upstream: Notepad_plus::init, checkMenuItem calls)
	checkMenuItem(IDM_VIEW_WRAP, svp._doWrap);
	checkMenuItem(IDM_VIEW_TAB_SPACE, svp._whiteSpaceShow);
	checkMenuItem(IDM_VIEW_EOL, svp._eolShow);
	checkMenuItem(IDM_VIEW_ALL_CHARACTERS, svp._whiteSpaceShow && svp._eolShow && svp._npcShow && svp._ccUniEolShow);
	checkMenuItem(IDM_VIEW_NPC, svp._npcShow);
	checkMenuItem(IDM_VIEW_NPC_CCUNIEOL, svp._ccUniEolShow);
	checkMenuItem(IDM_VIEW_ZOOM_SYNC, svp._zoomSync);
	checkSyncState(); // upstream Notepad_plus::init
	checkMenuItem(IDM_VIEW_INDENT_GUIDE, svp._indentGuideLineShow);
	checkMenuItem(IDM_VIEW_WRAP_SYMBOL, svp._wrapSymbolShow);

	const NppGUI& nppGUI = nppParam.getNppGUI();
	resize(nppGUI._appPos.right > 200 ? nppGUI._appPos.right : 1100, nppGUI._appPos.bottom > 200 ? nppGUI._appPos.bottom : 750);

	notifyBufferActivated(_mainEditView->getCurrentBufferID(), MAIN_VIEW);
	_mainEditView->setFocus();

	// upstream WM_ACTIVATEAPP -> PostMessage(NPPM_INTERNAL_CHECKDOCSTATUS): check the files changed outside
	connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
		if (state == Qt::ApplicationActive)
			QMetaObject::invokeMethod(this, [this]() { checkDocStatus(); }, Qt::QueuedConnection);
	});

	// Snapshot mode: periodic backup of the current document (upstream: backupDocument thread
	// posting NPPM_INTERNAL_SAVEBACKUP every _snapshotBackupTiming ms)
	if (nppGUI.isSnapshotMode())
	{
		_backupTimer = new QTimer(this);
		connect(_backupTimer, &QTimer::timeout, this, []() {
			if (NppParameters::getInstance().getNppGUI().isSnapshotMode())
				MainFileManager.backupCurrentBuffer();
		});
		_backupTimer->start(static_cast<int>(std::max<size_t>(nppGUI._snapshotBackupTiming, 1000)));
	}
}

// ============================================================================
// Menus (built from the upstream .rc menu: same structure and IDM_* ids)
// ============================================================================

QMenu* Notepad_plus::languageMenu() const
{
	// the menu holding "User Defined Language" (upstream MENUINDEX_LANGUAGE)
	const QAction* udlDlg = actionFor(IDM_LANG_USER_DLG);
	if (!udlDlg)
		return nullptr;
	for (QObject* p = udlDlg->parent(); p; p = p->parent())
	{
		if (auto* menu = qobject_cast<QMenu*>(p))
		{
			if (menu->parent() == menuBar())
				return menu;
		}
	}
	return nullptr;
}

void Notepad_plus::addUserDefineLangMenuItem(int cmdID, const wchar_t* name)
{
	// upstream: ::InsertMenu(hLangMenu, udlpos + i, MF_BYPOSITION, IDM_LANG_USER + i + 1, name) with
	// udlpos = the position of the last item of the Language menu
	QMenu* hLangMenu = languageMenu();
	if (!hLangMenu)
		return;
	QAction* action = new QAction(QString::fromWCharArray(name), hLangMenu);
	action->setData(cmdID);
	action->setCheckable(true);
	const QList<QAction*> actions = hLangMenu->actions();
	hLangMenu->insertAction(actions.isEmpty() ? nullptr : actions.last(), action);
	connect(action, &QAction::triggered, this, [this, cmdID, action]() {
		action->setChecked(!action->isChecked()); // Win32 semantics (see buildMainMenu)
		command(cmdID);
	});
	_actions.emplace(cmdID, action);
	_langActions[cmdID] = action;
}

void Notepad_plus::renameUserDefineLangMenuItem(int cmdID, const wchar_t* name)
{
	// upstream: ::ModifyMenu(hMenu, IDM_LANG_USER + i, MF_BYCOMMAND, IDM_LANG_USER + i, newName)
	if (QAction* action = actionFor(cmdID))
		action->setText(QString::fromWCharArray(name));
}

void Notepad_plus::removeUserDefineLangMenuItem(int cmdID)
{
	// upstream: ::RemoveMenu(hMenu, IDM_LANG_USER + i, MF_BYCOMMAND); the other items keep their ids
	auto it = _langActions.find(cmdID);
	if (it == _langActions.end())
		return;
	QAction* action = it->second;
	_langActions.erase(it);
	_actions.erase(cmdID);
	delete action;
}

bool Notepad_plus::isMenuItemChecked(int itemID) const
{
	const QAction* action = actionFor(itemID);
	return action && action->isChecked();
}

bool Notepad_plus::isRemovedOnLinux(int id)
{
	return isRemovedOnLinuxImpl(id);
}

QAction* Notepad_plus::actionFor(int id) const
{
	auto it = _actions.find(id);
	return it == _actions.end() ? nullptr : it->second;
}

void Notepad_plus::buildMainMenu()
{
	const NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();

	// Default shortcuts (upstream winKeyDefs in Parameters.cpp, as modified by shortcuts.xml)
	std::multimap<int, QKeySequence> shortcuts;
	for (const CommandShortcut& csc : NppParameters::getInstance().getUserShortcuts())
	{
		if (csc.isEnabled())
			shortcuts.emplace(static_cast<int>(csc.getID()), nplx::keyComboToKeySequence(csc.getKeyCombo()));
	}

	std::vector<QMenu*> menuStack;
	int languageMenuCount = 0;
	int skipDepth = 0;

	for (const NppMenuDef& def : g_mainMenuDef)
	{
		const bool topLevel = menuStack.empty();

		if (skipDepth > 0)
		{
			if (def._kind == NppMenuDef::PopupBegin)
				++skipDepth;
			else if (def._kind == NppMenuDef::PopupEnd)
				--skipDepth;
			continue;
		}

		switch (def._kind)
		{
			case NppMenuDef::PopupBegin:
			{
				const QString text = QString::fromWCharArray(def._text);
				if (topLevel)
				{
					// The .rc has two Language menus: flat and grouped by letter (upstream removes one at init)
					if (text == QStringLiteral("&Language"))
					{
						++languageMenuCount;
						const bool isCompactMenu = (languageMenuCount == 2);
						if (isCompactMenu != nppGUI._isLangMenuCompact)
						{
							skipDepth = 1;
							continue;
						}
					}
					// The tab-bar shortcut entries right of the menu bar ("+", "▼", "✕") are not used in Qt
					if (text == QStringLiteral("▼"))
					{
						skipDepth = 1;
						continue;
					}
					menuStack.push_back(menuBar()->addMenu(text));
				}
				else
				{
					menuStack.push_back(menuStack.back()->addMenu(QString::fromWCharArray(linuxPopupText(def._text))));
				}
				break;
			}

			case NppMenuDef::PopupEnd:
				if (!menuStack.empty())
				{
					// NPLX: no trailing separator once Windows-only items are removed
					const QList<QAction*> items = menuStack.back()->actions();
					if (!items.isEmpty() && items.last()->isSeparator())
						menuStack.back()->removeAction(items.last());
					menuStack.pop_back();
				}
				break;

			case NppMenuDef::Separator:
				if (!menuStack.empty())
				{
					// NPLX: no doubled separator once Windows-only items are removed
					const QList<QAction*> items = menuStack.back()->actions();
					if (!items.isEmpty() && !items.last()->isSeparator())
						menuStack.back()->addSeparator();
				}
				break;

			case NppMenuDef::Item:
			{
				if (topLevel) // right-aligned tab-bar shortcuts, see above
					break;
				if (isRemovedOnLinuxImpl(def._id))
					break;

				// Localised label (nativeLang.xml), falling back to the .rc text
				const wchar_t* linuxText = linuxMenuText(def._id);
				const wstring rcText = linuxText ? linuxText : def._text;
				const wstring label = _nativeLangSpeaker.getNativeLangMenuString(def._id, rcText);
				QAction* action = menuStack.back()->addAction(QString::fromStdWString(label));
				action->setData(def._id);

				QList<QKeySequence> keys;
				for (auto [it, end] = shortcuts.equal_range(def._id); it != end; ++it)
					keys.push_back(it->second);
				if (!keys.isEmpty())
					action->setShortcuts(keys);

				if (!isCommandPorted(def._id) || (def._flags & NppMenuDef::Grayed))
				{
					action->setEnabled(false);
					if (!(def._flags & NppMenuDef::Grayed))
						action->setToolTip(tr("Not yet ported to NotePadLX"));
				}

				const int id = def._id;
				connect(action, &QAction::triggered, this, [this, id, action]() {
					// NPLX: a Win32 menu item keeps its check mark until the command sets it (checkMenuItem);
					// QAction toggles it first, so restore the state the command expects to read
					if (action->isCheckable())
						action->setChecked(!action->isChecked());
					command(id);
				});
				_actions.emplace(id, action);
				if (id >= IDM_LANG && id < IDM_LANG + 400)
				{
					action->setCheckable(true);
					_langActions[id] = action;
				}
				break;
			}
		}
	}

	for (QMenu* menu : menuBar()->findChildren<QMenu*>())
		menu->setToolTipsVisible(true);

	// NPLX: the commands without a menu item still have their shortcuts (upstream: the accelerator table built from
	// all the shortcuts, e.g. Ctrl+Tab = IDC_NEXT_DOC); see updateShortcuts
	updateShortcuts();

	// upstream WM_INITMENUPOPUP -> WindowsMenu::initPopupMenu: the document list of the Window menu
	if (const QAction* windowsAction = actionFor(IDM_WINDOW_WINDOWS))
	{
		for (QObject* p = windowsAction->parent(); p; p = p->parent())
		{
			if (auto* windowMenu = qobject_cast<QMenu*>(p))
			{
				connect(windowMenu, &QMenu::aboutToShow, this, [this, windowMenu]() { initWindowsMenu(windowMenu); });
				break;
			}
		}
	}

	// Checkable items
	for (int id : { IDM_VIEW_WRAP, IDM_VIEW_TAB_SPACE, IDM_VIEW_EOL, IDM_VIEW_ALL_CHARACTERS, IDM_VIEW_INDENT_GUIDE,
	                IDM_VIEW_WRAP_SYMBOL, IDM_EDIT_TOGGLEREADONLY,
	                IDM_FORMAT_ANSI, IDM_FORMAT_AS_UTF_8, IDM_FORMAT_UTF_8, IDM_FORMAT_UTF_16BE, IDM_FORMAT_UTF_16LE,
	                IDM_FORMAT_TODOS, IDM_FORMAT_TOUNIX, IDM_FORMAT_TOMAC })
	{
		for (auto [it, end] = _actions.equal_range(id); it != end; ++it)
			it->second->setCheckable(true);
	}
}

void Notepad_plus::buildToolBar()
{
	// Same buttons and order as upstream toolBarIcons[]. NPLX: the icons are the NotePadLX set
	// (resources/nplx-icons, same names as upstream), drawn for the theme; the disabled state is generated.
	struct ToolBarButton { int _cmdID; const char* _icon; bool _hasDisabledIcon; };
	static constexpr ToolBarButton toolBarIcons[] = {
		{ IDM_FILE_NEW, "new", false }, { IDM_FILE_OPEN, "open", false }, { IDM_FILE_SAVE, "save", true },
		{ IDM_FILE_SAVEALL, "saveall", true }, { IDM_FILE_CLOSE, "close", false }, { IDM_FILE_CLOSEALL, "closeall", false },
		{ IDM_FILE_PRINT, "print", false },
		{ 0, nullptr, false },
		{ IDM_EDIT_CUT, "cut", true }, { IDM_EDIT_COPY, "copy", true }, { IDM_EDIT_PASTE, "paste", true },
		{ 0, nullptr, false },
		{ IDM_EDIT_UNDO, "undo", true }, { IDM_EDIT_REDO, "redo", true },
		{ 0, nullptr, false },
		{ IDM_SEARCH_FIND, "find", false }, { IDM_SEARCH_REPLACE, "findrep", false },
		{ 0, nullptr, false },
		{ IDM_VIEW_ZOOMIN, "zoomIn", false }, { IDM_VIEW_ZOOMOUT, "zoomOut", false },
		{ 0, nullptr, false },
		{ IDM_VIEW_SYNSCROLLV, "syncV", true }, { IDM_VIEW_SYNSCROLLH, "syncH", true },
		{ 0, nullptr, false },
		{ IDM_VIEW_WRAP, "wrap", false }, { IDM_VIEW_ALL_CHARACTERS, "allChars", false }, { IDM_VIEW_INDENT_GUIDE, "indentGuide", false },
		{ 0, nullptr, false },
		{ IDM_LANG_USER_DLG, "udl", false }, { IDM_VIEW_DOC_MAP, "docMap", false }, { IDM_VIEW_DOCLIST, "docList", false },
		{ IDM_VIEW_FUNC_LIST, "funcList", false }, { IDM_VIEW_FILEBROWSER, "fileBrowser", false },
		{ 0, nullptr, false },
		{ IDM_VIEW_MONITORING, "monitoring", true },
		{ 0, nullptr, false },
		{ IDM_MACRO_STARTRECORDINGMACRO, "startrecord", true }, { IDM_MACRO_STOPRECORDINGMACRO, "stoprecord", true },
		{ IDM_MACRO_PLAYBACKRECORDEDMACRO, "playrecord", true }, { IDM_MACRO_RUNMULTIMACRODLG, "playrecord_m", true },
		{ IDM_MACRO_SAVECURRENTMACRO, "saverecord", true },
	};

	_toolBar = addToolBar(tr("Tool Bar"));
	_toolBar->setObjectName(QStringLiteral("mainToolBar"));
	_toolBar->setMovable(false);
	// upstream TB_SMALL / TB_SMALL2 are 16 px, the others 24 px (Fluent); NPLX: 18 / 24
	const toolBarStatusType tbStatus = NppParameters::getInstance().getNppGUI()._tbIconInfo._tbIconSet;
	const int iconSize = (tbStatus == TB_SMALL || tbStatus == TB_SMALL2) ? 18 : 24;
	_toolBar->setIconSize(QSize(iconSize, iconSize));

	for (const ToolBarButton& b : toolBarIcons)
	{
		if (b._cmdID == 0)
		{
			_toolBar->addSeparator();
			continue;
		}
		QAction* action = actionFor(b._cmdID);
		if (!action)
			continue;

		action->setIcon(nplx::style::icon(QString::fromLatin1(b._icon)));
		action->setIconVisibleInMenu(false); // upstream menus have no icons
		_toolBar->addAction(action);
	}

	_toolBar->setVisible(NppParameters::getInstance().getNppGUI()._toolbarShow);
}

void Notepad_plus::checkMenuItem(int itemID, bool willBeChecked)
{
	for (auto [it, end] = _actions.equal_range(itemID); it != end; ++it)
	{
		it->second->setCheckable(true);
		it->second->setChecked(willBeChecked);
	}
}

void Notepad_plus::enableCommand(int cmdID, bool doEnable)
{
	for (auto [it, end] = _actions.equal_range(cmdID); it != end; ++it)
		it->second->setEnabled(doEnable && isCommandPorted(cmdID));
}

void Notepad_plus::checkLangsMenu(int id)
{
	Buffer* curBuf = _pEditView->getCurrentBuffer();
	if (id == -1)
	{
		id = NppParameters::getInstance().langTypeToCommandID(curBuf->getLangType());
		if (id == IDM_LANG_USER)
		{
			if (curBuf->isUserDefineLangExt())
			{
				const wchar_t *userLangName = curBuf->getUserDefineLangName();

				for (int i = IDM_LANG_USER + 1 ; i <= IDM_LANG_USER_LIMIT ; ++i)
				{
					const QAction* udlAction = actionFor(i);
					if (udlAction && udlAction->text().toStdWString() == userLangName)
					{
						id = i;
						break;
					}
				}
			}
		}
	}
	for (auto& [langId, action] : _langActions)
		action->setChecked(langId == id);
}

void Notepad_plus::checkUnicodeMenuItems()
{
	Buffer* buf = _pEditView->getCurrentBuffer();
	UniMode um = buf->getUnicodeMode();
	int encoding = buf->getEncoding();

	int id = -1;
	switch (um)
	{
		case uniUTF8: id = IDM_FORMAT_UTF_8; break;
		case uni16BE: id = IDM_FORMAT_UTF_16BE; break;
		case uni16LE: id = IDM_FORMAT_UTF_16LE; break;
		case uniUTF8_NoBOM: id = IDM_FORMAT_AS_UTF_8; break;
		case uni8Bit: id = IDM_FORMAT_ANSI; break;
		default: id = -1;
	}
	if (encoding != -1)
		id = -1;

	for (int formatId : { IDM_FORMAT_ANSI, IDM_FORMAT_AS_UTF_8, IDM_FORMAT_UTF_8, IDM_FORMAT_UTF_16BE, IDM_FORMAT_UTF_16LE })
		checkMenuItem(formatId, formatId == id);

	// NPLX: ANSI means "the locale code page"; on a UTF-8 system upstream disables it too
	enableCommand(IDM_FORMAT_ANSI, !NppParameters::getInstance().isCurrentSystemCodepageUTF8());
}

void Notepad_plus::checkDocState()
{
	Buffer* curBuf = _pEditView->getCurrentBuffer();

	bool isCurrentDirty = curBuf->isDirty();
	bool isSeveralDirty = isCurrentDirty;
	bool isFileExisting = doesFileExist(curBuf->getFullPathName());
	if (!isCurrentDirty)
	{
		for (size_t i = 0; i < MainFileManager.getNbBuffers(); ++i)
		{
			if (MainFileManager.getBufferByIndex(i)->isDirty())
			{
				isSeveralDirty = true;
				break;
			}
		}
	}

	bool isCurrentUntitled = curBuf->isUntitled();
	enableCommand(IDM_FILE_SAVE, isCurrentDirty);
	enableCommand(IDM_FILE_SAVEALL, isSeveralDirty);
	enableCommand(IDM_FILE_RELOAD, !isCurrentUntitled);
	enableCommand(IDM_FILE_OPEN_FOLDER, isFileExisting);
	enableCommand(IDM_FILE_OPEN_DEFAULT_VIEWER, isFileExisting);

	checkMenuItem(IDM_EDIT_TOGGLEREADONLY, curBuf->getUserReadOnly());
	_pEditView->execute(SCI_SETREADONLY, curBuf->isReadOnly());

	if (_pAnsiCharPanel)
		_pAnsiCharPanel->switchEncoding();
}

void Notepad_plus::checkModifiedDocument(bool bCheckOnlyCurrentBuffer)
{
	//this will trigger buffer updates. If the status changes, Notepad++ will be informed and can do its magic
	MainFileManager.checkFilesystemChanges(bCheckOnlyCurrentBuffer);
}

bool Notepad_plus::checkDocStatus()
{
	const NppGUI& nppgui = NppParameters::getInstance().getNppGUI();
	if (nppgui._fileAutoDetection != cdDisabled)
	{
		bool bCheckOnlyCurrentBuffer = (nppgui._fileAutoDetection & cdEnabled4CurrentFile) ? true : false;
		checkModifiedDocument(bCheckOnlyCurrentBuffer);
		return true;
	}
	return false;
}

// ============================================================================
// Views
// ============================================================================

bool Notepad_plus::viewVisibleConst(int whichOne) const
{
	QWidget* container = (whichOne == MAIN_VIEW) ? _mainContainer : _subContainer;
	return container && !container->isHidden();
}

bool Notepad_plus::viewVisible(int whichOne)
{
	return viewVisibleConst(whichOne);
}

int Notepad_plus::switchEditViewTo(int gid)
{
	if (_activeView == gid)
		return gid;

	if (!viewVisible(gid))
		return _activeView;

	int oldView = _activeView;
	_activeView = gid;
	_pDocTab = (gid == MAIN_VIEW) ? _mainDocTab : _subDocTab;
	_pNonDocTab = (gid == MAIN_VIEW) ? _subDocTab : _mainDocTab;
	_pEditView = (gid == MAIN_VIEW) ? _mainEditView : _subEditView;
	_pNonEditView = (gid == MAIN_VIEW) ? _subEditView : _mainEditView;

	UserDefineDialog::setScintilla(_pEditView); // upstream _pEditView->beSwitched()

	if (_pDocMap)
	{
		_pDocMap->initWrapMap();
	}

	if (!_pEditView->hasFocus())
		_pEditView->setFocus();

	notifyBufferActivated(_pEditView->getCurrentBufferID(), currentView());
	return oldView;
}

void Notepad_plus::showView(int whichOne)
{
	if (viewVisible(whichOne))
		return;
	(whichOne == MAIN_VIEW ? _mainContainer : _subContainer)->show();
}

bool Notepad_plus::canHideView(int whichOne)
{
	if (!viewVisible(whichOne))
		return false;	//cannot hide hidden view
	if (!bothActive())
		return false;	//cannot hide only window
	DocTabView* tabToCheck = (whichOne == MAIN_VIEW) ? _mainDocTab : _subDocTab;
	Buffer* buf = MainFileManager.getBufferByID(tabToCheck->getBufferByIndex(0));
	bool canHide = ((tabToCheck->nbItem() == 1) && !buf->isDirty() && buf->isUntitled());
	return canHide;
}

void Notepad_plus::hideView(int whichOne)
{
	if (!(bothActive()))	//cannot close if not both views visible
		return;

	(whichOne == MAIN_VIEW ? _mainContainer : _subContainer)->hide();
	switchEditViewTo(otherFromView(whichOne));
}

bool Notepad_plus::isEmpty()
{
	if (bothActive()) return false;

	DocTabView* tabToCheck = viewVisible(MAIN_VIEW) ? _mainDocTab : _subDocTab;

	Buffer* buf = MainFileManager.getBufferByID(tabToCheck->getBufferByIndex(0));
	bool isEmpty = ((tabToCheck->nbItem() == 1) && !buf->isDirty() && buf->isUntitled());
	return isEmpty;
}

Buffer* Notepad_plus::getCurrentBuffer()
{
	return _pEditView->getCurrentBuffer();
}

void Notepad_plus::syncZoom()
{
	const ScintillaViewParams& svp = NppParameters::getInstance().getSVP();
	if (svp._zoomSync)
		_pNonEditView->execute(SCI_SETZOOM, _pEditView->execute(SCI_GETZOOM));
}

void Notepad_plus::docGotoAnotherEditView(FileTransferMode mode)
{
	// Test if it's only doc to transfer on the hidden view
	// If so then do nothing
	if (mode == TransferMove)
	{
		if (_pDocTab->nbItem() == 1)
		{
			if (!viewVisible(otherView())) return;
		}
	}

	//First put the doc in the other view if not present (if it is, activate it).
	//Then if needed close in the original tab
	BufferID current = _pEditView->getCurrentBufferID();
	Buffer* buf = MainFileManager.getBufferByID(current);

	int viewToGo = otherView();
	int indexFound = _pNonDocTab->getIndexByBuffer(current);
	if (indexFound != -1)	//activate it
	{
		activateBuffer(current, otherView());
	}
	else	//open the document, also copying the position
	{
		// If both the views are visible then first save the position of non-edit view
		// So that moving document between views does not lose caret position
		if (viewVisible(currentView()) && viewVisible(otherView()))
		{
			_pNonEditView->saveCurrentPos();
		}

		loadBufferIntoView(current, viewToGo);

		_pEditView->saveCurrentPos();	//allow copying of position
		buf->setPosition(buf->getPosition(_pEditView), _pNonEditView);
		_pNonEditView->restoreCurrentPosPreStep();	//set position
		activateBuffer(current, viewToGo);
	}

	//Open the view if it was hidden
	if (!viewVisible(viewToGo))
	{
		showView(viewToGo);
	}

	//Close the document if we transfered the document instead of cloning it
	if (mode == TransferMove)
	{
		//just close the activate document, since thats the one we moved (no search)
		doClose(_pEditView->getCurrentBufferID(), currentView());
	} // else it was cone, so leave it

	//Activate the other view since thats where the document went
	switchEditViewTo(viewToGo);
	// NPLX_TODO(Phase 3): pinned tabs, monitoring state, document list panel
}

// ============================================================================
// Buffers in views (Notepad_plus.cpp)
// ============================================================================

void Notepad_plus::loadBufferIntoView(BufferID id, int whichOne, bool dontClose)
{
	DocTabView* tabToOpen = (whichOne == MAIN_VIEW) ? _mainDocTab : _subDocTab;
	ScintillaEditView* viewToOpen = (whichOne == MAIN_VIEW) ? _mainEditView : _subEditView;

	//check if buffer exists
	int index = tabToOpen->getIndexByBuffer(id);
	if (index != -1)	//already open, done
		return;

	BufferID idToClose = BUFFER_INVALID;
	//Check if the tab has a single clean buffer. Close it if so
	if (!dontClose && tabToOpen->nbItem() == 1)
	{
		idToClose = tabToOpen->getBufferByIndex(0);
		Buffer* buf = MainFileManager.getBufferByID(idToClose);
		if (buf->isDirty() || !buf->isUntitled())
		{
			idToClose = BUFFER_INVALID;
		}
	}

	MainFileManager.addBufferReference(id, viewToOpen);

	//close clean doc. Use special logic to prevent flicker of tab showing then hiding
	if (idToClose != BUFFER_INVALID)
	{
		tabToOpen->setBuffer(0, id);	//index 0 since only one open
		activateBuffer(id, whichOne);	//activate. DocTab already activated but not a problem
		MainFileManager.closeBuffer(idToClose, viewToOpen);	//delete the buffer
		if (_pDocumentListPanel)
			_pDocumentListPanel->closeItem(idToClose, whichOne);
	}
	else
	{
		tabToOpen->addBuffer(id);
	}
}

bool Notepad_plus::removeBufferFromView(BufferID id, int whichOne)
{
	DocTabView* tabToClose = (whichOne == MAIN_VIEW) ? _mainDocTab : _subDocTab;
	ScintillaEditView* viewToClose = (whichOne == MAIN_VIEW) ? _mainEditView : _subEditView;

	//check if buffer exists
	int index = tabToClose->getIndexByBuffer(id);
	if (index == -1)        //doesn't exist, done
		return false;

	Buffer* buf = MainFileManager.getBufferByID(id);

	//Cannot close doc if last and clean and not renamed
	if (tabToClose->nbItem() == 1)
	{
		wstring newTitle = ((NppParameters::getInstance()).getNativeLangSpeaker())->getLocalizedStrFromID("tab-untitled-string", UNTITLED_STR);
		wstring bufFileName = buf->getFullPathName();
		if (!buf->isDirty() && buf->isUntitled() && bufFileName.starts_with(newTitle.c_str()))
		{
			return false;
		}
	}

	int active = tabToClose->getCurrentTabIndex();
	if (active == index) //need an alternative (close real doc, put empty one back)
	{
		if (tabToClose->nbItem() == 1)  //need alternative doc, add new one. Use special logic to prevent flicker of adding new tab then closing other
		{
			BufferID newID = MainFileManager.newEmptyDocument();
			MainFileManager.addBufferReference(newID, viewToClose);
			tabToClose->setBuffer(0, newID);        //can safely use id 0, last (only) tab open
			activateBuffer(newID, whichOne);        //activate. DocTab already activated but not a problem
		}
		else
		{
			int toActivate = 0;
			//activate next doc, otherwise prev if not possible
			if (size_t(active) == tabToClose->nbItem() - 1) //prev
			{
				toActivate = active - 1;
			}
			else
			{
				toActivate = active;    //activate the 'active' index. Since we remove the tab first, the indices shift (on the right side)
			}

			if (NppParameters::getInstance().getNppGUI()._styleMRU)
			{
				// After closing a file choose the file to activate based on MRU list and not just last file in the list.
				TaskListInfo taskListInfo;
				this->taskListInfo(&taskListInfo, false); // upstream WM_GETTASKLISTINFO
				size_t i, n = taskListInfo._tlfsLst.size();
				for (i = 0; i < n; i++)
				{
					const TaskLstFnStatus& tfs = taskListInfo._tlfsLst[i];
					if (tfs._iView != whichOne || tfs._bufID == id)
						continue;
					toActivate = tfs._docIndex >= active ? tfs._docIndex - 1 : tfs._docIndex;
					break;
				}
			}

			tabToClose->deletItemAt((size_t)index); //delete first
			_isFolding = true; // So we can ignore events while folding is taking place
			activateBuffer(tabToClose->getBufferByIndex(toActivate), whichOne);     //then activate. The prevent jumpy tab behaviour
			_isFolding = false;
		}
	}
	else
	{
		tabToClose->deletItemAt((size_t)index);
	}

	MainFileManager.closeBuffer(id, viewToClose);
	return true;
}

bool Notepad_plus::activateBuffer(BufferID id, int whichOne, bool forceApplyHilite)
{
	const NppGUI& nppGui = NppParameters::getInstance().getNppGUI();
	bool isSnapshotMode = nppGui.isSnapshotMode();
	if (isSnapshotMode)
	{
		// Before switching off, synchronize backup file
		MainFileManager.backupCurrentBuffer();
	}

	Buffer* pBuf = MainFileManager.getBufferByID(id);
	bool reload = pBuf->getNeedReload();
	if (reload)
	{
		MainFileManager.reloadBuffer(id);
		pBuf->setNeedReload(false);
	}

	if (whichOne == MAIN_VIEW)
	{
		if (_mainDocTab->activateBuffer(id))	//only activate if possible
		{
			_isFolding = true;
			_mainEditView->activateBuffer(id, forceApplyHilite);
			_isFolding = false;
		}
		else
			return false;
	}
	else
	{
		if (_subDocTab->activateBuffer(id))
		{
			_isFolding = true;
			_subEditView->activateBuffer(id, forceApplyHilite);
			_isFolding = false;
		}
		else
			return false;
	}

	notifyBufferActivated(id, whichOne);

	return true;
}

bool Notepad_plus::switchToFile(BufferID id)
{
	int i = 0;
	int iView = currentView();
	if (id == BUFFER_INVALID)
		return false;

	if ((i = _pDocTab->getIndexByBuffer(id)) != -1)
	{
		iView = currentView();
	}
	else if ((i = _pNonDocTab->getIndexByBuffer(id)) != -1)
	{
		iView = otherView();
	}

	if (i != -1)
	{
		switchEditViewTo(iView);
		activateBuffer(id, currentView());
		return true;
	}
	return false;
}

void Notepad_plus::notifyBufferActivated(BufferID bufid, int view)
{
	Buffer* buf = MainFileManager.getBufferByID(bufid);
	buf->increaseRecentTag();

	if (view == MAIN_VIEW)
	{
		_autoCompleteMain->setLanguage(buf->getLangType());
		_mainEditView->maintainStateForNpc();
	}
	else if (view == SUB_VIEW)
	{
		_autoCompleteSub->setLanguage(buf->getLangType());
		_subEditView->maintainStateForNpc();
	}

	if (view != currentView())
		return;	//don't care if another view did something

	checkDocState();
	checkLangsMenu(-1);
	setLangStatus(buf->getLangType());
	updateStatusBar();
	checkUnicodeMenuItems();
	setUniModeText();
	setDisplayFormat(buf->getEolFormat());
	setTitle();

	const NppGUI& nppGui = NppParameters::getInstance().getNppGUI();
	bool isCurrBuffDetection = (nppGui._fileAutoDetection & cdEnabled4CurrentFile) ? true : false;
	bool reload = buf->getNeedReload();
	if (!reload && isCurrBuffDetection)
	{
		// Buffer has been activated, now check for file modification
		// If enabled for current buffer
		buf->checkFileState();
	}

	_linkTriggered = true;

	if (_pDocMap && (!_pDocMap->isClosed()) && _pDocMap->isVisible())
	{
		_pDocMap->reloadMap();
		_pDocMap->setSyntaxHiliting();
	}

	if (_pDocumentListPanel)
	{
		_pDocumentListPanel->activateItem(bufid, currentView());
	}

	if (_pFuncList && (!_pFuncList->isClosed()) && _pFuncList->isVisible())
	{
		_pFuncList->reload();
	}
}

void Notepad_plus::notifyBufferChanged(Buffer* buffer, int mask)
{
	NppParameters& nppParam = NppParameters::getInstance();
	const NppGUI& nppGUI = nppParam.getNppGUI();

	_mainEditView->bufferUpdated(buffer, mask);
	_subEditView->bufferUpdated(buffer, mask);
	_mainDocTab->bufferUpdated(buffer, mask);
	_subDocTab->bufferUpdated(buffer, mask);

	bool mainActive = (_mainEditView->getCurrentBuffer() == buffer);
	bool subActive = (_subEditView->getCurrentBuffer() == buffer);

	//Only event that applies to non-active Buffers
	if (mask & BufferChangeStatus)
	{	//reload etc
		switch (buffer->getStatus())
		{
			case DOC_UNNAMED: 	//nothing todo
			case DOC_REGULAR: 	//nothing todo
			case DOC_INACCESSIBLE: 	//nothing todo
			{
				break;
			}

			case DOC_MODIFIED:	//ask for reloading
			{
				// Since it is being monitored DOC_NEEDRELOAD is going to handle the change.
				if (buffer->isMonitoringOn())
					break;

				bool autoUpdate = (nppGUI._fileAutoDetection & cdAutoUpdate) ? true : false;
				if (!autoUpdate || buffer->isDirty())
				{
					// Then we ask user to update
					if (doReloadOrNot(buffer->getFullPathName(), buffer->isDirty()) == IDYES)
					{
						buffer->setInaccessibility(false); // it's accessible in any way
					}
					else
					{
						// Since the file content has changed but the user doesn't want to reload it, set state to dirty
						buffer->setDirty(true);

						// buffer in Notepad++ is not synchronized anymore with the file on disk
						buffer->setUnsync(true);

						break;	//abort
					}
				}
				// Set _isLoadedDirty false so when the document clean state is reached the icon will be set to blue
				buffer->setLoadedDirty(false);

				// buffer in Notepad++ is synchronized with the file on disk
				buffer->setUnsync(false);

				doReload(buffer->getID(), false);
				break;
			}

			case DOC_NEEDRELOAD: // by log monitoring
			{
				doReload(buffer->getID(), false);

				// not only test main view
				if (buffer == _mainEditView->getCurrentBuffer())
				{
					_mainEditView->setPositionRestoreNeeded(false);
					_mainEditView->execute(SCI_DOCUMENTEND);
				}
				// but also test sub-view, because the buffer could be cloned
				if (buffer == _subEditView->getCurrentBuffer())
				{
					_subEditView->setPositionRestoreNeeded(false);
					_subEditView->execute(SCI_DOCUMENTEND);
				}

				break;
			}

			case DOC_DELETED: 	//ask for keep
			{
				if (buffer->isInaccessible() && nppParam.isPlaceHolderEnabled())
				{
					buffer->setUnsync(true);
				}
				else
				{
					int doCloseDoc = doCloseOrNot(buffer->getFullPathName()) == IDNO;
					if (doCloseDoc)
					{
						//close in both views, doing current view last since that has to remain opened
						bool isSnapshotMode = nppGUI.isSnapshotMode();
						doClose(buffer->getID(), otherView(), isSnapshotMode);
						doClose(buffer->getID(), currentView(), isSnapshotMode);
						return;
					}
					else
					{
						// buffer in Notepad++ is not synchronized anymore with the file on disk
						buffer->setUnsync(true);
					}
				}
				break;
			}
		}
	}

	if (mask & (BufferChangeReadonly))
	{
		checkDocState();
	}

	if (_pDocumentListPanel)
		_pDocumentListPanel->setItemIconStatus(buffer);

	if (!mainActive && !subActive)
	{
		return;
	}

	if ((currentView() == MAIN_VIEW) && !mainActive)
		return;

	if ((currentView() == SUB_VIEW) && !subActive)
		return;

	if (mask & (BufferChangeDirty | BufferChangeFilename))
	{
		checkDocState();
		setTitle();
	}

	if (mask & (BufferChangeLanguage))
	{
		checkLangsMenu(-1);	//let Notepad++ do search for the item
		setLangStatus(buffer->getLangType());
		if (buffer == _mainEditView->getCurrentBuffer())
			_autoCompleteMain->setLanguage(buffer->getLangType());
		else if (buffer == _subEditView->getCurrentBuffer())
			_autoCompleteSub->setLanguage(buffer->getLangType());
		if (_pFuncList && (!_pFuncList->isClosed()) && _pFuncList->isVisible())
			_pFuncList->reload(); // sync FL with the current buffer lang
		if (_pDocMap) // NPLX: upstream calls it after each language command
			_pDocMap->setSyntaxHiliting();
		// NPLX_TODO(Phase 9): NPPN_LANGCHANGED
	}

	if (mask & (BufferChangeFormat | BufferChangeLanguage | BufferChangeUnicode))
	{
		updateStatusBar();
		checkUnicodeMenuItems();
		setUniModeText();
		setDisplayFormat(buffer->getEolFormat());
	}

	if (mask & (BufferChangeUnicode))
	{
		_mainEditView->maintainStateForNpc();
		_subEditView->maintainStateForNpc();
	}
}

// ============================================================================
// Status bar / title (Notepad_plus.cpp)
// ============================================================================

void Notepad_plus::setTitle()
{
	NppParameters& nppParams = NppParameters::getInstance();
	const NppGUI& nppGUI = nppParams.getNppGUI();
	//Get the buffer
	Buffer* buf = _pEditView->getCurrentBuffer();

	wstring result = L"";
	if (buf->isDirty())
	{
		result += L"*";
	}

	if (nppGUI._shortTitlebar)
	{
		result += buf->getFileName();
	}
	else
	{
		result += buf->getFullPathName();
	}
	result += L" - NotePadLX"; // NPLX: upstream appends the window class name "Notepad++"

	wstring tbAdd = nppParams.getTitleBarAdd();
	if (!tbAdd.empty())
	{
		result += L" - ";
		result += tbAdd;
	}

	setWindowTitle(QString::fromStdWString(result));
}

std::wstring Notepad_plus::getLangDesc(LangType langType, bool getName)
{
	NppParameters& nppParams = NppParameters::getInstance();

	if ((langType >= L_EXTERNAL) && (langType < nppParams.L_END))
	{
		const ExternalLangContainer* elc = nppParams.getELCFromIndex(langType - L_EXTERNAL);
		return string2wstring(elc->_name);
	}

	if (langType < L_TEXT || langType > L_EXTERNAL)
		langType = L_TEXT;

	wstring str2Show;
	if (getName)
		str2Show = ScintillaEditView::_langNameInfoArray[langType]._shortName;
	else
		str2Show = ScintillaEditView::_langNameInfoArray[langType]._longName;

	if (langType == L_USER)
	{
		Buffer* currentBuf = _pEditView->getCurrentBuffer();
		if (currentBuf->isUserDefineLangExt())
		{
			str2Show += L" - ";
			str2Show += currentBuf->getUserDefineLangName();
		}
	}
	return str2Show;
}

void Notepad_plus::setLangStatus(LangType langType)
{
	_statusBarParts[STATUSBAR_DOC_TYPE]->setText(QString::fromStdWString(getLangDesc(langType)));
}

void Notepad_plus::setDisplayFormat(EolType format)
{
	const wchar_t* str = L"??";
	switch (format)
	{
		case EolType::windows: str = L"Windows (CR LF)"; break;
		case EolType::macos:   str = L"Macintosh (CR)"; break;
		case EolType::unix:    str = L"Unix (LF)"; break;
		case EolType::unknown: str = L"Unknown"; break;
	}
	_statusBarParts[STATUSBAR_EOF_FORMAT]->setText(QString::fromWCharArray(str));

	checkMenuItem(IDM_FORMAT_TODOS, format == EolType::windows);
	checkMenuItem(IDM_FORMAT_TOUNIX, format == EolType::unix);
	checkMenuItem(IDM_FORMAT_TOMAC, format == EolType::macos);
}

void Notepad_plus::setUniModeText()
{
	Buffer* buf = _pEditView->getCurrentBuffer();
	int encoding = buf->getEncoding();
	UniMode um = buf->getUnicodeMode();

	wstring uniModeTextString;

	if (encoding == -1)
	{
		switch (um)
		{
			case uniUTF8:
				uniModeTextString = L"UTF-8-BOM"; break;
			case uni16BE:
				uniModeTextString = L"UTF-16 BE BOM"; break;
			case uni16LE:
				uniModeTextString = L"UTF-16 LE BOM"; break;
			case uni16BE_NoBOM:
				uniModeTextString = L"UTF-16 Big Endian"; break;
			case uni16LE_NoBOM:
				uniModeTextString = L"UTF-16 Little Endian"; break;
			case uniUTF8_NoBOM:
				uniModeTextString = L"UTF-8"; break;
			default:
				uniModeTextString = L"ANSI";
		}
	}
	else
	{
		const EncodingMapper& em = EncodingMapper::getInstance();
		int cmdID = em.getIndexFromEncoding(encoding);
		if (cmdID == -1)
		{
			return;
		}
		cmdID += IDM_FORMAT_ENCODE;

		// upstream reads the text of the Encoding menu item
		if (QAction* action = actionFor(cmdID))
			uniModeTextString = action->text().remove(QLatin1Char('&')).toStdWString();
	}
	_statusBarParts[STATUSBAR_UNICODE_TYPE]->setText(QString::fromStdWString(uniModeTextString));
}

void Notepad_plus::updateStatusBar()
{
	// these sections of status bar NOT updated by this function:
	// STATUSBAR_DOC_TYPE , STATUSBAR_EOF_FORMAT , STATUSBAR_UNICODE_TYPE

	size_t docLen = _pEditView->getCurrentDocLen();
	intptr_t nbLine = _pEditView->execute(SCI_GETLINECOUNT);

	wstring docLenStr = commafyInt(docLen);
	wstring nbLineStr = commafyInt(nbLine);

	NativeLangSpeaker* pNativeSpeaker = (NppParameters::getInstance()).getNativeLangSpeaker();
	wstring statusbarLengthLinesStr = pNativeSpeaker->getLocalizedStrFromID("statusbar-length-lines", L"length: $STR_REPLACE1$    lines: $STR_REPLACE2$");
	statusbarLengthLinesStr = stringReplace(statusbarLengthLinesStr, L"$STR_REPLACE1$", docLenStr);
	statusbarLengthLinesStr = stringReplace(statusbarLengthLinesStr, L"$STR_REPLACE2$", nbLineStr);

	_statusBarParts[STATUSBAR_DOC_SIZE]->setText(QString::fromStdWString(statusbarLengthLinesStr));

	wstring statusbarSelStr;

	size_t nbSelections = _pEditView->execute(SCI_GETSELECTIONS);
	if (nbSelections == 1)
	{
		if (_pEditView->execute(SCI_GETSELECTIONEMPTY))
		{
			size_t currPos = _pEditView->execute(SCI_GETCURRENTPOS);
			statusbarSelStr = pNativeSpeaker->getLocalizedStrFromID("statusbar-Pos", L"Pos: ");
			statusbarSelStr += commafyInt(currPos + 1);
		}
		else
		{
			const std::pair<size_t, size_t> oneSelCharsAndLines = _pEditView->getSelectedCharsAndLinesCount();
			statusbarSelStr = pNativeSpeaker->getLocalizedStrFromID("statusbar-Sel", L"Sel: ");
			statusbarSelStr += commafyInt(oneSelCharsAndLines.first) + L" | " + commafyInt(oneSelCharsAndLines.second);
		}
	}
	else if (_pEditView->execute(SCI_SELECTIONISRECTANGLE))
	{
		const std::pair<size_t, size_t> rectSelCharsAndLines = _pEditView->getSelectedCharsAndLinesCount();

		bool sameCharCountOnEveryLine = true;
		size_t maxLineCharCount = 0;

		for (size_t sel = 0; sel < nbSelections; ++sel)
		{
			size_t start = _pEditView->execute(SCI_GETSELECTIONNSTART, sel);
			size_t end = _pEditView->execute(SCI_GETSELECTIONNEND, sel);
			size_t lineCharCount = _pEditView->execute(SCI_COUNTCHARACTERS, start, end);

			if (sel == 0)
			{
				maxLineCharCount = lineCharCount;
			}
			else
			{
				if (lineCharCount != maxLineCharCount)
				{
					sameCharCountOnEveryLine = false;
					if (lineCharCount > maxLineCharCount)
					{
						maxLineCharCount = lineCharCount;
					}
				}
			}
		}

		wstring nbSelectionsStr = commafyInt(nbSelections);  // lines (rows) in rectangular selection
		wstring	maxLineCharCountStr = commafyInt(maxLineCharCount);  // show maximum width for columns
		wstring opStr = sameCharCountOnEveryLine ? L" = " : L" -> ";
		wstring rectSelCharsStr = commafyInt(rectSelCharsAndLines.first);

		statusbarSelStr = pNativeSpeaker->getLocalizedStrFromID("statusbar-Sel", L"Sel: ");
		statusbarSelStr += nbSelectionsStr + L"x" + maxLineCharCountStr + opStr + rectSelCharsStr;
	}
	else  // multiple stream selections
	{
		const int maxSelsToProcessLineCount = 99;  // limit the number of selections to process, for performance reasons
		const std::pair<size_t, size_t> multipleSelCharsAndLines = _pEditView->getSelectedCharsAndLinesCount(maxSelsToProcessLineCount);

		wstring nbSelectionsStr = commafyInt(nbSelections);
		wstring multipleSelChars = commafyInt(multipleSelCharsAndLines.first);
		wstring multipleSelLines = (nbSelections <= maxSelsToProcessLineCount) ? commafyInt(multipleSelCharsAndLines.second) : L"...";  // show ellipsis for line count if too many selections are active

		statusbarSelStr = pNativeSpeaker->getLocalizedStrFromID("statusbar-Sel-number", L"Sel");
		statusbarSelStr += L" " + nbSelectionsStr + L" : " + multipleSelChars + L" | " + multipleSelLines;
	}

	wstring lnStr = commafyInt(_pEditView->getCurrentLineNumber() + 1);
	wstring colStr = commafyInt(_pEditView->getCurrentColumnNumber() + 1);
	wstring statusbarLnColStr = pNativeSpeaker->getLocalizedStrFromID("statusbar-Ln-Col", L"Ln: $STR_REPLACE1$    Col: $STR_REPLACE2$");
	statusbarLnColStr = stringReplace(statusbarLnColStr, L"$STR_REPLACE1$", lnStr);
	statusbarLnColStr = stringReplace(statusbarLnColStr, L"$STR_REPLACE2$", colStr);
	wstring statusbarLnColSelStr = statusbarLnColStr + L"    " + statusbarSelStr;

	_statusBarParts[STATUSBAR_CUR_POS]->setText(QString::fromStdWString(statusbarLnColSelStr));

	_statusBarParts[STATUSBAR_TYPING_MODE]->setText(_pEditView->execute(SCI_GETOVERTYPE) ? QStringLiteral("OVR") : QStringLiteral("INS"));
}

void Notepad_plus::setLanguage(LangType langType)
{
	unsigned long MODEVENTMASK_ON = NppParameters::getInstance().getScintillaModEventMask();

	if (bothActive() && (_mainEditView->getCurrentBufferID() == _subEditView->getCurrentBufferID()))
	{
		// Add logic to prevent changing a language when a document is shared between two views
		// If so, release one document

		_subEditView->saveCurrentPos();
		Document subPrev = _subEditView->execute(SCI_GETDOCPOINTER);
		_subEditView->execute(SCI_SETMODEVENTMASK, MODEVENTMASK_OFF);
		_subEditView->execute(SCI_SETDOCPOINTER, 0, 0);
		_subEditView->execute(SCI_SETMODEVENTMASK, MODEVENTMASK_ON);

		(_mainEditView->getCurrentBuffer())->setLangType(langType);

		_subEditView->execute(SCI_SETMODEVENTMASK, MODEVENTMASK_OFF);
		_subEditView->execute(SCI_SETDOCPOINTER, 0, subPrev);
		_subEditView->execute(SCI_SETMODEVENTMASK, MODEVENTMASK_ON);
		_subEditView->maintainStateForNpc();
		_subEditView->setCRLF();
		_subEditView->restoreCurrentPosPreStep();
	}
	else
	{
		(_pEditView->getCurrentBuffer())->setLangType(langType);
	}

	_pEditView->restoreHiddenLines();
}

// ============================================================================
// Scintilla notifications (NppNotification.cpp)
// ============================================================================

void Notepad_plus::onScintillaNotify(Scintilla::NotificationData* scnData)
{
	// Scintilla::NotificationData has the same layout as SCNotification (Scintilla.h)
	SCNotification* notification = reinterpret_cast<SCNotification*>(scnData);
	ScintillaEditView* notifyView = qobject_cast<ScintillaEditView*>(sender());
	if (!notifyView)
		return;

	const bool isFromPrimary = (notifyView == _mainEditView);
	const bool isFromSecondary = (notifyView == _subEditView);

	switch (notification->nmhdr.code)
	{
		case SCN_MACRORECORD:
		{
			recordMacroStep(notification);
			break;
		}

		case SCN_SAVEPOINTREACHED:
		case SCN_SAVEPOINTLEFT:
		{
			Buffer* buf = nullptr;
			if (isFromPrimary)
			{
				buf = _mainEditView->getCurrentBuffer();
			}
			else if (isFromSecondary)
			{
				buf = _subEditView->getCurrentBuffer();
			}
			else
			{
				break;
			}

			bool isDirty = notification->nmhdr.code == SCN_SAVEPOINTLEFT;
			bool isSnapshotMode = NppParameters::getInstance().getNppGUI().isSnapshotMode();
			if (isSnapshotMode && !isDirty)
			{
				bool canUndo = _pEditView->execute(SCI_CANUNDO) == TRUE;
				if (!canUndo && buf->isLoadedDirty() && buf->isDirty())
					isDirty = true;
			}

			if (buf->isUnsync()) // buffer in Notepad++ is not synchronized with the file on disk - in this case the buffer is always dirty
				isDirty = true;

			if (buf->isSavePointDirty())
				isDirty = true;

			buf->setDirty(isDirty);
			break;
		}

		case SCN_MODIFIED:
		{
			if (notification->modificationType & (SC_MOD_DELETETEXT | SC_MOD_INSERTTEXT))  // SC_PERFORMED_UNDO | SC_PERFORMED_REDO are also included in SC_MOD_DELETETEXT and SC_MOD_INSERTTEXT
			{
				// Buffer of the document being modified
				Buffer* buffer = notifyView->getCurrentBuffer();

				// NPLX_TODO(Phase 3): useFirstLineAsTabName (ndds._useContentAsTabName)

				// Hold the correct position for "Begin/End &Select" or "Begin/End Select in Column Mode" commands
				notifyView->updateBeginEndSelectPosition(notification->modificationType & SC_MOD_INSERTTEXT, notification->position, notification->length);

				// While the text modification, we make sure the link beblow the modification will be reprocessed
				_linkTriggered = true;
				notifyView->viewport()->update();

				// for the backup system
				buffer->setModifiedStatus(true);
			}
			break;
		}

		case SCN_MODIFYATTEMPTRO:
		{
			// on fullread only mode, or if the current document is read-only, upstream beeps
			QApplication::beep();
			break;
		}

		case SCN_MARGINCLICK:
		{
			if (isFromPrimary)
				switchEditViewTo(MAIN_VIEW);
			else if (isFromSecondary)
				switchEditViewTo(SUB_VIEW);

			intptr_t lineClick = _pEditView->execute(SCI_LINEFROMPOSITION, notification->position);

			if (notification->margin == ScintillaEditView::_SC_MARGE_FOLDER)
			{
				_pEditView->marginClick(notification->position, notification->modifiers);
				if (_pDocMap)
					_pDocMap->fold(lineClick, _pEditView->isFolded(lineClick));
			}
			else if ((notification->margin == ScintillaEditView::_SC_MARGE_SYMBOL) && !notification->modifiers)
			{
				if (!_pEditView->hidelineMarkerClicked(lineClick))
					bookmarkToggle(lineClick);
			}
			break;
		}

		case SCN_UPDATEUI:
		{
			NppParameters& nppParam = NppParameters::getInstance();
			NppGUI& nppGui = nppParam.getNppGUI();

			Buffer* currentBuf = notifyView->getCurrentBuffer();

			// upstream NppBigSwitch WM_NOTIFY, before the notification is processed
			checkClipboard();
			checkUndoState();

			// replacement for obsolete custom SCN_SCROLLED
			if (notification->updated & SC_UPDATE_V_SCROLL)
			{
				int urlAction = (NppParameters::getInstance()).getNppGUI()._styleURL;
				if (urlAction != urlDisable && currentBuf->allowClickableLink())
				{
					addHotSpot(notifyView);
				}
			}

			// if it's searching/replacing, then do nothing
			if (nppParam._isFindReplacing)
				break;

			if (!isFromPrimary && !isFromSecondary)
				break;

			if (notifyView != _pEditView && currentBuf->allowSmartHilite()) // notification come from unfocus view - both views are visible
			{
				if (nppGui._smartHiliteOnAnotherView)
				{
					auto selectedText = _pEditView->getSelectedTextToWChar(false);
					if (!selectedText.empty())
						_smartHighlighter->highlightViewWithWord(notifyView, selectedText.c_str());
				}
				break;
			}

			braceMatch();

			if (nppGui._enableTagsMatchHilite)
			{
				XmlMatchedTagsHighlighter xmlTagMatchHiliter(_pEditView);
				xmlTagMatchHiliter.tagMatch(nppGui._enableTagAttrsHilite);
			}

			if (nppGui._enableSmartHilite && currentBuf->allowSmartHilite())
			{
				if (nppGui._disableSmartHiliteTmp)
					nppGui._disableSmartHiliteTmp = false;
				else
				{
					ScintillaEditView* anotherView = isFromPrimary ? _subEditView : _mainEditView;
					_smartHighlighter->highlightView(notifyView, anotherView);
				}
			}

			bool selectionIsChanged = (notification->updated & SC_UPDATE_SELECTION) != 0;
			// note: changing insert/overwrite mode will cause Scintilla to notify with SC_UPDATE_SELECTION
			bool contentIsChanged = (notification->updated & SC_UPDATE_CONTENT) != 0;
			if (selectionIsChanged || contentIsChanged)
			{
				updateStatusBar();
			}

			if (_pFuncList && (!_pFuncList->isClosed()) && _pFuncList->isVisible())
				_pFuncList->markEntry();
			AutoCompletion* autoC = isFromPrimary ? _autoCompleteMain.get() : _autoCompleteSub.get();
			autoC->update(0);
			break;
		}

		case SCN_DOUBLECLICK:
		{
			if (!isFromPrimary && !isFromSecondary)
				break;

			NppParameters& nppParam = NppParameters::getInstance();
			NppGUI& nppGUI = nppParam.getNppGUI();

			if (notification->modifiers == SCMOD_CTRL)
			{
				std::string bufstring;

				size_t position_of_click;
				// For some reason Ctrl+DoubleClick on an empty line means that notification->position == 1.
				// In that case we use SCI_GETCURRENTPOS to get the position.
				if (notification->position != -1)
					position_of_click = notification->position;
				else
					position_of_click = _pEditView->execute(SCI_GETCURRENTPOS);

				// Anonymous scope to limit use of the buf pointer (much easier to deal with std::string).
				{
					char* buf;

					if (nppGUI._delimiterSelectionOnEntireDocument)
					{
						// Get entire document.
						auto length = notifyView->execute(SCI_GETLENGTH);
						buf = new char[length + 1];
						notifyView->execute(SCI_GETTEXT, length + 1, reinterpret_cast<LPARAM>(buf));
					}
					else
					{
						// Get single line.
						auto length = notifyView->execute(SCI_GETCURLINE);
						buf = new char[length + 1];
						notifyView->execute(SCI_GETCURLINE, length, reinterpret_cast<LPARAM>(buf));

						// Compute the position of the click (relative to the beginning of the line).
						const auto line_position = notifyView->execute(SCI_POSITIONFROMLINE, notifyView->getCurrentLineNumber());
						position_of_click = position_of_click - line_position;
					}

					bufstring = buf;
					delete[] buf;
				}

				int leftmost_position = -1;
				int rightmost_position = -1;

				if (nppGUI._rightmostDelimiter == nppGUI._leftmostDelimiter)
				{
					// If the delimiters are the same (e.g. they are both a quotation mark), choose the ones
					// which are closest to the clicked position.
					for (int32_t i = static_cast<int32_t>(position_of_click); i >= 0; --i)
					{
						if (i >= static_cast<int32_t>(bufstring.size()))
							return;

						if (bufstring.at(i) == nppGUI._leftmostDelimiter)
						{
							// Respect escaped quotation marks.
							if (nppGUI._leftmostDelimiter == '"')
							{
								if (!(i > 0 && bufstring.at(i - 1) == '\\'))
								{
									leftmost_position = i;
									break;
								}
							}
							else
							{
								leftmost_position = i;
								break;
							}
						}
					}

					if (leftmost_position == -1)
						break;

					// Scan for right delimiter.
					for (size_t i = position_of_click; i < bufstring.length(); ++i)
					{
						if (bufstring.at(i) == nppGUI._rightmostDelimiter)
						{
							// Respect escaped quotation marks.
							if (nppGUI._rightmostDelimiter == '"')
							{
								if (!(i > 0 && bufstring.at(i - 1) == '\\'))
								{
									rightmost_position = static_cast<int32_t>(i);
									break;
								}
							}
							else
							{
								rightmost_position = static_cast<int32_t>(i);
								break;
							}
						}
					}
				}
				else
				{
					// Find matching pairs of delimiters (e.g. parentheses).
					// The pair where the distance from the left delimiter to position_of_click is at a minimum is the one we're looking for.
					// Of course position_of_click must lie between the delimiters.

					// This logic is required to handle cases like this:
					// (size_t i = function(); i < _buffers.size(); i++)

					std::stack<unsigned int> leftmost_delimiter_positions;

					for (unsigned int i = 0; i < bufstring.length(); ++i)
					{
						if (bufstring.at(i) == nppGUI._leftmostDelimiter)
							leftmost_delimiter_positions.push(i);
						else if (bufstring.at(i) == nppGUI._rightmostDelimiter && !leftmost_delimiter_positions.empty())
						{
							unsigned int matching_leftmost = leftmost_delimiter_positions.top();
							leftmost_delimiter_positions.pop();

							// We have either 1) chosen neither the left- or rightmost position, or 2) chosen both left- and rightmost position.
							assert((leftmost_position == -1 && rightmost_position == -1) || (leftmost_position >= 0 && rightmost_position >= 0));

							// Note: cast of leftmost_position to unsigned int is safe, since if leftmost_position is not -1 then it is guaranteed to be positive.
							// If it was possible, leftmost_position and rightmost_position should be of type optional<unsigned int>.
							if (matching_leftmost <= position_of_click && i >= position_of_click && (leftmost_position == -1 || matching_leftmost > static_cast<unsigned int>(leftmost_position)))
							{
								leftmost_position = matching_leftmost;
								rightmost_position = i;
							}
						}
					}
				}

				// Set selection to the position we found (if any).
				if (rightmost_position != -1 && leftmost_position != -1)
				{
					if (nppGUI._delimiterSelectionOnEntireDocument)
					{
						notifyView->execute(SCI_SETCURRENTPOS, rightmost_position);
						notifyView->execute(SCI_SETANCHOR, leftmost_position + 1);
					}
					else
					{
						const auto line_position = notifyView->execute(SCI_POSITIONFROMLINE, notifyView->getCurrentLineNumber());
						notifyView->execute(SCI_SETCURRENTPOS, line_position + rightmost_position);
						notifyView->execute(SCI_SETANCHOR, line_position + leftmost_position + 1);
					}
				}
			}
			else
			{
				// Double click with no modifiers
				// Check whether cursor is within URL
				auto indicMsk = notifyView->execute(SCI_INDICATORALLONFOR, notification->position);
				if (!(indicMsk & (1 << URL_INDIC)))
					break;

				auto startPos = notifyView->execute(SCI_INDICATORSTART, URL_INDIC, notification->position);
				auto endPos = notifyView->execute(SCI_INDICATOREND, URL_INDIC, notification->position);
				if ((notification->position < startPos) || (notification->position > endPos))
					break;

				// Revert selection of current word. Best to this early, otherwise the
				// selected word is visible all the time while the browser is starting
				notifyView->execute(SCI_SETSEL, notification->position, notification->position);

				// Open URL
				wstring url = notifyView->getGenericTextAsString(static_cast<size_t>(startPos), static_cast<size_t>(endPos));

				// NPLX: no UNC paths on Linux (upstream asks the network path warning box for them)
				QDesktopServices::openUrl(QUrl(QString::fromStdWString(url))); // NPLX: was ::ShellExecute
			}
			break;
		}

		case SCN_CHARADDED:
		{
			if (!isFromPrimary && !isFromSecondary)
				break;

			if (_recordingMacro || _playingBackMacro) // No macro recording or playing back
				break;

			const NppGUI& nppGui = NppParameters::getInstance().getNppGUI();
			if (nppGui._maintainIndent != autoIndent_none)
				maintainIndentation(static_cast<wchar_t>(notification->ch));

			Buffer* currentBuf = _pEditView->getCurrentBuffer();
			if (currentBuf->allowAutoCompletion())
			{
				AutoCompletion* autoC = isFromPrimary ? _autoCompleteMain.get() : _autoCompleteSub.get();
				bool isColumnMode = _pEditView->execute(SCI_GETSELECTIONS) > 1; // Multi-Selection || Column mode)
				if (nppGui._matchedPairConf.hasAnyPairsPair() && !isColumnMode)
					autoC->insertMatchedChars(notification->ch, nppGui._matchedPairConf);
				autoC->update(notification->ch);
			}
			break;
		}

		case SCN_CALLTIPCLICK:
		{
			if (!isFromPrimary && !isFromSecondary)
				break;

			AutoCompletion* autoC = isFromPrimary ? _autoCompleteMain.get() : _autoCompleteSub.get();
			autoC->callTipClick(notification->position);
			break;
		}

		case SCN_AUTOCSELECTION:
		{
			if (!isFromPrimary && !isFromSecondary)
				break;

			const NppGUI& nppGui = NppParameters::getInstance().getNppGUI();

			// if autocompletion is disabled and it is triggered manually, then both ENTER & TAB will insert the selection
			if (nppGui._autocStatus == NppGUI::AutocStatus::autoc_none)
			{
				break;
			}

			if (notification->listCompletionMethod == SC_AC_NEWLINE && !nppGui._autocInsertSelectedUseENTER)
			{
				notifyView->execute(SCI_AUTOCCANCEL);
				notifyView->execute(SCI_NEWLINE);
			}

			if (notification->listCompletionMethod == SC_AC_TAB && !nppGui._autocInsertSelectedUseTAB)
			{
				notifyView->execute(SCI_AUTOCCANCEL);
				notifyView->execute(SCI_TAB);
			}
			break;
		}

		case SCN_PAINTED:
		{
			if (!isFromPrimary && !isFromSecondary)
				break;

			if (_syncInfo.doSync())
				doSynScroll(notifyView);

			const NppParameters& nppParam = NppParameters::getInstance();

			// if it's searching/replacing, then do nothing
			if (_linkTriggered && !nppParam._isFindReplacing)
			{
				int urlAction = (NppParameters::getInstance()).getNppGUI()._styleURL;
				Buffer* currentBuf = _pEditView->getCurrentBuffer();
				if (urlAction != urlDisable && currentBuf->allowClickableLink())
				{
					addHotSpot();
				}
				_linkTriggered = false;
			}

			if (_pDocMap && (!_pDocMap->isClosed()) && _pDocMap->isVisible() && !_pDocMap->isTemporarilyShowing())
			{
				_pDocMap->wrapMap();
				_pDocMap->scrollMap();
			}
			break;
		}

		case SCN_ZOOM:
		{
			ScintillaEditView* unfocusView = isFromPrimary ? _subEditView : _mainEditView;
			if (isFromPrimary || isFromSecondary)
				_smartHighlighter->highlightView(notifyView, unfocusView);
			if (notifyView == _pEditView)
				syncZoom();
			break;
		}

		default:
			break;
	}
}

// ============================================================================
// Context menus (upstream NppBigSwitch.cpp WM_CONTEXTMENU, NppNotification.cpp NM_RCLICK)
// ============================================================================

bool Notepad_plus::eventFilter(QObject* watched, QEvent* event)
{
	if (event->type() == QEvent::ContextMenu)
	{
		// upstream: Scintilla doesn't show its own popup (SC_POPUP_NEVER), WM_CONTEXTMENU reaches the main window
		ScintillaEditView* view = nullptr;
		if (watched == _mainEditView->viewport())
			view = _mainEditView;
		else if (watched == _subEditView->viewport())
			view = _subEditView;

		if (view && NppParameters::getInstance().hasCustomContextMenu())
		{
			auto* contextEvent = static_cast<QContextMenuEvent*>(event);
			QPoint p = contextEvent->globalPos();
			if (contextEvent->reason() == QContextMenuEvent::Keyboard)
			{
				// context menu activated via keyboard; pop up at text caret position
				auto caretPos = view->execute(SCI_GETCURRENTPOS);
				const QPoint caret(static_cast<int>(view->execute(SCI_POINTXFROMPOSITION, 0, caretPos)),
					static_cast<int>(view->execute(SCI_POINTYFROMPOSITION, 0, caretPos)));
				p = view->viewport()->mapToGlobal(caret);
			}
			showEditorContextMenu(view, p);
			return true;
		}
	}
	return QMainWindow::eventFilter(watched, event);
}

void Notepad_plus::showEditorContextMenu(ScintillaEditView* view, const QPoint& globalPos)
{
	NppParameters& nppParam = NppParameters::getInstance();

	if (view == _mainEditView)
		switchEditViewTo(MAIN_VIEW);
	else
		switchEditViewTo(SUB_VIEW);

	ContextMenu scintillaContextmenu;

	std::vector<MenuItemUnit>& tmp = nppParam.getContextMenuItems();
	bool copyLink = (_pEditView->getSelectedTextCount() == 0) && _pEditView->getIndicatorRange(URL_INDIC);
	scintillaContextmenu.create(this, tmp, [this](int cmdID) { return actionFor(cmdID); }, copyLink);
	scintillaContextmenu.setCommandHandler([this](int cmdID) { command(cmdID); });

	scintillaContextmenu.display(globalPos);
}

void Notepad_plus::showTabContextMenu(int whichOne, const QPoint& globalPos)
{
	(void)whichOne;
	NppParameters& nppParam = NppParameters::getInstance();

	if (!_tabPopupMenu.isCreated())
	{
		std::vector<MenuItemUnit> itemUnitArray;

		if (nppParam.hasCustomTabContextMenu())
		{
			itemUnitArray = nppParam.getTabContextMenuItems();
		}
		else // default tab context menu
		{
			// IMPORTANT: If any submenu entry is added/moved/removed, you have to change the value of tabCmSubMenuEntryPos[] in localization.cpp file

			itemUnitArray.push_back(MenuItemUnit(IDM_FILE_CLOSE, L"Close"));
			itemUnitArray.push_back(MenuItemUnit(IDM_FILE_CLOSEALL_BUT_CURRENT, L"Close All BUT This", L"Close Multiple Tabs"));
			itemUnitArray.push_back(MenuItemUnit(IDM_FILE_CLOSEALL_BUT_PINNED, L"Close All BUT Pinned", L"Close Multiple Tabs"));
			itemUnitArray.push_back(MenuItemUnit(IDM_FILE_CLOSEALL_TOLEFT, L"Close All to the Left", L"Close Multiple Tabs"));
			itemUnitArray.push_back(MenuItemUnit(IDM_FILE_CLOSEALL_TORIGHT, L"Close All to the Right", L"Close Multiple Tabs"));
			itemUnitArray.push_back(MenuItemUnit(IDM_FILE_CLOSEALL_UNCHANGED, L"Close All Unchanged", L"Close Multiple Tabs"));
			itemUnitArray.push_back(MenuItemUnit(IDM_PINTAB, L"Pin Tab"));
			itemUnitArray.push_back(MenuItemUnit(IDM_FILE_SAVE, L"Save"));
			itemUnitArray.push_back(MenuItemUnit(IDM_FILE_SAVEAS, L"Save As..."));
			itemUnitArray.push_back(MenuItemUnit(IDM_FILE_OPEN_FOLDER, L"Open Containing Folder in File Manager", L"Open into")); // NPLX: was "...in Explorer"
			itemUnitArray.push_back(MenuItemUnit(IDM_FILE_OPEN_CMD, L"Open Containing Folder in Terminal", L"Open into")); // NPLX: was "...in cmd"
			// NPLX: no "Open Containing Folder in PowerShell" (Windows only)
			itemUnitArray.push_back(MenuItemUnit(IDM_FILE_CONTAININGFOLDERASWORKSPACE, L"Open Containing Folder as Workspace", L"Open into"));
			itemUnitArray.push_back(MenuItemUnit(0, NULL, L"Open into"));
			itemUnitArray.push_back(MenuItemUnit(IDM_FILE_OPEN_DEFAULT_VIEWER, L"Open in Default Viewer", L"Open into"));
			itemUnitArray.push_back(MenuItemUnit(IDM_FILE_RENAME, L"Rename"));
			itemUnitArray.push_back(MenuItemUnit(IDM_FILE_DELETE, L"Move to Trash")); // NPLX: was "Move to Recycle Bin"
			itemUnitArray.push_back(MenuItemUnit(IDM_FILE_RELOAD, L"Reload"));
			itemUnitArray.push_back(MenuItemUnit(IDM_FILE_PRINT, L"Print"));
			itemUnitArray.push_back(MenuItemUnit(0, NULL));
			itemUnitArray.push_back(MenuItemUnit(IDM_EDIT_TOGGLEREADONLY, L"Read-Only in NotePadLX"));
			itemUnitArray.push_back(MenuItemUnit(IDM_EDIT_TOGGLESYSTEMREADONLY, L"Read-Only Attribute in File System")); // NPLX: the write permission
			itemUnitArray.push_back(MenuItemUnit(0, NULL));
			itemUnitArray.push_back(MenuItemUnit(IDM_EDIT_FULLPATHTOCLIP, L"Copy Full File Path", L"Copy to Clipboard"));
			itemUnitArray.push_back(MenuItemUnit(IDM_EDIT_FILENAMETOCLIP, L"Copy Filename", L"Copy to Clipboard"));
			itemUnitArray.push_back(MenuItemUnit(IDM_EDIT_CURRENTDIRTOCLIP, L"Copy Current Dir. Path", L"Copy to Clipboard"));
			itemUnitArray.push_back(MenuItemUnit(IDM_VIEW_GOTO_START, L"Move to Start", L"Move Document"));
			itemUnitArray.push_back(MenuItemUnit(IDM_VIEW_GOTO_END, L"Move to End", L"Move Document"));
			itemUnitArray.push_back(MenuItemUnit(0, NULL, L"Move Document"));
			itemUnitArray.push_back(MenuItemUnit(IDM_VIEW_GOTO_ANOTHER_VIEW, L"Move to Other View", L"Move Document"));
			itemUnitArray.push_back(MenuItemUnit(IDM_VIEW_CLONE_TO_ANOTHER_VIEW, L"Clone to Other View", L"Move Document"));
			itemUnitArray.push_back(MenuItemUnit(IDM_VIEW_GOTO_NEW_INSTANCE, L"Move to New Instance", L"Move Document"));
			itemUnitArray.push_back(MenuItemUnit(IDM_VIEW_LOAD_IN_NEW_INSTANCE, L"Open in New Instance", L"Move Document"));
			itemUnitArray.push_back(MenuItemUnit(IDM_VIEW_TAB_COLOUR_1, L"Apply Color 1", L"Apply Color to Tab"));
			itemUnitArray.push_back(MenuItemUnit(IDM_VIEW_TAB_COLOUR_2, L"Apply Color 2", L"Apply Color to Tab"));
			itemUnitArray.push_back(MenuItemUnit(IDM_VIEW_TAB_COLOUR_3, L"Apply Color 3", L"Apply Color to Tab"));
			itemUnitArray.push_back(MenuItemUnit(IDM_VIEW_TAB_COLOUR_4, L"Apply Color 4", L"Apply Color to Tab"));
			itemUnitArray.push_back(MenuItemUnit(IDM_VIEW_TAB_COLOUR_5, L"Apply Color 5", L"Apply Color to Tab"));
			itemUnitArray.push_back(MenuItemUnit(IDM_VIEW_TAB_COLOUR_NONE, L"Remove Color", L"Apply Color to Tab"));

			// IMPORTANT: If any submenu entry is added/moved/removed, you have to change the value of tabCmSubMenuEntryPos[] in localization.cpp file
		}
		_tabPopupMenu.create(this, itemUnitArray, [this](int cmdID) { return actionFor(cmdID); });
		_tabPopupMenu.setCommandHandler([this](int cmdID) { command(cmdID); });
		// NPLX_TODO(Phase 8): _nativeLangSpeaker.changeLangTabContextMenu
	}

	// NPLX_TODO(Phase 3): tab colour icons (IDM_VIEW_TAB_COLOUR_*), with individual tab colours

	// NPLX: commands not ported yet stay disabled, like in the main menu
	auto enableItem = [this](int cmdID, bool isEnable) {
		_tabPopupMenu.enableItem(cmdID, isEnable && isCommandPorted(cmdID));
	};

	const QAction* saveAction = actionFor(IDM_FILE_SAVE);
	bool isEnable = saveAction && saveAction->isEnabled();
	_tabPopupMenu.enableItem(IDM_FILE_SAVE, isEnable);

	Buffer* buf = _pEditView->getCurrentBuffer();
	bool isUserReadOnly = buf->getUserReadOnly();
	_tabPopupMenu.checkItem(IDM_EDIT_TOGGLEREADONLY, isUserReadOnly);

	bool isSysReadOnly = buf->getFileReadOnly();
	bool isInaccessible = buf->isInaccessible();
	bool isUntitled = buf->isUntitled();
	enableItem(IDM_EDIT_TOGGLEREADONLY, !isSysReadOnly && !buf->isMonitoringOn() &&
		!(nppParam.getNppGUI()._isFullReadOnlySavingForbidden));
	if (isInaccessible || isUntitled)
	{
		_tabPopupMenu.checkItem(IDM_EDIT_TOGGLESYSTEMREADONLY, false);
		enableItem(IDM_EDIT_TOGGLESYSTEMREADONLY, false);
	}
	else
	{
		enableItem(IDM_EDIT_TOGGLESYSTEMREADONLY, true);
		_tabPopupMenu.checkItem(IDM_EDIT_TOGGLESYSTEMREADONLY, isSysReadOnly);
	}

	bool isFileExisting = doesFileExist(buf->getFullPathName());
	enableItem(IDM_FILE_DELETE, isFileExisting);
	enableItem(IDM_FILE_RELOAD, isFileExisting);
	enableItem(IDM_FILE_OPEN_FOLDER, isFileExisting);
	enableItem(IDM_FILE_OPEN_CMD, isFileExisting);
	enableItem(IDM_FILE_CONTAININGFOLDERASWORKSPACE, isFileExisting);

	enableItem(IDM_FILE_OPEN_DEFAULT_VIEWER, isFileExisting); // NPLX: xdg-open handles any existing file (upstream isAssoCommandExisting)

	bool isDirty = buf->isDirty();
	enableItem(IDM_VIEW_GOTO_ANOTHER_VIEW, !isInaccessible);
	enableItem(IDM_VIEW_CLONE_TO_ANOTHER_VIEW, !isInaccessible);
	enableItem(IDM_VIEW_GOTO_NEW_INSTANCE, !isInaccessible && !isDirty && !isUntitled);
	enableItem(IDM_VIEW_LOAD_IN_NEW_INSTANCE, !isInaccessible && !isDirty && !isUntitled);

	enableItem(IDM_FILE_SAVEAS, !isInaccessible);
	enableItem(IDM_FILE_RENAME, !isInaccessible);

	_tabPopupMenu.display(globalPos);
}

// ============================================================================
// Bookmarks (Notepad_plus.h / Notepad_plus.cpp)
// ============================================================================

void Notepad_plus::bookmarkAdd(intptr_t lineno) const
{
	if (lineno == -1)
		lineno = _pEditView->getCurrentLineNumber();
	if (!bookmarkPresent(lineno))
		_pEditView->execute(SCI_MARKERADD, lineno, MARK_BOOKMARK);
}

void Notepad_plus::bookmarkDelete(size_t lineno) const
{
	if (lineno == static_cast<size_t>(-1))
		lineno = _pEditView->getCurrentLineNumber();
	while (bookmarkPresent(lineno))
		_pEditView->execute(SCI_MARKERDELETE, lineno, MARK_BOOKMARK);
}

bool Notepad_plus::bookmarkPresent(intptr_t lineno) const
{
	if (lineno == -1)
		lineno = _pEditView->getCurrentLineNumber();
	LRESULT state = _pEditView->execute(SCI_MARKERGET, lineno);
	return ((state & (1 << MARK_BOOKMARK)) != 0);
}

void Notepad_plus::bookmarkToggle(intptr_t lineno) const
{
	if (lineno == -1)
		lineno = _pEditView->getCurrentLineNumber();

	if (bookmarkPresent(lineno))
		bookmarkDelete(lineno);
	else
		bookmarkAdd(lineno);
}

void Notepad_plus::bookmarkNext(bool forwardScan)
{
	size_t lineno = _pEditView->getCurrentLineNumber();
	int sci_marker = SCI_MARKERNEXT;
	size_t lineStart = lineno + 1;	//Scan starting from next line
	int lineRetry = 0;				//If not found, try from the beginning
	if (!forwardScan)
	{
		lineStart = lineno - 1;		//Scan starting from previous line
		lineRetry = int(_pEditView->execute(SCI_GETLINECOUNT));	//If not found, try from the end
		sci_marker = SCI_MARKERPREVIOUS;
	}
	intptr_t nextLine = _pEditView->execute(sci_marker, lineStart, 1 << MARK_BOOKMARK);
	if (nextLine < 0)
		nextLine = _pEditView->execute(sci_marker, lineRetry, 1 << MARK_BOOKMARK);

	if (nextLine < 0)
		return;

	_pEditView->execute(SCI_ENSUREVISIBLEENFORCEPOLICY, nextLine);
	_pEditView->execute(SCI_GOTOLINE, nextLine);
}

void Notepad_plus::bookmarkClearAll() const
{
	_pEditView->execute(SCI_MARKERDELETEALL, MARK_BOOKMARK);
}

// ============================================================================
// Window events
// ============================================================================

void Notepad_plus::closeEvent(QCloseEvent* event)
{
	// upstream WM_CLOSE (NppBigSwitch.cpp)
	NppParameters& nppParam = NppParameters::getInstance();
	const NppGUI& nppgui = nppParam.getNppGUI();
	bool isSnapshotMode = nppgui.isSnapshotMode();
	if (isSnapshotMode)
	{
		MainFileManager.backupCurrentBuffer();
	}

	Session currentSession;
	if (!((nppgui._multiInstSetting == monoInst) && !nppgui._rememberLastSession))
		getCurrentOpenedFiles(currentSession, true);

	if (nppgui._rememberLastSession)
	{
		//Lock the recent file list so it isn't populated with opened files
		//Causing them to show on restart even though they are loaded by session
		_lastRecentFileList.setLock(true);	//only lock when the session is remembered
	}

	_isAttemptingCloseOnQuit = true;
	bool allClosed = fileCloseAll(false, isSnapshotMode);	//try closing files before doing anything else
	_isAttemptingCloseOnQuit = false;

	if (nppgui._rememberLastSession)
		_lastRecentFileList.setLock(false);	//only unlock when the session is remembered

	if (!saveProjectPanelsParams()) allClosed = false; //writeProjectPanelsSettings
	saveFileBrowserParam();

	if (!allClosed && !nppParam.isEndSessionCritical())
	{
		// cancelled by user
		event->ignore();
		return; // abort quitting
	}

	// from this point on the Notepad++ exit is inevitable
	g_bNppExitFlag.store(true); // thread-safe op
	if (_backupTimer)
		_backupTimer->stop();

	// saveScintillasZoom + saveGUIParams (the parts ported so far)
	NppGUI& nppGUI = nppParam.getNppGUI();
	if (!isMaximized())
	{
		nppGUI._appPos.right = width();
		nppGUI._appPos.bottom = height();
	}
	nppGUI._isMaximized = isMaximized();
	nppGUI._userDefineDlgStatus = (_userDefineDlg->isDocked() ? UDD_DOCKED : 0) | (_userDefineDlg->isVisible() ? UDD_SHOW : 0);
	ScintillaViewParams& svp = const_cast<ScintillaViewParams&>(nppParam.getSVP());
	svp._zoom = static_cast<int>(_mainEditView->execute(SCI_GETZOOM));
	svp._zoom2 = static_cast<int>(_subEditView->execute(SCI_GETZOOM));

	saveDockingParams();
	//
	// saving shortcuts.xml: it should be done before "saveGUIParams" for recalculeting its HMAC to save in config.xml
	//
	saveShortcuts();
	saveUserDefineLangs();
	nppParam.createXmlTreeFromGUIParams();
	_findReplaceDlg->saveFindHistory(); // upstream saveFindHistory()
	nppParam.writeFindHistory();
	_lastRecentFileList.saveLRFL(); //writeRecentFileHistorySettings, writeHistory
	nppParam.saveConfig_xml();

	if (!_isNppSessionSavedAtExit)
	{
		_isNppSessionSavedAtExit = true; // prevents emptying of the session.xml file on another WM_ENDSESSION or WM_CLOSE
		//
		// saving session.xml
		//
		if (nppgui._rememberLastSession && !nppgui._isCmdlineNosessionActivated)
			saveSession(currentSession);
	}

	event->accept();
}


// upstream Notepad_plus::menuID2LangType (verbatim)
LangType Notepad_plus::menuID2LangType(int cmdID)
{
	switch (cmdID)
	{
        case IDM_LANG_C	:
            return L_C;
        case IDM_LANG_CPP :
            return L_CPP;
        case IDM_LANG_JAVA :
            return L_JAVA;
        case IDM_LANG_CS :
            return L_CS;
        case IDM_LANG_HTML :
            return L_HTML;
        case IDM_LANG_XML :
            return L_XML;
        case IDM_LANG_JS :
			return L_JAVASCRIPT;
		case IDM_LANG_JSON:
			return L_JSON;
		case IDM_LANG_JSON5:
			return L_JSON5;
        case IDM_LANG_PHP :
            return L_PHP;
        case IDM_LANG_ASP :
            return L_ASP;
        case IDM_LANG_JSP :
            return L_JSP;
        case IDM_LANG_CSS :
            return L_CSS;
        case IDM_LANG_LUA :
            return L_LUA;
        case IDM_LANG_PERL :
            return L_PERL;
        case IDM_LANG_PYTHON :
            return L_PYTHON;
        case IDM_LANG_PASCAL :
            return L_PASCAL;
        case IDM_LANG_BATCH :
            return L_BATCH;
        case IDM_LANG_OBJC :
            return L_OBJC;
        case IDM_LANG_VB :
            return L_VB;
        case IDM_LANG_SQL :
            return L_SQL;
        case IDM_LANG_MSSQL :
            return L_MSSQL;
        case IDM_LANG_ASCII :
            return L_ASCII;
        case IDM_LANG_TEXT :
            return L_TEXT;
        case IDM_LANG_RC :
            return L_RC;
        case IDM_LANG_MAKEFILE :
            return L_MAKEFILE;
        case IDM_LANG_INI :
            return L_INI;
        case IDM_LANG_TEX :
            return L_TEX;
        case IDM_LANG_FORTRAN :
            return L_FORTRAN;
		case IDM_LANG_FORTRAN_77 :
			return L_FORTRAN_77;
        case IDM_LANG_BASH :
            return L_BASH;
        case IDM_LANG_FLASH :
            return L_FLASH;
		case IDM_LANG_NSIS :
            return L_NSIS;
		case IDM_LANG_TCL :
            return L_TCL;
		case IDM_LANG_LISP :
			return L_LISP;
		case IDM_LANG_SCHEME :
			return L_SCHEME;
		case IDM_LANG_ASM :
            return L_ASM;
		case IDM_LANG_DIFF :
            return L_DIFF;
		case IDM_LANG_PROPS :
            return L_PROPS;
		case IDM_LANG_PS:
            return L_PS;
		case IDM_LANG_RUBY:
            return L_RUBY;
		case IDM_LANG_SMALLTALK:
            return L_SMALLTALK;
		case IDM_LANG_VHDL :
            return L_VHDL;
        case IDM_LANG_KIX :
            return L_KIX;
        case IDM_LANG_CAML :
            return L_CAML;
        case IDM_LANG_ADA :
            return L_ADA;
        case IDM_LANG_VERILOG :
            return L_VERILOG;
		case IDM_LANG_MATLAB :
            return L_MATLAB;
		case IDM_LANG_HASKELL :
            return L_HASKELL;
        case IDM_LANG_AU3 :
            return L_AU3;
		case IDM_LANG_INNO :
            return L_INNO;
		case IDM_LANG_CMAKE :
            return L_CMAKE;
		case IDM_LANG_YAML :
			return L_YAML;
        case IDM_LANG_COBOL :
            return L_COBOL;
        case IDM_LANG_D :
            return L_D;
        case IDM_LANG_GUI4CLI :
            return L_GUI4CLI;
        case IDM_LANG_POWERSHELL :
            return L_POWERSHELL;
        case IDM_LANG_R :
            return L_R;
		case IDM_LANG_COFFEESCRIPT :
            return L_COFFEESCRIPT;
		case IDM_LANG_BAANC:
			return L_BAANC;
		case IDM_LANG_SREC :
            return L_SREC;
		case IDM_LANG_IHEX :
            return L_IHEX;
		case IDM_LANG_TEHEX :
            return L_TEHEX;
		case IDM_LANG_SWIFT:
			return L_SWIFT;
        case IDM_LANG_ASN1 :
            return L_ASN1;
        case IDM_LANG_AVS :
            return L_AVS;
        case IDM_LANG_BLITZBASIC :
            return L_BLITZBASIC;
        case IDM_LANG_PUREBASIC :
            return L_PUREBASIC;
        case IDM_LANG_FREEBASIC :
            return L_FREEBASIC;
        case IDM_LANG_CSOUND :
            return L_CSOUND;
        case IDM_LANG_ERLANG :
            return L_ERLANG;
        case IDM_LANG_ESCRIPT :
            return L_ESCRIPT;
        case IDM_LANG_FORTH :
            return L_FORTH;
        case IDM_LANG_LATEX :
            return L_LATEX;
        case IDM_LANG_MMIXAL :
            return L_MMIXAL;
        case IDM_LANG_NIM :
            return L_NIM;
        case IDM_LANG_NNCRONTAB :
            return L_NNCRONTAB;
        case IDM_LANG_OSCRIPT :
            return L_OSCRIPT;
        case IDM_LANG_REBOL :
            return L_REBOL;
        case IDM_LANG_REGISTRY :
            return L_REGISTRY;
        case IDM_LANG_RUST :
            return L_RUST;
        case IDM_LANG_SPICE :
            return L_SPICE;
        case IDM_LANG_TXT2TAGS :
            return L_TXT2TAGS;
        case IDM_LANG_VISUALPROLOG:
            return L_VISUALPROLOG;
        case IDM_LANG_TYPESCRIPT:
            return L_TYPESCRIPT;
        case IDM_LANG_GDSCRIPT:
            return L_GDSCRIPT;
        case IDM_LANG_HOLLYWOOD:
            return L_HOLLYWOOD;	    
        case IDM_LANG_GOLANG:
            return L_GOLANG;
        case IDM_LANG_RAKU:
            return L_RAKU;
        case IDM_LANG_TOML:
            return L_TOML;
        case IDM_LANG_SAS:
            return L_SAS;
        case IDM_LANG_ERRORLIST:
            return L_ERRORLIST;
        case IDM_LANG_ESCSEQ:
            return L_ESCSEQ;
        case IDM_LANG_STTXT:
            return L_STTXT;
        case IDM_LANG_USER:
            return L_USER;
		default:
		{
			if (cmdID >= IDM_LANG_USER && cmdID <= IDM_LANG_USER_LIMIT)
			{
				return L_USER;
			}
			break; 
		}
	}
	return L_EXTERNAL;
}
