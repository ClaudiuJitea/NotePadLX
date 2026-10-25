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

#include "json.hpp"
#include "FunctionListPanel.h"

#include <fstream>

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QScrollBar>
#include <QToolButton>
#include <QVBoxLayout>

#include "Buffer.h"
#include "NplxStyle.h"
#include "Parameters.h"
#include "ScintillaEditView.h"
#include "localization.h"
#include "menuCmdID.h"

using nlohmann::json;
using namespace std;

#define INDEX_ROOT        0
#define INDEX_NODE        1
#define INDEX_LEAF        2

FunctionListPanel::FunctionListPanel(QWidget* parent)
	: DockingPanel(QString::fromWCharArray(FL_PANELTITLE), IDM_VIEW_FUNC_LIST, QStringLiteral("FunctionList"), parent)
{
	// upstream WM_INITDIALOG: toolbar (search field, Sort, Reload, Preferences) above the tree views
	NppParameters& nppParam = NppParameters::getInstance();
	NativeLangSpeaker *pNativeSpeaker = nppParam.getNativeLangSpeaker();
	if (pNativeSpeaker)
	{
		setWindowTitle(QString::fromStdWString(pNativeSpeaker->getAttrNameStr(FL_PANELTITLE, FL_FUNCTIONLISTROOTNODE, "PanelTitle")));
		_sortTipStr = pNativeSpeaker->getAttrNameStr(_sortTipStr.c_str(), FL_FUNCTIONLISTROOTNODE, FL_SORTLOCALNODENAME);
		_reloadTipStr = pNativeSpeaker->getAttrNameStr(_reloadTipStr.c_str(), FL_FUNCTIONLISTROOTNODE, FL_RELOADLOCALNODENAME);
		_preferenceTipStr = pNativeSpeaker->getAttrNameStr(_preferenceTipStr.c_str(), FL_FUNCTIONLISTROOTNODE, FL_PREFERENCESLOCALNODENAME);
	}

	auto* content = new QWidget(this);
	auto* layout = new QVBoxLayout(content);
	layout->setContentsMargins(4, 4, 4, 4);
	layout->setSpacing(4);

	auto* toolbar = new QHBoxLayout();
	toolbar->setSpacing(2);
	_hSearchEdit = new QLineEdit(content);
	_hSearchEdit->setClearButtonEnabled(true);
	_hSearchEdit->addAction(nplx::style::icon(QStringLiteral("search")), QLineEdit::LeadingPosition);
	_hSearchEdit->installEventFilter(this);
	toolbar->addWidget(_hSearchEdit, 1);

	auto makeButton = [content, toolbar](const char* icon, const std::wstring& tip) {
		auto* b = new QToolButton(content);
		b->setIcon(nplx::style::icon(QString::fromLatin1(icon)));
		b->setIconSize(QSize(18, 18));
		b->setAutoRaise(true);
		b->setToolTip(QString::fromStdWString(tip));
		toolbar->addWidget(b);
		return b;
	};
	_sortButton = makeButton("sort_az", _sortTipStr);
	_sortButton->setCheckable(true); // BTNS_CHECK
	QToolButton* reloadButton = makeButton("reload", _reloadTipStr);
	QToolButton* preferencesButton = makeButton("settings", _preferenceTipStr);
	layout->addLayout(toolbar);

	const std::vector<QIcon> imgIds = { nplx::style::icon(QStringLiteral("fl_root")), nplx::style::icon(QStringLiteral("fl_node")),
		nplx::style::icon(QStringLiteral("fl_leaf")) };
	_treeView = new TreeView(content);
	_treeView->setImageList(imgIds);
	_treeViewSearchResult = new TreeView(content);
	_treeViewSearchResult->setImageList(imgIds);
	_treeView->makeLabelEditable(false);
	_treeViewSearchResult->display(false);
	layout->addWidget(_treeView, 1);
	layout->addWidget(_treeViewSearchResult, 1);
	_pTreeView = _treeView;
	setWidget(content);

	initPreferencesMenu();

	// upstream WM_COMMAND
	connect(_hSearchEdit, &QLineEdit::textChanged, this, [this]() { searchFuncAndSwitchView(); }); // EN_CHANGE
	connect(_sortButton, &QToolButton::clicked, this, [this]() { sortOrUnsort(); });
	connect(reloadButton, &QToolButton::clicked, this, [this]() { reload(); });
	connect(preferencesButton, &QToolButton::clicked, this, [this, preferencesButton]() {
		_hPreferencesMenu->popup(preferencesButton->mapToGlobal(QPoint(0, preferencesButton->height())));
	});

	// upstream NM_DBLCLK / TVN_KEYDOWN
	for (TreeView* tree : { _treeView, _treeViewSearchResult })
	{
		tree->installEventFilter(this);
		connect(tree, &QTreeWidget::itemDoubleClicked, this, [this, tree]() { onActivated(tree); });
	}
}

