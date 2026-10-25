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

// NPLX: port of upstream WinControls/FunctionList/functionListPanel. The parser (src/editor/functionParser) is
// upstream code; the panel is a DockingPanel with a search field, Sort / Reload / Preferences buttons and two
// TreeViews (the function tree and the search results), like upstream.

#pragma once

#include <string>
#include <vector>

#include <QColor>

#include "DockingPanel.h"
#include "TreeView.h"
#include "functionParser.h"

class QLineEdit;
class QMenu;
class QToolButton;
class ScintillaEditView;

#define FL_PANELTITLE     L"Function List"
#define FL_FUNCTIONLISTROOTNODE "FunctionList"

#define FL_SORTLOCALNODENAME        "SortTip"
#define FL_RELOADLOCALNODENAME      "ReloadTip"
#define FL_PREFERENCESLOCALNODENAME "PreferencesTip"

#define FL_PREFERENCE_INITIALSORT "PreferencesInitialSort"

struct SearchParameters {
	std::wstring _text2Find;
	bool _doSort = false;

	bool hasParams() const {
		return (_text2Find != L"" || _doSort);
	}
};

struct TreeParams {
	TreeStateNode _treeState;
	SearchParameters _searchParameters;
};

class FunctionListPanel : public DockingPanel
{
	Q_OBJECT

public:
	explicit FunctionListPanel(QWidget* parent = nullptr);

	void init(ScintillaEditView** ppEditView);

	void setBackgroundColor(COLORREF bgColour);
	void setForegroundColor(COLORREF fgColour);

	// functionalities
	static int categorySortFunc(const std::wstring& posString1, const std::wstring& posString2);
	void sortOrUnsort();
	void reload();
	void markEntry();
	bool serialize(const std::wstring& outputFilename = L"");
	void addEntry(const wchar_t* nodeName, const wchar_t* displayText, size_t pos);
	void removeAllEntries();
	void searchFuncAndSwitchView();
	void grabFocus() override;

	// NPLX: tests
	TreeView* treeView() { return _treeView; }
	TreeView* treeViewSearchResult() { return _treeViewSearchResult; }
	QLineEdit* searchEdit() { return _hSearchEdit; }
	const std::vector<foundInfo>& foundFuncInfos() const { return _foundFuncInfos; }
	bool shouldSort();
	void setSort(bool isEnabled);
	bool openSelection(const TreeView& treeView);

protected:
	void onSwitchIn() override { reload(); } // upstream DMN_SWITCHIN
	bool eventFilter(QObject* watched, QEvent* event) override; // upstream TVN_KEYDOWN, funclstSearchEditProc

private:
	QToolButton* _sortButton = nullptr;
	QMenu* _hPreferencesMenu = nullptr;
	QLineEdit* _hSearchEdit = nullptr;

	TreeView* _pTreeView = nullptr;
	TreeView* _treeView = nullptr;
	TreeView* _treeViewSearchResult = nullptr;

	long _findLine = -1;
	long _findEndLine = -1;
	HTREEITEM _findItem = nullptr;

	std::wstring _sortTipStr = L"Sort";
	std::wstring _reloadTipStr = L"Reload";
	std::wstring _preferenceTipStr = L"Preferences";

	std::vector<foundInfo> _foundFuncInfos;

	ScintillaEditView** _ppEditView = nullptr;
	FunctionParsersManager _funcParserMgr;
	std::vector<TreeParams> _treeParams;

	void addInStateArray(TreeStateNode tree2Update, const wchar_t* searchText, bool isSorted);
	TreeParams* getFromStateArray(const std::wstring& fullFilePath);
	void findMarkEntry(HTREEITEM htItem, long line);
	void initPreferencesMenu();
	void updateSearchEditColour(); // upstream WM_CTLCOLOREDIT: red when nothing matches
	void onActivated(TreeView* treeView);
	void updateTreeColours();
	QColor _bgColour;
	QColor _fgColour;
};
