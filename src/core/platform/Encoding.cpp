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

#include "Encoding.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <langinfo.h>
#include <iconv.h>
#include <mutex>
#include <unordered_map>

namespace nplx::encoding
{
namespace
{
	struct CodepageName
	{
		int codepage;
		const char* charset;
	};

	// Every code page of upstream EncodingMapper, plus the Unicode ones used by Utf8_16 / uchardet.
	constexpr CodepageName codepageNames[] = {
		{ 65001, "UTF-8" },
		{ 1200,  "UTF-16LE" },
		{ 1201,  "UTF-16BE" },
		{ 12000, "UTF-32LE" },
		{ 12001, "UTF-32BE" },
		{ CP_WCHAR, "WCHAR_T" },
		{ 1250, "CP1250" }, { 1251, "CP1251" }, { 1252, "CP1252" }, { 1253, "CP1253" }, { 1254, "CP1254" },
		{ 1255, "CP1255" }, { 1256, "CP1256" }, { 1257, "CP1257" }, { 1258, "CP1258" },
		{ 28591, "ISO-8859-1" }, { 28592, "ISO-8859-2" }, { 28593, "ISO-8859-3" }, { 28594, "ISO-8859-4" },
		{ 28595, "ISO-8859-5" }, { 28596, "ISO-8859-6" }, { 28597, "ISO-8859-7" }, { 28598, "ISO-8859-8" },
		{ 28599, "ISO-8859-9" }, { 28600, "ISO-8859-10" }, { 28601, "ISO-8859-11" }, { 28603, "ISO-8859-13" },
		{ 28604, "ISO-8859-14" }, { 28605, "ISO-8859-15" }, { 28606, "ISO-8859-16" },
		{ 437, "CP437" }, { 720, "CP720" /* built-in table, see builtinSingleByteTable() */ }, { 737, "CP737" }, { 775, "CP775" }, { 850, "CP850" }, { 852, "CP852" },
		{ 855, "CP855" }, { 857, "CP857" }, { 858, "CP858" }, { 860, "CP860" }, { 861, "CP861" }, { 862, "CP862" },
		{ 863, "CP863" }, { 865, "CP865" }, { 866, "CP866" }, { 869, "CP869" },
		{ 950, "CP950" },     // Big5 (Microsoft variant)
		{ 936, "CP936" },     // GBK
		{ 54936, "GB18030" },
		{ 932, "CP932" },     // Shift_JIS (Microsoft variant)
		{ 949, "CP949" },     // Korean Unified Hangul Code
		{ 51949, "EUC-KR" },
		{ 51932, "EUC-JP" },
		{ 50220, "ISO-2022-JP" },
		{ 874, "CP874" },     // TIS-620 / Windows Thai
		{ 10007, "MAC-CYRILLIC" },
		{ 21866, "KOI8-U" },
		{ 20866, "KOI8-R" },
		{ 20127, "ASCII" },
	};

	// Code pages that glibc iconv does not provide: built-in single-byte tables for bytes 0x80-0xFF
	// (generated from Python's codecs; bytes 0x00-0x7F are ASCII).
	constexpr char16_t cp720High[128] = {
		0x0080, 0x0081, 0x00E9, 0x00E2, 0x0084, 0x00E0, 0x0086, 0x00E7,
		0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x008D, 0x008E, 0x008F,
		0x0090, 0x0651, 0x0652, 0x00F4, 0x00A4, 0x0640, 0x00FB, 0x00F9,
		0x0621, 0x0622, 0x0623, 0x0624, 0x00A3, 0x0625, 0x0626, 0x0627,
		0x0628, 0x0629, 0x062A, 0x062B, 0x062C, 0x062D, 0x062E, 0x062F,
		0x0630, 0x0631, 0x0632, 0x0633, 0x0634, 0x0635, 0x00AB, 0x00BB,
		0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
		0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
		0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
		0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
		0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
		0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
		0x0636, 0x0637, 0x0638, 0x0639, 0x063A, 0x0641, 0x00B5, 0x0642,
		0x0643, 0x0644, 0x0645, 0x0646, 0x0647, 0x0648, 0x0649, 0x064A,
		0x2261, 0x064B, 0x064C, 0x064D, 0x064E, 0x064F, 0x0650, 0x2248,
		0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0,
	};

