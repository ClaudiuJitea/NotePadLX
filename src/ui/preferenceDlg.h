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

// NPLX: port of upstream WinControls/Preference/preferenceDlg. Every page is an RcDialog built from its upstream
// IDD_PREFERENCE_SUB_* resource (RcPreference.inc), and its run_dlgProc becomes initDialog() (WM_INITDIALOG),
// onCommand() (WM_COMMAND) and onSliderChanged() (WM_HSCROLL). Messages to the main window go through the same
// ParentMessenger as the Find dialog and are answered by Notepad_plus::nppMessage.
// Not ported: the File Association page (Windows registry, PROMPT.md §6: .desktop MIME types instead).

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ParentMessenger.h"
#include "Parameters.h"
#include "RcDialog.h"

class ColourPicker;
class QListWidget;
class QMenu;

// the common base of the pages (upstream: StaticDialog children of the Preferences box)
class PreferenceSubDlg : public RcDialog
{
	Q_OBJECT

public:
	PreferenceSubDlg(QWidget* parent, ParentMessenger sendToParent);

	// upstream create(IDD_PREFERENCE_SUB_*): builds the page and runs WM_INITDIALOG
	void create(const RcDialogDef& def);

protected:
	// upstream WM_INITDIALOG
	virtual void initDialog() = 0;

	// upstream ::SendMessage(::GetParent(_hParent), message, wParam, lParam)
	intptr_t sendToNpp(unsigned int message, uptr_t wParam = 0, sptr_t lParam = 0) const
	{
		return _sendToParent ? _sendToParent(message, wParam, lParam) : 0;
	}

	void setItemInt(int id, int value) const;            // SetDlgItemInt
	unsigned int getItemInt(int id) const;               // GetDlgItemInt (0 when empty or not a number)
	void setTip(int id, const std::wstring& tip) const;   // upstream createToolTip on a control
	void redrawDlgItem(int id) const;
	ParentMessenger _sendToParent;
};

class GeneralSubDlg : public PreferenceSubDlg
{
	Q_OBJECT
public:
	using PreferenceSubDlg::PreferenceSubDlg;
protected:
	void initDialog() override;
	void onCommand(int id, int notification) override;
};

class ToolbarSubDlg : public PreferenceSubDlg
{
	Q_OBJECT
	friend class PreferenceDlg;
public:
	using PreferenceSubDlg::PreferenceSubDlg;
protected:
	void initDialog() override;
	void onCommand(int id, int notification) override;
private:
	unsigned int getToolbarIconSetMsg(int* iconSetID);
	void changeToolbarColorableState(); // upstream NPPM_INTERNAL_CHANGETOOLBARCOLORABLESTATE
	void enableIconColorPicker(bool enable);
	ColourPicker* _pIconColorPicker = nullptr;
};

class TabbarSubDlg : public PreferenceSubDlg
{
	Q_OBJECT
	friend class PreferenceDlg;
public:
	using PreferenceSubDlg::PreferenceSubDlg;
	void setTabbarAlternateIcons(bool enable = false);
protected:
	void initDialog() override;
	void onCommand(int id, int notification) override;
};

class EditingSubDlg : public PreferenceSubDlg
{
	Q_OBJECT
public:
	using PreferenceSubDlg::PreferenceSubDlg;
protected:
	void initDialog() override;
	void onCommand(int id, int notification) override;
	void onSliderChanged(int id, int value) override;
private:
	void initScintParam();
	void changeLineHiliteMode(bool enableSlider);
};

class Editing2SubDlg : public PreferenceSubDlg
{
	Q_OBJECT
public:
	using PreferenceSubDlg::PreferenceSubDlg;
protected:
	void initDialog() override;
	void onCommand(int id, int notification) override;
};

class DarkModeSubDlg : public PreferenceSubDlg
{
	Q_OBJECT
	friend class PreferenceDlg;
public:
	using PreferenceSubDlg::PreferenceSubDlg;
protected:
	void initDialog() override;
	void onCommand(int id, int notification) override;
private:
	void enableCustomizedColorCtrls(bool doEnable);
	void colourPicked(ColourPicker* picker);
	void applyChange(bool forceRefresh, bool doEnableCustomizedColorCtrls);
	std::vector<std::pair<ColourPicker*, COLORREF NppDarkMode::Colors::*>> _colorPickers;
	QMenu* _resetPopupMenu = nullptr;
};

