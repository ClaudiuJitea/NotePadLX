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

// NPLX: Win32 *data types* and MSVC CRT string helpers, so ported upstream logic compiles with
// minimal edits. By design this header does NOT provide windows, handles (HWND, HMENU, ...),
// messages (SendMessage) or the registry: every such call site gets real Linux/Qt code.

#pragma once

#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <strings.h>

#include "Sci_Position.h"

// ---------------------------------------------------------------------------
// Basic types
// ---------------------------------------------------------------------------
using BOOL = int;
using BYTE = unsigned char;
using UCHAR = unsigned char;
using WORD = unsigned short;
using DWORD = uint32_t;
using UINT = unsigned int;
using LONG = int32_t;
using ULONG = uint32_t;
using INT_PTR = intptr_t;
using UINT_PTR = uintptr_t;
using LONG_PTR = intptr_t;
using WPARAM = uintptr_t;  // same as Scintilla uptr_t
using LPARAM = intptr_t;   // same as Scintilla sptr_t
using LRESULT = intptr_t;
using COLORREF = DWORD;    // 0x00BBGGRR, same layout as Scintilla colours
using LPCWSTR = const wchar_t*;
using LPWSTR = wchar_t*;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#ifndef MAX_PATH
#define MAX_PATH 4096 // NPLX: PATH_MAX on Linux (upstream: 260)
#endif

#define NO_ERROR 0

struct RECT
{
	LONG left = 0;
	LONG top = 0;
	LONG right = 0;
	LONG bottom = 0;
};

struct POINT
{
	LONG x = 0;
	LONG y = 0;
};

struct SIZE
{
	LONG cx = 0;
	LONG cy = 0;
};

// Windows file time: 100-nanosecond intervals since 1601-01-01 (UTC).
// Kept as the in-memory timestamp type because session.xml stores it
// (originalFileLastModifTimestamp / originalFileLastModifTimestampHigh).
struct FILETIME
{
	DWORD dwLowDateTime = 0;
	DWORD dwHighDateTime = 0;
};

inline bool operator==(const FILETIME& a, const FILETIME& b)
{
	return a.dwLowDateTime == b.dwLowDateTime && a.dwHighDateTime == b.dwHighDateTime;
}

inline long CompareFileTime(const FILETIME* a, const FILETIME* b)
{
	const uint64_t va = (static_cast<uint64_t>(a->dwHighDateTime) << 32) | a->dwLowDateTime;
	const uint64_t vb = (static_cast<uint64_t>(b->dwHighDateTime) << 32) | b->dwLowDateTime;
	return va < vb ? -1 : (va > vb ? 1 : 0);
}

// File attributes as returned by GetFileAttributesEx (filled from stat() by Common.cpp).
// READONLY = no write permission for the current user; HIDDEN = dot-file name.
#define INVALID_FILE_ATTRIBUTES  static_cast<DWORD>(-1)
#define FILE_ATTRIBUTE_READONLY  0x00000001
#define FILE_ATTRIBUTE_HIDDEN    0x00000002
#define FILE_ATTRIBUTE_SYSTEM    0x00000004
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_NORMAL    0x00000080

struct WIN32_FILE_ATTRIBUTE_DATA
{
	DWORD dwFileAttributes = INVALID_FILE_ATTRIBUTES;
	FILETIME ftCreationTime;   // NPLX: Linux has no portable creation time: the status change time
	FILETIME ftLastAccessTime;
	FILETIME ftLastWriteTime;
	DWORD nFileSizeHigh = 0;
	DWORD nFileSizeLow = 0;
};

enum GET_FILEEX_INFO_LEVELS { GetFileExInfoStandard };

union ULARGE_INTEGER
{
	struct { DWORD LowPart; DWORD HighPart; };
	uint64_t QuadPart;
};

union LARGE_INTEGER
{
	struct { DWORD LowPart; LONG HighPart; };
	int64_t QuadPart;
};

// NPLX: ported code stores errno values where upstream stores GetLastError() codes
#define ERROR_ACCESS_DENIED  EACCES
#define ERROR_FILE_NOT_FOUND ENOENT

// ---------------------------------------------------------------------------
// Colours / words
// ---------------------------------------------------------------------------
constexpr COLORREF RGB(int r, int g, int b)
{
	return static_cast<COLORREF>((r & 0xFF) | ((g & 0xFF) << 8) | ((b & 0xFF) << 16));
}
constexpr BYTE GetRValue(COLORREF rgb) { return static_cast<BYTE>(rgb & 0xFF); }
constexpr BYTE GetGValue(COLORREF rgb) { return static_cast<BYTE>((rgb >> 8) & 0xFF); }
constexpr BYTE GetBValue(COLORREF rgb) { return static_cast<BYTE>((rgb >> 16) & 0xFF); }

