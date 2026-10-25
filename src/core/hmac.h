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

// NPLX: port of upstream MISC/hmac/hmac.h. Upstream keys the HMAC with the registry
// MachineGuid and uses Windows BCrypt; here the key is /etc/machine-id (systemd, D-Bus fallback)
// and HMAC-SHA256 is computed with upstream's portable sha-256.cpp.
// A shortcuts.xml HMAC stored by Windows Notepad++ therefore does not match on Linux: the user is
// told once that shortcuts.xml was changed outside the app, as upstream does for any mismatch.

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "hash/sha-256.h"

// Read the machine id (Linux equivalent of the Windows MachineGuid)
static std::string getMachineGUID()
{
	for (const char* path : { "/etc/machine-id", "/var/lib/dbus/machine-id" })
	{
		std::ifstream f(path);
		std::string id;
		if (f && std::getline(f, id) && !id.empty())
			return id;
	}
	return "";
}

// HMAC-SHA256 (RFC 2104), returned as lowercase hex like upstream
static std::string computeHMAC(const std::string& key, const std::string& data)
{
	constexpr size_t blockSize = 64;
	uint8_t keyBlock[blockSize]{};

	if (key.size() > blockSize)
		calc_sha_256(keyBlock, key.data(), key.size()); // first 32 bytes, rest stays 0
	else
		std::memcpy(keyBlock, key.data(), key.size());

	std::vector<uint8_t> inner(blockSize + data.size());
	for (size_t i = 0; i < blockSize; ++i)
		inner[i] = keyBlock[i] ^ 0x36;
	std::memcpy(inner.data() + blockSize, data.data(), data.size());

	uint8_t innerHash[32]{};
	calc_sha_256(innerHash, inner.data(), inner.size());

	uint8_t outer[blockSize + 32]{};
	for (size_t i = 0; i < blockSize; ++i)
		outer[i] = keyBlock[i] ^ 0x5c;
	std::memcpy(outer + blockSize, innerHash, sizeof(innerHash));

	uint8_t hash[32]{};
	calc_sha_256(hash, outer, sizeof(outer));

	std::string result;
	char hex[3];
	for (uint8_t b : hash)
	{
		std::snprintf(hex, sizeof(hex), "%02x", b);
		result += hex;
	}
	return result;
}