void FunctionListPanel::setBackgroundColor(COLORREF bgColour)
{
	// upstream TreeView_SetBkColor: the trees take the editor theme's default colours
	_bgColour = QColor(GetRValue(bgColour), GetGValue(bgColour), GetBValue(bgColour));
	updateTreeColours();
}

void FunctionListPanel::setForegroundColor(COLORREF fgColour)
{
	_fgColour = QColor(GetRValue(fgColour), GetGValue(fgColour), GetBValue(fgColour));
	updateTreeColours();
}

void FunctionListPanel::updateTreeColours()
{
	// NPLX: a widget stylesheet (the application stylesheet would override a palette)
	QString qss;
	if (_bgColour.isValid())
		qss += QStringLiteral("background: %1; ").arg(_bgColour.name());
	if (_fgColour.isValid())
		qss += QStringLiteral("color: %1; ").arg(_fgColour.name());
	for (TreeView* tree : { _treeView, _treeViewSearchResult })
		tree->setStyleSheet(qss.isEmpty() ? QString() : QStringLiteral("QTreeView { %1}").arg(qss));
}

void FunctionListPanel::addEntry(const wchar_t *nodeName, const wchar_t *displayText, size_t pos)
{
	HTREEITEM itemParent = NULL;
	std::wstring posStr = std::to_wstring(pos);

	HTREEITEM root = _treeView->getRoot();

	if (nodeName != NULL && *nodeName != '\0')
	{
		itemParent = _treeView->searchSubItemByName(nodeName, root);
		if (!itemParent)
		{
			itemParent = _treeView->addItem(nodeName, root, INDEX_NODE, posStr);
		}
	}
	else
		itemParent = root;

	_treeView->addItem(displayText, itemParent, INDEX_LEAF, posStr);
}

void FunctionListPanel::removeAllEntries()
{
	_treeView->removeAllItems();
}

void FunctionListPanel::addInStateArray(TreeStateNode tree2Update, const wchar_t *searchText, bool isSorted)
{
	bool found = false;
	for (size_t i = 0, len = _treeParams.size(); i < len; ++i)
	{
		if (_treeParams[i]._treeState._extraData == tree2Update._extraData)
		{
			_treeParams[i]._searchParameters._text2Find = searchText;
			_treeParams[i]._searchParameters._doSort = isSorted;
			_treeParams[i]._treeState = tree2Update;
			found = true;
		}
	}

	if (!found)
	{
		TreeParams params;
		params._treeState = tree2Update;
		params._searchParameters._text2Find = searchText;
		params._searchParameters._doSort = isSorted;
		_treeParams.push_back(params);
	}
}

TreeParams* FunctionListPanel::getFromStateArray(const wstring& fullFilePath)
{
	for (size_t i = 0, len = _treeParams.size(); i < len; ++i)
	{
		if (_treeParams[i]._treeState._extraData == fullFilePath)
			return &_treeParams[i];
	}
	return NULL;
}

