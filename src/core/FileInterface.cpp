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

#include "FileInterface.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include "Common.h"
#include "Parameters.h"
#include "UiServices.h"

NppIO_File::NppIO_File(const wchar_t* fname)
{
	if (fname)
	{
		_path = nplx::toNativePath(fname);

		// O_TRUNC keeps an existing file's inode and attributes (upstream: TRUNCATE_EXISTING /
		// CREATE_ALWAYS + restoring the creation time and attributes). New files get 0666 & ~umask.
		do
		{
			_fd = ::open(_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
		} while (_fd == -1 && errno == EINTR);

		if (_fd == -1)
			_dwErrorCode = static_cast<DWORD>(errno);
	}
}

void NppIO_File::close()
{
	_dwErrorCode = NO_ERROR; // reset

	if (isOpened())
	{
		DWORD flushError = NO_ERROR;
		if (_written)
		{
			if (::fsync(_fd) != 0 && errno != EINVAL && errno != EROFS) // EINVAL: special files (pipes, /dev/...)
			{
				flushError = static_cast<DWORD>(errno);
				std::wstring errNumberMsg = std::to_wstring(flushError) + L" - " + string2wstring(std::strerror(static_cast<int>(flushError)), CP_UTF8);

				NppParameters& nppParam = NppParameters::getInstance();
				if (!nppParam.isEndSessionCritical())
				{
					// because of there is not an externally forced shutdown/restart in progress,
					// we can at least alert the user that the file data could not actually be saved
					std::wstring errMsg = L"NotePadLX has encountered a serious system problem while saving:\n\n";
					errMsg += string2wstring(_path, CP_UTF8);
					errMsg += L"\n\nThat file, temporarily stored in the system cache, cannot be finally committed to the storage device selected! \
This is probably a storage driver or hardware issue, beyond the control of NotePadLX. \
Please try using another storage and also check if your saved data is not corrupted.\n\nError Code reported: ";
					errMsg += errNumberMsg;
					nplx::messageBox(errMsg, L"WARNING - filebuffer flushing fail!", MB_OK | MB_ICONWARNING);
				}
				else
				{
					// writing breif log here
					std::wstring nppIssueLog = nppParam.getUserPath();
					pathAppend(nppIssueLog, L"nppFlushFileBuffersFails.log");
					writeLog(nppIssueLog.c_str(), wstring2string(errNumberMsg, CP_UTF8).c_str());
				}
			}
		}

		_dwErrorCode = flushError; // store possible flushing error 1st

		if (::close(_fd) != 0)
		{
			if (!flushError)
				_dwErrorCode = static_cast<DWORD>(errno); // store
		}

		_fd = -1;
	}
}

bool NppIO_File::write(const void* wbuf, size_t buf_size)
{
	_dwErrorCode = NO_ERROR; // reset

	if (!isOpened() || (wbuf == nullptr))
		return false;

	const char* p = static_cast<const char*>(wbuf);
	size_t bytes_left_to_write = buf_size;

	while (bytes_left_to_write > 0)
	{
		const ssize_t written = ::write(_fd, p, bytes_left_to_write);
		if (written < 0)
		{
			if (errno == EINTR)
				continue;
			_dwErrorCode = static_cast<DWORD>(errno); // store
			return false;
		}
		p += written;
		bytes_left_to_write -= static_cast<size_t>(written);
	}

	_written = true;
	return true;
}
