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

// NPLX: the main window side of Settings > Preferences: the NppBigSwitch cases of the messages the Preferences
// pages send (NPPM_INTERNAL_*), the toolbar icon sets (upstream ToolBar::reduce, enlarge...), the dark mode
// refresh (upstream Notepad_plus::refreshDarkMode) and the Language menu items.

#include "Notepad_plus.h"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPixmap>
#include <QStatusBar>
#include <QToolBar>

#include "Buffer.h"
#include "DocTabView.h"
#include "FindReplaceDlg.h"
#include "NplxStyle.h"
#include "NppDarkMode.h"
#include "NppDarkModeOptions.h"
#include "Parameters.h"
#include "ScintillaEditView.h"
#include "WordStyleDlg.h"
#include "colors.h"
#include "menuCmdID.h"
#include "preferenceDlg.h"
#include "preference_rc.h"
#include "resource.h"

using namespace std;

void Notepad_plus::applyToolbarIconSet()
{
	// upstream TB_SMALL / TB_SMALL2 are 16 px, TB_LARGE / TB_LARGE2 24 px (Fluent), TB_STANDARD the 16 px bitmaps.
	// NPLX: 18 / 24 px NotePadLX icons; the "2" (filled) sets are drawn bolder, the standard set is not colorized
	if (!_toolBar)
		return;
	const TbIconInfo& tbInfo = NppParameters::getInstance().getNppGUI()._tbIconInfo;
	const toolBarStatusType tbStatus = tbInfo._tbIconSet;
	const bool isLarge = (tbStatus == TB_LARGE || tbStatus == TB_LARGE2);
	const bool isBold = (tbStatus == TB_SMALL2 || tbStatus == TB_LARGE2);

	// upstream ToolBarIcons::changeIcons: the Fluent colours (ImageListSet.cpp)
	QColor tint;
	if (tbStatus != TB_STANDARD)
	{
		switch (tbInfo._tbColor)
		{
			case FluentColor::red: tint = QColor(0xE8, 0x11, 0x23); break;
			case FluentColor::green: tint = QColor(0x00, 0x8B, 0x00); break;
			case FluentColor::blue: tint = QColor(0x00, 0x78, 0xD4); break;
			case FluentColor::purple: tint = QColor(0xB1, 0x46, 0xC2); break;
			case FluentColor::cyan: tint = QColor(0x00, 0xB7, 0xC3); break;
			case FluentColor::olive: tint = QColor(0x49, 0x82, 0x05); break;
			case FluentColor::yellow: tint = QColor(0xFF, 0xB9, 0x00); break;
			case FluentColor::accent:
				tint = QGuiApplication::palette().color(QPalette::Accent); // the desktop accent colour
				break;
			case FluentColor::custom:
				tint = tbInfo._tbCustomColor != 0
					? QColor(GetRValue(tbInfo._tbCustomColor), GetGValue(tbInfo._tbCustomColor), GetBValue(tbInfo._tbCustomColor))
					: QGuiApplication::palette().color(QPalette::Accent);
				break;
			case FluentColor::defaultColor:
			default:
				break; // the theme colours
		}
	}
	nplx::style::setToolbarIconStyle(tint, tbInfo._tbUseMono, isBold);

	const int iconSize = isLarge ? 24 : 18;
	_toolBar->setIconSize(QSize(iconSize, iconSize));
	_toolBar->update();
	for (QAction* action : _toolBar->actions())
		action->setIcon(action->icon()); // the icons are drawn again with the new style
}

