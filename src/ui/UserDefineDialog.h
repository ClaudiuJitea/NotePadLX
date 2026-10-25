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

// NPLX: port of upstream ScintillaComponent/UserDefineDialog (the User Defined Language dialog). The dialogs are
// built from the upstream UserDefineDialog.rc (RcDialog / RcDialogs.inc); run_dlgProc handlers become onCommand.
// The Language menu is updated through a callback set by Notepad_plus (upstream: InsertMenu / ModifyMenu /
// RemoveMenu on the menu it gets with NPPM_INTERNAL_GETMENU).

#pragma once

#include <functional>
#include <memory>
#include <string>

#include "RcDialog.h"
#include "Parameters.h"
#include "UdlMappers.h"

class ColourPicker;
class QTabBar;
class ScintillaEditView;

inline constexpr bool DOCK = true;
inline constexpr bool UNDOCK = false;

class SharedParametersDialog : public RcDialog
{
friend class StylerDlg;
public:
	explicit SharedParametersDialog(QWidget* parent = nullptr, bool asChild = true) : RcDialog(parent, asChild) {}
	virtual void updateDlg() = 0;

	// NPLX: the UDL being edited and the view to restyle (upstream: static members, same here)
	static UserLangContainer* currentUserLang() { return _pUserLang; }

protected:
	//Shared data
	static std::wstring _kwlBuffer;
	static UserLangContainer *_pUserLang;
	static ScintillaEditView *_pScintilla;

	void onCommand(int id, int notification) override; // upstream SharedParametersDialog::run_dlgProc
	bool setPropertyByCheck(int id, bool& bool2set);
	virtual void setKeywords2List(int ctrlID) = 0;

	size_t getTextIntoBuffer(int ctrlID); // NPLX: ::GetDlgItemText(_hSelf, id, _kwlBuffer.data(), max_char)
	void openStyler(int stylerIndex, int enabledNesters = -1); // NPLX: StylerDlg stylerDlg(...); stylerDlg.doDialog();
};

class FolderStyleDialog : public SharedParametersDialog
{
public:
	explicit FolderStyleDialog(QWidget* parent = nullptr);
	void updateDlg() override;
protected :
	void onCommand(int id, int notification) override;
	void setKeywords2List(int ctrlID) override;
private :
	static void retrieve(wchar_t* dest, size_t destMaxLen, const wchar_t* toRetrieve, const wchar_t* prefix);
};

class KeyWordsStyleDialog : public SharedParametersDialog
{
public:
	explicit KeyWordsStyleDialog(QWidget* parent = nullptr);
	void updateDlg() override;
protected :
	void onCommand(int id, int notification) override;
	void setKeywords2List(int id) override;
};

class CommentStyleDialog : public SharedParametersDialog
{
public :
	explicit CommentStyleDialog(QWidget* parent = nullptr);
	void updateDlg() override;
protected :
	void onCommand(int id, int notification) override;
	void setKeywords2List(int id) override;
	void showEvent(QShowEvent* event) override; // upstream WM_SHOWWINDOW
private :
	static void retrieve(wchar_t* dest, size_t destMaxLen, const wchar_t* toRetrieve, const wchar_t* prefix);
};

class SymbolsStyleDialog : public SharedParametersDialog
{
public :
	explicit SymbolsStyleDialog(QWidget* parent = nullptr);
	void updateDlg() override;
protected :
	void onCommand(int id, int notification) override;
	void setKeywords2List(int id) override;
private :
	static void retrieve(wchar_t* dest, size_t destMaxLen, const wchar_t* toRetrieve, const wchar_t* prefix);
};

class UserDefineDialog : public SharedParametersDialog
{
public :
	// NPLX: upstream changes the Language menu directly; Notepad_plus provides these
	struct MenuHandler
	{
		std::function<void(int cmdID, const wchar_t* name)> _insert;  // InsertMenu (before the last item)
		std::function<void(int cmdID, const wchar_t* name)> _rename;  // ModifyMenu
		std::function<void(int cmdID)> _remove;                      // RemoveMenu
		std::function<void(const wchar_t* newName, const wchar_t* oldName)> _renamed; // WM_RENAME_USERLANG
		std::function<void(const wchar_t* name)> _removed;           // WM_REMOVE_USERLANG
		std::function<void()> _closed;                               // WM_CLOSE_USERDEFINE_DLG
		std::function<void()> _dock;                                 // WM_DOCK_USERDEFINE_DLG
		std::function<void()> _undock;                               // WM_UNDOCK_USERDEFINE_DLG
	};

