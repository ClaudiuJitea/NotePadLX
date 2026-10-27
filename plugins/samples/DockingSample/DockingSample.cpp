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

// Docking Sample: a plugin with a docking panel (a Qt widget, PluginDockingQt.h). Plugins > Docking Sample >
// Document Info shows a panel with the current file, its length and the caret line, and a button that converts the
// selection to upper case. The panel follows NPPN_BUFFERACTIVATED and SCN_UPDATEUI; its menu item is checked while
// it is shown; NotePadLX saves where it is docked and opens it again at the next start.
//
// The plugin links Qt 6 Widgets: it must be built with the Qt version NotePadLX runs with.

#include <cstdio>
#include <string>

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include "PluginDockingQt.h"

namespace
{
	NppData nppData;

	enum { CmdShowPanel, CmdUpperCase, NbFunc };
	FuncItem funcItems[NbFunc] = {};

	LRESULT npp(UINT msg, WPARAM w = 0, LPARAM l = 0)
	{
		return nppData._nppSend(nppData._nppHandle, msg, w, l);
	}

	NppHandle currentScintilla()
	{
		int which = -1;
		npp(NPPM_GETCURRENTSCINTILLA, 0, reinterpret_cast<LPARAM>(&which));
		return (which == 1) ? nppData._scintillaSecondHandle : nppData._scintillaMainHandle;
	}

	LRESULT sci(UINT msg, WPARAM w = 0, LPARAM l = 0)
	{
		return nppData._sciSend(currentScintilla(), msg, w, l);
	}

	void upperCaseSelection()
	{
		// the "Convert to UPPERCASE" command of NotePadLX (any IDM_* works with NPPM_MENUCOMMAND); the plugin could
		// also edit the text itself with SCI_GETSELTEXT / SCI_REPLACESEL
		const LRESULT start = sci(SCI_GETSELECTIONSTART);
		const LRESULT end = sci(SCI_GETSELECTIONEND);
		if (start == end)
			return;
		std::string text(static_cast<size_t>(end - start), '\0');
		sci(SCI_GETSELTEXT, 0, reinterpret_cast<LPARAM>(text.data()));
		for (char& c : text)
		{
			if (c >= 'a' && c <= 'z')
				c = static_cast<char>(c - 'a' + 'A');
		}
		sci(SCI_REPLACESEL, 0, reinterpret_cast<LPARAM>(text.c_str()));
		sci(SCI_SETSEL, start, end);
	}

	class InfoPanel : public QWidget
	{
	public:
		InfoPanel()
		{
			setObjectName(QStringLiteral("DockingSamplePanel"));
			auto* layout = new QVBoxLayout(this);
			_file = new QLabel(this);
			_file->setObjectName(QStringLiteral("file"));
			_file->setWordWrap(true);
			_stats = new QLabel(this);
			_stats->setObjectName(QStringLiteral("stats"));
			auto* upper = new QPushButton(QStringLiteral("Selection to UPPERCASE"), this);
			upper->setObjectName(QStringLiteral("upperCase"));
			QObject::connect(upper, &QPushButton::clicked, upper, []() { upperCaseSelection(); });
			layout->addWidget(_file);
			layout->addWidget(_stats);
			layout->addWidget(upper);
			layout->addStretch(1);
		}

		void refresh()
		{
			const LRESULT len = npp(NPPM_GETFULLCURRENTPATH_UTF8, 0, 0);
			std::string path(len > 0 ? static_cast<size_t>(len) : 0, '\0');
			if (len > 0)
				npp(NPPM_GETFULLCURRENTPATH_UTF8, path.size() + 1, reinterpret_cast<LPARAM>(path.data()));
			_file->setText(QString::fromStdString(path));
			const LRESULT line = sci(SCI_LINEFROMPOSITION, sci(SCI_GETCURRENTPOS)) + 1;
			_stats->setText(QStringLiteral("%1 characters, %2 lines, caret on line %3")
				.arg(sci(SCI_GETLENGTH)).arg(sci(SCI_GETLINECOUNT)).arg(line));
		}

	private:
		QLabel* _file = nullptr;
		QLabel* _stats = nullptr;
	};

	InfoPanel* panel = nullptr;
	DockedWidgetData dockData;

	void showPanel()
	{
		if (!panel)
		{
			panel = new InfoPanel(); // NotePadLX puts it in a dock (its parent from now on)
			dockData.hClient = panel;
			dockData.pszName = "Document Info";
			dockData.dlgID = CmdShowPanel;
			dockData.uMask = DWS_DF_CONT_RIGHT | DWS_ADDINFO;
			dockData.pszAddInfo = "Docking Sample";
			dockData.pszModuleName = "DockingSample.so";
			npp(NPPM_DMMREGASDCKDLG, 0, reinterpret_cast<LPARAM>(&dockData));
		}

		// the command toggles the panel, as upstream panels do
		const bool toShow = !panel->isVisible();
		npp(toShow ? NPPM_DMMSHOW : NPPM_DMMHIDE, 0, reinterpret_cast<LPARAM>(panel));
		npp(NPPM_SETMENUITEMCHECK, funcItems[CmdShowPanel]._cmdID, toShow);
		if (toShow)
			panel->refresh();
	}
}

NPLX_PLUGIN_EXPORT int getAbiVersion()
{
	return NPLX_PLUGIN_ABI_VERSION;
}

NPLX_PLUGIN_EXPORT void setInfo(NppData notepadPlusData)
{
	nppData = notepadPlusData;
	std::snprintf(funcItems[CmdShowPanel]._itemName, menuItemSize, "Document Info");
	funcItems[CmdShowPanel]._pFunc = showPanel;
	std::snprintf(funcItems[CmdUpperCase]._itemName, menuItemSize, "Selection to UPPERCASE");
	funcItems[CmdUpperCase]._pFunc = upperCaseSelection;
}

NPLX_PLUGIN_EXPORT const char* getName()
{
	return "Docking Sample";
}

NPLX_PLUGIN_EXPORT FuncItem* getFuncsArray(int* nbF)
{
	*nbF = NbFunc;
	return funcItems;
}

NPLX_PLUGIN_EXPORT void beNotified(SCNotification* notifyCode)
{
	if (!panel || !panel->isVisible())
		return;
	switch (notifyCode->nmhdr.code)
	{
		case NPPN_BUFFERACTIVATED:
		case NPPN_FILESAVED:
		case NPPN_FILERENAMED:
			panel->refresh();
			break;
		case SCN_UPDATEUI:
			if (notifyCode->updated & (SC_UPDATE_CONTENT | SC_UPDATE_SELECTION))
				panel->refresh();
			break;
		default:
			break;
	}
}

NPLX_PLUGIN_EXPORT LRESULT messageProc(UINT, WPARAM, LPARAM)
{
	return TRUE;
}
