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

// NPLX: non-UI part of upstream localization.cpp: reading translated strings from
// nativeLang XML files. The functions that translate Win32 menus and dialogs
// (changeMenuLang, changeDlgLang, ...) are reimplemented on Qt widgets in ui/.

#include "localization.h"

#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

#include "Common.h"
#include "NppDarkMode.h"
#include "NppXml.h"
#include "Parameters.h"
#include "resource.h"

static constexpr MenuPosition g_menuFolderPositions[]{
//==============================================
//	{L0,  L1, L2, id},
//==============================================
	{ 0,  -1, -1, "file" },
	{ 1,  -1, -1, "edit" },
	{ 2,  -1, -1, "search" },
	{ 3,  -1, -1, "view" },
	{ 4,  -1, -1, "encoding" },
	{ 5,  -1, -1, "language" },
	{ 6,  -1, -1, "settings" },
	{ 7,  -1, -1, "tools" },
	{ 8,  -1, -1, "macro" },
	{ 9,  -1, -1, "run" },
	{ 10, -1, -1, "Plugins" },
	{ 11, -1, -1, "Window" },

	{ 0,   2, -1, "file-openFolder" },
	{ 0,  13, -1, "file-closeMore" },
	{ 0,  22, -1, "file-recentFiles" },

	{ 1,  11, -1, "edit-insert" },
	{ 1,  12, -1, "edit-copyToClipboard" },
	{ 1,  13, -1, "edit-indent" },
	{ 1,  14, -1, "edit-convertCaseTo" },
	{ 1,  15, -1, "edit-lineOperations" },
	{ 1,  16, -1, "edit-comment" },
	{ 1,  17, -1, "edit-autoCompletion" },
	{ 1,  18, -1, "edit-eolConversion" },
	{ 1,  19, -1, "edit-blankOperations" },
	{ 1,  20, -1, "edit-pasteSpecial" },
	{ 1,  21, -1, "edit-onSelection" },
	{ 1,  23, -1, "edit-multiSelectALL" },
	{ 1,  24, -1, "edit-multiSelectNext" },
	{ 1,  33, -1, "edit-readonlyInNotepad++" },

	{ 2,  18, -1, "search-changeHistory" },
	{ 2,  20, -1, "search-markAll" },
	{ 2,  21, -1, "search-markOne" },
	{ 2,  22, -1, "search-unmarkAll" },
	{ 2,  23, -1, "search-jumpUp" },
	{ 2,  24, -1, "search-jumpDown" },
	{ 2,  25, -1, "search-copyStyledText" },
	{ 2,  27, -1, "search-bookmark" },

	{ 3,   5, -1, "view-currentFileIn" },
	{ 3,   7, -1, "view-showSymbol" },
	{ 3,   8, -1, "view-zoom" },
	{ 3,   9, -1, "view-moveCloneDocument" },
	{ 3,  10, -1, "view-tab" },
	{ 3,  19, -1, "view-collapseLevel" },
	{ 3,  20, -1, "view-uncollapseLevel" },
	{ 3,  24, -1, "view-project" },

	{ 4,   5, -1, "encoding-characterSets" },
	{ 4,   5,  0, "encoding-arabic" },
	{ 4,   5,  1, "encoding-baltic" },
	{ 4,   5,  2, "encoding-celtic" },
	{ 4,   5,  3, "encoding-cyrillic" },
	{ 4,   5,  4, "encoding-centralEuropean" },
	{ 4,   5,  5, "encoding-chinese" },
	{ 4,   5,  6, "encoding-easternEuropean" },
	{ 4,   5,  7, "encoding-greek" },
	{ 4,   5,  8, "encoding-hebrew" },
	{ 4,   5,  9, "encoding-japanese" },
	{ 4,   5, 10, "encoding-korean" },
	{ 4,   5, 11, "encoding-northEuropean" },
	{ 4,   5, 12, "encoding-thai" },
	{ 4,   5, 13, "encoding-turkish" },
	{ 4,   5, 14, "encoding-westernEuropean" },
	{ 4,   5, 15, "encoding-vietnamese" },

	{ 5,  25, -1, "language-userDefinedLanguage" },

	{ 6,   4, -1, "settings-import" },

	{ 7,   0, -1, "tools-md5" },
	{ 7,   1, -1, "tools-sha1" },
	{ 7,   2, -1, "tools-sha256" },
	{ 7,   3, -1, "tools-sha512" },

	{ 11,  0, -1, "window-sortby"},

	{ -1,  -1, -1, "" } // End of array
};

