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

#include "TaskListDlg.h"

#include <QApplication>
#include <QKeyEvent>
#include <QListWidget>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "NplxStyle.h"
#include "Parameters.h"

int TaskListDlg::_instanceCount = 0;

namespace
{
	QIcon statusIcon(int status)
	{
		// upstream: the tab bar image list (saved, unsaved, read-only, monitoring)
		switch (status)
		{
			case tb_unsaved:   return nplx::style::icon(QStringLiteral("tab_unsaved"));
			case tb_ro:        return nplx::style::icon(QStringLiteral("tab_readonly"));
			case tb_monitored: return nplx::style::icon(QStringLiteral("tab_monitoring"));
			default:           return nplx::style::icon(QStringLiteral("tab_saved"));
		}
	}
}

TaskListDlg::TaskListDlg(QWidget* parent, bool dir, std::function<void(TaskListInfo&)> getTaskListInfo, std::function<void(int view, int index)> activate)
	: QDialog(parent, Qt::Popup | Qt::FramelessWindowHint), _activate(std::move(activate))
{
	++_instanceCount;

	// upstream WM_INITDIALOG
	if (getTaskListInfo)
		getTaskListInfo(_taskListInfo);

	const int nbTotal = static_cast<int>(_taskListInfo._tlfsLst.size());
	int i2set = _taskListInfo._currentIndex + (dir == dirDown ? 1 : -1);
	if (i2set < 0)
		i2set = nbTotal - 1;
	if (i2set > (nbTotal - 1))
		i2set = 0;
	_currentIndex = i2set;

	_taskList = new QListWidget(this);
	_taskList->setFrameShape(QFrame::NoFrame);
	_taskList->setUniformItemSizes(true);
	_taskList->setFocusPolicy(Qt::StrongFocus);
	_taskList->installEventFilter(this);
	_taskList->viewport()->installEventFilter(this);
	QFont font = _taskList->font();
	font.setPointSizeF(font.pointSizeF() + 2); // upstream setFont(getDlgFontSize() + 2)
	_taskList->setFont(font);

	for (const TaskLstFnStatus& fileNameStatus : _taskListInfo._tlfsLst)
		new QListWidgetItem(statusIcon(fileNameStatus._status), QString::fromStdWString(fileNameStatus._fn), _taskList);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(6, 6, 6, 6);
	layout->addWidget(_taskList);

	// upstream TaskList::adjustSize: as wide as the longest name (bold), as high as the list, within the screen
	const QFontMetrics fm(font);
	int maxWidth = 200;
	for (const TaskLstFnStatus& f : _taskListInfo._tlfsLst)
		maxWidth = std::max(maxWidth, fm.horizontalAdvance(QString::fromStdWString(f._fn)) + 60);
	const QRect screen = (parent && parent->screen()) ? parent->screen()->availableGeometry() : QRect(0, 0, 1920, 1080);
	const int rowHeight = std::max(fm.height() + 8, 24);
	resize(std::min(maxWidth, screen.width() - 80), std::min(nbTotal * rowHeight + 16, screen.height() - 120));

	// upstream: light yellow background (the tooltip colour)
	const nplx::style::Tokens& t = nplx::style::tokens();
	setStyleSheet(QStringLiteral("TaskListDlg { background: %1; border: 1px solid %2; border-radius: 8px; }"
		"QListWidget { background: transparent; } QListWidget::item { padding: 3px 6px; border-radius: 5px; }"
		"QListWidget::item:selected { background: %3; color: %4; font-weight: 600; }")
		.arg(t._base.name(), t._borderStrong.name(), t._accent.name(), t._accentText.name()));

	if (nbTotal > 0)
		_taskList->setCurrentRow(_currentIndex);

	connect(_taskList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
		// upstream NM_CLICK / NM_RCLICK
		_currentIndex = _taskList->row(item);
		pick();
	});
}

TaskListDlg::~TaskListDlg()
{
	_instanceCount--;
}

