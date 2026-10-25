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

#include "Paths.h"

#include <climits>
#include <cstdlib>
#include <cstring>
#include <langinfo.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include "Common.h"

namespace nplx
{
namespace
{
	bool isDirectory(const std::string& path)
	{
		struct stat st{};
		return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
	}

	bool hasModelFiles(const std::string& dir)
	{
		struct stat st{};
		return ::stat((dir + "/langs.model.xml").c_str(), &st) == 0;
	}

	std::string homeDirectory()
	{
		if (const char* home = ::getenv("HOME"); home && *home)
			return home;
		if (const passwd* pw = ::getpwuid(::getuid()); pw && pw->pw_dir)
			return pw->pw_dir;
		return "/tmp";
	}

	std::wstring xdgDir(const char* envName, const char* defaultSubDir)
	{
		const char* v = ::getenv(envName);
		if (v && v[0] == '/') // the XDG spec: relative paths are invalid and must be ignored
			return fromNativePath(v);
		return fromNativePath(homeDirectory() + "/" + defaultSubDir);
	}
}

std::wstring executablePath()
{
	char buf[PATH_MAX]{};
	const ssize_t len = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
	if (len <= 0)
		return L"";
	buf[len] = '\0';
	return fromNativePath(buf);
}

std::wstring executableDirectory()
{
	std::wstring dir = executablePath();
	pathRemoveFileSpec(dir);
	return dir;
}

std::wstring dataDirectory()
{
	if (const char* env = ::getenv("NPLX_DATA_DIR"); env && *env && isDirectory(env))
		return fromNativePath(env);

	const std::string exeDir = toNativePath(executableDirectory());
	for (const std::string& candidate : { exeDir + "/data", exeDir + "/../share/notepadlx" })
	{
		if (hasModelFiles(candidate))
		{
			char resolved[PATH_MAX]{};
			if (::realpath(candidate.c_str(), resolved))
				return fromNativePath(resolved);
			return fromNativePath(candidate);
		}
	}

#ifdef NPLX_SOURCE_DATA_DIR
	if (hasModelFiles(NPLX_SOURCE_DATA_DIR))
		return fromNativePath(NPLX_SOURCE_DATA_DIR);
#endif

	return executableDirectory();
}

std::wstring currentDirectory()
{
	char buf[PATH_MAX]{};
	return ::getcwd(buf, sizeof(buf)) ? fromNativePath(buf) : std::wstring();
}

std::wstring xdgConfigHome()
{
	return xdgDir("XDG_CONFIG_HOME", ".config");
}

std::wstring xdgDataHome()
{
	return xdgDir("XDG_DATA_HOME", ".local/share");
}

std::wstring xdgCacheHome()
{
	return xdgDir("XDG_CACHE_HOME", ".cache");
}

std::wstring userConfigDirectory()
{
	std::wstring dir = xdgConfigHome();
	pathAppend(dir, L"notepadlx");
	return dir;
}

int currentSystemCodepage()
{
	const char* cs = ::nl_langinfo(CODESET);
	if (!cs || ::strcasecmp(cs, "UTF-8") == 0 || ::strcasecmp(cs, "utf8") == 0)
		return CP_UTF8;
	if (::strcasecmp(cs, "ISO-8859-1") == 0)
		return 28591;
	if (::strcasecmp(cs, "ISO-8859-15") == 0)
		return 28605;
	if (::strcasecmp(cs, "ANSI_X3.4-1968") == 0) // "C" locale: plain ASCII, treat as UTF-8 like modern systems
		return CP_UTF8;
	return CP_UTF8;
}
}