const MenuPosition& MenuPosition::getMenuPosition(const char* id)
{
	int nbSubMenuPos = sizeof(g_menuFolderPositions) / sizeof(MenuPosition);

	for (int i = 0; i < nbSubMenuPos; ++i)
	{
		if (strcmp(g_menuFolderPositions[i]._id, id) == 0)
			return g_menuFolderPositions[i];
	}
	return g_menuFolderPositions[nbSubMenuPos - 1];
}

static void purifyMenuString(std::string& s)
{
	// Remove & for CJK localization
	size_t posAndCJK = s.find("(&", 0);
	if (posAndCJK != std::string::npos)
	{
		if (posAndCJK + 3 < s.length())
		{
			if (s[posAndCJK + 3] == ')')
				s.erase(posAndCJK, 4);
		}
	}

	// Remove & and transform && to & for all localizations
	for (int i = static_cast<int>(s.length()) - 1; i >= 0; --i)
	{
		if (s[i] == '&')
		{
			if (i - 1 >= 0 && s[static_cast<size_t>(i) - 1] == '&')
			{
				s.erase(i, 1);
				i -= 1;
			}
			else
			{
				s.erase(i, 1);
			}
		}
	}

	// Remove ellipsis...
	size_t len = s.length();
	if (len <= 3)
		return;
	size_t posEllipsis = len - 3;
	if (s.substr(posEllipsis) == "...")
		s.erase(posEllipsis, 3);

}

void NativeLangSpeaker::init(NppXml::Document nativeLangDocRoot, bool loadIfEnglish)
{
	if (nativeLangDocRoot)
	{
		_nativeLang = NppXml::firstChildElement(nativeLangDocRoot, "NotepadPlus");
		if (_nativeLang)
		{
			_nativeLang = NppXml::firstChildElement(_nativeLang, "Native-Langue");
			if (_nativeLang)
			{
				const char* rtl = NppXml::attribute(_nativeLang, "RTL");
				if (rtl != nullptr)
				{
					_isRTL = (strcmp(rtl, "yes") == 0);
					if (_isRTL)
					{
						const char* editZoneRtl = NppXml::attribute(_nativeLang, "editZoneRTL");
						if (editZoneRtl)
							_isEditZoneRTL = !(strcmp(editZoneRtl, "no") == 0);
						else
							_isEditZoneRTL = true;
					}
					else
						_isEditZoneRTL = false;
				}
				else
				{
					_isRTL = false;
					_isEditZoneRTL = false;
				}

				// get original file name (defined by Notpad++) from the attribute
				_fileName = NppXml::attribute(_nativeLang, "filename");

				if (!loadIfEnglish && _fileName && _stricmp("english.xml", _fileName) == 0)
				{
					_nativeLang = {};
				}
			}
		}
	}
}

void NativeLangSpeaker::getAlternativeNameFromTabContextMenu(std::wstring& output, int cmdID, bool isAlternative, const std::wstring& defaultValue) const
{
	if (_nativeLang)
	{
		NppXml::Element tabBarMenu = NppXml::firstChildElement(_nativeLang, "Menu");
		if (tabBarMenu)
		{
			tabBarMenu = NppXml::firstChildElement(tabBarMenu, "TabBar");
			if (tabBarMenu)
			{
				for (NppXml::Element childNode = NppXml::firstChildElement(tabBarMenu, "Item");
					childNode;
					childNode = NppXml::nextSiblingElement(childNode, "Item"))
				{
					if (NppXml::intAttribute(childNode, "CMDID", -1) == cmdID)
					{
						const char* pName = NppXml::attribute(childNode, isAlternative ? "alternativeName" : "name");
						if (pName)
						{
							WcharMbcsConvertor& wmc = WcharMbcsConvertor::getInstance();
							output = wmc.char2wchar(pName, _nativeLangEncoding);
							return;
						}
					}
				}
			}
		}
	}
	output = defaultValue;
}

NppXml::Element NativeLangSpeaker::searchDlgNode(NppXml::Element node, const char* dlgTagName)
{
	NppXml::Element dlgNode = NppXml::firstChildElement(node, dlgTagName);
	if (dlgNode) return dlgNode;
	for (NppXml::Element childNode = NppXml::firstChildElement(node, nullptr);
		childNode;
		childNode = NppXml::nextSibling(childNode))
	{
		dlgNode = searchDlgNode(childNode, dlgTagName);
		if (dlgNode) return dlgNode;
	}
	return {};
}

