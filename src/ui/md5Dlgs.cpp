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

#include "md5Dlgs.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>

#include <QClipboard>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QPlainTextEdit>

#include "calc_sha1.h"
#include "md5.h"
#include "sha-256.h"
#include "sha512.h"

#include "Common.h"
#include "UiServices.h"
#include "md5Dlgs_rc.h"
#include "resource.h"

namespace
{
#include "RcHashDialogs.inc"

void str2Clipboard(const std::wstring& str)
{
	// upstream Common.cpp str2Clipboard
	QGuiApplication::clipboard()->setText(QString::fromStdWString(str));
}
}

std::string hashToHex(hashType ht, const char* data, size_t len)
{
	if (ht == hash_md5)
	{
		// NPLX: MD5::digestMemory is binary safe (upstream: digestFile for the files, digestString for the text)
		MD5 md5;
		std::string bytes(data, len);
		return md5.digestMemory(reinterpret_cast<BYTE*>(bytes.data()), static_cast<int>(len));
	}

	uint8_t hash[HASH_MAX_LENGTH]{};
	switch (ht)
	{
		case hash_sha1:
			calc_sha1(hash, reinterpret_cast<const uint8_t*>(data), len);
			break;
		case hash_sha256:
			calc_sha_256(hash, reinterpret_cast<const uint8_t*>(data), len);
			break;
		case hash_sha512:
			calc_sha_512(hash, reinterpret_cast<const uint8_t*>(data), len);
			break;
		default:
			return std::string();
	}

	char hashStr[HASH_STR_MAX_LENGTH]{};
	for (int i = 0; i < ht; i++)
		snprintf(hashStr + i * 2, 3, "%02x", hash[i]);
	return hashStr;
}

void HashFromFilesDlg::onCommand(int id, int notification)
{
	if (notification != RcNotifyClicked)
		return;

	switch (id)
	{
		case IDCANCEL:
			hide();
			return;

		case IDOK:
			return;

		case IDC_HASH_FILEBROWSER_BUTTON:
		{
			// NPLX: CustomFileDialog::doOpenMultiFilesDlg
			const auto fns = nplx::uiServices()
				? nplx::uiServices()->getOpenFileNames(windowTitle().toStdWString(), L"All types (*)", L"")
				: std::vector<std::wstring>();
			if (!fns.empty())
				hashFiles(fns);
			return;
		}

		case IDC_HASH_TOCLIPBOARD_BUTTON:
		{
			const std::wstring rStr = getItemText(IDC_HASH_RESULT_EDIT);
			if (!rStr.empty())
				str2Clipboard(rStr);
			return;
		}

		default :
			return;
	}
}

void HashFromFilesDlg::hashFiles(const std::vector<std::wstring>& fns)
{
	std::wstring files2check, hashResultStr;
	for (const auto& it : fns)
	{
		// NPLX: upstream hashes MD5 with MD5::digestFile and the others from getFileContent; one path here
		bool bLoadingFailed = false;
		std::string content = getFileContent(it.c_str(), &bLoadingFailed);
		if (bLoadingFailed)
			return;

		const std::string hashStr = hashToHex(_ht, content.c_str(), content.length());

		files2check += it;
		files2check += L"\n"; // NPLX: LF (upstream CRLF)

		const std::wstring fileName = std::filesystem::path(it).filename().wstring(); // ::PathFindFileName
		hashResultStr += string2wstring(hashStr, CP_UTF8);
		hashResultStr += L"  ";
		hashResultStr += fileName;
		hashResultStr += L"\n";
	}

	if (!files2check.empty() && !hashResultStr.empty())
	{
		setItemText(IDC_HASH_PATH_EDIT, files2check);
		setItemText(IDC_HASH_RESULT_EDIT, hashResultStr);
	}
}

void HashFromFilesDlg::setHashType(hashType hashType2set)
{
	_ht = hashType2set;
}

void HashFromFilesDlg::doDialog()
{
	if (!isCreated())
	{
		buildFrom(IDD_HASHFROMFILES_DLG_def);
		setFont();
		// NPLX: Ctrl+A in the edits (upstream TextEditSelectAllProc) is the Qt default

		std::wstring title;
		std::wstring buttonText;

		switch (_ht)
		{
			case hash_md5:
			{
				title = L"Generate MD5 digest from files";
				buttonText = L"Choose files to &generate MD5...";
			}
			break;

			case hash_sha1:
			{
				title = L"Generate SHA-1 digest from files";
				buttonText = L"Choose files to &generate SHA-1...";
			}
			break;

			case hash_sha256:
			{
				title = L"Generate SHA-256 digest from files";
				buttonText = L"Choose files to &generate SHA-256...";
			}
			break;

			case hash_sha512:
			{
				title = L"Generate SHA-512 digest from files"; // NPLX: upstream says "SHA-1" here (typo)
				buttonText = L"Choose files to &generate SHA-512...";
			}
			break;

			default:
				return;
		}
		setWindowTitle(QString::fromStdWString(title));
		setItemText(IDC_HASH_FILEBROWSER_BUTTON, buttonText);
		// NPLX_TODO(Phase 8): nativeLangSpeaker->changeDlgLang(_hSelf, "MD5FromFilesDlg" / "SHA...FromFilesDlg")
	}

	show();
	raise();
	activateWindow();
}