constexpr WORD LOWORD(uintptr_t l) { return static_cast<WORD>(l & 0xFFFF); }
constexpr WORD HIWORD(uintptr_t l) { return static_cast<WORD>((l >> 16) & 0xFFFF); }
constexpr BYTE LOBYTE(uintptr_t w) { return static_cast<BYTE>(w & 0xFF); }
constexpr BYTE HIBYTE(uintptr_t w) { return static_cast<BYTE>((w >> 8) & 0xFF); }
constexpr DWORD MAKELONG(WORD a, WORD b) { return static_cast<DWORD>(a) | (static_cast<DWORD>(b) << 16); }

// ---------------------------------------------------------------------------
// Code pages (numbers kept, so config.xml and EncodingMapper stay compatible)
// ---------------------------------------------------------------------------
#define CP_ACP   0      // NPLX: the locale charset (normally UTF-8 on Linux)
#define CP_OEMCP 1
#define CP_UTF8  65001

// ---------------------------------------------------------------------------
// Messages: only the numeric bases, needed by NPPM_*/IDM_* definitions
// ---------------------------------------------------------------------------
#define WM_USER 0x0400
#define WM_COMMAND 0x0111 // NPLX: message ids kept for the internal dispatcher (Notepad_plus::nppMessage)
#define WM_MOUSEWHEEL 0x020A // NPLX: the editor forwards a wheel turned with the right button down
#define MK_RBUTTON 0x0002
#define WM_APP  0x8000

// MessageBox-style flags used by upstream non-UI code paths (mapped to Qt dialogs in ui/)
#define MB_OK          0x00000000L
#define MB_OKCANCEL    0x00000001L
#define MB_YESNOCANCEL 0x00000003L
#define MB_YESNO       0x00000004L
#define MB_ICONHAND        0x00000010L
#define MB_ICONQUESTION    0x00000020L
#define MB_ICONEXCLAMATION 0x00000030L
#define MB_ICONASTERISK    0x00000040L
#define MB_ICONWARNING     MB_ICONEXCLAMATION
#define MB_ICONERROR       MB_ICONHAND
#define MB_ICONSTOP        MB_ICONHAND
#define MB_DEFBUTTON1      0x00000000L
#define MB_DEFBUTTON2      0x00000100L
#define MB_ICONINFORMATION MB_ICONASTERISK
#define MB_APPLMODAL   0x00000000L
#define MB_TASKMODAL   0x00002000L
#define MB_SETFOREGROUND 0x00010000L
#define IDOK     1
#define IDCANCEL 2
#define IDYES    6
#define IDNO     7
#define IDRETRY  4
#define IDIGNORE 5

// ---------------------------------------------------------------------------
// MSVC CRT helpers
// ---------------------------------------------------------------------------
template <typename T, size_t N>
constexpr size_t _countof(T (&)[N]) noexcept { return N; }

inline int _stricmp(const char* a, const char* b) { return ::strcasecmp(a, b); }
inline int _strnicmp(const char* a, const char* b, size_t n) { return ::strncasecmp(a, b, n); }
inline int _wcsicmp(const wchar_t* a, const wchar_t* b) { return ::wcscasecmp(a, b); }
inline int _wcsnicmp(const wchar_t* a, const wchar_t* b, size_t n) { return ::wcsncasecmp(a, b, n); }
inline int lstrcmp(const wchar_t* a, const wchar_t* b) { return ::wcscmp(a, b); }
inline int lstrcmpi(const wchar_t* a, const wchar_t* b) { return ::wcscasecmp(a, b); }
inline size_t lstrlen(const wchar_t* s) { return ::wcslen(s); }

inline int wcscpy_s(wchar_t* dest, size_t destSize, const wchar_t* src)
{
	if (!dest || destSize == 0)
		return EINVAL;
	const size_t len = ::wcslen(src);
	const size_t n = std::min(len, destSize - 1);
	::wmemcpy(dest, src, n);
	dest[n] = L'\0';
	return len < destSize ? 0 : ERANGE;
}

template <size_t N>
inline int wcscpy_s(wchar_t (&dest)[N], const wchar_t* src) { return wcscpy_s(dest, N, src); }

inline int wcscat_s(wchar_t* dest, size_t destSize, const wchar_t* src)
{
	const size_t cur = ::wcsnlen(dest, destSize);
	if (cur >= destSize)
		return EINVAL;
	return wcscpy_s(dest + cur, destSize - cur, src);
}

