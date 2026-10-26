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

#include <cstring>
#include "Common.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fnmatch.h>
#include <future>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <thread>
#include <unistd.h>

#include "Encoding.h"
#include "FileInterface.h"

namespace fs = std::filesystem;
using std::wstring;
using std::string;

// ---------------------------------------------------------------------------
// NPLX: Linux helpers
// ---------------------------------------------------------------------------
namespace nplx
{
	std::string toNativePath(const std::wstring& path)
	{
		return wstring2string(path, CP_UTF8);
	}

	std::wstring fromNativePath(const std::string& path)
	{
		return string2wstring(path, CP_UTF8);
	}

	bool createDirectories(const std::wstring& dir)
	{
		std::error_code ec;
		fs::create_directories(toNativePath(dir), ec);
		return !ec && fs::is_directory(toNativePath(dir), ec);
	}

	bool copyFile(const std::wstring& from, const std::wstring& to, bool failIfExists)
	{
		std::error_code ec;
		const auto opt = failIfExists ? fs::copy_options::none : fs::copy_options::overwrite_existing;
		return fs::copy_file(toNativePath(from), toNativePath(to), opt, ec) && !ec;
	}

	bool moveFileReplace(const std::wstring& from, const std::wstring& to)
	{
		std::error_code ec;
		fs::rename(toNativePath(from), toNativePath(to), ec);
		if (!ec)
			return true;

		// Different file systems: copy + delete (MOVEFILE_COPY_ALLOWED)
		if (!fs::copy_file(toNativePath(from), toNativePath(to), fs::copy_options::overwrite_existing, ec) || ec)
			return false;
		fs::remove(toNativePath(from), ec);
		return true;
	}

	bool deleteFile(const std::wstring& path)
	{
		return ::unlink(toNativePath(path).c_str()) == 0;
	}

	std::wstring fullPathName(const std::wstring& path)
	{
		if (path.empty())
			return path;
		std::error_code ec;
		fs::path p = fs::absolute(toNativePath(path), ec);
		if (ec)
			return path;
		return fromNativePath(p.lexically_normal().string());
	}

	std::wstring tempDirectory()
	{
		std::error_code ec;
		fs::path p = fs::temp_directory_path(ec);
		return ec ? L"/tmp" : fromNativePath(p.string());
	}

	std::wstring getEnv(const char* name)
	{
		const char* v = ::getenv(name);
		return v ? fromNativePath(v) : std::wstring();
	}
}

// ---------------------------------------------------------------------------
// Ported from upstream Common.cpp
// ---------------------------------------------------------------------------

wstring commafyInt(size_t n)
{
	std::wstringstream ss;
	ss.imbue(getSysLocale());
	ss << n;
	return ss.str();
}

void writeLog(const wchar_t* logFileName, const char* log2write)
{
	// NPLX: append mode with fopen instead of CreateFileW(OPEN_ALWAYS) + SetFilePointerEx(FILE_END)
	FILE* f = ::fopen(nplx::toNativePath(logFileName).c_str(), "ab");
	if (!f)
		return;

	const std::time_t now = std::time(nullptr);
	std::tm currentTime{};
	::localtime_r(&now, &currentTime);
	wstring dateTimeStrW = getDateTimeStrFrom(L"yyyy-MM-dd HH:mm:ss", currentTime);
	string log2writeStr = wstring2string(dateTimeStrW, CP_UTF8);
	log2writeStr += "  ";
	log2writeStr += log2write;
	log2writeStr += "\n";

	::fwrite(log2writeStr.c_str(), 1, log2writeStr.length(), f);
	::fflush(f);
	::fclose(f);
}

void writeLog(const wchar_t* logFileName, const wchar_t* log2write)
{
	string log2WriteA = wstring2string(log2write, CP_ACP);
	return writeLog(logFileName, log2WriteA.c_str());
}

wstring purgeMenuItemString(const wchar_t* menuItemStr, bool keepAmpersand)
{
	static constexpr size_t cleanedNameLen = 64;
	wchar_t cleanedName[cleanedNameLen] = L"";
	size_t j = 0;
	size_t menuNameLen = lstrlen(menuItemStr);
	if (menuNameLen >= cleanedNameLen)
		menuNameLen = cleanedNameLen - 1;

	for (size_t k = 0 ; k < menuNameLen ; ++k)
	{
		if (menuItemStr[k] == '\t')
		{
			cleanedName[k] = 0;
			break;
		}
		else
		{
			if (menuItemStr[k] == '&')
			{
				if (keepAmpersand)
					cleanedName[j++] = menuItemStr[k];
				//else skip
			}
			else
				cleanedName[j++] = menuItemStr[k];
		}
	}

	cleanedName[j] = 0;
	return cleanedName;
}

std::vector<wstring> tokenizeString(const wstring& tokenString, const char delim)
{
	//Vector is created on stack and copied on return
	std::vector<wstring> tokens;

	// Skip delimiters at beginning.
	wstring::size_type lastPos = tokenString.find_first_not_of(delim, 0);
	// Find first "non-delimiter".
	wstring::size_type pos = tokenString.find_first_of(delim, lastPos);

	while (pos != std::string::npos || lastPos != std::string::npos)
	{
		// Found a token, add it to the vector.
		tokens.push_back(tokenString.substr(lastPos, pos - lastPos));
		// Skip delimiters.  Note the "not_of"
		lastPos = tokenString.find_first_not_of(delim, pos);
		// Find next "non-delimiter"
		pos = tokenString.find_first_of(delim, lastPos);
	}
	return tokens;
}

std::wstring string2wstring(const std::string& rString, UINT codepage)
{
	// upstream converts up to the first null character (MultiByteToWideChar with length -1)
	return nplx::encoding::toWide(rString.c_str(), std::strlen(rString.c_str()), static_cast<int>(codepage));
}

std::string wstring2string(const std::wstring& rwString, UINT codepage)
{
	// upstream converts up to the first null character (WideCharToMultiByte with length -1)
	return nplx::encoding::fromWide(rwString.c_str(), std::wcslen(rwString.c_str()), static_cast<int>(codepage));
}