void FunctionListPanel::sortOrUnsort()
{
	bool doSort = shouldSort();
	if (doSort)
		_pTreeView->sort(_pTreeView->getRoot(), true);
	else
	{
		const wstring text2search = _hSearchEdit->text().toStdWString();

		if (text2search.empty()) // main view
		{
			_pTreeView->customSorting(_pTreeView->getRoot(), categorySortFunc, true);
		}
		else // aux view
		{
			reload();

			if (_treeView->getRoot() == NULL)
				return;

			_treeViewSearchResult->removeAllItems();
			const wchar_t *fn = ((*_ppEditView)->getCurrentBuffer())->getFileName();

			_treeViewSearchResult->addItem(fn, NULL, INDEX_ROOT, L"-1");

			_treeView->searchLeafAndBuildTree(*_treeViewSearchResult, text2search, INDEX_LEAF);
			_treeViewSearchResult->display(true);
			_treeViewSearchResult->expand(_treeViewSearchResult->getRoot());
			_treeView->display(false);
			_pTreeView = _treeViewSearchResult;
		}
	}
}

int FunctionListPanel::categorySortFunc(const std::wstring& posString1, const std::wstring& posString2)
{
	size_t pos1 = std::wcstol(posString1.c_str(), nullptr, 10); // NPLX: _wtoi
	size_t pos2 = std::wcstol(posString2.c_str(), nullptr, 10);
	if (pos1 > pos2)
		return 1;
	else
		return -1;
}

bool FunctionListPanel::serialize(const wstring & outputFilename)
{
	Buffer* currentBuf = (*_ppEditView)->getCurrentBuffer();
	const wchar_t* fileNameLabel = currentBuf->getFileName();

	wstring fname2write;
	if (outputFilename.empty()) // if outputFilename is not given, get the current file path by adding the file extension
	{
		const wchar_t *fullFilePath = currentBuf->getFullPathName();

		// Export function list from an existing file
		bool exportFuncntionList = (NppParameters::getInstance()).doFunctionListExport();
		if (exportFuncntionList && doesFileExist(fullFilePath))
		{
			fname2write = fullFilePath;
			fname2write += L".result";
			fname2write += L".json";
		}
		else
			return false;
	}
	else
	{
		fname2write = outputFilename;
	}

	const char* rootLabel = "root";
	const char* nodesLabel = "nodes";
	const char* leavesLabel = "leaves";
	const char* nameLabel = "name";

	json j;
	j[rootLabel] = wstring2string(fileNameLabel, CP_UTF8); // NPLX: was wmc.wchar2char(fileNameLabel, CP_ACP)

	for (const auto & info : _foundFuncInfos)
	{
		std::string leafName = info._data.c_str();

		if (!info._data2.empty()) // node
		{
			bool isFound = false;
			std::string nodeName = info._data2.c_str();

			for (auto & i : j[nodesLabel])
			{
				if (nodeName == std::string{ i[nameLabel] })
				{
					i[leavesLabel].push_back(leafName.c_str());
					isFound = true;
					break;
				}
			}

			if (!isFound)
			{
				json aNode = { { leavesLabel, json::array() },{ nameLabel, nodeName.c_str() } };
				aNode[leavesLabel].push_back(leafName.c_str());
				j[nodesLabel].push_back(aNode);
			}
		}
		else // leaf
		{
			j[leavesLabel].push_back(leafName.c_str());
		}
	}

	std::ofstream file(wstring2string(fname2write, CP_UTF8)); // NPLX: was wmc.wchar2char(..., CP_ACP)
	file << j;

	return true;
}