int TaskListDlg::doDialog()
{
	if (parentWidget())
	{
		// goToCenter
		const QRect pr = parentWidget()->geometry();
		move(pr.center() - rect().center());
	}
	return exec();
}

void TaskListDlg::showEvent(QShowEvent* event)
{
	QDialog::showEvent(event);
	_taskList->setFocus();

	// upstream WM_ACTIVATE: dismissed when shown without Ctrl down (but allow mouse clicking to change document)
	if (!(QGuiApplication::queryKeyboardModifiers() & Qt::ControlModifier))
		QTimer::singleShot(0, this, [this]() { pick(); }); // NPLX: a quick Ctrl+Tab still switches (upstream: the dialog sees Ctrl down)
}

void TaskListDlg::moveSelection(int direction)
{
	const int nbItem = _taskList->count();
	if (nbItem == 0)
		return;
	int newIndex = _currentIndex + direction;
	if (newIndex < 0)
		newIndex = nbItem - 1;
	if (newIndex >= nbItem)
		newIndex = 0;
	_currentIndex = newIndex;
	_taskList->setCurrentRow(_currentIndex);
	_taskList->scrollToItem(_taskList->currentItem());
}

void TaskListDlg::pick()
{
	// upstream ID_PICKEDUP
	if (_currentIndex >= 0 && _currentIndex < static_cast<int>(_taskListInfo._tlfsLst.size()))
	{
		const int view2set = _taskListInfo._tlfsLst[_currentIndex]._iView;
		const int index2Switch = _taskListInfo._tlfsLst[_currentIndex]._docIndex;
		if (_activate)
			_activate(view2set, index2Switch);
	}
	accept();
}

void TaskListDlg::keyPressEvent(QKeyEvent* event)
{
	const bool ctrl = event->modifiers() & Qt::ControlModifier;
	const bool shift = event->modifiers() & Qt::ShiftModifier;
	switch (event->key())
	{
		case Qt::Key_Escape:
			reject(); // fallback in case window is not dismissed automatically
			return;
		case Qt::Key_F4:
			if (ctrl)
				reject(); // Ctrl+F4 will dismiss document switcher
			return;
		case Qt::Key_Backtab: // Shift+Tab
			moveSelection(-1);
			return;
		case Qt::Key_Tab:
			moveSelection(shift ? -1 : 1);
			return;
		case Qt::Key_Up:
		case Qt::Key_Left:
			moveSelection(-1);
			return;
		case Qt::Key_Down:
		case Qt::Key_Right:
			moveSelection(1);
			return;
		case Qt::Key_Return:
		case Qt::Key_Enter:
			pick();
			return;
		default:
			QDialog::keyPressEvent(event);
	}
}

void TaskListDlg::keyReleaseEvent(QKeyEvent* event)
{
	if (event->key() == Qt::Key_Control)
	{
		pick();
		return;
	}
	if (event->key() == Qt::Key_W && (event->modifiers() & Qt::ControlModifier))
	{
		// Ctrl+W will dismiss document switcher on releasing 'W' key to avoid accidently closing newly focused editor
		reject();
		return;
	}
	QDialog::keyReleaseEvent(event);
}

void TaskListDlg::wheelEvent(QWheelEvent* event)
{
	moveSelection(event->angleDelta().y() > 0 ? -1 : 1);
}

bool TaskListDlg::eventFilter(QObject* watched, QEvent* event)
{
	// the list's keys and wheel go to the dialog (upstream TaskListSelectProc)
	if (event->type() == QEvent::KeyPress)
	{
		keyPressEvent(static_cast<QKeyEvent*>(event));
		return true;
	}
	if (event->type() == QEvent::KeyRelease)
	{
		keyReleaseEvent(static_cast<QKeyEvent*>(event));
		return true;
	}
	if (event->type() == QEvent::Wheel)
	{
		wheelEvent(static_cast<QWheelEvent*>(event));
		return true;
	}
	return QDialog::eventFilter(watched, event);
}