bool isInList(const wchar_t* token, const wchar_t* list)
{
	if ((!token) || (!list))
		return false;

	static constexpr size_t wordLen = 64;
	const size_t listLen = std::wcslen(list);

	wchar_t word[wordLen] = { L'\0' };
	size_t i = 0;
	size_t j = 0;

	for (; i <= listLen; ++i)
	{
		if ((list[i] == L' ') || (list[i] == L'\0'))
		{
			if (j != 0)
			{
				word[j] = '\0';
				j = 0;

				if (!::_wcsicmp(token, word))
					return true;
			}
		}
		else
		{
			word[j] = list[i];
			++j;

			if (j >= wordLen)
				return false;
		}
	}
	return false;
}

namespace
{
	// Upstream convertFileName(): doubles '&' so it isn't taken as a menu mnemonic
	template <typename It>
	wstring convertFileName(It first, It last)
	{
		wstring strTmp;
		for (It it = first; it != last; ++it)
		{
			if (*it == '&')
				strTmp.push_back('&');
			strTmp.push_back(*it);
		}
		return strTmp;
	}

}

// NPLX: replacement for PathCompactPathEx: "/home/user/.../file.txt", or "verylongna..." for a bare name
std::wstring pathCompact(const std::wstring& path, size_t maxLen)
{
	if (path.length() <= maxLen || maxLen < 4)
		return path;

	const size_t lastSep = path.find_last_of(NPLX_PATH_SEP);
	if (lastSep == wstring::npos)
		return path.substr(0, maxLen - 3) + L"...";

	const wstring fileName = path.substr(lastSep);
	if (fileName.length() + 3 >= maxLen)
		return L"..." + fileName.substr(fileName.length() - (maxLen - 3));

	const size_t headLen = maxLen - 3 - fileName.length();
	return path.substr(0, headLen) + L"..." + fileName;
}

wstring BuildMenuFileName(int filenameLen, unsigned int pos, const wstring& filename, bool ordinalNumber)
{
	wstring strTemp;

	if (ordinalNumber)
	{
		if (pos < 9)
		{
			strTemp.push_back('&');
			strTemp.push_back('1' + static_cast<wchar_t>(pos));
		}
		else if (pos == 9)
		{
			strTemp.append(L"1&0");
		}
		else
		{
			div_t splitDigits = div(pos + 1, 10);
			strTemp.append(std::to_wstring(splitDigits.quot));
			strTemp.push_back('&');
			strTemp.append(std::to_wstring(splitDigits.rem));
		}
		strTemp.append(L": ");
	}
	else
	{
		strTemp.push_back('&');
	}

	if (filenameLen > 0)
	{
		const wstring compacted = pathCompact(filename, static_cast<size_t>(filenameLen));
		strTemp.append(convertFileName(compacted.begin(), compacted.end()));
	}
	else
	{
		// (filenameLen < 0)
		wstring::const_iterator it = filename.begin();

		if (filenameLen == 0)
		{
			const size_t lastSep = filename.find_last_of(NPLX_PATH_SEP);
			if (lastSep != wstring::npos)
				it += static_cast<std::ptrdiff_t>(lastSep + 1);
		}

		// MAX_PATH is still here to keep old trimming behaviour (NPLX: upstream value 260).
		constexpr std::ptrdiff_t maxMenuPath = 260;
		if (filename.end() - it < maxMenuPath)
		{
			strTemp.append(convertFileName(it, filename.end()));
		}
		else
		{
			strTemp.append(convertFileName(it, it + maxMenuPath / 2 - 3));
			strTemp.append(L"...");
			strTemp.append(convertFileName(filename.end() - maxMenuPath / 2, filename.end()));
		}
	}

	return strTemp;
}

std::string getFileContent(const wchar_t* file2read, bool* pbFailed)
{
	if (pbFailed)
		*pbFailed = false; // reset

	if (!doesFileExist(file2read))
	{
		if (pbFailed)
			*pbFailed = true;
		return "";
	}

	FILE* fp = ::fopen(nplx::toNativePath(file2read).c_str(), "rb");
	if (!fp)
	{
		if (pbFailed)
			*pbFailed = true;
		return "";
	}

	static constexpr size_t blockSize = 1024 * 4; // 4K is optimal chunk for memory, cache, disk or network
	char data[blockSize];
	std::string wholeFileContent;
	size_t lenFile = 0;
	try {
		do
		{
			lenFile = fread(data, 1, blockSize, fp);
			if (lenFile == 0) break;
			wholeFileContent.append(data, lenFile);
		} while (lenFile > 0);
	}
	catch (...)
	{
		if (pbFailed)
			*pbFailed = true;
		std::string().swap(wholeFileContent); // to immediately release all the allocated memory
	}

	fclose(fp);
	return wholeFileContent;
}

wstring relativeFilePathToFullFilePath(const wchar_t* relativeFilePath)
{
	return nplx::fullPathName(relativeFilePath);
}

void writeFileContent(const wchar_t* file2write, const char* content2write)
{
	NppIO_File file(file2write);

	if (file.isOpened())
		file.writeStr(content2write);
}

bool pathMatchSpec(const wchar_t* fileName, const wchar_t* spec)
{
	if (!fileName || !spec)
		return false;

	// Like PathMatchSpec: the spec is matched against the file name part, case-insensitive,
	// and may hold several patterns separated by ';'.
	string name = wstring2string(fileName, CP_UTF8);
	const size_t sep = name.find_last_of('/');
	if (sep != string::npos)
		name.erase(0, sep + 1);

	for (const wstring& oneSpec : tokenizeString(spec, ';'))
	{
		wstring trimmed = oneSpec;
		trim(trimmed);
		if (trimmed.empty())
			continue;
		if (trimmed == L"*.*") // PathMatchSpec: matches everything, even names without a dot
			return true;
		if (::fnmatch(wstring2string(trimmed, CP_UTF8).c_str(), name.c_str(), FNM_CASEFOLD) == 0)
			return true;
	}
	return false;
}

bool matchInList(const wchar_t* fileName, const std::vector<wstring>& patterns)
{
	bool is_matched = false;
	for (size_t i = 0, len = patterns.size(); i < len; ++i)
	{
		if (patterns[i].length() > 1 && patterns[i][0] == '!')
		{
			if (pathMatchSpec(fileName, patterns[i].c_str() + 1))
				return false;

			continue;
		}

		if (pathMatchSpec(fileName, patterns[i].c_str()))
			is_matched = true;
	}
	return is_matched;
}