class MarginsBorderEdgeSubDlg : public PreferenceSubDlg
{
	Q_OBJECT
public:
	using PreferenceSubDlg::PreferenceSubDlg;
protected:
	void initDialog() override;
	void onCommand(int id, int notification) override;
	void onSliderChanged(int id, int value) override;
	bool eventFilter(QObject* watched, QEvent* event) override; // upstream EditNumSpaceProc
private:
	void initScintParam();
};

class NewDocumentSubDlg : public PreferenceSubDlg
{
	Q_OBJECT
public:
	using PreferenceSubDlg::PreferenceSubDlg;
protected:
	void initDialog() override;
	void onCommand(int id, int notification) override;
private:
	void makeOpenAnsiAsUtf8(bool doIt) const;
};

class DefaultDirectorySubDlg : public PreferenceSubDlg
{
	Q_OBJECT
public:
	using PreferenceSubDlg::PreferenceSubDlg;
protected:
	void initDialog() override;
	void onCommand(int id, int notification) override;
};

class RecentFilesHistorySubDlg : public PreferenceSubDlg
{
	Q_OBJECT
public:
	using PreferenceSubDlg::PreferenceSubDlg;
protected:
	void initDialog() override;
	void onCommand(int id, int notification) override;
private:
	void setCustomLen(int val);
};

class LanguageSubDlg : public PreferenceSubDlg
{
	Q_OBJECT
public:
	using PreferenceSubDlg::PreferenceSubDlg;
protected:
	void initDialog() override;
	void onCommand(int id, int notification) override;
private:
	std::vector<LangMenuItem> _langList;
};

class IndentationSubDlg : public PreferenceSubDlg
{
	Q_OBJECT
public:
	using PreferenceSubDlg::PreferenceSubDlg;
protected:
	void initDialog() override;
	void onCommand(int id, int notification) override;
private:
	Lang* selectedLang() const; // nullptr for [Default]
};

class HighlightingSubDlg : public PreferenceSubDlg
{
	Q_OBJECT
public:
	using PreferenceSubDlg::PreferenceSubDlg;
protected:
	void initDialog() override;
	void onCommand(int id, int notification) override;
};

class PrintSubDlg : public PreferenceSubDlg
{
	Q_OBJECT
public:
	using PreferenceSubDlg::PreferenceSubDlg;
protected:
	void initDialog() override;
	void onCommand(int id, int notification) override;
	bool eventFilter(QObject* watched, QEvent* event) override; // upstream EN_SETFOCUS
private:
	void editFocused(int id);
	std::vector<std::wstring> varList;
	int _focusedEditCtrl = 0;
};

class SearchingSubDlg : public PreferenceSubDlg
{
	Q_OBJECT
public:
	using PreferenceSubDlg::PreferenceSubDlg;
protected:
	void initDialog() override;
	void onCommand(int id, int notification) override;
};

class BackupSubDlg : public PreferenceSubDlg
{
	Q_OBJECT
public:
	using PreferenceSubDlg::PreferenceSubDlg;
protected:
	void initDialog() override;
	void onCommand(int id, int notification) override;
private:
	void updateBackupSessionGUI();
	void updateBackupOnSaveGUI();
};

class AutoCompletionSubDlg : public PreferenceSubDlg
{
	Q_OBJECT
public:
	using PreferenceSubDlg::PreferenceSubDlg;
protected:
	void initDialog() override;
	void onCommand(int id, int notification) override;
	void onSliderChanged(int id, int value) override;
private:
	void enableAutoCompletionCtrls(bool isEnableAutoC);
};

class MultiInstanceSubDlg : public PreferenceSubDlg
{
	Q_OBJECT
public:
	using PreferenceSubDlg::PreferenceSubDlg;
protected:
	void initDialog() override;
	void onCommand(int id, int notification) override;
};

