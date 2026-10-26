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

#include <QAbstractButton>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>

#include "Parameters.h"
#include "RcDialog.h"
#include "localization.h"

namespace nplx
{
	const NativeLangSpeaker* nativeLangSpeaker()
	{
		return NppParameters::getInstance().getNativeLangSpeaker();
	}

	void setControlText(QWidget* w, const std::wstring& text)
	{
		const QString s = QString::fromStdWString(text);
		if (auto* l = qobject_cast<QLabel*>(w))
			l->setText(s);
		else if (auto* b = qobject_cast<QAbstractButton*>(w))
			b->setText(s);
		else if (auto* g = qobject_cast<QGroupBox*>(w))
			g->setTitle(s);
	}

	bool changeDlgLang(QWidget* dlg, const DlgItemLookup& item, const char* dlgTagName, const char* titleTag, std::wstring* titleOut)
	{
		if (titleOut)
			titleOut->clear();

		const NativeLangSpeaker* speaker = nativeLangSpeaker();
		DlgLang lang;
		if (!speaker || !speaker->getDlgLang(dlgTagName, lang, titleTag))
			return false;

		if (!lang._title.empty())
		{
			if (dlg)
				dlg->setWindowTitle(QString::fromStdWString(lang._title));
			if (titleOut)
				*titleOut = lang._title;
		}

		for (const auto& [id, name] : lang._items)
		{
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
}