void FunctionListPanel::reload()
{
	// NPLX: the scroll position (upstream GetScrollInfo / SetScrollInfo)
	const int scrollPos = _treeView->verticalScrollBar()->value();

	// clean up
	_findLine = -1;
	_findEndLine = -1;
	TreeStateNode currentTree;
	bool isOK = _treeView->retrieveFoldingStateTo(currentTree, _treeView->getRoot());
	if (isOK)
	{
		const wstring text2Search = _hSearchEdit->text().toStdWString();
		bool isSorted =  shouldSort();
		addInStateArray(currentTree, text2Search.c_str(), isSorted);
	}
	removeAllEntries();
	{
		QSignalBlocker blocker(_hSearchEdit);
		_hSearchEdit->clear();
	}
	searchFuncAndSwitchView(); // NPLX: upstream's WM_SETTEXT sends EN_CHANGE
	setSort(false);

	_foundFuncInfos.clear();

	Buffer* currentBuf = (*_ppEditView)->getCurrentBuffer();
	const wchar_t *fn = currentBuf->getFileName();
	LangType langID = currentBuf->getLangType();
	if (langID == L_JS_EMBEDDED)
		langID = L_JAVASCRIPT;

	const wchar_t *udln = NULL;
	if (langID == L_USER)
	{
		udln = currentBuf->getUserDefineLangName();
	}

	const wchar_t* ext = PathFindExtension(fn);

	bool parsedOK = _funcParserMgr.parse(_foundFuncInfos, AssociationInfo(-1, langID, ext, udln));
	if (parsedOK)
	{
		_treeView->addItem(fn, NULL, INDEX_ROOT, L"-1");
	}

	const auto codePage = static_cast<UINT>((*_ppEditView)->execute(SCI_GETCODEPAGE));

	for (const auto& foundFuncInfo : _foundFuncInfos)
	{
		addEntry(string2wstring(foundFuncInfo._data2, codePage).c_str(), string2wstring(foundFuncInfo._data, codePage).c_str(), foundFuncInfo._pos);
	}

	HTREEITEM root = _treeView->getRoot();

	if (root)
	{
		currentBuf = (*_ppEditView)->getCurrentBuffer();
		const wchar_t *fullFilePath = currentBuf->getFullPathName();

		_treeView->setItemParam(root, fullFilePath);
		TreeParams *previousParams = getFromStateArray(fullFilePath);
		if (!previousParams)
		{
			{
				QSignalBlocker blocker(_hSearchEdit);
				_hSearchEdit->clear();
			}
			setSort(NppParameters::getInstance().getNppGUI()._shouldSortFunctionList);
			sortOrUnsort();
			_treeView->expand(root);
		}
		else
		{
			{
				QSignalBlocker blocker(_hSearchEdit);
				_hSearchEdit->setText(QString::fromStdWString((previousParams->_searchParameters)._text2Find));
			}

			bool isSort = (previousParams->_searchParameters)._doSort;
			setSort(isSort);
			if (isSort)
				_pTreeView->sort(_pTreeView->getRoot(), true);

			_treeView->restoreFoldingStateFrom(previousParams->_treeState, root);
			if (!_hSearchEdit->text().isEmpty())
				searchFuncAndSwitchView();
		}
	}

	updateSearchEditColour();
	_treeView->verticalScrollBar()->setValue(scrollPos);
}

void FunctionListPanel::initPreferencesMenu()
{
	NativeLangSpeaker* pNativeSpeaker = NppParameters::getInstance().getNativeLangSpeaker();
	const NppGUI& nppGUI = NppParameters::getInstance().getNppGUI();

	wstring shouldSortFunctionListStr = L"Sort functions (A to Z) by default";
	if (pNativeSpeaker)
		shouldSortFunctionListStr = pNativeSpeaker->getAttrNameStr(shouldSortFunctionListStr.c_str(), FL_FUNCTIONLISTROOTNODE, FL_PREFERENCE_INITIALSORT);

	_hPreferencesMenu = new QMenu(this);
	QAction* initialSort = _hPreferencesMenu->addAction(QString::fromStdWString(shouldSortFunctionListStr));
	initialSort->setCheckable(true);
	initialSort->setChecked(nppGUI._shouldSortFunctionList);
	connect(initialSort, &QAction::triggered, this, [initialSort]() {
		// upstream FL_PREFERENCES_INITIALSORT_ID
		bool& shouldSortFunctionList = NppParameters::getInstance().getNppGUI()._shouldSortFunctionList;
		shouldSortFunctionList = !shouldSortFunctionList;
		initialSort->setChecked(shouldSortFunctionList);
	});
}

