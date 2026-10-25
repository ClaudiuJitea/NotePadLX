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

// NPLX: port of upstream WinControls/ColourPicker/WordStyleDlg (the Style Configurator) on QDialog.
// Controls are kept by their upstream IDs (WordStyleDlgRes.h); run_dlgProc becomes command(id).
// Messages to the main window (WM_UPDATESCINTILLAS...) go through the same ParentMessenger as the Find dialog.

#pragma once

#include <map>
#include <string>
#include <utility>

#include <QDialog>

#include "FindReplaceDlg.h" // ParentMessenger
#include "Parameters.h"

class ColourPicker;
class QComboBox;
class QLabel;
class QListWidget;

#define WM_UPDATESCINTILLAS      (WORDSTYLE_USER + 1) // WM_UPDATESCINTILLAS (BOOL doChangePanel, 0)
#define WM_UPDATEMAINMENUBITMAPS (WORDSTYLE_USER + 2)

enum fontStyleType {BOLD_STATUS, ITALIC_STATUS, UNDERLINE_STATUS};

class WordStyleDlg : public QDialog
{
	Q_OBJECT

public :
	explicit WordStyleDlg(QWidget* parent = nullptr);

	void init(ParentMessenger sendToParent) { _sendToParent = std::move(sendToParent); }

	bool isCreated() const { return _isCreated; }
	void create(); // NPLX: builds the Qt widgets (upstream: CreateDialog + WM_INITDIALOG)
	void doDialog(bool isRTL = false);
	void prepare2Cancel();
	void restoreGlobalOverrideValues() const;
	void addLastThemeEntry() const;
	bool selectThemeByName(const wchar_t* themeName);
	void syncWithSelFgSingleColorCtrl();
	bool goToSection(const wchar_t* sectionNames); // sectionNames is formed as following: "Language name:Style name"
	                                               // ex: "Global Styles:EOL custom color" will set Language on "Global Styles", then set Style on "EOL custom color" if both are found.

	// NPLX: upstream WM_COMMAND, and access to the controls for tests
	void command(int id);
	QWidget* dlgItem(int id) const;
	ColourPicker* fgColourPicker() const { return _pFgColour; }
	ColourPicker* bgColourPicker() const { return _pBgColour; }

protected:
	void reject() override; // Esc (upstream IDCANCEL)

private :
	ParentMessenger _sendToParent;
	bool _isCreated = false;
	std::map<int, QWidget*> _ctrls; // NPLX: upstream ::GetDlgItem(_hSelf, id)

	ColourPicker* _pFgColour = nullptr;
	ColourPicker* _pBgColour = nullptr;

	int _currentLexerIndex = 0;
	int _currentThemeIndex = 0;

	QComboBox* _hFontNameCombo = nullptr;
	QComboBox* _hFontSizeCombo = nullptr;
	QComboBox* _hSwitch2ThemeCombo = nullptr;
	QLabel* _hStyleInfoStaticText = nullptr;

	LexerStylerArray _lsArray;
	StyleArray _globalStyles;
	std::wstring _themeName;

	LexerStylerArray _styles2restored;
	StyleArray _gstyles2restored;
	GlobalOverride _gOverride2restored;
	bool _restoreInvalid = false;

	bool _isDirty = false;
	bool _isThemeDirty = false;
	bool _isShownGOCtrls = false;
	bool _isThemeChanged = false;
	bool _isUpdatingUi = false; // NPLX: Qt signals fire for programmatic changes (Win32 WM_SETTEXT → EN_CHANGE too)

	bool isCheckedOrNot(int id) const;
	void setChecked(int id, bool checked) const;
	void showItem(int id, bool show) const;
	void enableItem(int id, bool enable) const;
	QComboBox* languagesCombo() const;
	QListWidget* stylesList() const;

	std::pair<intptr_t, intptr_t> goToPreferencesSettings();

	Style& getCurrentStyler();
	bool getStyleName(std::wstring& styleName, const size_t styleNameLenLimit = 128) const;

	int whichTabColourIndex() const;
	int whichIndividualTabColourId();
	int whichFindDlgStatusMsgColourIndex();
	void apply(int applicationInfo);
	int getApplicationInfo() const;
	bool isDocumentMapStyle();
	void updateColour(bool which);
	void updateFontStyleStatus(fontStyleType whitchStyle);
	void updateExtension();
	void updateFontName();
	void updateFontSize();
	void updateUserKeywords();
	void switchToTheme();
	static void updateThemeName(const std::wstring& themeName);

	void loadLangListFromNppParam();

	void enableFontStyle(bool isEnable) const;
	long notifyDataModified();
	void setStyleListFromLexer(int index);
	void setVisualFromStyleList();
	void updateGlobalOverrideCtrls();
	void showGlobalOverrideCtrls(bool show);
	void applyCurrentSelectedThemeAndUpdateUI();
	void colourPicked(ColourPicker* picker); // upstream CPN_COLOURPICKED
};
