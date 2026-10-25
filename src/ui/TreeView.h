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

// NPLX: port of upstream WinControls/TreeView on QTreeWidget. HTREEITEM is QTreeWidgetItem*; the item's LPARAM
// (a std::wstring* upstream) is a string kept in the item (Qt::UserRole); the image index is kept too, since the
// panels use it to tell roots, nodes and leaves apart.

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <QIcon>
#include <QTreeWidget>

using HTREEITEM = QTreeWidgetItem*;

struct TreeStateNode {
	std::wstring _label;
	std::wstring _extraData;
	bool _isExpanded = false;
	bool _isSelected = false;
	std::vector<TreeStateNode> _children;
};

class TreeView : public QTreeWidget
{
	Q_OBJECT

public:
	explicit TreeView(QWidget* parent = nullptr);

	void setImageList(const std::vector<QIcon>& icons) { _icons = icons; }

	HTREEITEM addItem(const wchar_t* itemName, HTREEITEM hParentItem, int iImage, const std::wstring& lParam = std::wstring());
	void removeAllItems() { clear(); }
	void removeItem(HTREEITEM hTreeItem) { delete hTreeItem; }

	HTREEITEM getRoot() const { return topLevelItemCount() ? topLevelItem(0) : nullptr; }
	HTREEITEM getChildFrom(HTREEITEM hTreeItem) const { return (hTreeItem && hTreeItem->childCount()) ? hTreeItem->child(0) : nullptr; }
	HTREEITEM getNextSibling(HTREEITEM hItem) const;
	HTREEITEM getSelection() const { return currentItem(); }
	HTREEITEM getParent(HTREEITEM hItem) const { return hItem ? hItem->parent() : nullptr; }
	void foldAll() { collapseAll(); }   // upstream TreeView::foldAll
	void expandAll() { QTreeWidget::expandAll(); }
	HTREEITEM searchSubItemByName(const wchar_t* itemName, HTREEITEM hParentItem) const;

	std::wstring getItemDisplayName(HTREEITEM item) const { return item ? item->text(0).toStdWString() : std::wstring(); }
	bool renameItem(HTREEITEM hTreeItem, const wchar_t* newLabel);
	std::wstring getItemParam(HTREEITEM item) const;
	void setItemParam(HTREEITEM item, const std::wstring& lParam) const;
	int getItemImage(HTREEITEM item) const;
	void setItemImage(HTREEITEM item, int iImage) const;

	void expand(HTREEITEM hItem) const { if (hItem) hItem->setExpanded(true); }
	void fold(HTREEITEM hItem) const { if (hItem) hItem->setExpanded(false); }
	void toggleExpandCollapse(HTREEITEM hItem) const { if (hItem) hItem->setExpanded(!hItem->isExpanded()); }
	// upstream TVM_SELECTITEM (TVGN_CARET): the collapsed parents are expanded to reveal the item
	void selectItem(HTREEITEM hTreeItem2Select) {
		if (!hTreeItem2Select)
			return;
		for (HTREEITEM p = hTreeItem2Select->parent(); p; p = p->parent())
			p->setExpanded(true);
		setCurrentItem(hTreeItem2Select);
		scrollToItem(hTreeItem2Select);
	}
	void display(bool toShow = true) { setVisible(toShow); }
	void makeLabelEditable(bool toBeEnabled) { setEditTriggers(toBeEnabled ? QAbstractItemView::EditKeyPressed : QAbstractItemView::NoEditTriggers); }

	// upstream TVM_SORTCHILDREN: by label, case-insensitively
	void sort(HTREEITEM hTreeItem, bool isRecusive);
	// upstream TVM_SORTCHILDRENCB: the callback compares the items' params
	void customSorting(HTREEITEM hTreeItem, const std::function<int(const std::wstring&, const std::wstring&)>& sortingCallbackFunc, bool isRecusive);

	// NPLX: the comparison sees the items (upstream callbacks get the items' LPARAM, which hold more than a string)
	void customSortingItems(HTREEITEM hTreeItem, const std::function<int(HTREEITEM, HTREEITEM)>& sortingCallbackFunc, bool isRecusive);

	bool searchLeafAndBuildTree(TreeView& tree2Build, const std::wstring& text2Search, int index2Search);

	// upstream moveDown / moveUp (swap with the next / previous sibling)
	bool moveDown(HTREEITEM itemToMove);
	bool moveUp(HTREEITEM itemToMove);

	// upstream TreeView_EditLabel; labelEdited is upstream TVN_ENDLABELEDIT (the new text is in the item)
	void editLabel(HTREEITEM item);
	bool isEditing() const { return state() == QAbstractItemView::EditingState; }

	// upstream drag and drop: an item dropped on another becomes its last child; the image indexes in these lists
	// cannot take drops / cannot be dragged
	void addCanNotDropInList(int val2set) { _canNotDropInList.push_back(val2set); enableDragAndDrop(); }
	void addCanNotDragOutList(int val2set) { _canNotDragOutList.push_back(val2set); enableDragAndDrop(); }
	bool canDropIn(HTREEITEM targetItem) const;
	bool canDragOut(HTREEITEM targetItem) const;
	bool dropItem(HTREEITEM dragged, HTREEITEM target); // NPLX: also used by tests

signals:
	void labelEdited(QTreeWidgetItem* item, const QString& oldLabel);
	void itemDropped(QTreeWidgetItem* item);

protected:
	void dropEvent(QDropEvent* event) override;
	void startDrag(Qt::DropActions supportedActions) override;

public:
	bool retrieveFoldingStateTo(TreeStateNode& treeState2Construct, HTREEITEM treeviewNode) const;
	bool restoreFoldingStateFrom(const TreeStateNode& treeState2Compare, HTREEITEM treeviewNode);

private:
	std::vector<QIcon> _icons;
	std::vector<int> _canNotDropInList;
	std::vector<int> _canNotDragOutList;
	QTreeWidgetItem* _editingItem = nullptr;
	QString _editingOldLabel;

	void enableDragAndDrop();

	bool searchLeafRecusivelyAndBuildTree(TreeView& tree2Build, HTREEITEM tree2BuildItem, const std::wstring& text2Search, int index2Search, HTREEITEM tree2Search);
	void sortChildren(HTREEITEM hTreeItem, const std::function<bool(HTREEITEM, HTREEITEM)>& lessThan);
};