void FunctionListPanel::markEntry()
{
	long lineNr = static_cast<long>((*_ppEditView)->getCurrentLineNumber());
	HTREEITEM root = _treeView->getRoot();
	if (_findLine != -1 && _findEndLine != -1 && lineNr >= _findLine && lineNr < _findEndLine)
		return;
	_findLine = -1;
	_findEndLine = -1;
	findMarkEntry(root, lineNr);
	if (_findLine != -1)
	{
		_treeView->selectItem(_findItem);
	}
	else
	{
		_treeView->selectItem(root);
	}
}

void FunctionListPanel::findMarkEntry(HTREEITEM htItem, long line)
{
	HTREEITEM cItem{};
	for (; htItem != NULL; htItem = _treeView->getNextSibling(htItem))
	{
		cItem = _treeView->getChildFrom(htItem);
		if (cItem != NULL)
		{
			findMarkEntry(cItem, line);
		}
		else
		{
			const wstring posStr = _treeView->getItemParam(htItem);
			if (!posStr.empty())
			{
				int pos = static_cast<int>(std::wcstol(posStr.c_str(), nullptr, 10));
				if (pos != -1)
				{
					long sci_line = static_cast<long>((*_ppEditView)->execute(SCI_LINEFROMPOSITION, pos));
					if (line >= sci_line)
					{
						if (sci_line > _findLine || _findLine == -1)
						{
							_findLine = sci_line;
							_findItem = htItem;
						}
					}
					else
					{
						if (sci_line < _findEndLine)
							_findEndLine = sci_line;
					}
				}
			}
		}
	}
}

void FunctionListPanel::init(ScintillaEditView **ppEditView)
{
	_ppEditView = ppEditView;
	NppParameters& nppParams = NppParameters::getInstance();

	wstring funcListXmlPath = nppParams.getUserPath();
	pathAppend(funcListXmlPath, L"functionList");

	wstring funcListDefaultXmlPath = nppParams.getNppPath();
	pathAppend(funcListDefaultXmlPath, L"functionList");

	bool doLocalConf = nppParams.isLocal();

	if (!doLocalConf)
	{
		if (!doesDirectoryExist(funcListXmlPath.c_str()))
		{
			if (doesDirectoryExist(funcListDefaultXmlPath.c_str()))
			{
				// NPLX: upstream ::CopyFile on the folder, which fails: the installed rules are used directly
				_funcParserMgr.init(funcListXmlPath, funcListDefaultXmlPath, ppEditView);
			}
		}
		else
		{
			_funcParserMgr.init(funcListXmlPath, funcListDefaultXmlPath, ppEditView);
		}
	}
	else
	{
		if (doesDirectoryExist(funcListDefaultXmlPath.c_str()))
		{
			_funcParserMgr.init(funcListDefaultXmlPath, funcListDefaultXmlPath, ppEditView);
		}
	}
}

bool FunctionListPanel::openSelection(const TreeView & treeView)
{
	HTREEITEM item = treeView.getSelection();
	const int iImage = treeView.getItemImage(item);
	if (!item || iImage == INDEX_ROOT || iImage == INDEX_NODE)
	{
		return false;
	}

	const wstring posStr = treeView.getItemParam(item);
	if (posStr.empty())
		return false;

	int pos = static_cast<int>(std::wcstol(posStr.c_str(), nullptr, 10));
	if (pos == -1)
		return false;

	auto sci_line = (*_ppEditView)->execute(SCI_LINEFROMPOSITION, pos);
	(*_ppEditView)->execute(SCI_ENSUREVISIBLE, sci_line);
	(*_ppEditView)->scrollPosToCenter(pos);

	return true;
}

