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

#include "QtUiServices.h"

#include <QApplication>
#include <QGuiApplication>
#include <QFileDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>

#include "win32compat.h"

int QtUiServices::messageBox(const std::wstring& message, const std::wstring& title, unsigned int type)
{
	// Upstream messages use '\r' as line separator
	QString text = QString::fromStdWString(message);
	text.replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace(QLatin1Char('\r'), QLatin1Char('\n'));

	QMessageBox::Icon icon = QMessageBox::NoIcon;
	switch (type & 0xF0)
	{
		case MB_ICONHAND:        icon = QMessageBox::Critical; break;
		case MB_ICONQUESTION:    icon = QMessageBox::Question; break;
		case MB_ICONEXCLAMATION: icon = QMessageBox::Warning; break;
		case MB_ICONASTERISK:    icon = QMessageBox::Information; break;
		default: break;
	}

	QMessageBox box(icon, QString::fromStdWString(title), text, QMessageBox::NoButton, QApplication::activeWindow());
	switch (type & 0x0F)
	{
		case MB_OKCANCEL:
			box.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
			break;
		case MB_YESNOCANCEL:
			box.setStandardButtons(QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
			break;
		case MB_YESNO:
			box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
			break;
		default:
			box.setStandardButtons(QMessageBox::Ok);
			break;
	}
	if ((type & 0xF00) == 0x100) // MB_DEFBUTTON2
	{
		const auto buttons = box.buttons();
		if (buttons.size() > 1)
			box.setDefaultButton(qobject_cast<QPushButton*>(buttons[1]));
	}

	switch (box.exec())
	{
		case QMessageBox::Ok:     return IDOK;
		case QMessageBox::Cancel: return IDCANCEL;
		case QMessageBox::Yes:    return IDYES;
		case QMessageBox::No:     return IDNO;
		default:                  return IDCANCEL;
	}
}

int QtUiServices::networkPathWarning(const std::wstring& path, const std::string&)
{
	// NPLX_TODO(Phase 3): full port of NetworkPathWarningBox (always load / always skip)
	const int res = messageBox(L"\"" + path + L"\" is on a network location that may be unreachable.\rLoad it?",
		L"Network path", MB_YESNO | MB_ICONQUESTION);
	return res == IDYES ? IDOK : IDCANCEL;
}

SIZE QtUiServices::primaryScreenSize()
{
	const QScreen* screen = QGuiApplication::primaryScreen();
	if (!screen)
		return SIZE{ 1920, 1080 };
	const QRect g = screen->geometry();
	return SIZE{ g.width(), g.height() };
}

bool QtUiServices::isRectVisibleOnAnyScreen(const RECT& rc)
{
	const QRect r(rc.left, rc.top, rc.right, rc.bottom); // x, y, width, height
	for (const QScreen* screen : QGuiApplication::screens())
	{
		if (screen->availableGeometry().intersects(r))
			return true;
	}
	return false;
}

std::wstring QtUiServices::getExistingDirectory(const std::wstring& title)
{
	return QFileDialog::getExistingDirectory(QApplication::activeWindow(), QString::fromStdWString(title)).toStdWString();
}

std::wstring QtUiServices::getOpenFileName(const std::wstring& title, const std::wstring& filter, const std::wstring& dir)
{
	return QFileDialog::getOpenFileName(QApplication::activeWindow(), QString::fromStdWString(title), QString::fromStdWString(dir), QString::fromStdWString(filter)).toStdWString();
}

std::vector<std::wstring> QtUiServices::getOpenFileNames(const std::wstring& title, const std::wstring& filter, const std::wstring& dir)
{
	std::vector<std::wstring> result;
	for (const QString& f : QFileDialog::getOpenFileNames(QApplication::activeWindow(), QString::fromStdWString(title), QString::fromStdWString(dir), QString::fromStdWString(filter)))
		result.push_back(f.toStdWString());
	return result;
}

std::wstring QtUiServices::getSaveFileName(const std::wstring& title, const std::wstring& filter, const std::wstring& dir)
{
	return QFileDialog::getSaveFileName(QApplication::activeWindow(), QString::fromStdWString(title), QString::fromStdWString(dir), QString::fromStdWString(filter)).toStdWString();
}

bool QtUiServices::getText(const std::wstring& title, const std::wstring& label, std::wstring& text)
{
	bool ok = false;
	const QString result = QInputDialog::getText(QApplication::activeWindow(), QString::fromStdWString(title), QString::fromStdWString(label),
		QLineEdit::Normal, QString::fromStdWString(text), &ok);
	if (ok)
		text = result.toStdWString();
	return ok;
}

int QtUiServices::saveOrNotMultiBox(const std::wstring& message, const std::wstring& title)
{
	// upstream DoSaveOrNotBox: Yes / No / Yes to all / No to all / Cancel
	QMessageBox box(QMessageBox::Question, QString::fromStdWString(title), QString::fromStdWString(message), QMessageBox::NoButton, QApplication::activeWindow());
	QPushButton* yes = box.addButton(QMessageBox::Yes);
	QPushButton* no = box.addButton(QMessageBox::No);
	QPushButton* yesAll = box.addButton(QMessageBox::YesToAll);
	QPushButton* noAll = box.addButton(QMessageBox::NoToAll);
	box.addButton(QMessageBox::Cancel);
	box.setDefaultButton(yes);
	box.exec();

	QAbstractButton* clicked = box.clickedButton();
	if (clicked == yes) return IDYES;
	if (clicked == no) return IDNO;
	if (clicked == yesAll) return IDRETRY;  // upstream: IDRETRY = "Yes to all"
	if (clicked == noAll) return IDIGNORE;  // upstream: IDIGNORE = "No to all"
	return IDCANCEL;
}
