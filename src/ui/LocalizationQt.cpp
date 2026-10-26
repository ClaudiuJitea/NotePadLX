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

#include "LocalizationQt.h"

#include <cwchar>
#include <iterator>
#include <vector>

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QPointer>
#include <QLabel>
#include <QLayout>
#include <QRadioButton>
#include <QRegularExpression>
#include <QTabBar>

#include "FindReplaceDlg.h"
#include "Parameters.h"
#include "RcDialog.h"
#include "UserDefineDialog.h"
#include "UserDefineResource.h"
#include "WordStyleDlg.h"
#include "preferenceDlg.h"
#include "localization.h"

namespace nplx
{
	namespace
	{
		bool isInLayout(const QWidget* w)
		{
			const QWidget* parent = w->parentWidget();
			return parent && parent->layout() && parent->layout()->indexOf(w) >= 0;
		}

		// NPLX: upstream reloadLang calls the changeXxxLang of each created dialog; here every translated dialog is
		// remembered with its translation, which retranslateDialogs runs again
		struct Translator
		{
			QPointer<QWidget> _dlg;
			std::string _key;
			std::function<void()> _translate;
		};
		std::vector<Translator> g_translators;
		int g_rememberBlocked = 0; // inside a dialog-specific translation, or while retranslating

		struct BlockRemember
		{
			BlockRemember() { ++g_rememberBlocked; }
			~BlockRemember() { --g_rememberBlocked; }
			BlockRemember(const BlockRemember&) = delete;
			BlockRemember& operator=(const BlockRemember&) = delete;
		};

		void remember(QWidget* dlg, std::string key, std::function<void()> translate)
		{
			if (!dlg || g_rememberBlocked > 0)
				return;
			for (const Translator& t : g_translators)
			{
				if (t._dlg == dlg && t._key == key)
					return;
			}
			g_translators.push_back(Translator{ dlg, std::move(key), std::move(translate) });
		}
	}

	void retranslateDialogs()
	{
		const BlockRemember block;
		std::erase_if(g_translators, [](const Translator& t) { return t._dlg.isNull(); });
		const std::vector<Translator> translators = g_translators; // a translation may create a dialog
		for (const Translator& t : translators)
		{
			if (!t._dlg.isNull())
				t._translate();
		}
	}

	const NativeLangSpeaker* nativeLangSpeaker()
	{
		return NppParameters::getInstance().getNativeLangSpeaker();
	}

	void setControlText(QWidget* w, const std::wstring& text)
	{
		const QString s = QString::fromStdWString(text);
		if (auto* l = qobject_cast<QLabel*>(w))
		{
			// NPLX: a link label (upstream URLCtrl) stays a link
			static const QRegularExpression linkRe(QStringLiteral("^<a href=\"([^\"]*)\">"));
			const QRegularExpressionMatch link = linkRe.match(l->text());
			if (link.hasMatch())
				l->setText(QStringLiteral("<a href=\"%1\">%2</a>").arg(link.captured(1), s.toHtmlEscaped()));
			else
				l->setText(s);
		}
		else if (auto* b = qobject_cast<QAbstractButton*>(w))
		{
			b->setText(s);
			// upstream resizeCheckboxRadioBtn: a check box or radio button takes its ideal width, so a longer
			// translation is not cut (a control placed by a layout is sized by the layout instead)
			if ((qobject_cast<QCheckBox*>(b) || qobject_cast<QRadioButton*>(b)) && !isInLayout(b))
				b->resize(b->sizeHint().width(), b->height());
		}
		else if (auto* g = qobject_cast<QGroupBox*>(w))
			g->setTitle(s);
	}

	bool applyDlgLang(QWidget* dlg, const DlgItemLookup& item, const DlgLang& lang, std::wstring* titleOut, int minItemId)
	{
		if (!lang._title.empty())
		{
			if (dlg)
				dlg->setWindowTitle(QString::fromStdWString(lang._title));
			if (titleOut)
				*titleOut = lang._title;
		}

		for (const auto& [id, name] : lang._items)
		{
			if (id < minItemId)
				continue;
			if (QWidget* w = item(id))
				setControlText(w, name);
		}

		for (const auto& [id, elements] : lang._comboBoxes)
		{
			auto* combo = qobject_cast<QComboBox*>(item(id));
			if (!combo)
				return false;

			// upstream: only when the translation has as many entries; the selection is kept
			if (combo->count() == static_cast<int>(elements.size()))
			{
				const QSignalBlocker blocker(combo);
				for (int i = 0; i < combo->count(); ++i)
					combo->setItemText(i, QString::fromStdWString(elements[static_cast<size_t>(i)]));
			}
		}
		return true;
	}