std::wstring NativeLangSpeaker::getSubMenuEntryName(const char* nodeName) const
{
	if (!_nativeLang) return L"";
	NppXml::Element mainMenu = NppXml::firstChildElement(_nativeLang, "Menu");
	if (!mainMenu) return L"";
	mainMenu = NppXml::firstChildElement(mainMenu, "Main");
	if (!mainMenu) return L"";
	NppXml::Element entriesRoot = NppXml::firstChildElement(mainMenu, "SubEntries");
	if (!entriesRoot) return L"";

	for (NppXml::Element childNode = NppXml::firstChildElement(entriesRoot, "Item");
		childNode;
		childNode = NppXml::nextSiblingElement(childNode, "Item"))
	{
		const char* idName = NppXml::attribute(childNode, "subMenuId");
		if (idName)
		{
			const char* name = NppXml::attribute(childNode, "name");
			if (std::strcmp(idName, nodeName) == 0)
			{
				return string2wstring(name, _nativeLangEncoding);
			}
		}
	}
	return L"";
}

std::wstring NativeLangSpeaker::getNativeLangMenuString(int itemID, const std::wstring& inCaseOfFailureStr, bool removeMarkAnd) const
{
	if (!_nativeLang)
		return inCaseOfFailureStr;

	NppXml::Element node = NppXml::firstChildElement(_nativeLang, "Menu");
	if (!node) return inCaseOfFailureStr;

	node = NppXml::firstChildElement(node, "Main");
	if (!node) return inCaseOfFailureStr;

	node = NppXml::firstChildElement(node, "Commands");
	if (!node) return inCaseOfFailureStr;

	for (NppXml::Element childNode = NppXml::firstChildElement(node, "Item");
		childNode;
		childNode = NppXml::nextSiblingElement(childNode, "Item"))
	{
		if (const int id = NppXml::intAttribute(childNode, "id", 0); (id == itemID))
		{
			const char* name = NppXml::attribute(childNode, "name");
			if (name)
			{
				std::string nameStr = name;
				if (removeMarkAnd)
				{
					purifyMenuString(nameStr);
				}
				return string2wstring(nameStr, _nativeLangEncoding);
			}
		}
	}
	return inCaseOfFailureStr;
}

std::wstring NativeLangSpeaker::getShortcutNameString(int itemID) const
{
	if (!_nativeLang)
		return L"";

	NppXml::Element node = NppXml::firstChildElement(_nativeLang, "Dialog");
	if (!node) return L"";

	node = NppXml::firstChildElement(node, "ShortcutMapper");
	if (!node) return L"";

	node = NppXml::firstChildElement(node, "MainCommandNames");
	if (!node) return L"";

	for (NppXml::Element childNode = NppXml::firstChildElement(node, "Item");
		childNode;
		childNode = NppXml::nextSiblingElement(childNode, "Item"))
	{
		if (NppXml::intAttribute(childNode, "id", 0) == itemID)
		{
			const char* name = NppXml::attribute(childNode, "name");
			if (name)
			{
				return string2wstring(name, _nativeLangEncoding);
			}
		}
	}
	return L"";
}

std::string NativeLangSpeaker::getLocalizedStrFromID(const char* strID, const std::string& defaultString) const
{
	if (!_nativeLang)
		return defaultString;

	if (!strID)
		return defaultString;

	NppXml::Element node = NppXml::firstChildElement(_nativeLang, "MiscStrings");
	if (!node) return defaultString;

	node = NppXml::firstChildElement(node, strID);
	if (!node) return defaultString;

	const char* value = NppXml::attribute(node, "value");
	if (!value) return defaultString;

	return value;
}

std::wstring NativeLangSpeaker::getLocalizedStrFromID(const char* strID, const std::wstring& defaultString) const
{
	const std::string defaultStringA = wstring2string(defaultString, _nativeLangEncoding);
	return string2wstring(NativeLangSpeaker::getLocalizedStrFromID(strID, defaultStringA), _nativeLangEncoding);
}

bool NativeLangSpeaker::getDoSaveOrNotStrings(std::wstring& title, std::wstring& msg) const
{
	if (!_nativeLang) return false;

	NppXml::Element dlgNode = NppXml::firstChildElement(_nativeLang, "Dialog");
	if (!dlgNode) return false;

	dlgNode = searchDlgNode(dlgNode, "DoSaveOrNot");
	if (!dlgNode) return false;

	const char* title2set = NppXml::attribute(dlgNode, "title");
	if (!title2set || !title2set[0]) return false;

	WcharMbcsConvertor& wmc = WcharMbcsConvertor::getInstance();
	const wchar_t* titleW = wmc.char2wchar(title2set, _nativeLangEncoding);
	title = titleW;

	for (NppXml::Element childNode = NppXml::firstChildElement(dlgNode, "Item");
		childNode;
		childNode = NppXml::nextSiblingElement(childNode, "Item"))
	{
		const int id = NppXml::intAttribute(childNode, "id", -1); // id starts with <Item id="2" name="&amp;Cancel"/>
		const char* name = NppXml::attribute(childNode, "name");
		if (id >= 0 && (name && name[0]))
		{
			if (id == IDC_DOSAVEORNOTTEXT)
			{
				const wchar_t* msgW = wmc.char2wchar(name, _nativeLangEncoding);
				msg = msgW;

				return true;
			}
		}
	}
	return false;
}