bool matchInExcludeDirList(const wchar_t* dirName, const std::vector<wstring>& patterns, size_t level)
{
	for (size_t i = 0, len = patterns.size(); i < len; ++i)
	{
		size_t patterLen = patterns[i].length();

		// NPLX: accept both '\' (upstream syntax, kept for config compatibility) and '/'
		auto isSep = [](wchar_t c) { return c == '\\' || c == '/'; };
		if (patterLen > 3 && patterns[i][0] == '!' && patterns[i][1] == '+' && isSep(patterns[i][2])) // check for !+\folderPattern: for all levels - search this pattern recursively
		{
			if (pathMatchSpec(dirName, patterns[i].c_str() + 3))
				return true;
		}
		else if (patterLen > 2 && patterns[i][0] == '!' && isSep(patterns[i][1])) // check for !\folderPattern: exclusive pattern for only the 1st level
		{
			if (level == 1)
				if (pathMatchSpec(dirName, patterns[i].c_str() + 2))
					return true;
		}
	}
	return false;
}

bool allPatternsAreExclusion(const std::vector<wstring>& patterns)
{
	bool oneInclusionPatternFound = false;
	for (size_t i = 0, len = patterns.size(); i < len; ++i)
	{
		if (patterns[i][0] != '!')
		{
			oneInclusionPatternFound = true;
			break;
		}
	}
	return !oneInclusionPatternFound;
}

const wchar_t* WcharMbcsConvertor::char2wchar(const char* mbcs2Convert, size_t codepage, int lenMbcs, int* pLenWc, int* pBytesNotProcessed)
{
	if (!mbcs2Convert)
		return nullptr;

	const size_t len = lenMbcs == -1 ? ::strlen(mbcs2Convert) : static_cast<size_t>(lenMbcs);
	_wideCharStr = nplx::encoding::toWide(mbcs2Convert, len, static_cast<int>(codepage), pBytesNotProcessed);
	if (pLenWc)
		*pLenWc = static_cast<int>(_wideCharStr.size());
	return _wideCharStr.c_str();
}

const char* WcharMbcsConvertor::wchar2char(const wchar_t* wcharStr2Convert, size_t codepage, int lenWc, int* pLenMbcs)
{
	if (!wcharStr2Convert)
		return nullptr;

	const size_t len = lenWc == -1 ? ::wcslen(wcharStr2Convert) : static_cast<size_t>(lenWc);
	_multiByteStr = nplx::encoding::fromWide(wcharStr2Convert, len, static_cast<int>(codepage));
	if (pLenMbcs)
		*pLenMbcs = static_cast<int>(_multiByteStr.size());
	return _multiByteStr.c_str();
}

const wchar_t* WcharMbcsConvertor::char2wchar(const char* mbcs2Convert, size_t codepage, intptr_t* mstart, intptr_t* mend, int mbcsLen)
{
	// Do not process NULL pointer
	if (!mbcs2Convert) return nullptr;

	const size_t len = (mbcsLen == 0 || mbcsLen == -1) ? ::strlen(mbcs2Convert) : static_cast<size_t>(mbcsLen);
	_wideCharStr = nplx::encoding::toWide(mbcs2Convert, len, static_cast<int>(codepage));

	const intptr_t wideLen = static_cast<intptr_t>(_wideCharStr.size());
	if (*mstart < static_cast<intptr_t>(len) && *mend <= static_cast<intptr_t>(len) && *mstart >= 0 && *mend >= 0)
	{
		*mstart = static_cast<intptr_t>(nplx::encoding::toWide(mbcs2Convert, static_cast<size_t>(*mstart), static_cast<int>(codepage)).size());
		*mend = static_cast<intptr_t>(nplx::encoding::toWide(mbcs2Convert, static_cast<size_t>(*mend), static_cast<int>(codepage)).size());
		if (*mstart >= wideLen || *mend > wideLen)
		{
			*mstart = 0;
			*mend = 0;
		}
	}
	else
	{
		*mstart = 0;
		*mend = 0;
	}
	return _wideCharStr.c_str();
}

const char* WcharMbcsConvertor::wchar2char(const wchar_t* wcharStr2Convert, size_t codepage, intptr_t* mstart, intptr_t* mend, int wcharLenIn, int* lenOut)
{
	if (!wcharStr2Convert)
		return nullptr;

	const size_t len = (wcharLenIn == 0 || wcharLenIn == -1) ? ::wcslen(wcharStr2Convert) : static_cast<size_t>(wcharLenIn);
	_multiByteStr = nplx::encoding::fromWide(wcharStr2Convert, len, static_cast<int>(codepage));

	const intptr_t mbLen = static_cast<intptr_t>(_multiByteStr.size());
	if (*mstart < static_cast<intptr_t>(len) && *mend < static_cast<intptr_t>(len) && *mstart >= 0 && *mend >= 0)
	{
		*mstart = static_cast<intptr_t>(nplx::encoding::fromWide(wcharStr2Convert, static_cast<size_t>(*mstart), static_cast<int>(codepage)).size());
		*mend = static_cast<intptr_t>(nplx::encoding::fromWide(wcharStr2Convert, static_cast<size_t>(*mend), static_cast<int>(codepage)).size());
		if (*mstart >= mbLen || *mend > mbLen)
		{
			*mstart = 0;
			*mend = 0;
		}
	}
	else
	{
		*mstart = 0;
		*mend = 0;
	}
	if (lenOut)
		*lenOut = static_cast<int>(mbLen);
	return _multiByteStr.c_str();
}

const char* WcharMbcsConvertor::encode(UINT fromCodepage, UINT toCodepage, const char* txt2Encode, int lenIn, int* pLenOut, int* pBytesNotProcessed)
{
	// NPLX: direct conversion with iconv, no UTF-16 round trip
	if (!txt2Encode)
		return nullptr;

	const size_t len = lenIn == -1 ? ::strlen(txt2Encode) : static_cast<size_t>(lenIn);
	nplx::encoding::convert(static_cast<int>(fromCodepage), static_cast<int>(toCodepage), txt2Encode, len, _multiByteStr, pBytesNotProcessed);
	if (pLenOut)
		*pLenOut = static_cast<int>(_multiByteStr.size());
	return _multiByteStr.c_str();
}

