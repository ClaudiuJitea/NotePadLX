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

// NPLX: the URL detection functions of upstream Notepad_plus.cpp (used by addHotSpot), in their own file
// so the upstream Test/UrlDetection data can drive unit tests.

#pragma once

// isUrl checks, whether there is a valid URL at <text [start]>.
// If yes:
// - True is returned.
// - The length of the URL is stored in <segmentLen>.
// If no:
// - False is returned.
// - The number of characters between <text[start]> and the next URL is stored in <segementLen>.
// - If no URL is found at all, then the number of characters between <text[start]> and the end of text is stored in <segmentLen>.
bool isUrl(wchar_t * text, int textLen, int start, int* segmentLen);
