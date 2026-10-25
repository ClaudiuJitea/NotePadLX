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

// NPLX: port of upstream MISC/md5/md5Dlgs (Tools > MD5 / SHA-1 / SHA-256 / SHA-512 > Generate..., Generate from
// files...), built from IDD_HASHFROMTEXT_DLG / IDD_HASHFROMFILES_DLG. Modeless, like upstream. The digests are the
// upstream portable implementations in core/hash (SHA-512: a portable replacement of the CryptoAPI call).

#pragma once

#include <string>
#include <vector>

#include "RcDialog.h"

enum hashType {hash_md5 = 16, hash_sha1 = 20, hash_sha256 = 32, hash_sha512 = 64};

#define HASH_MAX_LENGTH hash_sha512
#define HASH_STR_MAX_LENGTH (hash_sha512 * 2 + 1)

// NPLX: the hex digest of a byte string (the computation shared by the dialogs and the "into clipboard" commands)
std::string hashToHex(hashType ht, const char* data, size_t len);

class HashFromFilesDlg : public RcDialog
{
	Q_OBJECT

public :
	explicit HashFromFilesDlg(QWidget* parent = nullptr) : RcDialog(parent) {}
	void doDialog();
	void setHashType(hashType hashType2set);
	void hashFiles(const std::vector<std::wstring>& fns); // NPLX: IDC_HASH_FILEBROWSER_BUTTON after the file dialog

protected :
	void onCommand(int id, int notification) override;
	void reject() override { hide(); } // upstream IDCANCEL: display(false)
	hashType _ht = hash_md5;

private :
	void setFont();
};

class HashFromTextDlg : public RcDialog
{
	Q_OBJECT

public :
	explicit HashFromTextDlg(QWidget* parent = nullptr) : RcDialog(parent) {}
	void doDialog();
	void generateHash();
	void generateHashPerLine();
	void setHashType(hashType hashType2set);

protected :
	void onCommand(int id, int notification) override;
	void reject() override { hide(); } // upstream IDCANCEL: display(false)
	hashType _ht = hash_md5;

private :
	void setFont();
};
