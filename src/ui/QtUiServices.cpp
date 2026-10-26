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
#include <QCheckBox>
#include <QFileDialog>
#include <QProcess>
#include <QGridLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>

#include <map>

#include "win32compat.h"
#include "Common.h"
#include "Parameters.h"
#include "localization.h"
#include "resource.h"

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

int QtUiServices::networkPathWarning(const std::wstring& path, const std::string& titleTag)
{
	// upstream NetworkPathWarningBox (IDD_NETWORKPATHWARNINGBOX): Skip, Always load from this server (serverWhiteList.xml),
	// Always skip network paths, Always load network paths
	std::wstring title = L"NotePadLX session loading";
	if (titleTag == "title2")
		title = L"Clickable file:// link";
	else if (titleTag == "title3")
		title = L"Loading toolbarButtonsConf.xml";
	else if (titleTag == "title4")
		title = L"Loading Project Panel XML file";
	// NPLX: no Windows login is sent on Linux; the server is still contacted
	std::wstring msg = L"Network Path Warning:\n\n$STR_REPLACE$\n\nLoading this file will contact its server.\n\nLoad anyway?";
	std::map<int, std::wstring> buttonText = {
		{ IDCANCEL, L"&Skip" }, { IDYES, L"Always &load from this server" },
		{ IDNO, L"&Always skip network paths" }, { IDRETRY, L"Always load &network paths" },
	};

	NppParameters& nppParam = NppParameters::getInstance();
	if (const NativeLangSpeaker* speaker = nppParam.getNativeLangSpeaker())
	{
		DlgLang lang;
		if (speaker->getDlgLang("NetworkPathWarning", lang, titleTag.empty() ? nullptr : titleTag.c_str()))
		{
			if (!lang._title.empty())
				title = lang._title;
			for (const auto& [id, name] : lang._items)
			{
				if (id == IDC_NETWORKPATHWARNINGTEXT)
					msg = name;
				else if (buttonText.count(id))
					buttonText[id] = name;
			}
		}
	}
	msg = stringReplace(msg, L"$STR_REPLACE$", path);

	QMessageBox box(QMessageBox::Warning, QString::fromStdWString(title), QString::fromStdWString(msg), QMessageBox::NoButton,
		QApplication::activeWindow());
	std::map<QAbstractButton*, int> ids;
	for (int id : { IDCANCEL, IDYES, IDNO, IDRETRY })
	{
		QPushButton* button = box.addButton(QString::fromStdWString(buttonText[id]),
			id == IDCANCEL ? QMessageBox::RejectRole : QMessageBox::AcceptRole);
		ids[button] = id;
		if (id == IDCANCEL)
			box.setDefaultButton(button);
	}
	box.setEscapeButton(box.defaultButton());
	box.exec();
	const auto it = ids.find(box.clickedButton());
	const int clicked = it == ids.end() ? IDCANCEL : it->second;

	switch (clicked)
	{
		case IDYES: // "Always load from this server" - loading of the current net-file will continue
			nppParam.addServerToWhiteList(wstring2string(path, CP_UTF8).c_str()); // add the current net-file server to whitelist for the future
			break;
		case IDNO: // "Always skip network paths" - loading of the current net-file and any subsequent ones will be skipped
			nppParam.setNetworkPathAlwaysActionInServerWhitelist(NppParameters::networkPathAlwaysSkip);
			break;
		case IDRETRY: // "Always load network paths" - loading of the current net-file and any subsequent ones will continue
			nppParam.setNetworkPathAlwaysActionInServerWhitelist(NppParameters::networkPathAlwaysLoad);
			break;
		default: // "Skip" - loading of the current net-file will be skipped
			break;
	}
	return clicked;
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

std::wstring QtUiServices::getSaveFileNameEx(const std::wstring& title, const std::wstring& filter, const std::wstring& dir,
	const std::wstring& defExt, const std::wstring& checkboxLabel, bool* checkboxState)
{
	QFileDialog dlg(QApplication::activeWindow(), QString::fromStdWString(title), QString::fromStdWString(dir), QString::fromStdWString(filter));
	dlg.setAcceptMode(QFileDialog::AcceptSave);
	dlg.setFileMode(QFileDialog::AnyFile);
	if (!defExt.empty())
		dlg.setDefaultSuffix(QString::fromStdWString(defExt));

	QCheckBox* checkbox = nullptr;
	if (!checkboxLabel.empty())
	{
		// NPLX: upstream adds the checkbox to the native IFileDialog; a desktop (portal) dialog cannot take one, so
		// the Qt dialog is used then
		dlg.setOption(QFileDialog::DontUseNativeDialog);
		checkbox = new QCheckBox(QString::fromStdWString(checkboxLabel), &dlg);
		if (auto* grid = qobject_cast<QGridLayout*>(dlg.layout()))
			grid->addWidget(checkbox, grid->rowCount(), 0, 1, grid->columnCount());
		else
			checkbox->hide();
	}

	std::wstring fileName;
	if (dlg.exec() == QDialog::Accepted && !dlg.selectedFiles().isEmpty())
		fileName = dlg.selectedFiles().first().toStdWString();
	if (checkboxState)
		*checkboxState = checkbox && checkbox->isChecked();
	return fileName;
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

bool QtUiServices::startDetached(const std::wstring& program, const std::vector<std::wstring>& args)
{
	QStringList arguments;
	for (const std::wstring& arg : args)
		arguments << QString::fromStdWString(arg);
	return QProcess::startDetached(QString::fromStdWString(program), arguments);
}
