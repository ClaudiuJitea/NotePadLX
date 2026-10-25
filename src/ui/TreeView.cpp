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

#include "TreeView.h"

#include <algorithm>

#include <QDropEvent>
#include <QHeaderView>

#include "Common.h"

namespace
{
	constexpr int ParamRole = Qt::UserRole;
	constexpr int ImageRole = Qt::UserRole + 1;
}

TreeView::TreeView(QWidget* parent)
	: QTreeWidget(parent)
{
	setHeaderHidden(true);
	setColumnCount(1);
	setUniformRowHeights(true);
	setExpandsOnDoubleClick(false); // upstream: double-click opens a leaf; nodes toggle (handled by the panels)
	setEditTriggers(QAbstractItemView::NoEditTriggers);
	setFrameShape(QFrame::NoFrame);

	connect(this, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item) {
		if (item != _editingItem)
			return;
		_editingItem = nullptr;
		item->setFlags(item->flags() & ~Qt::ItemIsEditable); // (itemChanged again: _editingItem is already null)
		if (item->text(0) != _editingOldLabel)
			emit labelEdited(item, _editingOldLabel);
	});
}

HTREEITEM TreeView::addItem(const wchar_t* itemName, HTREEITEM hParentItem, int iImage, const std::wstring& lParam)
{
	auto* item = hParentItem ? new QTreeWidgetItem(hParentItem) : new QTreeWidgetItem(this);
	item->setText(0, QString::fromWCharArray(itemName));
	setItemImage(item, iImage);
	item->setData(0, ParamRole, QString::fromStdWString(lParam));
	return item;
}

HTREEITEM TreeView::getNextSibling(HTREEITEM hItem) const
{
	if (!hItem)
		return nullptr;
	QTreeWidgetItem* parent = hItem->parent();
	if (parent)
	{
		const int i = parent->indexOfChild(hItem);
		return (i + 1 < parent->childCount()) ? parent->child(i + 1) : nullptr;
	}
	const int i = indexOfTopLevelItem(hItem);
	return (i + 1 < topLevelItemCount()) ? topLevelItem(i + 1) : nullptr;
}

HTREEITEM TreeView::searchSubItemByName(const wchar_t* itemName, HTREEITEM hParentItem) const
{
	HTREEITEM hItem = hParentItem ? getChildFrom(hParentItem) : getRoot();
	const QString name = QString::fromWCharArray(itemName);
	for (; hItem; hItem = getNextSibling(hItem))
	{
		if (hItem->text(0) == name)
			return hItem;
	}
	return nullptr;
}

bool TreeView::renameItem(HTREEITEM hTreeItem, const wchar_t* newLabel)
{
	if (!hTreeItem || !newLabel)
		return false;
	hTreeItem->setText(0, QString::fromWCharArray(newLabel));
	return true;
}

std::wstring TreeView::getItemParam(HTREEITEM item) const
{
	return item ? item->data(0, ParamRole).toString().toStdWString() : std::wstring();
}

void TreeView::setItemParam(HTREEITEM item, const std::wstring& lParam) const
{
	if (item)
		item->setData(0, ParamRole, QString::fromStdWString(lParam));
}

int TreeView::getItemImage(HTREEITEM item) const
{
	return item ? item->data(0, ImageRole).toInt() : -1;
}

void TreeView::setItemImage(HTREEITEM item, int iImage) const
{
	if (!item)
		return;
	item->setData(0, ImageRole, iImage);
	if (iImage >= 0 && static_cast<size_t>(iImage) < _icons.size())
		item->setIcon(0, _icons[iImage]);
}

void TreeView::sortChildren(HTREEITEM hTreeItem, const std::function<bool(HTREEITEM, HTREEITEM)>& lessThan)
{
	if (!hTreeItem)
		return;
	QList<QTreeWidgetItem*> children = hTreeItem->takeChildren();
	std::stable_sort(children.begin(), children.end(), lessThan);
	hTreeItem->addChildren(children);
}

void TreeView::sort(HTREEITEM hTreeItem, bool isRecusive)
{
	// takeChildren loses the expanded state: keep it
	std::vector<std::pair<QTreeWidgetItem*, bool>> expanded;
	for (QTreeWidgetItemIterator it(this); *it; ++it)
		expanded.emplace_back(*it, (*it)->isExpanded());

	sortChildren(hTreeItem, [](HTREEITEM a, HTREEITEM b) { return a->text(0).compare(b->text(0), Qt::CaseInsensitive) < 0; });
	if (isRecusive)
	{
		for (HTREEITEM hItem = getChildFrom(hTreeItem); hItem; hItem = getNextSibling(hItem))
			sort(hItem, isRecusive);
	}

	for (const auto& [item, isExpanded] : expanded)
		item->setExpanded(isExpanded);
}