wstring pathRemoveFileSpec(wstring& path)
{
	// NPLX: '/' separator; "/foo" becomes "/"
	wstring::size_type lastSep = path.find_last_of(NPLX_PATH_SEP);
	if (lastSep == wstring::npos)
		path.erase();
	else if (lastSep == 0 && path.size() > 1)
		path.erase(1);
	else
		path.erase(lastSep);
	return path;
}

wstring pathAppend(wstring& strDest, const wstring& str2append)
{
	// NPLX: upstream appends Windows sub-paths such as L"localization\\"; convert them to '/'.
	wstring toAppend = str2append;
	std::replace(toAppend.begin(), toAppend.end(), L'\\', NPLX_PATH_SEP);

	if (strDest.empty() && toAppend.empty()) // "" + ""
	{
		strDest = NPLX_PATH_SEP;
		return strDest;
	}

	if (strDest.empty() && !toAppend.empty()) // "" + titi
	{
		strDest = toAppend;
		return strDest;
	}

	if (strDest[strDest.length() - 1] == NPLX_PATH_SEP && (!toAppend.empty() && toAppend[0] == NPLX_PATH_SEP)) // toto/ + /titi
	{
		strDest.erase(strDest.length() - 1, 1);
		strDest += toAppend;
		return strDest;
	}

	if ((strDest[strDest.length() - 1] == NPLX_PATH_SEP && (!toAppend.empty() && toAppend[0] != NPLX_PATH_SEP)) // toto/ + titi
		|| (strDest[strDest.length() - 1] != NPLX_PATH_SEP && (!toAppend.empty() && toAppend[0] == NPLX_PATH_SEP))) // toto + /titi
	{
		strDest += toAppend;
		return strDest;
	}

	// toto + titi
	strDest += NPLX_PATH_SEP;
	strDest += toAppend;

	return strDest;
}

std::wstring stringToLower(std::wstring strToConvert)
{
	std::transform(strToConvert.begin(), strToConvert.end(), strToConvert.begin(),
		[](auto ch) { return std::tolower(ch, getSysLocale()); });
	return strToConvert;
}

std::wstring stringToUpper(std::wstring strToConvert)
{
	std::transform(strToConvert.begin(), strToConvert.end(), strToConvert.begin(),
		[](auto ch) { return std::toupper(ch, getSysLocale()); });
	return strToConvert;
}

wstring stringReplace(wstring subject, const wstring& search, const wstring& replace)
{
	size_t pos = 0;
	while ((pos = subject.find(search, pos)) != std::string::npos)
	{
		subject.replace(pos, search.length(), replace);
		pos += replace.length();
	}
	return subject;
}

void stringSplit(const wstring& input, const wstring& delimiter, std::vector<wstring>& output)
{
	size_t start = 0U;
	size_t end = input.find(delimiter);
	const size_t delimiterLength = delimiter.length();
	while (end != std::string::npos)
	{
		output.push_back(input.substr(start, end - start));
		start = end + delimiterLength;
		end = input.find(delimiter, start);
	}
	output.push_back(input.substr(start, end));
}

bool str2numberVector(wstring str2convert, std::vector<size_t>& numVect)
{
	numVect.clear();

	for (auto i : str2convert)
	{
		switch (i)
		{
		case ' ':
		case '0': case '1':	case '2': case '3':	case '4':
		case '5': case '6':	case '7': case '8':	case '9':
		{
			// correct. do nothing
		}
		break;

		default:
			return false;
		}
	}

	std::vector<wstring> v;
	stringSplit(str2convert, L" ", v);
	for (const auto& i : v)
	{
		// Don't treat empty string and the number greater than 9999
		if (!i.empty() && i.length() < 5)
		{
			numVect.push_back(std::stoi(i));
		}
	}
	return true;
}

void stringJoin(const std::vector<wstring>& strings, const wstring& separator, wstring& joinedString)
{
	size_t length = strings.size();
	for (size_t i = 0; i < length; ++i)
	{
		joinedString += strings.at(i);
		if (i != length - 1)
		{
			joinedString += separator;
		}
	}
}

wstring stringTakeWhileAdmissable(const wstring& input, const wstring& admissable)
{
	// Find first non-admissable character in "input", and remove everything after it.
	size_t idx = input.find_first_not_of(admissable);
	if (idx == std::string::npos)
	{
		return input;
	}
	else
	{
		return input.substr(0, idx);
	}
}

double stodLocale(const wstring& str, locale_t loc, size_t* idx)
{
	// Copied from the std::stod implementation but uses wcstod_l instead of wcstod.
	const wchar_t* ptr = str.c_str();
	errno = 0;
	wchar_t* eptr = nullptr;
	double ans = loc ? ::wcstod_l(ptr, &eptr, loc) : ::wcstod(ptr, &eptr);
	if (ptr == eptr)
		throw std::invalid_argument("invalid stod argument");
	if (errno == ERANGE)
		throw std::out_of_range("stod argument out of range");
	if (idx != NULL)
		*idx = static_cast<size_t>(eptr - ptr);
	return ans;
}

const std::locale& getSysLocale()
{
	// NPLX: the user's locale from the environment (LANG/LC_*), falling back to "C"
	static const std::locale sysLocale = []()
	{
		try
		{
			return std::locale("");
		}
		catch (...)
		{
			return std::locale::classic();
		}
	}();
	return sysLocale;
}

bool deleteFileOrFolder(const wstring& f2delete)
{
	// NPLX: upstream uses SHFileOperation(FO_DELETE, FOF_ALLOWUNDO), i.e. the recycle bin.
	// Moving to the freedesktop trash is done in ui/ with QFile::moveToTrash; here it is a real delete.
	std::error_code ec;
	fs::remove_all(nplx::toNativePath(f2delete), ec);
	return !ec;
}

void getFilesInFolder(std::vector<wstring>& files, const wstring& extTypeFilter, const wstring& inFolder)
{
	std::error_code ec;
	for (const fs::directory_entry& entry : fs::directory_iterator(nplx::toNativePath(inFolder), ec))
	{
		if (entry.is_directory(ec))
			continue;

		const wstring fileName = nplx::fromNativePath(entry.path().filename().string());
		if (pathMatchSpec(fileName.c_str(), extTypeFilter.c_str()))
		{
			wstring foundFullPath = inFolder;
			pathAppend(foundFullPath, fileName);
			files.push_back(foundFullPath);
		}
	}
	// NPLX: directory_iterator order is unspecified; FindFirstFile on NTFS is alphabetical
	std::sort(files.begin(), files.end());
}