void Notepad_plus::setupColorSampleBitmapsOnMainMenuItems()
{
	// set up bitmaps on menu items
	// style-related bitmaps of color samples
	struct
	{
		int firstOfThisColorMenuId;
		int styleIndic;
		std::vector<int> sameColorMenuIds;
	}
	bitmapOnStyleMenuItemsInfo[] =
	{
		{ IDM_SEARCH_GONEXTMARKER5, SCE_UNIVERSAL_FOUND_STYLE_EXT5, { IDM_SEARCH_MARKALLEXT5, IDM_SEARCH_MARKONEEXT5, IDM_SEARCH_UNMARKALLEXT5, IDM_SEARCH_GOPREVMARKER5, IDM_SEARCH_STYLE5TOCLIP } },
		{ IDM_SEARCH_GONEXTMARKER4, SCE_UNIVERSAL_FOUND_STYLE_EXT4, { IDM_SEARCH_MARKALLEXT4, IDM_SEARCH_MARKONEEXT4, IDM_SEARCH_UNMARKALLEXT4, IDM_SEARCH_GOPREVMARKER4, IDM_SEARCH_STYLE4TOCLIP } },
		{ IDM_SEARCH_GONEXTMARKER3, SCE_UNIVERSAL_FOUND_STYLE_EXT3, { IDM_SEARCH_MARKALLEXT3, IDM_SEARCH_MARKONEEXT3, IDM_SEARCH_UNMARKALLEXT3, IDM_SEARCH_GOPREVMARKER3, IDM_SEARCH_STYLE3TOCLIP } },
		{ IDM_SEARCH_GONEXTMARKER2, SCE_UNIVERSAL_FOUND_STYLE_EXT2, { IDM_SEARCH_MARKALLEXT2, IDM_SEARCH_MARKONEEXT2, IDM_SEARCH_UNMARKALLEXT2, IDM_SEARCH_GOPREVMARKER2, IDM_SEARCH_STYLE2TOCLIP } },
		{ IDM_SEARCH_GONEXTMARKER1, SCE_UNIVERSAL_FOUND_STYLE_EXT1, { IDM_SEARCH_MARKALLEXT1, IDM_SEARCH_MARKONEEXT1, IDM_SEARCH_UNMARKALLEXT1, IDM_SEARCH_GOPREVMARKER1, IDM_SEARCH_STYLE1TOCLIP } },
		{ IDM_SEARCH_GONEXTMARKER_DEF, SCE_UNIVERSAL_FOUND_STYLE, { IDM_SEARCH_GOPREVMARKER_DEF, IDM_SEARCH_MARKEDTOCLIP } }
	};

	// upstream generateSolidColourMenuItemIcon: a square of the colour with a frame
	auto colorIcon = [](COLORREF color) {
		QPixmap pm(16, 16);
		pm.fill(Qt::transparent);
		QPainter p(&pm);
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(QColor(0x80, 0x80, 0x80));
		p.setBrush(QColor(GetRValue(color), GetGValue(color), GetBValue(color)));
		p.drawRoundedRect(QRectF(1.5, 1.5, 13, 13), 3, 3);
		return QIcon(pm);
	};
	auto setIcon = [this](int cmdID, const QIcon& icon) {
		for (auto [it, end] = _actions.equal_range(cmdID); it != end; ++it)
		{
			it->second->setIcon(icon);
			it->second->setIconVisibleInMenu(true);
		}
		if (QAction* tabAction = _tabPopupMenu.action(cmdID))
		{
			tabAction->setIcon(icon);
			tabAction->setIconVisibleInMenu(true);
		}
	};

	NppParameters& nppParam = NppParameters::getInstance();
	StyleArray& styleArray = nppParam.getMiscStylerArray();

	for (const auto& info : bitmapOnStyleMenuItemsInfo)
	{
		const Style* pStyle = styleArray.findByID(info.styleIndic);
		if (pStyle == nullptr)
			continue;

		const QIcon icon = colorIcon(pStyle->_bgColor);
		setIcon(info.firstOfThisColorMenuId, icon);
		for (int relatedMenuId : info.sameColorMenuIds)
			setIcon(relatedMenuId, icon);
	}

	// Adds tab color icons
	for (int i = 0; i < TAB_COLORS_COUNT; ++i)
		setIcon(IDM_VIEW_TAB_COLOUR_1 + i, colorIcon(nppParam.getIndividualTabColor(i, NppDarkMode::isEnabled(), true)));
}

void Notepad_plus::removeAllHotSpot()
{
	for (DocTabView* pDocView : { _mainDocTab, _subDocTab })
	{
		for (size_t i = 0; i < pDocView->nbItem(); ++i)
		{
			BufferID id = pDocView->getBufferByIndex(i);
			Buffer* buf = MainFileManager.getBufferByID(id);

			if (buf->allowClickableLink()) // if it's not allowed clickabled link in the buffer, there's nothing to be cleared
				MainFileManager.removeHotSpot(buf);
		}
	}
}

void Notepad_plus::showLanguageMenuItem(int cmdID, bool show)
{
	// upstream: DeleteMenu / InsertMenu; NPLX: the item is hidden, and an empty letter sub-menu of the compact
	// Language menu too
	std::vector<QAction*> actions;
	for (auto [it, end] = _actions.equal_range(cmdID); it != end; ++it)
		actions.push_back(it->second);
	if (auto it = _langActions.find(cmdID); it != _langActions.end())
		actions.push_back(it->second);
	for (QAction* action : actions)
	{
		action->setVisible(show);
		for (QObject* p = action->parent(); p; p = p->parent())
		{
			auto* menu = qobject_cast<QMenu*>(p);
			if (!menu || menu == languageMenu())
				break;
			bool hasVisibleItem = false;
			for (QAction* item : menu->actions())
				hasVisibleItem = hasVisibleItem || (item->isVisible() && !item->isSeparator());
			menu->menuAction()->setVisible(hasVisibleItem);
		}
	}
}

void Notepad_plus::hideExcludedLanguages()
{
	for (const LangMenuItem& lmi : NppParameters::getInstance().getNppGUI()._excludedLangList)
	{
		const int cmdID = lmi._cmdID > 0 ? lmi._cmdID : NppParameters::getInstance().langTypeToCommandID(lmi._langType);
		if (cmdID > 0)
			showLanguageMenuItem(cmdID, false);
	}
}