void TreeView::customSorting(HTREEITEM hTreeItem, const std::function<int(const std::wstring&, const std::wstring&)>& sortingCallbackFunc, bool isRecusive)
{
	std::vector<std::pair<QTreeWidgetItem*, bool>> expanded;
	for (QTreeWidgetItemIterator it(this); *it; ++it)
		expanded.emplace_back(*it, (*it)->isExpanded());

	sortChildren(hTreeItem, [this, &sortingCallbackFunc](HTREEITEM a, HTREEITEM b) {
		return sortingCallbackFunc(getItemParam(a), getItemParam(b)) < 0;
	});
	if (isRecusive)
	{
		for (HTREEITEM hItem = getChildFrom(hTreeItem); hItem; hItem = getNextSibling(hItem))
			customSorting(hItem, sortingCallbackFunc, isRecusive);
	}

	for (const auto& [item, isExpanded] : expanded)
		item->setExpanded(isExpanded);
}

void TreeView::customSortingItems(HTREEITEM hTreeItem, const std::function<int(HTREEITEM, HTREEITEM)>& sortingCallbackFunc, bool isRecusive)
{
	std::vector<std::pair<QTreeWidgetItem*, bool>> expanded;
	for (QTreeWidgetItemIterator it(this); *it; ++it)
		expanded.emplace_back(*it, (*it)->isExpanded());

	sortChildren(hTreeItem, [&sortingCallbackFunc](HTREEITEM a, HTREEITEM b) { return sortingCallbackFunc(a, b) < 0; });
	if (isRecusive)
	{
		for (HTREEITEM hItem = getChildFrom(hTreeItem); hItem; hItem = getNextSibling(hItem))
			customSortingItems(hItem, sortingCallbackFunc, isRecusive);
	}

	for (const auto& [item, isExpanded] : expanded)
		item->setExpanded(isExpanded);
}

bool TreeView::moveDown(HTREEITEM itemToMove)
{
	HTREEITEM hItemToUp = getNextSibling(itemToMove);
	if (!hItemToUp)
		return false;
	QTreeWidgetItem* parent = itemToMove->parent() ? itemToMove->parent() : invisibleRootItem();
	const bool isSelected = getSelection() == itemToMove;
	const bool isExpanded = itemToMove->isExpanded();
	const int index = parent->indexOfChild(itemToMove);
	parent->takeChild(index);
	parent->insertChild(index + 1, itemToMove);
	itemToMove->setExpanded(isExpanded);
	if (isSelected)
		setCurrentItem(itemToMove);
	return true;
}

bool TreeView::moveUp(HTREEITEM itemToMove)
{
	QTreeWidgetItem* parent = itemToMove->parent() ? itemToMove->parent() : invisibleRootItem();
	const int index = parent->indexOfChild(itemToMove);
	if (index <= 0)
		return false;
	return moveDown(parent->child(index - 1)) && (setCurrentItem(itemToMove), true);
}

void TreeView::editLabel(HTREEITEM item)
{
	if (!item)
		return;
	item->setFlags(item->flags() | Qt::ItemIsEditable); // emits itemChanged: before _editingItem is set
	_editingItem = item;
	_editingOldLabel = item->text(0);
	setCurrentItem(item);
	editItem(item, 0);
}

void TreeView::enableDragAndDrop()
{
	setDragEnabled(true);
	setAcceptDrops(true);
	setDropIndicatorShown(true);
	setDragDropMode(QAbstractItemView::InternalMove);
}

bool TreeView::canDropIn(HTREEITEM targetItem) const
{
	const int image = getItemImage(targetItem);
	return std::find(_canNotDropInList.begin(), _canNotDropInList.end(), image) == _canNotDropInList.end();
}

bool TreeView::canDragOut(HTREEITEM targetItem) const
{
	const int image = getItemImage(targetItem);
	return std::find(_canNotDragOutList.begin(), _canNotDragOutList.end(), image) == _canNotDragOutList.end();
}

void TreeView::startDrag(Qt::DropActions supportedActions)
{
	// upstream TVN_BEGINDRAG: canDragOut
	HTREEITEM item = currentItem();
	if (!item || !canDragOut(item))
		return;
	QTreeWidget::startDrag(supportedActions);
}