void trim(std::wstring& str)
{
	std::wstring::size_type pos = str.find_last_not_of(' ');

	if (pos != std::wstring::npos)
	{
		str.erase(pos + 1);
		pos = str.find_first_not_of(' ');
		if (pos != std::wstring::npos) str.erase(0, pos);
	}
	else str.erase(str.begin(), str.end());
}

int nbDigitsFromNbLines(size_t nbLines)
{
	int nbDigits = 0; // minimum number of digit should be 4
	if (nbLines < 10) nbDigits = 1;
	else if (nbLines < 100) nbDigits = 2;
	else if (nbLines < 1000) nbDigits = 3;
	else if (nbLines < 10000) nbDigits = 4;
	else if (nbLines < 100000) nbDigits = 5;
	else if (nbLines < 1000000) nbDigits = 6;
	else // rare case
	{
		nbDigits = 7;
		nbLines /= 10000000;

		while (nbLines)
		{
			nbLines /= 10;
			++nbDigits;
		}
	}
	return nbDigits;
}

std::wstring getDateTimeStrFrom(const std::wstring& dateTimeFormat, const std::tm& st)
{
	// NPLX: replacement for GetDateFormatEx + GetTimeFormatEx with the same pattern letters
	auto pad = [](int v, size_t width) {
		wstring s = std::to_wstring(v);
		while (s.length() < width)
			s.insert(s.begin(), L'0');
		return s;
	};

	wstring out;
	const size_t len = dateTimeFormat.length();
	for (size_t i = 0; i < len; )
	{
		const wchar_t c = dateTimeFormat[i];
		size_t run = 1;
		while (i + run < len && dateTimeFormat[i + run] == c)
			++run;

		switch (c)
		{
		case 'y':
			if (run >= 3)
				out += pad(st.tm_year + 1900, 4);
			else
				out += pad((st.tm_year + 1900) % 100, run);
			break;
		case 'M':
			out += pad(st.tm_mon + 1, std::min<size_t>(run, 2));
			break;
		case 'd':
			out += pad(st.tm_mday, std::min<size_t>(run, 2));
			break;
		case 'H':
			out += pad(st.tm_hour, std::min<size_t>(run, 2));
			break;
		case 'h':
			out += pad(st.tm_hour % 12 == 0 ? 12 : st.tm_hour % 12, std::min<size_t>(run, 2));
			break;
		case 'm':
			out += pad(st.tm_min, std::min<size_t>(run, 2));
			break;
		case 's':
			out += pad(st.tm_sec, std::min<size_t>(run, 2));
			break;
		case 't':
			out += (st.tm_hour < 12) ? (run >= 2 ? L"AM" : L"A") : (run >= 2 ? L"PM" : L"P");
			break;
		default:
			out.append(run, c);
			break;
		}
		i += run;
	}
	return out;
}

bool removeReadOnlyFlagFromFileAttributes(const wchar_t* fileFullPath)
{
	// NPLX: "read-only" = no write permission for the owner
	struct stat st{};
	const string path = nplx::toNativePath(fileFullPath);
	if (::stat(path.c_str(), &st) != 0)
		return false;
	return ::chmod(path.c_str(), st.st_mode | S_IWUSR) == 0;
}

bool toggleReadOnlyFlagFromFileAttributes(const wchar_t* fileFullPath, bool& isChangedToReadOnly)
{
	struct stat st{};
	const string path = nplx::toNativePath(fileFullPath);
	if (::stat(path.c_str(), &st) != 0)
		return false;

	const bool isReadOnly = (st.st_mode & S_IWUSR) == 0;
	const mode_t newMode = isReadOnly ? (st.st_mode | S_IWUSR) : (st.st_mode & ~(S_IWUSR | S_IWGRP | S_IWOTH));
	if (::chmod(path.c_str(), newMode) != 0)
		return false;
	isChangedToReadOnly = !isReadOnly;
	return true;
}

bool isUncPath(const std::wstring& path)
{
	// NPLX: Windows UNC paths do not exist on Linux. Network shares (smb://, mounted gvfs)
	// are handled as regular paths. Kept so session files from Windows are recognised.
	auto isPathSep = [](wchar_t c) { return c == L'\\'; };
	return path.size() >= 2 && isPathSep(path[0]) && isPathSep(path[1]);
}

bool isUncFileUrl(const std::wstring& url)
{
	// file://server/share/... (a remote host). file:///path or file://localhost/... is local.
	if (url.size() < 7 || _wcsnicmp(url.c_str(), L"file://", 7) != 0)
		return false;

	const size_t hostStart = 7; // after "file://"
	const size_t hostEnd = url.find_first_of(L"/\\", hostStart);
	const wstring host = (hostEnd == std::wstring::npos) ? url.substr(hostStart) : url.substr(hostStart, hostEnd - hostStart);

	if (host.empty() || host == L".")
		return false;

	if (_wcsicmp(host.c_str(), L"localhost") == 0)
		return false;

	if (host == L"127.0.0.1" || host == L"::1" || host == L"[::1]")
		return false;

	return true;
}

Version::Version(const wstring& versionStr)
{
	// NPLX: release-build behaviour of upstream: an invalid string gives the empty version 0.0.0.0
	auto ss = tokenizeString(versionStr, '.');
	if (ss.size() > 4)
		return;

	unsigned long parts[4] = {};
	for (size_t i = 0; i < ss.size(); ++i)
	{
		if (!isNumber(ss[i]))
			return;
		try
		{
			parts[i] = std::stoul(ss[i]);
		}
		catch (...)
		{
			return;
		}
	}
	_major = parts[0];
	_minor = parts[1];
	_patch = parts[2];
	_build = parts[3];
}