	const char16_t* builtinSingleByteTable(int codepage)
	{
		return codepage == 720 ? cp720High : nullptr;
	}

	void appendUtf8(std::string& out, char32_t cp)
	{
		if (cp < 0x80)
			out += static_cast<char>(cp);
		else if (cp < 0x800)
		{
			out += static_cast<char>(0xC0 | (cp >> 6));
			out += static_cast<char>(0x80 | (cp & 0x3F));
		}
		else
		{
			out += static_cast<char>(0xE0 | (cp >> 12));
			out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (cp & 0x3F));
		}
	}

	const char* localeCharset()
	{
		const char* cs = ::nl_langinfo(CODESET);
		return (cs && *cs) ? cs : "UTF-8";
	}

	bool isUnicodeCharset(const char* cs)
	{
		return ::strncmp(cs, "UTF", 3) == 0 || ::strcmp(cs, "WCHAR_T") == 0;
	}

	// Size of one code unit, to skip an invalid one without losing alignment
	size_t codeUnitSize(const char* cs)
	{
		if (::strcmp(cs, "WCHAR_T") == 0)
			return sizeof(wchar_t);
		if (::strncmp(cs, "UTF-16", 6) == 0)
			return 2;
		if (::strncmp(cs, "UTF-32", 6) == 0)
			return 4;
		return 1;
	}

	// iconv descriptors are expensive to open: cache one per (from, to) pair and thread.
	class IconvCache
	{
	public:
		~IconvCache()
		{
			for (auto& [key, cd] : _cache)
				if (cd != reinterpret_cast<iconv_t>(-1))
					::iconv_close(cd);
		}

		iconv_t get(const char* from, const char* to)
		{
			std::string key = std::string(from) + '>' + to;
			auto it = _cache.find(key);
			if (it == _cache.end())
				it = _cache.emplace(std::move(key), ::iconv_open(to, from)).first;
			else
				::iconv(it->second, nullptr, nullptr, nullptr, nullptr); // reset shift state
			return it->second;
		}

	private:
		std::unordered_map<std::string, iconv_t> _cache;
	};

	thread_local IconvCache iconvCache;
}

const char* charsetFromCodepage(int codepage)
{
	if (codepage == 0 || codepage == 1)
		return localeCharset();

	for (const CodepageName& cn : codepageNames)
	{
		if (cn.codepage == codepage)
			return cn.charset;
	}
	return nullptr;
}

bool isCodepageSupported(int codepage)
{
	if (builtinSingleByteTable(codepage))
		return true;
	const char* cs = charsetFromCodepage(codepage);
	if (!cs)
		return false;
	return iconvCache.get(cs, "UTF-8") != reinterpret_cast<iconv_t>(-1);
}

