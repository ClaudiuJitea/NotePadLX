// This file is part of NotePadLX, a Linux port of Notepad++.
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

// NPLX: builds a dialog from an upstream DIALOGEX resource (generated into RcDialogs.inc by tools/gen_rcdialogs.py):
// same controls, IDs and positions (dialog units converted with the font metrics, like Win32). The helpers mirror the
// Win32 calls upstream dialogs make (::GetDlgItem, BM_GETCHECK, WM_SETTEXT, EnableWindow...) and the controls report
// their notifications through onCommand(id, notification), like WM_COMMAND, so upstream run_dlgProc code ports with
// few changes.

#pragma once

#include <map>
#include <string>

#include <QDialog>

class QComboBox;
class QListWidget;
class QSlider;

enum RcKind { RcLabel, RcButton, RcGroup, RcEdit, RcCombo, RcList, RcSlider, RcSpin, RcTab, RcListView, RcTree, RcOther };

enum RcStyle : unsigned
{
	RcCheckBox = 1u << 0, RcRadio = 1u << 1, RcTriState = 1u << 2, RcGroupBox = 1u << 3, RcDefault = 1u << 4,
	RcSplitButton = 1u << 5, RcPushLike = 1u << 6, RcOwnerDraw = 1u << 7, RcMultiLine = 1u << 8, RcReadOnly = 1u << 9,
	RcNumber = 1u << 10, RcPassword = 1u << 11, RcCenter = 1u << 12, RcRight = 1u << 13, RcWantReturn = 1u << 14,
	RcVCenter = 1u << 15, RcNotify = 1u << 16, RcSunken = 1u << 17, RcEtchedLine = 1u << 18, RcDropDownList = 1u << 19,
	RcDropDown = 1u << 20, RcSort = 1u << 21, RcVScroll = 1u << 22, RcHScroll = 1u << 23, RcDisabled = 1u << 24,
	RcVertical = 1u << 25, RcReport = 1u << 26, RcLeftText = 1u << 27, RcHidden = 1u << 28
};

struct RcControlDef
{
	RcKind _kind;
	const wchar_t* _text;
	int _id;
	int _x, _y, _w, _h; // dialog units
	unsigned _style;
	const char* _className;
};

struct RcDialogDef
{
	const char* _name;
	int _w, _h; // dialog units
	const wchar_t* _caption;
	const RcControlDef* _controls;
	int _count;
};

// Win32 notification codes passed to onCommand (HIWORD(wParam) of WM_COMMAND)
inline constexpr int RcNotifyClicked = 0;        // BN_CLICKED
inline constexpr int RcNotifySelChange = 1;      // CBN_SELCHANGE == LBN_SELCHANGE
inline constexpr int RcNotifyDblClick = 2;       // LBN_DBLCLK
inline constexpr int RcNotifyEditChange = 0x300; // EN_CHANGE
inline constexpr int RcNotifyKillFocus = 0x200;  // EN_KILLFOCUS

class RcDialog : public QDialog
{
	Q_OBJECT

public:
	// asChild: a page embedded in another dialog (upstream child dialog of a tab control)
	explicit RcDialog(QWidget* parent = nullptr, bool asChild = false);

	void buildFrom(const RcDialogDef& def);
	bool isCreated() const { return _isCreated; }

	// --- Win32-like access to the controls, by upstream ID
	QWidget* dlgItem(int id) const;                     // ::GetDlgItem
	bool isCheckedOrNot(int id) const;                  // BM_GETCHECK == BST_CHECKED
	void setChecked(int id, bool checked = true) const; // BM_SETCHECK (no notification)
	std::wstring getItemText(int id) const;             // GetDlgItemText / WM_GETTEXT
	void setItemText(int id, const std::wstring& text) const; // SetDlgItemText / WM_SETTEXT (no notification)
	void enableItem(int id, bool enable = true) const;  // EnableWindow
	void showItem(int id, bool show = true) const;      // ShowWindow
	QComboBox* comboBox(int id) const;
	QListWidget* listBox(int id) const;
	QSlider* slider(int id) const;

	// dialog units -> pixels (MapDialogRect)
	QRect mapDialogRect(int x, int y, int w, int h) const;

	// NPLX: command(id) simulates a click / notification, for tests and for upstream SendMessage(WM_COMMAND, id)
	void command(int id, int notification = RcNotifyClicked) { onCommand(id, notification); }

protected:
	// upstream WM_COMMAND: LOWORD(wParam) == id, HIWORD(wParam) == notification
	virtual void onCommand(int /*id*/, int /*notification*/) {}
	// upstream WM_HSCROLL from a trackbar
	virtual void onSliderChanged(int /*id*/, int /*value*/) {}

	bool _isCreated = false;
	bool _isUpdatingControls = false; // set while code changes the controls, to mute their notifications

private:
	std::map<int, QWidget*> _ctrls;
	qreal _baseUnitX = 6; // NPLX: fractional, the integer division made the dialogs too small for their text
	qreal _baseUnitY = 13;
	int _padding = 0;     // NPLX: an outer margin around the .rc layout of a top-level dialog
};