bool NativeLangSpeaker::getMsgBoxLang(const char* msgBoxTagName, std::wstring& title, std::wstring& message) const
{
	title = L"";
	message = L"";

	if (!_nativeLang) return false;

	NppXml::Element msgBoxNode = NppXml::firstChildElement(_nativeLang, "MessageBox");
	if (!msgBoxNode) return false;

	msgBoxNode = searchDlgNode(msgBoxNode, msgBoxTagName);
	if (!msgBoxNode) return false;

	WcharMbcsConvertor& wmc = WcharMbcsConvertor::getInstance();

	// Set Title
	const char* titre = NppXml::attribute(msgBoxNode, "title");
	const char* msg = NppXml::attribute(msgBoxNode, "message");
	if ((titre && titre[0]) && (msg && msg[0]))
	{
		title = wmc.char2wchar(titre, _nativeLangEncoding);
		message = wmc.char2wchar(msg, _nativeLangEncoding);
		return true;
	}
	return false;
}

std::wstring NativeLangSpeaker::getShortcutMapperLangStr(const char* nodeName, const wchar_t* defaultStr) const
{
	if (!_nativeLang) return defaultStr;

	NppXml::Element targetNode = NppXml::firstChildElement(_nativeLang, "Dialog");
	if (!targetNode) return defaultStr;

	targetNode = NppXml::firstChildElement(targetNode, "ShortcutMapper");
	if (!targetNode) return defaultStr;

	targetNode = NppXml::firstChildElement(targetNode, nodeName);
	if (!targetNode) return defaultStr;

	const char* name = NppXml::attribute(targetNode, "name");
	if (name && name[0])
	{
		return string2wstring(name, _nativeLangEncoding);
	}

	return defaultStr;
}

std::wstring NativeLangSpeaker::getProjectPanelLangMenuStr(const char* nodeName, int cmdID, const wchar_t* defaultStr) const
{
	if (!_nativeLang) return defaultStr;

	NppXml::Element targetNode = NppXml::firstChildElement(_nativeLang, "ProjectManager");
	if (!targetNode) return defaultStr;

	targetNode = NppXml::firstChildElement(targetNode, "Menus");
	if (!targetNode) return defaultStr;

	targetNode = NppXml::firstChildElement(targetNode, nodeName);
	if (!targetNode) return defaultStr;

	const char* name = nullptr;
	for (NppXml::Element childNode = NppXml::firstChildElement(targetNode, "Item");
		childNode;
		childNode = NppXml::nextSiblingElement(childNode, "Item"))
	{
		const int id = NppXml::intAttribute(childNode, "id", -1); // id starts with <Item id="0" name="Workspace"/>
		if (id >= 0 && id == cmdID)
		{
			name = NppXml::attribute(childNode, "name");
			break;
		}
	}

	if (name && name[0])
	{
		return string2wstring(name, _nativeLangEncoding);
	}
	return defaultStr;
}

std::wstring NativeLangSpeaker::getDlgLangMenuStr(const char* firstLevelNodeName, const char* secondLevelNodeName, int cmdID, const wchar_t* defaultStr) const
{
	if (!_nativeLang) return defaultStr;

	NppXml::Element targetNode = NppXml::firstChildElement(_nativeLang, firstLevelNodeName);
	if (!targetNode) return defaultStr;

	if (secondLevelNodeName && secondLevelNodeName[0])
	{
		targetNode = NppXml::firstChildElement(targetNode, secondLevelNodeName);
		if (!targetNode) return defaultStr;
	}

	targetNode = NppXml::firstChildElement(targetNode, "Menu");
	if (!targetNode) return defaultStr;

	const char* name = nullptr;
	for (NppXml::Element childNode = NppXml::firstChildElement(targetNode, "Item");
		childNode;
		childNode = NppXml::nextSiblingElement(childNode, "Item"))
	{
		// depends on which menu (Find smap button, FolderAsWorkspace menu)
		// smallest id starts with <Item id="1726" name="⇅ Swap Find with Replace"/>
		const int id = NppXml::intAttribute(childNode, "id", -1);
		if (id >= 0 && id == cmdID)
		{
			name = NppXml::attribute(childNode, "name");
			break;
		}
	}

	if (name && name[0])
	{
		return string2wstring(name, _nativeLangEncoding);
	}
	return defaultStr;
}