void HashFromFilesDlg::setFont()
{
	// upstream createFont(L"Courier New", getDlgFontSize() + 1)
	const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
	dlgItem(IDC_HASH_PATH_EDIT)->setFont(mono);
	dlgItem(IDC_HASH_RESULT_EDIT)->setFont(mono);
}

void HashFromTextDlg::generateHash()
{
	if (_ht != hash_md5 && _ht != hash_sha1 && _ht != hash_sha256 && _ht != hash_sha512)
		return;

	const std::wstring text = getItemText(IDC_HASH_TEXT_EDIT);
	if (!text.empty())
	{
		// it's important to get text from UNICODE then convert it to UTF8
		// So we get the result of UTF8 text (tested with Chinese).
		const std::string newText = wstring2string(text, CP_UTF8);
		setItemText(IDC_HASH_RESULT_FOMTEXT_EDIT, string2wstring(hashToHex(_ht, newText.c_str(), newText.length()), CP_UTF8));
	}
	else
	{
		setItemText(IDC_HASH_RESULT_FOMTEXT_EDIT, L"");
	}
}

void HashFromTextDlg::generateHashPerLine()
{
	const std::wstring text = getItemText(IDC_HASH_TEXT_EDIT);
	if (!text.empty())
	{
		std::wstringstream ss(text);
		std::wstring aLine;
		std::string result;

		while (std::getline(ss, aLine))
		{
			if (aLine.empty()) // in case of UNIX EOL
			{
				result += "\n"; // NPLX: LF (upstream CRLF)
			}
			else
			{
				// getline() detect only '\n' but not "\r\n" under Windows
				// this hack is to walk around such bug
				if (aLine.back() == '\r')
					aLine.pop_back();

				if (aLine.empty()) // Windows EOL, both \n & \r are removed
				{
					result += "\n";
				}
				else
				{
					const std::string newText = wstring2string(aLine, CP_UTF8);
					result += hashToHex(_ht, newText.c_str(), newText.length());
					result += "\n";
				}
			}
		}

		setItemText(IDC_HASH_RESULT_FOMTEXT_EDIT, string2wstring(result, CP_UTF8));
	}
	else
	{
		setItemText(IDC_HASH_RESULT_FOMTEXT_EDIT, L"");
	}
}

void HashFromTextDlg::onCommand(int id, int notification)
{
	if (notification == RcNotifyEditChange && id == IDC_HASH_TEXT_EDIT)
	{
		if (isCheckedOrNot(IDC_HASH_EACHLINE_CHECK))
		{
			generateHashPerLine();
		}
		else
		{
			generateHash();
		}
		return;
	}
	if (notification != RcNotifyClicked)
		return;

	switch (id)
	{
		case IDCANCEL :
			hide();
			return;

		case IDOK :
			return;

		case IDC_HASH_EACHLINE_CHECK:
		{
			if (isCheckedOrNot(IDC_HASH_EACHLINE_CHECK))
			{
				generateHashPerLine();
			}
			else
			{
				generateHash();
			}
		}
		return;

		case IDC_HASH_FROMTEXT_TOCLIPBOARD_BUTTON:
		{
			const std::wstring rStr = getItemText(IDC_HASH_RESULT_FOMTEXT_EDIT);
			if (!rStr.empty())
				str2Clipboard(rStr);
		}
		return;

		default :
			return;
	}
}

void HashFromTextDlg::setHashType(hashType hashType2set)
{
	_ht = hashType2set;
}

void HashFromTextDlg::doDialog()
{
	if (!isCreated())
	{
		buildFrom(IDD_HASHFROMTEXT_DLG_def);
		setFont();

		std::wstring title;
		switch (_ht)
		{
			case hash_md5:
			{
				title = L"Generate MD5 digest";
			}
			break;

			case hash_sha1:
			{
				title = L"Generate SHA-1 digest";
			}
			break;

			case hash_sha256:
			{
				title = L"Generate SHA-256 digest";
			}
			break;

			case hash_sha512:
			{
				title = L"Generate SHA-512 digest";
			}
			break;

			default:
				break;
		}

		setWindowTitle(QString::fromStdWString(title));
		// NPLX_TODO(Phase 8): nativeLangSpeaker->changeDlgLang(_hSelf, "MD5FromTextDlg" / "SHA...FromTextDlg")
	}

	show();
	raise();
	activateWindow();
}

void HashFromTextDlg::setFont()
{
	// upstream createFont(L"Courier New", getDlgFontSize() + 1)
	const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
	dlgItem(IDC_HASH_TEXT_EDIT)->setFont(mono);
	dlgItem(IDC_HASH_RESULT_FOMTEXT_EDIT)->setFont(mono);
}