wstring Version::toString() const
{
	if (_build == 0 && _patch == 0 && _minor == 0 && _major == 0) // ""
	{
		return L"";
	}
	else if (_build == 0 && _patch == 0 && _minor == 0) // "major"
	{
		return std::to_wstring(_major);
	}
	else if (_build == 0 && _patch == 0) // "major.minor"
	{
		std::wstring v = std::to_wstring(_major);
		v += L".";
		v += std::to_wstring(_minor);
		return v;
	}
	else if (_build == 0) // "major.minor.patch"
	{
		std::wstring v = std::to_wstring(_major);
		v += L".";
		v += std::to_wstring(_minor);
		v += L".";
		v += std::to_wstring(_patch);
		return v;
	}

	// "major.minor.patch.build"
	std::wstring ver = std::to_wstring(_major);
	ver += L".";
	ver += std::to_wstring(_minor);
	ver += L".";
	ver += std::to_wstring(_patch);
	ver += L".";
	ver += std::to_wstring(_build);

	return ver;
}

int Version::compareTo(const Version& v2c) const
{
	if (_major != v2c._major)
		return _major > v2c._major ? 1 : -1;
	if (_minor != v2c._minor)
		return _minor > v2c._minor ? 1 : -1;
	if (_patch != v2c._patch)
		return _patch > v2c._patch ? 1 : -1;
	if (_build != v2c._build)
		return _build > v2c._build ? 1 : -1;
	return 0;
}

bool Version::isCompatibleTo(const Version& from, const Version& to) const
{
	// This method determines if Version object is in between "from" version and "to" version, it's useful for testing compatibility of application.
	// test in versions <from, to> example:
	// 1. <0.0.0.0, 0.0.0.0>: both from to versions are empty, so it's
	// 2. <6.9, 6.9>: plugin is compatible to only v6.9
	// 3. <4.2, 6.6.6>: from v4.2 (included) to v6.6.6 (included)
	// 4. <0.0.0.0, 8.2.1>: all version until v8.2.1 (included)
	// 5. <8.3, 0.0.0.0>: from v8.3 (included) to the latest version

	if (empty()) // if this version is empty, then no compatible to all version
		return false;

	if (from.empty() && to.empty()) // both versions "from" and "to" are empty: it's considered compatible, whatever this version is (match to 1)
	{
		return true;
	}

	if (from <= *this && to >= *this) // from_ver <= this_ver <= to_ver (match to 2, 3 and 4)
	{
		return true;
	}

	if (from <= *this && to.empty()) // from_ver <= this_ver (match to 5)
	{
		return true;
	}

	return false;
}

bool doesFileExist(const wchar_t* filePath, DWORD milliSec2wait, bool* isTimeoutReached)
{
	WIN32_FILE_ATTRIBUTE_DATA attributes{};
	attributes.dwFileAttributes = INVALID_FILE_ATTRIBUTES;
	getFileAttributesExWithTimeout(filePath, &attributes, milliSec2wait, isTimeoutReached);
	return (attributes.dwFileAttributes != INVALID_FILE_ATTRIBUTES && !(attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY));
}

bool doesDirectoryExist(const wchar_t* dirPath, DWORD milliSec2wait, bool* isTimeoutReached)
{
	WIN32_FILE_ATTRIBUTE_DATA attributes{};
	attributes.dwFileAttributes = INVALID_FILE_ATTRIBUTES;
	getFileAttributesExWithTimeout(dirPath, &attributes, milliSec2wait, isTimeoutReached);
	return (attributes.dwFileAttributes != INVALID_FILE_ATTRIBUTES && (attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY));
}

bool doesPathExist(const wchar_t* path, DWORD milliSec2wait, bool* isTimeoutReached)
{
	WIN32_FILE_ATTRIBUTE_DATA attributes{};
	attributes.dwFileAttributes = INVALID_FILE_ATTRIBUTES;
	getFileAttributesExWithTimeout(path, &attributes, milliSec2wait, isTimeoutReached);
	return (attributes.dwFileAttributes != INVALID_FILE_ATTRIBUTES);
}

void expandEnv(wstring& path2Expand)
{
	// NPLX: ExpandEnvironmentStrings expands %VAR%; on Linux expand $VAR, ${VAR} and a leading ~.
	// %VAR% is also accepted so that Windows-style settings keep working.
	wstring result;
	const size_t len = path2Expand.length();
	for (size_t i = 0; i < len; ++i)
	{
		const wchar_t c = path2Expand[i];
		if (c == L'~' && i == 0 && (len == 1 || path2Expand[1] == L'/'))
		{
			result += nplx::getEnv("HOME");
		}
		else if (c == L'$' || c == L'%')
		{
			size_t start = i + 1;
			size_t end = start;
			wchar_t closing = 0;
			if (c == L'%')
				closing = L'%';
			else if (start < len && path2Expand[start] == L'{')
			{
				closing = L'}';
				++start;
				end = start;
			}

			while (end < len && (std::iswalnum(path2Expand[end]) || path2Expand[end] == L'_'))
				++end;

			const bool closed = closing == 0 || (end < len && path2Expand[end] == closing);
			if (end > start && closed)
			{
				const std::string varName = wstring2string(path2Expand.substr(start, end - start), CP_UTF8);
				const char* value = ::getenv(varName.c_str());
				if (value)
				{
					result += nplx::fromNativePath(value);
					i = closing ? end : end - 1;
					continue;
				}
			}
			result += c;
		}
		else
		{
			result += c;
		}
	}
	path2Expand = result;
}

// NPLX: algorithm of shlwapi ColorRGBToHLS / ColorHLSToRGB (HLSMAX = 240, RGBMAX = 255)
namespace
{
	constexpr int HLSMAX = 240;
	constexpr int RGBMAX = 255;
	constexpr int HUE_UNDEFINED = HLSMAX * 2 / 3;

	int hueToRGB(int n1, int n2, int hue)
	{
		if (hue < 0)
			hue += HLSMAX;
		if (hue > HLSMAX)
			hue -= HLSMAX;

		if (hue < (HLSMAX / 6))
			return n1 + (((n2 - n1) * hue + (HLSMAX / 12)) / (HLSMAX / 6));
		if (hue < (HLSMAX / 2))
			return n2;
		if (hue < ((HLSMAX * 2) / 3))
			return n1 + (((n2 - n1) * (((HLSMAX * 2) / 3) - hue) + (HLSMAX / 12)) / (HLSMAX / 6));
		return n1;
	}
}