	bool changeDlgLang(QWidget* dlg, const DlgItemLookup& item, const char* dlgTagName, const char* titleTag, std::wstring* titleOut)
	{
		if (titleOut)
			titleOut->clear();

		const std::string tag = dlgTagName;
		const std::string title = titleTag ? titleTag : "";
		remember(dlg, tag + "|" + title, [dlg, item, tag, title]() {
			changeDlgLang(dlg, item, tag.c_str(), title.empty() ? nullptr : title.c_str());
		});

		const NativeLangSpeaker* speaker = nativeLangSpeaker();
		DlgLang lang;
		if (!speaker || !speaker->getDlgLang(dlgTagName, lang, titleTag))
			return false;
		return applyDlgLang(dlg, item, lang, titleOut);
	}

	bool changeDlgLang(RcDialog& dlg, const char* dlgTagName, const char* titleTag, std::wstring* titleOut)
	{
		return changeDlgLang(&dlg, [&dlg](int id) { return dlg.dlgItem(id); }, dlgTagName, titleTag, titleOut);
	}

	void setCtrlId(QWidget* w, int id)
	{
		w->setProperty("nplxCtrlId", id);
	}

	QWidget* ctrlById(const QWidget* dlg, int id)
	{
		for (QWidget* w : dlg->findChildren<QWidget*>())
		{
			const QVariant v = w->property("nplxCtrlId");
			if (v.isValid() && v.toInt() == id)
				return w;
		}
		return nullptr;
	}

	bool changeDlgLang(QWidget* dlg, const char* dlgTagName, const char* titleTag, std::wstring* titleOut)
	{
		return changeDlgLang(dlg, [dlg](int id) { return ctrlById(dlg, id); }, dlgTagName, titleTag, titleOut);
	}

	void changePreferenceDlgLang(PreferenceDlg& preference)
	{
		remember(&preference, "Preference*", [dlg = &preference]() { changePreferenceDlgLang(*dlg); });
		const BlockRemember block;

		const int currentSel = preference.getListSelectedIndex();
		changeDlgLang(preference, "Preference");

		// the pages, by the internal name that is also their nativeLang.xml tag; the title renames the list entry
		static constexpr const wchar_t* pageNames[] = {
			L"Global", L"Toolbar", L"Tabbar", L"Scintillas", L"Scintillas2", L"DarkMode", L"MarginsBorderEdge",
			L"NewDoc", L"DefaultDir", L"RecentFilesHistory", L"FileAssoc", L"Language", L"Indentation",
			L"Highlighting", L"Print", L"Searching", L"MISC", L"Backup", L"AutoCompletion", L"MultiInstance",
			L"Delimiter", L"Performance", L"Cloud", L"SearchEngine",
		};
		for (const wchar_t* name : pageNames)
		{
			RcDialog* page = preference.pageByName(name);
			if (!page)
				continue; // FileAssoc: not on Linux
			const std::string tag(name, name + wcslen(name));
			std::wstring title;
			changeDlgLang(*page, tag.c_str(), nullptr, &title);
			if (!title.empty())
				preference.renameDialogTitle(name, title.c_str());
		}

		if (currentSel >= 0)
			preference.setListSelection(static_cast<size_t>(currentSel));
	}