void Notepad_plus::refreshDarkMode(bool resetStyle)
{
	// upstream NppDarkMode::refreshDarkMode + Notepad_plus::refreshDarkMode
	NppParameters& nppParams = NppParameters::getInstance();
	nplx::style::initDarkMode();
	nplx::style::refresh();

	if (resetStyle)
	{
		nppMessage(NPPM_SETEDITORBORDEREDGE, 0, nppParams.getSVP()._showBorderEdge);

		const int tabIconSet = NppDarkMode::getTabIconSet(NppDarkMode::isEnabled());
		if (tabIconSet != -1)
		{
			if (_preference && _preference->tabbarSubDlg())
				_preference->tabbarSubDlg()->setTabbarAlternateIcons(tabIconSet == 1);
			nppMessage(NPPM_INTERNAL_CHANGETABBARICONSET, static_cast<uptr_t>(false), tabIconSet);
		}

		auto& nppGUITbInfo = nppParams.getNppGUI()._tbIconInfo;
		nppGUITbInfo = NppDarkMode::getToolbarIconInfo();
		applyToolbarIconSet();

		// the editor theme of the mode (DarkMode darkThemeName / lightThemeName)
		ThemeSwitcher& themeSwitcher = nppParams.getThemeSwitcher();
		wstring themePath;
		wstring themeName;
		const wstring xmlFileName = NppDarkMode::getThemeName();
		if (!xmlFileName.empty())
		{
			if (!nppParams.isLocal() || nppParams.isCloud())
			{
				themePath = nppParams.getUserPath();
				pathAppend(themePath, L"themes");
				pathAppend(themePath, xmlFileName);
			}

			if (themePath.empty() || !doesFileExist(themePath.c_str()))
			{
				themePath = themeSwitcher.getThemeDirPath();
				pathAppend(themePath, xmlFileName);
			}

			themeName = themeSwitcher.getThemeFromXmlFileName(themePath.c_str());
		}
		else
		{
			//use _stylerPath;
			const pair<wstring, wstring>& themeInfo = themeSwitcher.getElementFromIndex(0);
			themePath = themeInfo.second;
			themeName = themeSwitcher.getDefaultThemeLabel();
		}

		if (doesFileExist(themePath.c_str()))
		{
			nppParams.getNppGUI()._themeName = themePath;

			if (_configStyleDlg && _configStyleDlg->isCreated())
			{
				_configStyleDlg->selectThemeByName(themeName.c_str());
			}
			else
			{
				nppParams.reloadStylers(themePath.c_str());
				nppMessage(WM_UPDATESCINTILLAS, TRUE, 0);
			}
		}
	}

	// the panels and the views follow the new colours
	nppMessage(WM_UPDATESCINTILLAS, TRUE, 0);
	setupColorSampleBitmapsOnMainMenuItems(); // the tab colours of the mode
	for (DocTabView* tab : { _mainDocTab, _subDocTab })
		tab->applyTabStatus();
}

