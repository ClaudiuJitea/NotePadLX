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

// NPLX: portable subset of upstream PowerEditor/src/MISC/Common/Common.h.
// Same names and signatures; the Win32/UI helpers (folderBrowser, tooltips, clipboard, fonts,
// COM, UAC, certificate checks) are not here and will be provided by ui/ where needed.

#pragma once

#include <algorithm>
#include <ctime>
#include <locale>
#include <locale.h>
#include <string>
#include <unordered_set>
#include <vector>

#include "win32compat.h"

// NPLX: native path separator. Upstream hard-codes '\\'.
inline constexpr wchar_t NPLX_PATH_SEP = L'/';

std::wstring commafyInt(size_t n);

void writeLog(const wchar_t* logFileName, const char* log2write);
void writeLog(const wchar_t* logFileName, const wchar_t* log2write);

std::wstring purgeMenuItemString(const wchar_t* menuItemStr, bool keepAmpersand = false);
std::vector<std::wstring> tokenizeString(const std::wstring& tokenString, const char delim);

std::wstring string2wstring(const std::string& rString, UINT codepage = CP_UTF8);
std::string wstring2string(const std::wstring& rwString, UINT codepage = CP_UTF8);
bool isInList(const wchar_t* token, const wchar_t* list);
std::wstring BuildMenuFileName(int filenameLen, unsigned int pos, const std::wstring& filename, bool ordinalNumber = true);

std::string getFileContent(const wchar_t* file2read, bool* pbFailed = nullptr);
std::wstring relativeFilePathToFullFilePath(const wchar_t* relativeFilePath);
void writeFileContent(const wchar_t* file2write, const char* content2write);
bool matchInList(const wchar_t* fileName, const std::vector<std::wstring>& patterns);
bool matchInExcludeDirList(const wchar_t* dirName, const std::vector<std::wstring>& patterns, size_t level);
bool allPatternsAreExclusion(const std::vector<std::wstring>& patterns);

// NPLX: replacement for shlwapi PathMatchSpec (case-insensitive '*' and '?' wildcards, ';'-separated specs)
bool pathMatchSpec(const wchar_t* fileName, const wchar_t* spec);

class WcharMbcsConvertor final
{
public:
	static WcharMbcsConvertor& getInstance() {
		static WcharMbcsConvertor instance;
		return instance;
	}

	// NPLX: on Linux wchar_t is UTF-32. Lengths are in wchar_t units / bytes like upstream.
	const wchar_t* char2wchar(const char* mbcs2Convert, size_t codepage, int lenMbcs = -1, int* pLenWc = nullptr, int* pBytesNotProcessed = nullptr);
	// mstart/mend: byte offsets in the input, converted to wchar_t offsets in the output
	const wchar_t* char2wchar(const char* mbcs2Convert, size_t codepage, intptr_t* mstart, intptr_t* mend, int mbcsLen = 0);
	size_t getSizeW() const { return _wideCharStr.size(); }

	const char* wchar2char(const wchar_t* wcharStr2Convert, size_t codepage, int lenWc = -1, int* pLenMbcs = nullptr);
	// mstart/mend: wchar_t offsets in the input, converted to byte offsets in the output
	const char* wchar2char(const wchar_t* wcharStr2Convert, size_t codepage, intptr_t* mstart, intptr_t* mend, int wcharLenIn = 0, int* lenOut = nullptr);
	size_t getSizeA() const { return _multiByteStr.size(); }

	const char* encode(UINT fromCodepage, UINT toCodepage, const char* txt2Encode, int lenIn = -1, int* pLenOut = nullptr, int* pBytesNotProcessed = nullptr);

private:
	WcharMbcsConvertor() = default;
	~WcharMbcsConvertor() = default;
	WcharMbcsConvertor(const WcharMbcsConvertor&) = delete;
	WcharMbcsConvertor& operator= (const WcharMbcsConvertor&) = delete;

	std::string _multiByteStr;
	std::wstring _wideCharStr;
};