bool convert(int fromCodepage, int toCodepage, const char* src, size_t len, std::string& out, int* bytesNotProcessed)
{
	out.clear();
	if (bytesNotProcessed)
		*bytesNotProcessed = 0;

	// Built-in single-byte code pages: go through UTF-8
	if (const char16_t* table = builtinSingleByteTable(fromCodepage))
	{
		std::string utf8;
		utf8.reserve(len * 2);
		for (size_t i = 0; i < len; ++i)
		{
			const unsigned char c = static_cast<unsigned char>(src[i]);
			appendUtf8(utf8, c < 0x80 ? c : table[c - 0x80]);
		}
		if (toCodepage == 65001) // UTF-8
		{
			out = std::move(utf8);
			return true;
		}
		return convert(65001, toCodepage, utf8.data(), utf8.size(), out, bytesNotProcessed);
	}

	if (const char16_t* table = builtinSingleByteTable(toCodepage))
	{
		std::string utf32;
		if (!convert(fromCodepage, 12000, src, len, utf32, bytesNotProcessed)) // to UTF-32LE
			return false;
		out.reserve(utf32.size() / 4);
		for (size_t i = 0; i + 3 < utf32.size(); i += 4)
		{
			const char32_t cp = static_cast<unsigned char>(utf32[i]) | (static_cast<unsigned char>(utf32[i + 1]) << 8)
				| (static_cast<unsigned char>(utf32[i + 2]) << 16) | (static_cast<char32_t>(static_cast<unsigned char>(utf32[i + 3])) << 24);
			if (cp < 0x80)
			{
				out += static_cast<char>(cp);
				continue;
			}
			const char16_t* found = std::find(table, table + 128, static_cast<char16_t>(cp));
			out += (cp <= 0xFFFF && found != table + 128) ? static_cast<char>(0x80 + (found - table)) : '?';
		}
		return true;
	}

	const char* fromCs = charsetFromCodepage(fromCodepage);
	const char* toCs = charsetFromCodepage(toCodepage);
	if (!fromCs || !toCs)
		return false;

	iconv_t cd = iconvCache.get(fromCs, toCs);
	if (cd == reinterpret_cast<iconv_t>(-1))
		return false;

	if (!src || len == 0)
		return true;

	// Replacement, in the target charset, for an invalid input sequence
	std::string replacement;
	if (isUnicodeCharset(toCs))
	{
		iconv_t rcd = iconvCache.get("UTF-8", toCs);
		char rin[] = "\xEF\xBF\xBD"; // U+FFFD
		char rout[16];
		char* pin = rin;
		size_t inLeft = 3;
		char* pout = rout;
		size_t outLeft = sizeof(rout);
		if (::iconv(rcd, &pin, &inLeft, &pout, &outLeft) != static_cast<size_t>(-1))
			replacement.assign(rout, sizeof(rout) - outLeft);
	}
	if (replacement.empty())
		replacement = "?";

	const size_t unitSize = codeUnitSize(fromCs);
	out.resize(len * 4 + 16);
	char* inPtr = const_cast<char*>(src);
	size_t inLeft = len;
	size_t outPos = 0;

	while (inLeft > 0)
	{
		char* outPtr = out.data() + outPos;
		size_t outLeft = out.size() - outPos;
		const size_t r = ::iconv(cd, &inPtr, &inLeft, &outPtr, &outLeft);
		outPos = out.size() - outLeft;

		if (r != static_cast<size_t>(-1))
			break;

		if (errno == E2BIG)
		{
			out.resize(out.size() * 2);
		}
		else if (errno == EINVAL && bytesNotProcessed) // incomplete sequence at the end
		{
			*bytesNotProcessed = static_cast<int>(inLeft);
			break;
		}
		else // EILSEQ, or EINVAL without bytesNotProcessed: replace one code unit and continue
		{
			const size_t skip = std::min(unitSize, inLeft);
			if (out.size() - outPos < replacement.size())
				out.resize(out.size() * 2);
			::memcpy(out.data() + outPos, replacement.data(), replacement.size());
			outPos += replacement.size();
			inPtr += skip;
			inLeft -= skip;
		}
	}

	// Flush the shift state (stateful encodings such as ISO-2022-JP)
	for (;;)
	{
		char* outPtr = out.data() + outPos;
		size_t outLeft = out.size() - outPos;
		if (::iconv(cd, nullptr, nullptr, &outPtr, &outLeft) != static_cast<size_t>(-1))
		{
			outPos = out.size() - outLeft;
			break;
		}
		if (errno != E2BIG)
			break;
		out.resize(out.size() * 2);
	}

	out.resize(outPos);
	return true;
}

std::wstring toWide(const char* src, size_t len, int codepage, int* bytesNotProcessed)
{
	std::string out;
	if (!convert(codepage, CP_WCHAR, src, len, out, bytesNotProcessed))
		return {};
	return std::wstring(reinterpret_cast<const wchar_t*>(out.data()), out.size() / sizeof(wchar_t));
}

std::string fromWide(const wchar_t* src, size_t len, int codepage)
{
	std::string out;
	convert(CP_WCHAR, codepage, reinterpret_cast<const char*>(src), len * sizeof(wchar_t), out);
	return out;
}
}