bool TreeView::dropItem(HTREEITEM dragged, HTREEITEM target)
{
	// upstream dropItem: the dragged item (and its children) become the last child of the target
	if (!dragged || !target || dragged == target || !canDropIn(target) || !canDragOut(dragged))
		return false;
	for (HTREEITEM p = target; p; p = p->parent())
	{
		if (p == dragged)
			return false; // into itself
	}

	std::vector<std::pair<QTreeWidgetItem*, bool>> expanded;
	for (QTreeWidgetItemIterator it(dragged); *it && (*it == dragged || (*it)->parent()); ++it)
		expanded.emplace_back(*it, (*it)->isExpanded());

	QTreeWidgetItem* oldParent = dragged->parent() ? dragged->parent() : invisibleRootItem();
	oldParent->removeChild(dragged);
	target->addChild(dragged);
	target->setExpanded(true);
	for (const auto& [item, isExpanded] : expanded)
		item->setExpanded(isExpanded);
	setCurrentItem(dragged);
	emit itemDropped(dragged);
	return true;
}

void TreeView::dropEvent(QDropEvent* event)
{
	// NPLX: the Qt drop (between / on items) becomes upstream's "on the target" drop
	HTREEITEM target = itemAt(event->position().toPoint());
	HTREEITEM dragged = currentItem();
	event->setDropAction(Qt::IgnoreAction);
	event->accept();
	dropItem(dragged, target);
}

bool TreeView::searchLeafAndBuildTree(TreeView& tree2Build, const std::wstring& text2Search, int index2Search)
{
	return searchLeafRecusivelyAndBuildTree(tree2Build, tree2Build.getRoot(), text2Search, index2Search, getRoot());
}

bool TreeView::searchLeafRecusivelyAndBuildTree(TreeView& tree2Build, HTREEITEM tree2BuildItem, const std::wstring& text2Search, int index2Search, HTREEITEM tree2Search)
{
	if (!tree2Search)
		return false;

	if (getItemImage(tree2Search) == index2Search)
	{
		std::wstring itemNameUpperCase = stringToUpper(getItemDisplayName(tree2Search));
		std::wstring text2SearchUpperCase = stringToUpper(text2Search);

		size_t res = itemNameUpperCase.find(text2SearchUpperCase);
		if (res != std::wstring::npos)
		{
			HTREEITEM item = tree2Build.addItem(getItemDisplayName(tree2Search).c_str(), tree2BuildItem, index2Search, getItemParam(tree2Search));
			item->setIcon(0, tree2Search->icon(0));
		}
	}

	bool isOk = true;
	for (HTREEITEM hItem = getChildFrom(tree2Search); hItem != nullptr; hItem = getNextSibling(hItem))
	{
		isOk = searchLeafRecusivelyAndBuildTree(tree2Build, tree2BuildItem, text2Search, index2Search, hItem);
		if (!isOk)
			break;
	}
	return isOk;
}

bool TreeView::retrieveFoldingStateTo(TreeStateNode& treeState2Construct, HTREEITEM treeviewNode) const
{
	if (!treeviewNode)
		return false;

	treeState2Construct._label = getItemDisplayName(treeviewNode);
	treeState2Construct._isExpanded = treeviewNode->isExpanded();
	treeState2Construct._isSelected = treeviewNode->isSelected();
	treeState2Construct._extraData = getItemParam(treeviewNode);

	int i = 0;
	for (HTREEITEM hItem = getChildFrom(treeviewNode); hItem != nullptr; hItem = getNextSibling(hItem))
	{
		treeState2Construct._children.push_back(TreeStateNode());
		retrieveFoldingStateTo(treeState2Construct._children.at(i), hItem);
		++i;
	}
	return true;
}

bool TreeView::restoreFoldingStateFrom(const TreeStateNode& treeState2Compare, HTREEITEM treeviewNode)
{
	if (!treeviewNode)
		return false;

	if (treeState2Compare._isExpanded)
		expand(treeviewNode);
	else
		fold(treeviewNode);

	if (treeState2Compare._isSelected)
		selectItem(treeviewNode);

	size_t i = 0;
	bool isOk = true;
	for (HTREEITEM hItem = getChildFrom(treeviewNode); hItem != nullptr; hItem = getNextSibling(hItem))
	{
		if (i >= treeState2Compare._children.size())
			return false;
		isOk = restoreFoldingStateFrom(treeState2Compare._children.at(i), hItem);
		if (!isOk)
			break;
		++i;
	}
	return isOk;
}