std::wstring pathRemoveFileSpec(std::wstring& path);
std::wstring pathAppend(std::wstring& strDest, const std::wstring& str2append);

std::wstring stringToUpper(std::wstring strToConvert);
std::wstring stringToLower(std::wstring strToConvert);
std::wstring stringReplace(std::wstring subject, const std::wstring& search, const std::wstring& replace);
void stringSplit(const std::wstring& input, const std::wstring& delimiter, std::vector<std::wstring>& output);
bool str2numberVector(std::wstring str2convert, std::vector<size_t>& numVect);
void stringJoin(const std::vector<std::wstring>& strings, const std::wstring& separator, std::wstring& joinedString);
std::wstring stringTakeWhileAdmissable(const std::wstring& input, const std::wstring& admissable);

// NPLX: _locale_t -> POSIX locale_t (wcstod_l)
double stodLocale(const std::wstring& str, locale_t loc, size_t* idx = nullptr);
const std::locale& getSysLocale();

bool deleteFileOrFolder(const std::wstring& f2delete);
// NPLX: extTypeFilter is a wildcard such as L"*.xml"
void getFilesInFolder(std::vector<std::wstring>& files, const std::wstring& extTypeFilter, const std::wstring& inFolder);

template<typename T>
size_t vecRemoveDuplicates(std::vector<T>& vec, bool isSorted = false, bool canSort = false)
{
	if (!isSorted && canSort)
	{
		std::sort(vec.begin(), vec.end());
		isSorted = true;
	}

	if (isSorted)
	{
		typename std::vector<T>::iterator it;
		it = std::unique(vec.begin(), vec.end());
		vec.resize(distance(vec.begin(), it));  // unique() does not shrink the vector
	}
	else
	{
		std::unordered_set<T> seen;
		auto newEnd = std::remove_if(vec.begin(), vec.end(), [&seen](const T& value)
			{
				return !seen.insert(value).second;
			});
		vec.erase(newEnd, vec.end());
	}
	return vec.size();
}

void trim(std::wstring& str);

int nbDigitsFromNbLines(size_t nbLines);

// NPLX: dateTimeFormat uses the upstream (Windows GetDateFormat/GetTimeFormat) pattern letters:
// yyyy yy MM M dd d HH H hh h mm m ss s tt
std::wstring getDateTimeStrFrom(const std::wstring& dateTimeFormat, const std::tm& st);

bool removeReadOnlyFlagFromFileAttributes(const wchar_t* fileFullPath);
bool toggleReadOnlyFlagFromFileAttributes(const wchar_t* fileFullPath, bool& isChangedToReadOnly);

bool isUncPath(const std::wstring& path);
bool isUncFileUrl(const std::wstring& url);

class Version final
{
public:
	Version() = default;
	explicit Version(const std::wstring& versionStr);

	std::wstring toString() const;
	static bool isNumber(const std::wstring& s) {
		static const auto& loc = std::locale::classic();
		return !s.empty() &&
			find_if(s.begin(), s.end(), [](auto c) { return !std::isdigit(c, loc); }) == s.end();
	}

	int compareTo(const Version& v2c) const;

	bool operator < (const Version& v2c) const { return compareTo(v2c) == -1; }
	bool operator <= (const Version& v2c) const { int r = compareTo(v2c); return r == -1 || r == 0; }
	bool operator > (const Version& v2c) const { return compareTo(v2c) == 1; }
	bool operator >= (const Version& v2c) const { int r = compareTo(v2c); return r == 1 || r == 0; }
	bool operator == (const Version& v2c) const { return compareTo(v2c) == 0; }
	bool operator != (const Version& v2c) const { return compareTo(v2c) != 0; }
	bool empty() const { return _major == 0 && _minor == 0 && _patch == 0 && _build == 0; }
	bool isCompatibleTo(const Version& from, const Version& to) const;

private:
	unsigned long _major = 0;
	unsigned long _minor = 0;
	unsigned long _patch = 0;
	unsigned long _build = 0;
};