bool Notepad_plus::preferenceMessage(unsigned int message, uptr_t wParam, sptr_t lParam, intptr_t& result)
{
	NppParameters& nppParam = NppParameters::getInstance();
	result = TRUE;

	switch (message)
	{
		case NPPM_INTERNAL_LAUNCHPREFERENCES:
		{
			// Launch _configStyleDlg (create or display it)
			command(IDM_SETTING_PREFERENCE);

			// go into the section we need
			_preference->goToSection(wParam, lParam);
			return true;
		}

		case NPPM_INTERNAL_REFRESHDARKMODE:
		{
			refreshDarkMode(static_cast<bool>(wParam));
			// NPLX_TODO(Phase 9): notify the plugins (NPPN_DARKMODECHANGED)
			return true;
		}

		case NPPM_INTERNAL_CHANGETABBARICONSET:
		{
			// upstream: lParam 1 = alternate icons, 0 / 2 = the light / dark icons; NPLX: the icons follow the theme,
			// the alternate set is chosen by TAB_ALTICONS
			NppGUI& nppGUI = nppParam.getNppGUI();
			if (lParam == 1)
				nppGUI._tabStatus |= TAB_ALTICONS;
			else
				nppGUI._tabStatus &= ~TAB_ALTICONS;
			for (DocTabView* tab : { _mainDocTab, _subDocTab })
				tab->applyTabStatus();
			return true;
		}

		case NPPM_INTERNAL_RELOADNATIVELANG:
		{
			// NPLX_TODO(Phase 8): reloadLang() at runtime; the new nativeLang.xml is read at the next start
			return true;
		}

		case NPPM_INTERNAL_ENABLESNAPSHOT:
		{
			launchDocumentBackupTask();
			return true;
		}

		case NPPM_INTERNAL_SETCARETWIDTH:
		{
			const NppGUI& nppGUI = nppParam.getNppGUI();
			for (ScintillaEditView* view : { _mainEditView, _subEditView })
			{
				if (nppGUI._caretWidth < 4)
				{
					view->execute(SCI_SETCARETSTYLE, CARETSTYLE_LINE);
					view->execute(SCI_SETCARETWIDTH, nppGUI._caretWidth);
				}
				else if (nppGUI._caretWidth == 4)
				{
					view->execute(SCI_SETCARETWIDTH, 1);
					view->execute(SCI_SETCARETSTYLE, CARETSTYLE_BLOCK);
				}
				else // nppGUI._caretWidth == 5
				{
					view->execute(SCI_SETCARETWIDTH, 1);
					view->execute(SCI_SETCARETSTYLE, CARETSTYLE_BLOCK | CARETSTYLE_BLOCK_AFTER);
				}
			}
			return true;
		}

		case NPPM_SETSMOOTHFONT:
		{
			int param = (lParam == 0 ? SC_EFF_QUALITY_DEFAULT : SC_EFF_QUALITY_LCD_OPTIMIZED);
			_mainEditView->execute(SCI_SETFONTQUALITY, param);
			_subEditView->execute(SCI_SETFONTQUALITY, param);
			return true;
		}

		case NPPM_INTERNAL_CARETLINEFRAME:
		{
			_mainEditView->execute(SCI_SETCARETLINEFRAME, lParam);
			_subEditView->execute(SCI_SETCARETLINEFRAME, lParam);
			return true;
		}

		case NPPM_SETEDITORBORDEREDGE:
		{
			bool withBorderEdge = (lParam == 1);
			_mainEditView->setBorderEdge(withBorderEdge);
			_subEditView->setBorderEdge(withBorderEdge);
			return true;
		}

		case NPPM_INTERNAL_VIRTUALSPACE:
		{
			const bool virtualSpace = (nppParam.getSVP())._virtualSpace;

			int virtualSpaceOptions = SCVS_RECTANGULARSELECTION;
			if (virtualSpace)
				virtualSpaceOptions |= SCVS_USERACCESSIBLE | SCVS_NOWRAPLINESTART;

			_mainEditView->execute(SCI_SETVIRTUALSPACEOPTIONS, virtualSpaceOptions);
			_subEditView->execute(SCI_SETVIRTUALSPACEOPTIONS, virtualSpaceOptions);
			return true;
		}

		case NPPM_INTERNAL_SCROLLBEYONDLASTLINE:
		{
			const bool endAtLastLine = !(nppParam.getSVP())._scrollBeyondLastLine;
			_mainEditView->execute(SCI_SETENDATLASTLINE, endAtLastLine);
			_subEditView->execute(SCI_SETENDATLASTLINE, endAtLastLine);
			return true;
		}

		case NPPM_INTERNAL_CHANGESELECTTEXTFORGROUND:
		{
			StyleArray& stylers = nppParam.getMiscStylerArray();

			COLORREF selectColorFore = black;
			const Style* pStyle = stylers.findByName(L"Selected text colour");
			if (pStyle)
				selectColorFore = pStyle->_fgColor;

			for (ScintillaEditView* view : { _mainEditView, _subEditView })
			{
				if ((nppParam.getSVP())._selectedTextForegroundSingleColor)
				{
					view->setElementColour(SC_ELEMENT_SELECTION_TEXT, selectColorFore);
					view->setElementColour(SC_ELEMENT_SELECTION_INACTIVE_TEXT, selectColorFore);
					view->setElementColour(SC_ELEMENT_SELECTION_ADDITIONAL_TEXT, selectColorFore);
				}
				else
				{
					view->execute(SCI_RESETELEMENTCOLOUR, SC_ELEMENT_SELECTION_TEXT);
					view->execute(SCI_RESETELEMENTCOLOUR, SC_ELEMENT_SELECTION_INACTIVE_TEXT);
					view->execute(SCI_RESETELEMENTCOLOUR, SC_ELEMENT_SELECTION_ADDITIONAL_TEXT);
				}
			}

			if (_configStyleDlg && _configStyleDlg->isCreated())
				_configStyleDlg->syncWithSelFgSingleColorCtrl();
			return true;
		}

		case NPPM_INTERNAL_SETWORDCHARS:
		{
			_mainEditView->setWordChars();
			_subEditView->setWordChars();
			return true;
		}

		case NPPM_INTERNAL_SETNPC:
		{
			const auto& svp = nppParam.getSVP();
			const bool isFromIncCcUniEolCtrl = wParam == IDC_CHECK_NPC_INCLUDECCUNIEOL;
			if (isFromIncCcUniEolCtrl || svp._npcIncludeCcUniEol)
			{
				const bool isShown = svp._ccUniEolShow;
				_mainEditView->showCcUniEol(isShown);
				_subEditView->showCcUniEol(isShown);
			}

			if (!isFromIncCcUniEolCtrl)
			{
				const bool isShown = svp._npcShow;
				_mainEditView->showNpc(isShown);
				_subEditView->showNpc(isShown);
				_findReplaceDlg->updateFinderScintillaForNpc();
			}
			return true;
		}

		case NPPM_INTERNAL_SETMULTISELECTION:
		{
			const ScintillaViewParams& svp = nppParam.getSVP();
			_mainEditView->execute(SCI_SETMULTIPLESELECTION, svp._multiSelection);
			_subEditView->execute(SCI_SETMULTIPLESELECTION, svp._multiSelection);
			return true;
		}

		case NPPM_INTERNAL_SETCARETBLINKRATE:
		{
			const NppGUI& nppGUI = nppParam.getNppGUI();
			_mainEditView->execute(SCI_SETCARETPERIOD, nppGUI._caretBlinkRate);
			_subEditView->execute(SCI_SETCARETPERIOD, nppGUI._caretBlinkRate);
			return true;
		}

		case NPPM_INTERNAL_LINECUTCOPYWITHOUTSELECTION:
		{
			if (nppParam.getSVP()._lineCopyCutWithoutSelection) // "Disable Copy Cut Line Without Selection" is just unchecked: From NOT using it to using this feature
			{
				// Enable Copy & Cut unconditionally
				enableCommand(IDM_EDIT_CUT, true);
				enableCommand(IDM_EDIT_COPY, true);
			}
			else // "Disable Copy Cut Line Without Selection" is just checked: From using this feature to NOT using it
			{
				// Check the current selection to disable/enable Copy & Cut
				bool hasSelection = _pEditView->hasSelection();
				enableCommand(IDM_EDIT_CUT, hasSelection);
				enableCommand(IDM_EDIT_COPY, hasSelection);
			}
			return true;
		}

		case NPPM_INTERNAL_DISABLESELECTEDTEXTDRAGDROP:
		{
			_mainEditView->execute(SCI_SETDRAGDROPENABLED, !nppParam.getSVP()._disableSelectedTextDragDrop);
			_subEditView->execute(SCI_SETDRAGDROPENABLED, !nppParam.getSVP()._disableSelectedTextDragDrop);
			return true;
		}

		case NPPM_HIDETOOLBAR:
		{
			bool show = (lParam != TRUE);
			bool currentStatus = _toolBar && _toolBar->isVisible();
			if (_toolBar && show != currentStatus)
				_toolBar->setVisible(show);
			nppParam.getNppGUI()._toolbarShow = show;
			result = currentStatus;
			return true;
		}

		case NPPM_HIDEMENU:
		{
			bool hide = (lParam == TRUE);
			bool isHidden = !menuBar()->isVisible();
			result = isHidden;
			if (hide == isHidden)
				return true;

			NppGUI& nppGUI = nppParam.getNppGUI();
			nppGUI._menuBarShow = !hide;
			menuBar()->setVisible(nppGUI._menuBarShow);
			return true;
		}

		case NPPM_HIDESTATUSBAR:
		{
			bool show = (lParam != TRUE);
			NppGUI& nppGUI = nppParam.getNppGUI();
			bool oldVal = nppGUI._statusBarShow;
			result = oldVal;
			if (show == oldVal)
				return true;

			nppGUI._statusBarShow = show;
			statusBar()->setVisible(nppGUI._statusBarShow);
			return true;
		}

		case NPPM_INTERNAL_CLEARINDICATOR:
		{
			_pEditView->clearIndicator(SCE_UNIVERSAL_FOUND_STYLE_SMART);
			return true;
		}

		case NPPM_INTERNAL_CLEARINDICATORTAGMATCH:
		{
			_pEditView->clearIndicator(SCE_UNIVERSAL_TAGMATCH);
			_pEditView->clearIndicator(SCE_UNIVERSAL_TAGATTR);
			return true;
		}

		case NPPM_INTERNAL_CLEARINDICATORTAGATTR:
		{
			_pEditView->clearIndicator(SCE_UNIVERSAL_TAGATTR);
			return true;
		}

		case NPPM_INTERNAL_UPDATETITLEBAR:
		{
			setTitle();
			return true;
		}

		case NPPM_INTERNAL_CRLFFORMCHANGED:
		{
			_mainEditView->setCRLF();
			_subEditView->setCRLF();
			return true;
		}

		case NPPM_INTERNAL_NPCFORMCHANGED:
		{
			_mainEditView->setNpcAndCcUniEOL();
			_subEditView->setNpcAndCcUniEOL();

			const auto& svp = nppParam.getSVP();
			if (svp._npcShow)
				_findReplaceDlg->updateFinderScintillaForNpc(true);
			return true;
		}

		case NPPM_INTERNAL_ENABLECHANGEHISTORY:
		{
			static bool stopActionUntilNextSession = false;

			const ScintillaViewParams& svp = nppParam.getSVP();

			int enabledCHFlag = SC_CHANGE_HISTORY_DISABLED;
			if (svp._isChangeHistoryMarginEnabled || svp._isChangeHistoryIndicatorEnabled)
			{
				enabledCHFlag = SC_CHANGE_HISTORY_ENABLED;

				if (svp._isChangeHistoryMarginEnabled)
					enabledCHFlag |= SC_CHANGE_HISTORY_MARKERS;

				if (svp._isChangeHistoryIndicatorEnabled)
					enabledCHFlag |= SC_CHANGE_HISTORY_INDICATORS;
			}

			if (!stopActionUntilNextSession)
			{
				_mainEditView->execute(SCI_SETCHANGEHISTORY, enabledCHFlag);
				_subEditView->execute(SCI_SETCHANGEHISTORY, enabledCHFlag);

				if (enabledCHFlag == SC_CHANGE_HISTORY_DISABLED)
					stopActionUntilNextSession = true;

				_mainEditView->showChangeHistoryMargin(svp._isChangeHistoryMarginEnabled);
				_subEditView->showChangeHistoryMargin(svp._isChangeHistoryMarginEnabled);

				_mainEditView->viewport()->update();
				_subEditView->viewport()->update();

				const bool isEnabled = svp._isChangeHistoryMarginEnabled || svp._isChangeHistoryIndicatorEnabled;
				enableCommand(IDM_SEARCH_CHANGED_PREV, isEnabled);
				enableCommand(IDM_SEARCH_CHANGED_NEXT, isEnabled);
				enableCommand(IDM_SEARCH_CLEAR_CHANGE_HISTORY, isEnabled);
			}
			return true;
		}

		case NPPM_INTERNAL_CLEANBRACEMATCH:
		{
			_mainEditView->execute(SCI_SETHIGHLIGHTGUIDE, 0);
			_subEditView->execute(SCI_SETHIGHLIGHTGUIDE, 0);
			_mainEditView->execute(SCI_BRACEBADLIGHT, static_cast<uptr_t>(-1));
			_subEditView->execute(SCI_BRACEBADLIGHT, static_cast<uptr_t>(-1));
			return true;
		}

		case NPPM_INTERNAL_CLEANSMARTHILITING:
		{
			_mainEditView->clearIndicator(SCE_UNIVERSAL_FOUND_STYLE_SMART);
			_subEditView->clearIndicator(SCE_UNIVERSAL_FOUND_STYLE_SMART);
			return true;
		}

		case NPPM_INTERNAL_CRLFLAUNCHSTYLECONF:
		{
			// Launch _configStyleDlg (create or display it)
			command(IDM_LANGSTYLE_CONFIG_DLG);

			// go into the section we need
			_configStyleDlg->goToSection(L"Global Styles:EOL custom color");
			return true;
		}

		case NPPM_INTERNAL_NPCLAUNCHSTYLECONF:
		{
			// Launch _configStyleDlg (create or display it)
			command(IDM_LANGSTYLE_CONFIG_DLG);

			// go into the section we need
			wstring npcStr = L"Global Styles:";
			npcStr += g_npcStyleName;
			_configStyleDlg->goToSection(npcStr.c_str());
			return true;
		}

		case NPPM_INTERNAL_SETTING_HISTORY_SIZE:
		{
			_lastRecentFileList.setUserMaxNbLRF(nppParam.getNbMaxRecentFile());
			return true;
		}

		case NPPM_INTERNAL_EDGEMULTISETSIZE:
		{
			_mainEditView->execute(SCI_MULTIEDGECLEARALL);
			_subEditView->execute(SCI_MULTIEDGECLEARALL);

			const ScintillaViewParams& svp = nppParam.getSVP();

			COLORREF multiEdgeColor = liteGrey;
			const Style* pStyle = nppParam.getMiscStylerArray().findByName(L"Edge colour");
			if (pStyle)
				multiEdgeColor = pStyle->_fgColor;

			const size_t twoPower13 = 8192;
			size_t nbColAdded = 0;
			for (auto i : svp._edgeMultiColumnPos)
			{
				// it's absurd to set columns beyond 8000, even it's a long line.
				// So let's ignore all the number greater than 2^13
				if (i > twoPower13)
					continue;

				_mainEditView->execute(SCI_MULTIEDGEADDLINE, i, multiEdgeColor);
				_subEditView->execute(SCI_MULTIEDGEADDLINE, i, multiEdgeColor);

				++nbColAdded;
			}

			int mode;
			switch (nbColAdded)
			{
				case 0:
				{
					mode = EDGE_NONE;
					break;
				}
				case 1:
				{
					if (svp._isEdgeBgMode)
					{
						mode = EDGE_BACKGROUND;
						_mainEditView->execute(SCI_SETEDGECOLUMN, svp._edgeMultiColumnPos[0]);
						_subEditView->execute(SCI_SETEDGECOLUMN, svp._edgeMultiColumnPos[0]);
					}
					else
					{
						mode = EDGE_MULTILINE;
					}
					break;
				}
				default:
					mode = EDGE_MULTILINE;
			}

			_mainEditView->execute(SCI_SETEDGEMODE, mode);
			_subEditView->execute(SCI_SETEDGEMODE, mode);
			return true;
		}

		case NPPM_INTERNAL_SET_TAB_SETTINGS:
		{
			_pEditView->setTabSettings(_pEditView->getCurrentBuffer()->getCurrentLang());
			return true;
		}

		case NPPM_INTERNAL_RECENTFILELIST_UPDATE:
		{
			_lastRecentFileList.updateMenu();
			return true;
		}

		case NPPM_INTERNAL_RECENTFILELIST_SWITCH:
		{
			_lastRecentFileList.switchMode();
			_lastRecentFileList.updateMenu();
			return true;
		}

		case NPPM_INTERNAL_SETTING_TABCOMPACTLABELLEN:
		{
			// reflect current tab-label length setting change in both views
			_mainDocTab->refreshLabels();
			_subDocTab->refreshLabels();
			return true;
		}

		case NPPM_INTERNAL_UPDATECLICKABLELINKS:
		{
			if (wParam == 1)
			{
				removeAllHotSpot();
				return true;
			}

			ScintillaEditView* pView = reinterpret_cast<ScintillaEditView*>(wParam);

			int urlAction = nppParam.getNppGUI()._styleURL;
			if (urlAction != urlDisable)
			{
				if (pView == nullptr)
				{
					if (_pEditView->getCurrentBuffer()->allowClickableLink())
						addHotSpot(_pEditView);

					if (_pNonEditView->getCurrentBuffer()->allowClickableLink())
						addHotSpot(_pNonEditView);
				}
				else
				{
					if (pView->getCurrentBuffer()->allowClickableLink())
						addHotSpot(pView);
				}
			}
			return true;
		}

		case NPPM_INTERNAL_UPDATETEXTZONEPADDING:
		{
			const ScintillaViewParams& svp = nppParam.getSVP();
			if (_beforeSpecialView._isDistractionFree)
			{
				int paddingLen = svp.getDistractionFreePadding(_pEditView->width());
				_pEditView->execute(SCI_SETMARGINLEFT, 0, paddingLen);
				_pEditView->execute(SCI_SETMARGINRIGHT, 0, paddingLen);
			}
			else
			{
				_mainEditView->execute(SCI_SETMARGINLEFT, 0, svp._paddingLeft);
				_mainEditView->execute(SCI_SETMARGINRIGHT, 0, svp._paddingRight);
				_subEditView->execute(SCI_SETMARGINLEFT, 0, svp._paddingLeft);
				_subEditView->execute(SCI_SETMARGINRIGHT, 0, svp._paddingRight);
			}
			return true;
		}

		case NPPM_INTERNAL_REFRESHWORKDIR:
		{
			// upstream setWorkingDir(the folder of the current file)
			const NppGUI& nppGUI = nppParam.getNppGUI();
			if (nppGUI._openSaveDir == dir_userDef)
			{
				nppParam.setWorkingDir(nppGUI._defaultDirExp);
			}
			else if (nppGUI._openSaveDir == dir_followCurrent)
			{
				const Buffer* buf = _pEditView->getCurrentBuffer();
				wstring path = buf ? buf->getFullPathName() : L"";
				pathRemoveFileSpec(path);
				if (!path.empty() && doesDirectoryExist(path.c_str()))
					nppParam.setWorkingDir(path.c_str());
			}
			return true;
		}

		case NPPM_INTERNAL_HILITECURRENTLINE:
		{
			const ScintillaViewParams& svp = nppParam.getSVP();

			const COLORREF bgColour{ nppParam.getCurLineHilitingColour() };
			const sptr_t frameWidth{ (svp._currentLineHiliteMode == LINEHILITE_FRAME) ? svp._currentLineFrameWidth : 0 };

			for (ScintillaEditView* view : { _mainEditView, _subEditView })
			{
				if (svp._currentLineHiliteMode != LINEHILITE_NONE)
					view->setElementColour(SC_ELEMENT_CARET_LINE_BACK, bgColour);
				else
					view->execute(SCI_RESETELEMENTCOLOUR, SC_ELEMENT_CARET_LINE_BACK, 0);
				view->execute(SCI_SETCARETLINEFRAME, frameWidth);
			}
			return true;
		}

		case NPPM_INTERNAL_LINENUMBER:
		case NPPM_INTERNAL_SYMBOLMARGIN:
		{
			// NPLX: the margin follows the setting (upstream toggles it, which hides the line numbers when only the
			// dynamic / constant width is changed)
			const ScintillaViewParams& svp = nppParam.getSVP();
			const int margin = (message == NPPM_INTERNAL_LINENUMBER) ? ScintillaEditView::_SC_MARGE_LINENUMBER : ScintillaEditView::_SC_MARGE_SYMBOL;
			const bool show = (message == NPPM_INTERNAL_LINENUMBER) ? svp._lineNumberMarginShow : svp._bookMarkMarginShow;
			_mainEditView->showMargin(margin, show);
			_subEditView->showMargin(margin, show);
			return true;
		}

		case NPPM_INTERNAL_LWDEF:
		case NPPM_INTERNAL_LWALIGN:
		case NPPM_INTERNAL_LWINDENT:
		{
			int mode = (message == NPPM_INTERNAL_LWALIGN) ? SC_WRAPINDENT_SAME :
				(message == NPPM_INTERNAL_LWINDENT) ? SC_WRAPINDENT_INDENT : SC_WRAPINDENT_FIXED;
			_mainEditView->execute(SCI_SETWRAPINDENTMODE, mode);
			_subEditView->execute(SCI_SETWRAPINDENTMODE, mode);
			return true;
		}

		case NPPM_INTERNAL_FOLDSYMBOLSIMPLE:
		case NPPM_INTERNAL_FOLDSYMBOLARROW:
		case NPPM_INTERNAL_FOLDSYMBOLCIRCLE:
		case NPPM_INTERNAL_FOLDSYMBOLBOX:
		case NPPM_INTERNAL_FOLDSYMBOLNONE:
		{
			folderStyle fStyle =
				(message == NPPM_INTERNAL_FOLDSYMBOLSIMPLE) ? FOLDER_STYLE_SIMPLE :
				(message == NPPM_INTERNAL_FOLDSYMBOLARROW) ? FOLDER_STYLE_ARROW :
				(message == NPPM_INTERNAL_FOLDSYMBOLCIRCLE) ? FOLDER_STYLE_CIRCLE :
				(message == NPPM_INTERNAL_FOLDSYMBOLNONE) ? FOLDER_STYLE_NONE : FOLDER_STYLE_BOX;

			_mainEditView->setMakerStyle(fStyle);
			_subEditView->setMakerStyle(fStyle);
			return true;
		}

		case NPPM_INTERNAL_TOOLBARREDUCE:
		case NPPM_INTERNAL_TOOLBARENLARGE:
		case NPPM_INTERNAL_TOOLBARREDUCESET2:
		case NPPM_INTERNAL_TOOLBARENLARGESET2:
		case NPPM_INTERNAL_TOOLBARSTANDARD:
		{
			const toolBarStatusType state =
				(message == NPPM_INTERNAL_TOOLBARREDUCE) ? TB_SMALL :
				(message == NPPM_INTERNAL_TOOLBARENLARGE) ? TB_LARGE :
				(message == NPPM_INTERNAL_TOOLBARREDUCESET2) ? TB_SMALL2 :
				(message == NPPM_INTERNAL_TOOLBARENLARGESET2) ? TB_LARGE2 : TB_STANDARD;
			nppParam.getNppGUI()._tbIconInfo._tbIconSet = state;
			applyToolbarIconSet();
			return true;
		}

		case NPPM_INTERNAL_DRAWINACTIVETAB:
		case NPPM_INTERNAL_DRAWTABTOPBAR:
		case NPPM_INTERNAL_VERTICALTABBAR:
		case NPPM_INTERNAL_MULTILINETABBAR:
		case NPPM_INTERNAL_REDUCETABBAR:
		case NPPM_INTERNAL_DRAWTABBARCLOSEBUTTON:
		case NPPM_INTERNAL_DRAWTABBARPINBUTTON:
		case NPPM_INTERNAL_REFRESHTABBAR:
		{
			// upstream TabBarPlus::triggerOwnerDrawTabbar, doVertical, doMultiLine, the tab sizes and buttons
			_mainDocTab->applyTabStatus();
			_subDocTab->applyTabStatus();
			return true;
		}

		case NPPM_INTERNAL_HIDEMENURIGHTSHORTCUTS:
		{
			// NPLX: the "＋ ▼ ✕" menu bar items are not created (see buildMainMenu)
			return true;
		}

		case NPPM_INTERNAL_SQLBACKSLASHESCAPE:
		{
			// Go through all open files, and if there are any SQL files open, make sure the sql.backslash.escapes propery
			// is updated for each of the SQL buffers' Scintilla wrapper.
			// This message will only be called on the rare circumstance when the backslash-is-escape-for-sql preference is toggled, so this loop won't be run very often.
			const bool kbBackSlash = nppParam.getNppGUI()._backSlashIsEscapeCharacterForSql;
			Document oldDoc = _invisibleEditView->execute(SCI_GETDOCPOINTER);
			Buffer* oldBuf = _invisibleEditView->getCurrentBuffer();

			DocTabView* pTab[2] = { _mainDocTab, _subDocTab };
			ScintillaEditView* pView[2] = { _mainEditView, _subEditView };

			for (size_t v = 0; v < 2; ++v)
			{
				for (size_t i = 0, len = pTab[v]->nbItem(); i < len; ++i)
				{
					Buffer* pBuf = MainFileManager.getBufferByID(pTab[v]->getBufferByIndex(i));

					if (pBuf->getLangType() == L_SQL)
					{
						_invisibleEditView->execute(SCI_SETDOCPOINTER, 0, pBuf->getDocument());
						_invisibleEditView->setCurrentBuffer(pBuf);

						_invisibleEditView->execute(SCI_SETPROPERTY, reinterpret_cast<uptr_t>("sql.backslash.escapes"), reinterpret_cast<sptr_t>(kbBackSlash ? "1" : "0"));

						if (pBuf == pView[v]->getCurrentBuffer())
							pView[v]->defineDocType(L_SQL);
					}
				}
			}
			_invisibleEditView->execute(SCI_SETDOCPOINTER, 0, oldDoc);
			_invisibleEditView->setCurrentBuffer(oldBuf);
			return true;
		}

		case NPPM_INTERNAL_INCSEARCHBARPOSITION:
		{
			placeIncrementalSearchBar();
			return true;
		}

		case NPPM_INTERNAL_NPLX_SHOWLANGMENUITEM:
		{
			showLanguageMenuItem(static_cast<int>(wParam), lParam != FALSE);
			return true;
		}

		default:
			return false;
	}
}