class DelimiterSubDlg : public PreferenceSubDlg
{
	Q_OBJECT
public:
	using PreferenceSubDlg::PreferenceSubDlg;
protected:
	void initDialog() override;
	void onCommand(int id, int notification) override;
private:
	void detectSpace(const char* text2Check, int& nbSp, int& nbTab) const;
	std::wstring getWarningText(size_t nbSp, size_t nbTab) const;
	void setWarningIfNeed() const;
	void setCtrlsPos(bool isMultiline);
	QRect _closeEditGeometry;
	QRect _closeTextGeometry;
	int _secondLineShift = 0;
};

class CloudAndLinkSubDlg : public PreferenceSubDlg
{
	Q_OBJECT
public:
	using PreferenceSubDlg::PreferenceSubDlg;
protected:
	void initDialog() override;
	void onCommand(int id, int notification) override;
};

class SearchEngineSubDlg : public PreferenceSubDlg
{
	Q_OBJECT
public:
	using PreferenceSubDlg::PreferenceSubDlg;
protected:
	void initDialog() override;
	void onCommand(int id, int notification) override;
};

class PerformanceSubDlg : public PreferenceSubDlg
{
	Q_OBJECT
public:
	using PreferenceSubDlg::PreferenceSubDlg;
protected:
	void initDialog() override;
	void onCommand(int id, int notification) override;
private:
	void enableRestrictionCtrls(bool enable);
};

class MiscSubDlg : public PreferenceSubDlg
{
	Q_OBJECT
	friend class PreferenceDlg;
public:
	using PreferenceSubDlg::PreferenceSubDlg;
protected:
	void initDialog() override;
	void onCommand(int id, int notification) override;
private:
	void updateFileAutoDetection();
};

class PreferenceDlg : public RcDialog
{
	Q_OBJECT

public:
	explicit PreferenceDlg(QWidget* parent = nullptr);
	~PreferenceDlg() override;

	void init(ParentMessenger sendToParent) { _sendToParent = std::move(sendToParent); }
	void doDialog(bool isRTL = false);

	bool renameDialogTitle(const wchar_t* internalName, const wchar_t* newName);
	int getListSelectedIndex() const;
	void showDialogByName(const wchar_t* name);
	bool setListSelection(size_t currentSel) const;
	bool goToSection(size_t iPage, intptr_t ctrlID = -1);

	// NPLX: the pages, for Notepad_plus (upstream friend access) and the tests
	size_t nbPages() const { return _wVector.size(); }
	RcDialog* page(size_t index) const { return index < _wVector.size() ? _wVector[index]._dlg : nullptr; }
	RcDialog* pageByName(const wchar_t* internalName) const;
	TabbarSubDlg* tabbarSubDlg() const { return _tabbarSubDlg; }
	ToolbarSubDlg* toolbarSubDlg() const { return _toolbarSubDlg; }
	DarkModeSubDlg* darkModeSubDlg() const { return _darkModeSubDlg; }
	MiscSubDlg* miscSubDlg() const { return _miscSubDlg; }

	// upstream NPPM_INTERNAL_SETTOOLICONSSET (sent by the Dark Mode page): the toolbar page shows the icon set of the
	// chosen mode
	void setToolIconsSet(bool useDark);

protected:
	void onCommand(int id, int notification) override;
	void reject() override;

private:
	struct DlgInfo
	{
		PreferenceSubDlg* _dlg = nullptr;
		std::wstring _name;
		std::wstring _internalName;
	};

	void create();
	void makeCategoryList();
	int getIndexFromName(const wchar_t* name) const;
	void showDialogByIndex(size_t index) const;
	template <class T>
	T* addPage(const RcDialogDef& def, const wchar_t* name, const wchar_t* internalName);

	ParentMessenger _sendToParent;
	std::vector<DlgInfo> _wVector;
	TabbarSubDlg* _tabbarSubDlg = nullptr;
	ToolbarSubDlg* _toolbarSubDlg = nullptr;
	DarkModeSubDlg* _darkModeSubDlg = nullptr;
	MiscSubDlg* _miscSubDlg = nullptr;
};