// NPLX: GetFileAttributesEx on POSIX stat(). Returns FALSE if the path does not exist.
BOOL GetFileAttributesEx(const wchar_t* path, GET_FILEEX_INFO_LEVELS, WIN32_FILE_ATTRIBUTE_DATA* fileAttr);
BOOL getFileAttributesExWithTimeout(const wchar_t* filePath, WIN32_FILE_ATTRIBUTE_DATA* fileAttr,
	DWORD milliSec2wait = 0, bool* isTimeoutReached = nullptr, DWORD* pdwWin32ApiError = nullptr);
BOOL getDiskFreeSpaceWithTimeout(const wchar_t* dirPath, ULARGE_INTEGER* freeBytesForUser,
	DWORD milliSec2wait = 0, bool* isTimeoutReached = nullptr);

// NPLX: Windows-only path forms (\\?\ prefix) never occur on Linux
inline bool isWin32NamespacePrefixedFileName(const std::wstring&) { return false; }
inline bool isWin32NamespacePrefixedFileName(const wchar_t*) { return false; }

// NPLX: true if the path is on a network file system (NFS, SMB/CIFS, sshfs/FUSE, ...)
BOOL PathIsNetworkPath(const wchar_t* path);

// NPLX: replacement for PathCompactPathEx: shortens a path/file name to maxLen characters with "..."
std::wstring pathCompact(const std::wstring& path, size_t maxLen);

// NPLX_TODO(Phase 3): elevated file operations (upstream: UAC via "notepad++.exe #UAC-...#").
// Returns an errno value; currently always EACCES (no elevation helper yet).
DWORD invokeNppUacOp(const std::wstring& strCmdLineParams);

// NPLX: upstream variants take a timeout for network paths; on Linux stat() is used directly.
bool doesFileExist(const wchar_t* filePath, DWORD milliSec2wait = 0, bool* isTimeoutReached = nullptr);
bool doesDirectoryExist(const wchar_t* dirPath, DWORD milliSec2wait = 0, bool* isTimeoutReached = nullptr);
bool doesPathExist(const wchar_t* path, DWORD milliSec2wait = 0, bool* isTimeoutReached = nullptr);

// NPLX: Linux helpers used in place of Win32 file APIs by ported code
namespace nplx
{
	std::string toNativePath(const std::wstring& path);      // UTF-8 for syscalls
	std::wstring fromNativePath(const std::string& path);
	bool createDirectories(const std::wstring& dir);
	bool copyFile(const std::wstring& from, const std::wstring& to, bool failIfExists);
	bool moveFileReplace(const std::wstring& from, const std::wstring& to);
	bool deleteFile(const std::wstring& path);
	std::wstring fullPathName(const std::wstring& path);   // absolute, normalised; symlinks are kept
	std::wstring tempDirectory();
	std::wstring getEnv(const char* name);
}

void expandEnv(std::wstring& path2Expand);

// NPLX: shlwapi path helpers used by ported code (pure string functions, '/' separator)
const wchar_t* PathFindFileName(const wchar_t* path);
const wchar_t* PathFindExtension(const wchar_t* path);  // points to the '.' or to the terminating NUL
void PathRemoveExtension(std::wstring& path);
bool PathIsRelative(const wchar_t* path);

// NPLX: shlwapi ColorRGBToHLS / ColorHLSToRGB (hue, lightness, saturation in 0..240)
void ColorRGBToHLS(COLORREF clrRGB, WORD* pwHue, WORD* pwLuminance, WORD* pwSaturation);
COLORREF ColorHLSToRGB(WORD wHue, WORD wLuminance, WORD wSaturation);

// NPLX: FILETIME <-> Unix time, and the last modification time of a file (FILETIME, UTC)
FILETIME unixTimeToFileTime(int64_t seconds, int64_t nanoseconds = 0);
int64_t fileTimeToUnixTime(const FILETIME& ft);
bool getFileLastWriteTime(const wchar_t* filePath, FILETIME& lastWriteTime);
FILETIME getSystemTimeAsFileTime();
bool fileTimeToYMD(const FILETIME& ft, int& yyyymmdd); // local date as yyyymmdd