std::wstring NativeLangSpeaker::getCmdLangStr(const std::vector<const char*>& nodeNames, int cmdID, const wchar_t* defaultStr) const
{
	if (!_nativeLang) return defaultStr;
	NppXml::Element targetNode = NppXml::firstChildElement(_nativeLang, nodeNames.at(0));
	if (targetNode == nullptr)
		return defaultStr;

	auto it = nodeNames.begin();
	++it;

	for (auto end = nodeNames.end(); it != end; ++it)
	{
		targetNode = NppXml::firstChildElement(targetNode, *it);
		if (targetNode == nullptr)
			return defaultStr;
	}

	if (targetNode == nullptr)
		return defaultStr;

	const char* name = nullptr;
	for (NppXml::Element childNode = NppXml::firstChildElement(targetNode, "Item");
		childNode;
		childNode = NppXml::nextSiblingElement(childNode, "Item"))
	{
		// used by dark mode custom color reset button
		// id starts with <Item id="7102" name="Black"/>
		const int id = NppXml::intAttribute(childNode, "id", -1); 
		if (id >= 0 && id == cmdID)
		{
			name = NppXml::attribute(childNode, "name");
			break;
		}
	}

	if (name && name[0])
	{
		return string2wstring(name, _nativeLangEncoding);
	}
	return defaultStr;
}

std::wstring NativeLangSpeaker::getAttrNameStr(const wchar_t* defaultStr, const char* nodeL1Name, const char* nodeL2Name, const char* nodeL3Name) const
{
	if (!_nativeLang) return defaultStr;

	NppXml::Element targetNode = NppXml::firstChildElement(_nativeLang, nodeL1Name);
	if (!targetNode) return defaultStr;
	if (nodeL2Name)
		targetNode = NppXml::firstChildElement(targetNode, nodeL2Name);

	if (!targetNode) return defaultStr;

	const char* name = NppXml::attribute(targetNode, nodeL3Name);
	if (name && name[0])
	{
		return string2wstring(name, _nativeLangEncoding);
	}
	return defaultStr;
}

std::wstring NativeLangSpeaker::getAttrNameByIdStr(const wchar_t* defaultStr, NppXml::Element targetNode, const char* nodeL1Value, const char* nodeL1Name, const char* nodeL2Name)
{
	if (!targetNode) return defaultStr;

	for (NppXml::Element childNode = NppXml::firstChildElement(targetNode, "Item");
		childNode;
		childNode = NppXml::nextSiblingElement(childNode, "Item"))
	{
		const char* id = NppXml::attribute(childNode, nodeL1Name);
		if (id && id[0] && !strcmp(id, nodeL1Value))
		{
			const char* name = NppXml::attribute(childNode, nodeL2Name);
			if (name && name[0])
			{
				return string2wstring(name, _nativeLangEncoding);
			}
		}
	}
	return defaultStr;
}

int NativeLangSpeaker::messageBox(const char* msgBoxTagName, const wchar_t* defaultMessage, const wchar_t* defaultTitle, int msgBoxType, int intInfo, const wchar_t* strInfo) const
{
	if (NppParameters::getInstance().isEndSessionCritical())
		return IDCANCEL; // simulate Esc-key or Cancel-button as there should not be any big delay / code-flow block

	std::wstring msg;
	std::wstring title;
	if (!getMsgBoxLang(msgBoxTagName, title, msg))
	{
		title = defaultTitle;
		msg = defaultMessage;
	}
	title = stringReplace(title, L"$INT_REPLACE$", std::to_wstring(intInfo));
	msg = stringReplace(msg, L"$INT_REPLACE$", std::to_wstring(intInfo));
	if (strInfo)
	{
		title = stringReplace(title, L"$STR_REPLACE$", strInfo);
		msg = stringReplace(msg, L"$STR_REPLACE$", strInfo);
	}
	// NPLX: RTL layout is applied by the Qt message box from the application layout direction

	if (title.empty() || title == L"0")
	{
		title = getLocalizedStrFromID("common-error", L"Error");
	}

	return NppDarkMode::darkMessageBoxW(msg.c_str(), title.c_str(), msgBoxType);
}
