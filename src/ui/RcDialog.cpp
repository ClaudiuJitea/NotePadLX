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

#include "RcDialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpressionValidator>
#include <QSlider>
#include <QSpinBox>
#include <QTabBar>
#include <QTreeWidget>

namespace
{
	QString rcText(const wchar_t* text)
	{
		return QString::fromWCharArray(text);
	}

	Qt::Alignment alignmentOf(unsigned style)
	{
		Qt::Alignment a = (style & RcCenter) ? Qt::AlignHCenter : (style & RcRight) ? Qt::AlignRight : Qt::AlignLeft;
		return a | ((style & RcVCenter) ? Qt::AlignVCenter : Qt::AlignTop);
	}
}

RcDialog::RcDialog(QWidget* parent, bool asChild)
	: QDialog(parent)
{
	if (asChild)
		setWindowFlags(Qt::Widget); // the QDialog constructor turns Qt::Widget (0) into Qt::Dialog
	else
	{
		setWindowFlag(Qt::WindowContextHelpButtonHint, false);
		_padding = 6;
	}

	// Win32 dialog base units: the average width of the alphabet and the font height
	const QFontMetrics fm(font());
	_baseUnitX = std::max<qreal>(1, fm.horizontalAdvance(QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz")) / 52.0);
	_baseUnitY = std::max<qreal>(1, fm.height());
}

QRect RcDialog::mapDialogRect(int x, int y, int w, int h) const
{
	const int left = qRound(x * _baseUnitX / 4) + _padding;
	const int top = qRound(y * _baseUnitY / 8) + _padding;
	return QRect(left, top, qRound((x + w) * _baseUnitX / 4) + _padding - left, qRound((y + h) * _baseUnitY / 8) + _padding - top);
}

void RcDialog::buildFrom(const RcDialogDef& def)
{
	if (_isCreated)
		return;

	setWindowTitle(rcText(def._caption));
	const QRect dlgRect = mapDialogRect(0, 0, def._w, def._h);
	setFixedSize(dlgRect.size() + QSize(2 * _padding, 2 * _padding));

	QButtonGroup* radioGroup = nullptr;
	QLabel* pendingBuddy = nullptr;
	std::vector<QGroupBox*> groups;

	for (int i = 0; i < def._count; ++i)
	{
		const RcControlDef& c = def._controls[i];
		QRect r = mapDialogRect(c._x, c._y, c._w, c._h);
		QWidget* w = nullptr;
		const int id = c._id;

		switch (c._kind)
		{
			case RcLabel:
			{
				if (c._style & RcEtchedLine)
				{
					auto* line = new QFrame(this);
					line->setFrameShape(c._w > c._h ? QFrame::HLine : QFrame::VLine);
					line->setFrameShadow(QFrame::Sunken);
					w = line;
					break;
				}
				auto* label = new QLabel(rcText(c._text), this);
				label->setAlignment(alignmentOf(c._style));
				label->setWordWrap(r.height() > 2 * _baseUnitY);
				if (c._style & RcSunken)
					label->setFrameShape(QFrame::Panel), label->setFrameShadow(QFrame::Sunken);
				if (rcText(c._text).contains(QLatin1Char('&')))
					pendingBuddy = label;
				w = label;
				break;
			}

			case RcGroup:
			{
				auto* group = new QGroupBox(rcText(c._text), this);
				if (c._style & RcCenter)
					group->setAlignment(Qt::AlignHCenter);
				groups.push_back(group);
				w = group;
				break;
			}

			case RcButton:
			{
				if (c._style & RcCheckBox)
				{
					auto* check = new QCheckBox(rcText(c._text), this);
					check->setTristate(c._style & RcTriState);
					if (c._style & RcLeftText)
						check->setLayoutDirection(Qt::RightToLeft);
					connect(check, &QCheckBox::clicked, this, [this, id]() { onCommand(id, RcNotifyClicked); });
					w = check;
				}
				else if (c._style & RcRadio)
				{
					auto* radio = new QRadioButton(rcText(c._text), this);
					radio->setAutoExclusive(false);
					if (!radioGroup || (c._style & RcGroupStart))
					{
						radioGroup = new QButtonGroup(this);
						radioGroup->setExclusive(true);
					}
					radioGroup->addButton(radio);
					connect(radio, &QRadioButton::clicked, this, [this, id]() { onCommand(id, RcNotifyClicked); });
					w = radio;
				}
				else
				{
					auto* button = new QPushButton(rcText(c._text), this);
					button->setAutoDefault(false);
					if (c._style & RcDefault)
						button->setDefault(true);
					// NPLX: a narrow .rc button ("...", "+", "<<") keeps its text with less padding
					if (r.width() < fontMetrics().horizontalAdvance(button->text().remove(QLatin1Char('&'))) + 32)
						button->setProperty("compact", true);
					connect(button, &QPushButton::clicked, this, [this, id]() { onCommand(id, RcNotifyClicked); });
					w = button;
				}
				break;
			}

			case RcEdit:
			{
				if (c._style & RcMultiLine)
				{
					auto* edit = new QPlainTextEdit(this);
					edit->setReadOnly(c._style & RcReadOnly);
					edit->setLineWrapMode((c._style & RcHScroll) ? QPlainTextEdit::NoWrap : QPlainTextEdit::WidgetWidth);
					connect(edit, &QPlainTextEdit::textChanged, this, [this, id]() {
						if (!_isUpdatingControls)
							onCommand(id, RcNotifyEditChange);
					});
					w = edit;
				}
				else
				{
					auto* edit = new QLineEdit(this);
					edit->setReadOnly(c._style & RcReadOnly);
					edit->setAlignment(alignmentOf(c._style) & Qt::AlignHorizontal_Mask);
					if (c._style & RcPassword)
						edit->setEchoMode(QLineEdit::Password);
					if (c._style & RcNumber)
						edit->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9]*")), edit));
					connect(edit, &QLineEdit::textChanged, this, [this, id]() {
						if (!_isUpdatingControls)
							onCommand(id, RcNotifyEditChange);
					});
					connect(edit, &QLineEdit::editingFinished, this, [this, id]() { onCommand(id, RcNotifyKillFocus); });
					w = edit;
				}
				break;
			}

			case RcCombo:
			{
				auto* combo = new QComboBox(this);
				combo->setEditable(c._style & RcDropDown);
				if (combo->isEditable())
					combo->setInsertPolicy(QComboBox::NoInsert);
				connect(combo, &QComboBox::currentIndexChanged, this, [this, id]() {
					if (!_isUpdatingControls)
						onCommand(id, RcNotifySelChange);
				});
				// the .rc height includes the drop-down list
				r.setHeight(std::max(combo->sizeHint().height(), mapDialogRect(0, 0, 0, 14).height()));
				w = combo;
				break;
			}

			case RcList:
			{
				auto* list = new QListWidget(this);
				if (c._style & RcSort)
					list->setSortingEnabled(true);
				connect(list, &QListWidget::currentRowChanged, this, [this, id]() {
					if (!_isUpdatingControls)
						onCommand(id, RcNotifySelChange);
				});
				connect(list, &QListWidget::itemDoubleClicked, this, [this, id]() { onCommand(id, RcNotifyDblClick); });
				w = list;
				break;
			}

			case RcSlider:
			{
				auto* slider = new QSlider((c._style & RcVertical) ? Qt::Vertical : Qt::Horizontal, this);
				connect(slider, &QSlider::valueChanged, this, [this, id](int value) {
					if (!_isUpdatingControls)
						onSliderChanged(id, value);
				});
				w = slider;
				break;
			}

			case RcSpin:
				w = new QSpinBox(this);
				break;

			case RcTab:
				w = new QTabBar(this);
				break;

			case RcListView:
			case RcTree:
			{
				auto* tree = new QTreeWidget(this);
				tree->setRootIsDecorated(c._kind == RcTree);
				w = tree;
				break;
			}

			case RcOther:
			default:
				w = new QWidget(this);
				break;
		}

		if (c._kind != RcButton || !(c._style & RcRadio))
			radioGroup = nullptr; // Win32: a group of auto radio buttons ends with the next other control

		// Win32: a mnemonic on a static focuses the next control
		if (pendingBuddy && w != pendingBuddy && c._kind != RcLabel && c._kind != RcGroup)
		{
			pendingBuddy->setBuddy(w);
			pendingBuddy = nullptr;
		}

		w->setGeometry(r);
		if (c._style & RcHidden)
			w->hide();
		if (c._style & RcDisabled)
			w->setEnabled(false);
		if (id != -1) // IDC_STATIC
			_ctrls[id] = w;
	}

	// group boxes behind the controls they frame
	for (QGroupBox* g : groups)
		g->lower();

	_isCreated = true;
}

QWidget* RcDialog::dlgItem(int id) const
{
	auto it = _ctrls.find(id);
	return it == _ctrls.end() ? nullptr : it->second;
}

bool RcDialog::isCheckedOrNot(int id) const
{
	auto* b = qobject_cast<QAbstractButton*>(dlgItem(id));
	return b && b->isChecked();
}

void RcDialog::setChecked(int id, bool checked) const
{
	if (auto* b = qobject_cast<QAbstractButton*>(dlgItem(id)))
	{
		QSignalBlocker blocker(b);
		// Win32 BM_SETCHECK also unchecks a radio button, which an exclusive QButtonGroup refuses
		QButtonGroup* group = b->group();
		const bool reopen = !checked && group && group->exclusive();
		if (reopen)
			group->setExclusive(false);
		b->setChecked(checked);
		if (reopen)
			group->setExclusive(true);
	}
}

std::wstring RcDialog::getItemText(int id) const
{
	QWidget* w = dlgItem(id);
	if (auto* e = qobject_cast<QLineEdit*>(w))
		return e->text().toStdWString();
	if (auto* e = qobject_cast<QPlainTextEdit*>(w))
		return e->toPlainText().toStdWString();
	if (auto* c = qobject_cast<QComboBox*>(w))
		return c->currentText().toStdWString();
	if (auto* l = qobject_cast<QLabel*>(w))
		return l->text().toStdWString();
	if (auto* b = qobject_cast<QAbstractButton*>(w))
		return b->text().toStdWString();
	if (auto* g = qobject_cast<QGroupBox*>(w))
		return g->title().toStdWString();
	return std::wstring();
}

void RcDialog::setItemText(int id, const std::wstring& text) const
{
	QWidget* w = dlgItem(id);
	const QString s = QString::fromStdWString(text);
	auto* self = const_cast<RcDialog*>(this);
	const bool wasUpdating = self->_isUpdatingControls;
	self->_isUpdatingControls = true;
	if (auto* e = qobject_cast<QLineEdit*>(w))
		e->setText(s);
	else if (auto* e = qobject_cast<QPlainTextEdit*>(w))
		e->setPlainText(s);
	else if (auto* c = qobject_cast<QComboBox*>(w))
		c->setEditText(s);
	else if (auto* l = qobject_cast<QLabel*>(w))
		l->setText(s);
	else if (auto* b = qobject_cast<QAbstractButton*>(w))
		b->setText(s);
	else if (auto* g = qobject_cast<QGroupBox*>(w))
		g->setTitle(s);
	self->_isUpdatingControls = wasUpdating;
}

void RcDialog::enableItem(int id, bool enable) const
{
	if (QWidget* w = dlgItem(id))
		w->setEnabled(enable);
}

void RcDialog::showItem(int id, bool show) const
{
	if (QWidget* w = dlgItem(id))
		w->setVisible(show);
}

QComboBox* RcDialog::comboBox(int id) const
{
	return qobject_cast<QComboBox*>(dlgItem(id));
}

QListWidget* RcDialog::listBox(int id) const
{
	return qobject_cast<QListWidget*>(dlgItem(id));
}

QSlider* RcDialog::slider(int id) const
{
	return qobject_cast<QSlider*>(dlgItem(id));
}