	explicit UserDefineDialog(QWidget* parent = nullptr);
	~UserDefineDialog() override;

	void init(ScintillaEditView *pSev, MenuHandler menuHandler) {
		if (!_pScintilla)
			_pScintilla = pSev;
		_menuHandler = std::move(menuHandler);
	}
	static void setScintilla(ScintillaEditView* pScinView) {
		_pScintilla = pScinView;
	}

	void doDialog(bool willBeShown = true, bool isRTL = false);
	void reloadLangCombo();
	bool isDocked() const { return _status == DOCK; }
	void changeStyle(); // upstream: the dock state and the Dock / Undock button label

	// NPLX: tests
	FolderStyleDialog* folderStyleDlg() const { return _folderStyleDlg; }
	KeyWordsStyleDialog* keyWordsStyleDlg() const { return _keyWordsStyleDlg; }
	CommentStyleDialog* commentStyleDlg() const { return _commentStyleDlg; }
	SymbolsStyleDialog* symbolsStyleDlg() const { return _symbolsStyleDlg; }
	QTabBar* tabBar() const { return _ctrlTab; }
	void setNextNameAnswer(const std::wstring& name) { _nextNameAnswer = name; _hasNextNameAnswer = true; }

protected :
	void onCommand(int id, int notification) override;
	void onSliderChanged(int id, int value) override;
	void reject() override { onCommand(IDCANCEL_UDL, RcNotifyClicked); }

private :
	static constexpr int IDCANCEL_UDL = 2; // IDCANCEL

	QTabBar* _ctrlTab = nullptr;
	std::unique_ptr<UserLangContainer> _pCurrentUserLang;
	FolderStyleDialog*   _folderStyleDlg = nullptr;
	KeyWordsStyleDialog* _keyWordsStyleDlg = nullptr;
	CommentStyleDialog*  _commentStyleDlg = nullptr;
	SymbolsStyleDialog*  _symbolsStyleDlg = nullptr;
	bool _status = UNDOCK;
	MenuHandler _menuHandler;

	std::wstring _nextNameAnswer; // NPLX: tests answer the name dialog
	bool _hasNextNameAnswer = false;

	void create(); // upstream WM_INITDIALOG
	void enableLangAndControlsBy(size_t index);
	bool askName(const std::wstring& title, const std::wstring& label, const std::wstring& initial, std::wstring& result);

protected :
	void setKeywords2List(int) override {}
	void updateDlg() override;
};

class StringDlg : public RcDialog
{
public :
	explicit StringDlg(QWidget* parent = nullptr);
	void init(const wchar_t *title, const wchar_t *staticName, const wchar_t *text2Set, int txtLen = 0, const wchar_t* restrictedChars = nullptr, bool bGotoCenter = false);

	// upstream returns the text pointer, or 0 when cancelled
	bool doDialog(std::wstring& result);

protected :
	void onCommand(int id, int notification) override;
	bool isAllowed(const std::wstring& txt);

private :
	std::wstring _title;
	std::wstring _textValue;
	std::wstring _static;
	std::wstring _restrictedChars;
	int _txtLen = 0;
	bool _shouldGotoCenter = false;
};

class StylerDlg : public RcDialog
{
public:
	StylerDlg(QWidget* parent, int stylerIndex = 0, int enabledNesters = -1);
	~StylerDlg() override;

	int doDialog();

protected:
	void onCommand(int id, int notification) override;
	void reject() override; // upstream WM_CLOSE / IDCANCEL

private:
	int _stylerIndex = 0;
	int _enabledNesters = 0;
	ColourPicker* _pFgColour = nullptr;
	ColourPicker* _pBgColour = nullptr;
	Style _initialStyle;

	void initDialog(); // upstream WM_INITDIALOG
	void colourPicked(int id); // upstream CPN_COLOURPICKED (== BN_CLICKED) part of WM_COMMAND
	void move2CtrlRight(int ctrlID, QWidget* handle2Move, int handle2MoveWidth, int handle2MoveHeight);
};