void ColorRGBToHLS(COLORREF clrRGB, WORD* pwHue, WORD* pwLuminance, WORD* pwSaturation)
{
	const int r = GetRValue(clrRGB);
	const int g = GetGValue(clrRGB);
	const int b = GetBValue(clrRGB);

	const int cMax = std::max({ r, g, b });
	const int cMin = std::min({ r, g, b });

	int h = 0;
	int s = 0;
	const int l = (((cMax + cMin) * HLSMAX) + RGBMAX) / (2 * RGBMAX);

	if (cMax == cMin)
	{
		s = 0;
		h = HUE_UNDEFINED;
	}
	else
	{
		if (l <= (HLSMAX / 2))
			s = (((cMax - cMin) * HLSMAX) + ((cMax + cMin) / 2)) / (cMax + cMin);
		else
			s = (((cMax - cMin) * HLSMAX) + ((2 * RGBMAX - cMax - cMin) / 2)) / (2 * RGBMAX - cMax - cMin);

		const int rDelta = (((cMax - r) * (HLSMAX / 6)) + ((cMax - cMin) / 2)) / (cMax - cMin);
		const int gDelta = (((cMax - g) * (HLSMAX / 6)) + ((cMax - cMin) / 2)) / (cMax - cMin);
		const int bDelta = (((cMax - b) * (HLSMAX / 6)) + ((cMax - cMin) / 2)) / (cMax - cMin);

		if (r == cMax)
			h = bDelta - gDelta;
		else if (g == cMax)
			h = (HLSMAX / 3) + rDelta - bDelta;
		else
			h = ((2 * HLSMAX) / 3) + gDelta - rDelta;

		if (h < 0)
			h += HLSMAX;
		if (h > HLSMAX)
			h -= HLSMAX;
	}

	if (pwHue) *pwHue = static_cast<WORD>(h);
	if (pwLuminance) *pwLuminance = static_cast<WORD>(l);
	if (pwSaturation) *pwSaturation = static_cast<WORD>(s);
}

COLORREF ColorHLSToRGB(WORD wHue, WORD wLuminance, WORD wSaturation)
{
	const int hue = wHue;
	const int lum = std::min<int>(wLuminance, HLSMAX);
	const int sat = std::min<int>(wSaturation, HLSMAX);

	int r = 0;
	int g = 0;
	int b = 0;

	if (sat == 0)
	{
		r = g = b = (lum * RGBMAX) / HLSMAX;
	}
	else
	{
		int magic2 = 0;
		if (lum <= (HLSMAX / 2))
			magic2 = (lum * (HLSMAX + sat) + (HLSMAX / 2)) / HLSMAX;
		else
			magic2 = lum + sat - ((lum * sat) + (HLSMAX / 2)) / HLSMAX;
		const int magic1 = 2 * lum - magic2;

		r = (hueToRGB(magic1, magic2, hue + (HLSMAX / 3)) * RGBMAX + (HLSMAX / 2)) / HLSMAX;
		g = (hueToRGB(magic1, magic2, hue) * RGBMAX + (HLSMAX / 2)) / HLSMAX;
		b = (hueToRGB(magic1, magic2, hue - (HLSMAX / 3)) * RGBMAX + (HLSMAX / 2)) / HLSMAX;
	}

	return RGB(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255));
}

// Windows epoch (1601-01-01) to Unix epoch (1970-01-01), in 100 ns units
static constexpr int64_t EPOCH_DIFFERENCE_100NS = 116444736000000000LL;

FILETIME unixTimeToFileTime(int64_t seconds, int64_t nanoseconds)
{
	const uint64_t v = static_cast<uint64_t>(seconds * 10000000LL + nanoseconds / 100 + EPOCH_DIFFERENCE_100NS);
	FILETIME ft;
	ft.dwLowDateTime = static_cast<DWORD>(v & 0xFFFFFFFF);
	ft.dwHighDateTime = static_cast<DWORD>(v >> 32);
	return ft;
}