template <size_t N>
inline int wcscat_s(wchar_t (&dest)[N], const wchar_t* src) { return wcscat_s(dest, N, src); }

inline int wcsncpy_s(wchar_t* dest, size_t destSize, const wchar_t* src, size_t count)
{
	if (!dest || destSize == 0)
		return EINVAL;
	const size_t n = std::min({ ::wcsnlen(src, count), destSize - 1 });
	::wmemcpy(dest, src, n);
	dest[n] = L'\0';
	return 0;
}

inline int strcpy_s(char* dest, size_t destSize, const char* src)
{
	if (!dest || destSize == 0)
		return EINVAL;
	const size_t len = ::strlen(src);
	const size_t n = std::min(len, destSize - 1);
	::memcpy(dest, src, n);
	dest[n] = '\0';
	return len < destSize ? 0 : ERANGE;
}

template <size_t N>
inline int strcpy_s(char (&dest)[N], const char* src) { return strcpy_s(dest, N, src); }

inline int strcat_s(char* dest, size_t destSize, const char* src)
{
	const size_t cur = ::strnlen(dest, destSize);
	if (cur >= destSize)
		return EINVAL;
	return strcpy_s(dest + cur, destSize - cur, src);
}

#define _TRUNCATE (static_cast<size_t>(-1))
#define __inout // SAL annotation

inline int strncpy_s(char* dest, size_t destSize, const char* src, size_t count)
{
	if (!dest || destSize == 0)
		return EINVAL;
	const size_t n = std::min({ ::strnlen(src, count), destSize - 1 });
	::memcpy(dest, src, n);
	dest[n] = '\0';
	return 0;
}

inline int memcpy_s(void* dest, size_t destSize, const void* src, size_t count)
{
	if (count > destSize)
		return ERANGE;
	::memcpy(dest, src, count);
	return 0;
}

#define swprintf_s swprintf
#define sprintf_s snprintf
#define _snwprintf_s(buf, size, count, ...) swprintf(buf, size, __VA_ARGS__)

inline wchar_t* _wcsupr_s(wchar_t* s, size_t) { for (wchar_t* p = s; *p; ++p) *p = static_cast<wchar_t>(::towupper(*p)); return s; }
inline wchar_t* _wcslwr_s(wchar_t* s, size_t) { for (wchar_t* p = s; *p; ++p) *p = static_cast<wchar_t>(::towlower(*p)); return s; }

// user32 character functions. CharUpperW/CharLowerW with a character (high word zero) in place of a
// string pointer convert that single character, which is how upstream calls them.
inline LPWSTR CharUpperW(LPWSTR lpsz)
{
	const uintptr_t v = reinterpret_cast<uintptr_t>(lpsz);
	if (v <= 0xFFFF)
		return reinterpret_cast<LPWSTR>(static_cast<uintptr_t>(::towupper(static_cast<wint_t>(v))));
	for (wchar_t* p = lpsz; *p; ++p) *p = static_cast<wchar_t>(::towupper(*p));
	return lpsz;
}
inline LPWSTR CharLowerW(LPWSTR lpsz)
{
	const uintptr_t v = reinterpret_cast<uintptr_t>(lpsz);
	if (v <= 0xFFFF)
		return reinterpret_cast<LPWSTR>(static_cast<uintptr_t>(::towlower(static_cast<wint_t>(v))));
	for (wchar_t* p = lpsz; *p; ++p) *p = static_cast<wchar_t>(::towlower(*p));
	return lpsz;
}
inline BOOL IsCharAlphaW(wchar_t ch) { return ::iswalpha(static_cast<wint_t>(ch)) ? TRUE : FALSE; }
inline BOOL IsCharAlphaNumericW(wchar_t ch) { return ::iswalnum(static_cast<wint_t>(ch)) ? TRUE : FALSE; }
inline BOOL IsCharLowerW(wchar_t ch) { return ::iswlower(static_cast<wint_t>(ch)) ? TRUE : FALSE; }
inline BOOL IsCharUpperW(wchar_t ch) { return ::iswupper(static_cast<wint_t>(ch)) ? TRUE : FALSE; }

inline int _wtoi(const wchar_t* s) { return static_cast<int>(::wcstol(s, nullptr, 10)); }
inline long long _wtoi64(const wchar_t* s) { return ::wcstoll(s, nullptr, 10); }
inline double _wtof(const wchar_t* s) { return ::wcstod(s, nullptr); }
