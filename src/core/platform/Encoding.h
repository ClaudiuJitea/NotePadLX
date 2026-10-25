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

// NPLX: replacement for Win32 MultiByteToWideChar / WideCharToMultiByte, built on iconv.
// Windows code page numbers are kept as the public currency (config.xml, EncodingMapper,
// the Encoding menu all use them).

#pragma once

#include <string>
#include <cstddef>

namespace nplx::encoding
{
	// Code page used for UTF-32 wchar_t ("WCHAR_T" in iconv). Not a Windows code page.
	inline constexpr int CP_WCHAR = -2;

	// iconv charset name for a Windows code page number, nullptr if unknown.
	// CP_ACP (0) and CP_OEMCP (1) map to the current locale's charset.
	const char* charsetFromCodepage(int codepage);

	bool isCodepageSupported(int codepage);

	// Converts len bytes of src from one code page to another.
	// Invalid input bytes are replaced (U+FFFD for Unicode targets, '?' otherwise), like Windows does.
	// If bytesNotProcessed is given, an incomplete multibyte sequence at the end of the input is not
	// converted and its length is returned there (for chunked conversions); otherwise it is replaced.
	// Returns false if one of the code pages is unsupported.
	bool convert(int fromCodepage, int toCodepage, const char* src, size_t len, std::string& out, int* bytesNotProcessed = nullptr);

	std::wstring toWide(const char* src, size_t len, int codepage, int* bytesNotProcessed = nullptr);
	std::string fromWide(const wchar_t* src, size_t len, int codepage);
}