int64_t fileTimeToUnixTime(const FILETIME& ft)
{
	const int64_t v = static_cast<int64_t>((static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime);
	return (v - EPOCH_DIFFERENCE_100NS) / 10000000LL;
}

bool getFileLastWriteTime(const wchar_t* filePath, FILETIME& lastWriteTime)
{
	struct stat st{};
	if (::stat(nplx::toNativePath(filePath).c_str(), &st) != 0)
		return false;
	lastWriteTime = unixTimeToFileTime(st.st_mtim.tv_sec, st.st_mtim.tv_nsec);
	return true;
}

FILETIME getSystemTimeAsFileTime()
{
	struct timespec ts{};
	::clock_gettime(CLOCK_REALTIME, &ts);
	return unixTimeToFileTime(ts.tv_sec, ts.tv_nsec);
}

const wchar_t* PathFindFileName(const wchar_t* path)
{
	if (!path)
		return path;
	const wchar_t* lastSep = ::wcsrchr(path, NPLX_PATH_SEP);
	return lastSep ? lastSep + 1 : path;
}

const wchar_t* PathFindExtension(const wchar_t* path)
{
	if (!path)
		return path;
	const wchar_t* fileName = PathFindFileName(path);
	const wchar_t* dot = ::wcsrchr(fileName, L'.');
	return dot ? dot : fileName + ::wcslen(fileName);
}

void PathRemoveExtension(std::wstring& path)
{
	const size_t extPos = static_cast<size_t>(PathFindExtension(path.c_str()) - path.c_str());
	path.erase(extPos);
}

bool PathIsRelative(const wchar_t* path)
{
	return !path || path[0] != NPLX_PATH_SEP;
}

bool fileTimeToYMD(const FILETIME& ft, int& yyyymmdd)
{
	const std::time_t t = static_cast<std::time_t>(fileTimeToUnixTime(ft));
	std::tm stLocal{};
	if (!::localtime_r(&t, &stLocal))
		return false;

	yyyymmdd = ((stLocal.tm_year + 1900) * 10000) + ((stLocal.tm_mon + 1) * 100) + stLocal.tm_mday;
	return true;
}

BOOL GetFileAttributesEx(const wchar_t* path, GET_FILEEX_INFO_LEVELS, WIN32_FILE_ATTRIBUTE_DATA* fileAttr)
{
	if (!path || !fileAttr)
		return FALSE;

	const std::string nativePath = nplx::toNativePath(path);
	struct stat st{};
	if (::stat(nativePath.c_str(), &st) != 0)
	{
		fileAttr->dwFileAttributes = INVALID_FILE_ATTRIBUTES;
		return FALSE;
	}

	DWORD attr = 0;
	if (S_ISDIR(st.st_mode))
		attr |= FILE_ATTRIBUTE_DIRECTORY;
	if (::access(nativePath.c_str(), W_OK) != 0 && errno != ENOENT)
		attr |= FILE_ATTRIBUTE_READONLY;
	const wchar_t* fileName = PathFindFileName(path);
	if (fileName && fileName[0] == L'.' && fileName[1] != L'\0' && !(fileName[1] == L'.' && fileName[2] == L'\0'))
		attr |= FILE_ATTRIBUTE_HIDDEN;
	if (attr == 0)
		attr = FILE_ATTRIBUTE_NORMAL;

	fileAttr->dwFileAttributes = attr;
	fileAttr->ftCreationTime = unixTimeToFileTime(st.st_ctim.tv_sec, st.st_ctim.tv_nsec);
	fileAttr->ftLastAccessTime = unixTimeToFileTime(st.st_atim.tv_sec, st.st_atim.tv_nsec);
	fileAttr->ftLastWriteTime = unixTimeToFileTime(st.st_mtim.tv_sec, st.st_mtim.tv_nsec);
	const uint64_t size = static_cast<uint64_t>(st.st_size);
	fileAttr->nFileSizeHigh = static_cast<DWORD>(size >> 32);
	fileAttr->nFileSizeLow = static_cast<DWORD>(size & 0xFFFFFFFF);
	return TRUE;
}

static constexpr DWORD DEFAULT_MILLISEC = 3000;

namespace
{
	// NPLX: upstream runs the call in a worker thread and terminates it when the timeout has elapsed, to survive dead
	// network drives. A thread cannot be terminated on Linux: the worker owns its data, and one blocked by a hung
	// NFS / SMB / sshfs mount is left to finish on its own while the caller goes on.
	template <class Result, class Work>
	bool runWithTimeout(Work work, DWORD milliSec2wait, Result& result)
	{
		auto task = std::make_shared<std::packaged_task<Result()>>(std::move(work));
		std::future<Result> done = task->get_future();
		try
		{
			std::thread([task]() { (*task)(); }).detach();
		}
		catch (const std::system_error&)
		{
			return false; // upstream: CreateThread failed
		}
		if (done.wait_for(std::chrono::milliseconds(milliSec2wait == 0 ? DEFAULT_MILLISEC : milliSec2wait)) != std::future_status::ready)
			return false;
		result = done.get();
		return true;
	}

	struct GetAttrExParamResult
	{
		WIN32_FILE_ATTRIBUTE_DATA _attributes{};
		BOOL _result = FALSE;
		DWORD _error = NO_ERROR;
	};
}

BOOL getFileAttributesExWithTimeout(const wchar_t* filePath, WIN32_FILE_ATTRIBUTE_DATA* fileAttr,
	DWORD milliSec2wait, bool* isTimeoutReached, DWORD* pdwWin32ApiError)
{
	GetAttrExParamResult data;
	data._attributes.dwFileAttributes = INVALID_FILE_ATTRIBUTES;
	const bool isDone = runWithTimeout([path = std::wstring(filePath ? filePath : L"")]() {
		GetAttrExParamResult inAndOut;
		inAndOut._attributes.dwFileAttributes = INVALID_FILE_ATTRIBUTES;
		inAndOut._result = GetFileAttributesEx(path.c_str(), GetFileExInfoStandard, &inAndOut._attributes);
		if (!inAndOut._result)
			inAndOut._error = static_cast<DWORD>(errno);
		return inAndOut;
	}, milliSec2wait, data);

	*fileAttr = data._attributes;

	if (isTimeoutReached != nullptr)
		*isTimeoutReached = !isDone;

	if (pdwWin32ApiError != nullptr)
		*pdwWin32ApiError = data._error;

	return data._result;
}

BOOL getDiskFreeSpaceWithTimeout(const wchar_t* dirPath, ULARGE_INTEGER* freeBytesForUser, DWORD milliSec2wait, bool* isTimeoutReached)
{
	if (!freeBytesForUser)
		return FALSE;
	std::pair<BOOL, uint64_t> data{ FALSE, 0 };
	const bool isDone = runWithTimeout([path = nplx::toNativePath(dirPath)]() {
		struct statvfs sv{};
		if (::statvfs(path.c_str(), &sv) != 0)
			return std::pair<BOOL, uint64_t>{ FALSE, 0 };
		return std::pair<BOOL, uint64_t>{ TRUE, static_cast<uint64_t>(sv.f_bavail) * sv.f_frsize }; // space available to unprivileged users
	}, milliSec2wait, data);
	if (isTimeoutReached)
		*isTimeoutReached = !isDone;
	if (data.first)
		freeBytesForUser->QuadPart = data.second;
	return data.first;
}

BOOL PathIsNetworkPath(const wchar_t* path)
{
	if (!path || !path[0])
		return FALSE;

	// statfs on the path, or on its directory if the file does not exist (yet)
	std::wstring probe = path;
	struct statfs sf{};
	while (::statfs(nplx::toNativePath(probe).c_str(), &sf) != 0)
	{
		const std::wstring before = probe;
		pathRemoveFileSpec(probe);
		if (probe.empty() || probe == before)
			return FALSE;
	}

	switch (static_cast<unsigned long>(sf.f_type))
	{
		case 0x6969:     // NFS_SUPER_MAGIC
		case 0x517B:     // SMB_SUPER_MAGIC
		case 0xFF534D42: // CIFS_MAGIC_NUMBER
		case 0xFE534D42: // SMB2_MAGIC_NUMBER
		case 0x564C:     // NCP_SUPER_MAGIC
		case 0x65735546: // FUSE_SUPER_MAGIC (sshfs, gvfs, rclone...)
		case 0x00C36400: // CEPH_SUPER_MAGIC
		case 0x01161970: // GFS2_MAGIC
		case 0x6B414653: // AFS_FS_MAGIC
			return TRUE;
		default:
			return FALSE;
	}
}

DWORD invokeNppUacOp(const std::wstring&)
{
	return EACCES;
}