void FunctionListPanel::onActivated(TreeView* treeView)
{
	// upstream NM_DBLCLK: open the leaf, then SCEN_SETFOCUS to the editor; nodes toggle
	if (openSelection(*treeView))
		(*_ppEditView)->setFocus();
	else
		treeView->toggleExpandCollapse(treeView->getSelection());
}

bool FunctionListPanel::eventFilter(QObject* watched, QEvent* event)
{
	if (event->type() != QEvent::KeyPress)
		return DockingPanel::eventFilter(watched, event);

	auto* keyEvent = static_cast<QKeyEvent*>(event);
	if (watched == _hSearchEdit)
	{
		// upstream funclstSearchEditProc
		if (keyEvent->key() == Qt::Key_Escape)
		{
			_hSearchEdit->clear();
			return true;
		}
		if (keyEvent->key() == Qt::Key_Tab)
		{
			if (_treeViewSearchResult->isVisible())
				_treeViewSearchResult->setFocus();
			else
				_treeView->setFocus();
			return true;
		}
		return false;
	}

	auto* treeView = qobject_cast<TreeView*>(watched);
	if (!treeView)
		return false;

	// upstream TVN_KEYDOWN
	switch (keyEvent->key())
	{
		case Qt::Key_Return:
		case Qt::Key_Enter:
		{
			if (!openSelection(*treeView))
			{
				HTREEITEM hItem = treeView->getSelection();
				treeView->toggleExpandCollapse(hItem);
				return true;
			}
			(*_ppEditView)->setFocus();
			return true;
		}
		case Qt::Key_Tab:
			_hSearchEdit->setFocus();
			return true;
		case Qt::Key_Escape:
			_hSearchEdit->clear();
			(*_ppEditView)->setFocus();
			return true;
		default:
			return false;
	}
}

void FunctionListPanel::searchFuncAndSwitchView()
{
	const wstring text2search = _hSearchEdit->text().toStdWString();

	if (text2search.empty())
	{
		_treeViewSearchResult->display(false);
		_treeView->display(true);
		_pTreeView = _treeView;
	}
	else
	{
		if (_treeView->getRoot() == NULL)
			return;

		_treeViewSearchResult->removeAllItems();
		const wchar_t *fn = ((*_ppEditView)->getCurrentBuffer())->getFileName();

		_treeViewSearchResult->addItem(fn, NULL, INDEX_ROOT, L"-1");
		_treeView->searchLeafAndBuildTree(*_treeViewSearchResult, text2search, INDEX_LEAF);
		_treeViewSearchResult->display(true);
		_treeViewSearchResult->expand(_treeViewSearchResult->getRoot());
		_treeView->display(false);
		_pTreeView = _treeViewSearchResult;
	}

	// restore selected sorting
	if (shouldSort())
		_pTreeView->sort(_pTreeView->getRoot(), true);
	else
		_pTreeView->customSorting(_pTreeView->getRoot(), categorySortFunc, true);

	updateSearchEditColour();
}

void FunctionListPanel::updateSearchEditColour()
{
	// upstream WM_CTLCOLOREDIT: the search field turns red when the text matches nothing
	bool textFound = _hSearchEdit->text().isEmpty();
	if (!textFound)
	{
		HTREEITEM searchViewRoot = _treeViewSearchResult->getRoot();
		textFound = !searchViewRoot || _treeViewSearchResult->getChildFrom(searchViewRoot);
	}
	const nplx::style::Tokens& t = nplx::style::tokens();
	_hSearchEdit->setStyleSheet(textFound ? QString()
		: QStringLiteral("QLineEdit { background: %1; border-color: %2; }").arg(t._dangerSoft.name(), t._danger.name()));
}

bool FunctionListPanel::shouldSort()
{
	return _sortButton->isChecked();
}

void FunctionListPanel::setSort(bool isEnabled)
{
	_sortButton->setChecked(isEnabled);
}

void FunctionListPanel::grabFocus()
{
	if (_treeViewSearchResult->isVisible())
		_treeViewSearchResult->setFocus();
	else
		_treeView->setFocus();
}