	void changeFindReplaceDlgLang(FindReplaceDlg& findReplaceDlg)
	{
		remember(&findReplaceDlg, "Find*", [dlg = &findReplaceDlg]() { changeFindReplaceDlgLang(*dlg); });
		const BlockRemember block;

		changeDlgLang(&findReplaceDlg, [&findReplaceDlg](int id) { return findReplaceDlg.dlgItem(id); }, "Find");

		// NPLX: after the dialog title, as the window shows the current tab's name
		if (const NativeLangSpeaker* speaker = nativeLangSpeaker())
		{
			NppParameters& nppParam = NppParameters::getInstance();
			auto& titles = nppParam.getFindDlgTabTitles();
			struct { const char* _attr; DIALOG_TYPE _tab; std::wstring* _title; } const tabs[] = {
				{ "titleFind", FIND_DLG, &titles._find },
				{ "titleReplace", REPLACE_DLG, &titles._replace },
				{ "titleFindInFiles", FINDINFILES_DLG, &titles._findInFiles },
				{ "titleFindInProjects", FINDINPROJECTS_DLG, &titles._findInProjects },
				{ "titleMark", MARK_DLG, &titles._mark },
			};
			for (const auto& tab : tabs)
			{
				DlgLang lang;
				if (speaker->getDlgLang("Find", lang, tab._attr) && !lang._title.empty())
				{
					*tab._title = lang._title;
					findReplaceDlg.changeTabName(tab._tab, tab._title->c_str());
				}
			}
		}
	}

	void changeConfigLang(WordStyleDlg& styleDlg)
	{
		remember(&styleDlg, "StyleConfig*", [dlg = &styleDlg]() { changeConfigLang(*dlg); });
		const BlockRemember block;

		const NativeLangSpeaker* speaker = nativeLangSpeaker();
		if (!speaker)
			return;

		const auto item = [&styleDlg](int id) { return styleDlg.dlgItem(id); };
		DlgLang lang;
		if (!speaker->getDlgLangByPath({ "StyleConfig" }, lang))
			return;
		applyDlgLang(&styleDlg, item, lang);

		// <SubDialog>: the style controls (Bold, Italic, Font name...)
		if (speaker->getDlgLangByPath({ "StyleConfig", "SubDialog" }, lang))
		{
			lang._title.clear();
			applyDlgLang(&styleDlg, item, lang);
		}
	}

	void changeUserDefineLang(UserDefineDialog& userDefineDlg)
	{
		remember(&userDefineDlg, "UserDefine*", [dlg = &userDefineDlg]() { changeUserDefineLang(*dlg); });
		const BlockRemember block;

		const NativeLangSpeaker* speaker = nativeLangSpeaker();
		if (!speaker)
			return;

		DlgLang lang;
		if (!speaker->getDlgLangByPath({ "UserDefine" }, lang))
			return;
		applyDlgLang(&userDefineDlg, [&userDefineDlg](int id) { return userDefineDlg.dlgItem(id); }, lang, nullptr, 31);

		// upstream: the Dock button reads "Undock" when docked
		if (userDefineDlg.isDocked())
		{
			for (const auto& [id, name] : lang._items)
			{
				if (id == IDC_UNDOCK_BUTTON)
					setControlText(userDefineDlg.dlgItem(IDC_DOCK_BUTTON), name);
			}
		}

		struct { const char* _node; RcDialog* _dlg; } const pages[] = {
			{ "Folder", userDefineDlg.folderStyleDlg() },
			{ "Keywords", userDefineDlg.keyWordsStyleDlg() },
			{ "Comment", userDefineDlg.commentStyleDlg() },
			{ "Operator", userDefineDlg.symbolsStyleDlg() },
		};
		for (int i = 0; i < static_cast<int>(std::size(pages)); ++i)
		{
			RcDialog* page = pages[i]._dlg;
			if (!page || !speaker->getDlgLangByPath({ "UserDefine", pages[i]._node }, lang))
				continue;
			if (!lang._title.empty() && userDefineDlg.tabBar())
				userDefineDlg.tabBar()->setTabText(i, QString::fromStdWString(lang._title));
			lang._title.clear(); // the tab name, not the page's window title
			applyDlgLang(page, [page](int id) { return page->dlgItem(id); }, lang);
		}
	}

	void changeUserDefineLangPopupDlg(StylerDlg& stylerDlg)
	{
		remember(&stylerDlg, "StylerDialog*", [dlg = &stylerDlg]() { changeUserDefineLangPopupDlg(*dlg); });
		const BlockRemember block;

		const NativeLangSpeaker* speaker = nativeLangSpeaker();
		DlgLang lang;
		if (speaker && speaker->getDlgLangByPath({ "UserDefine", "StylerDialog" }, lang))
			applyDlgLang(&stylerDlg, [&stylerDlg](int id) { return stylerDlg.dlgItem(id); }, lang);
	}
}
