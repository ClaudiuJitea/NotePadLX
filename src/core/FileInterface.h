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

#pragma once

#include <string>
#include <cstdint>

#include "win32compat.h"

// NPLX: POSIX port of upstream Win32_IO_File (MISC/Common/FileInterface.h).
// Like upstream, an existing file is truncated and rewritten in place, so its inode, permissions,
// owner, hard links and symlinks are preserved. Data is fsync()ed on close.
// getLastErrorCode() returns an errno value (0 = no error).
class NppIO_File final
{
public:
	explicit NppIO_File(const wchar_t* fname);

	NppIO_File() = delete;
	NppIO_File(const NppIO_File&) = delete;
	NppIO_File& operator=(const NppIO_File&) = delete;

	~NppIO_File() {
		close();
	}

	bool isOpened() const {
		return _fd != -1;
	}

	void close();

	bool write(const void* wbuf, size_t buf_size);

	bool writeStr(const std::string& str) {
		return write(str.c_str(), str.length());
	}

	DWORD getLastErrorCode() const {
		return _dwErrorCode;
	}

private:
	int _fd = -1;
	bool _written = false;
	std::string _path;

	DWORD _dwErrorCode = NO_ERROR;
};
