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

#include "NplxStyle.h"

#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QIconEngine>
#include <QImageReader>
#include <QPainter>
#include <QPixmapCache>
#include <QMenu>
#include <QProxyStyle>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QStyleHints>

#include "NppDarkMode.h"
#include "Parameters.h"

namespace nplx::style
{
namespace
{
	Tokens lightTokens()
	{
		Tokens t;
		t._dark = false;
		t._window = QColor(0xf6f7f9);
		t._base = QColor(0xffffff);
		t._alt = QColor(0xf1f3f5);
		t._text = QColor(0x1f2328);
		t._muted = QColor(0x59636e);
		t._border = QColor(0xd0d7de);
		t._borderSoft = QColor(0xe4e8ec);
		t._borderStrong = QColor(0xafb8c1);
		t._hover = QColor(0xe9edf1);
		t._pressed = QColor(0xdbe1e7);
		t._button = QColor(0xffffff);
		t._accent = QColor(0x2563eb);
		t._accentHover = QColor(0x1d4ed8);
		t._accentText = QColor(0xffffff);
		t._accentSoft = QColor(0xdde8fd);
		t._accentBorder = QColor(0x9dbcf7);
		t._danger = QColor(0xd1242f);
		t._dangerSoft = QColor(0xfde2e4);
		t._warn = QColor(0xe08a00);
		t._disabled = QColor(0xa3abb3);
		t._tooltip = QColor(0x24292f);
		t._tooltipText = QColor(0xffffff);
		t._scrollHandle = QColor(0xc4cbd2);
		t._scrollHandleHover = QColor(0x9aa4ae);
		t._menu = QColor(0xffffff);
		t._menuHover = QColor(0xedf0f3);
		t._menuBorder = QColor(0xd5dbe1);
		t._iconFg = QColor(0x3c4651);
		t._iconMuted = QColor(0x6e7781);
		t._iconAccent = QColor(0x2563eb);
		return t;
	}

	Tokens darkTokens()
	{
		Tokens t;
		t._dark = true;
		t._window = QColor(0x1e2126);
		t._base = QColor(0x16181c);
		t._alt = QColor(0x23272e);
		t._text = QColor(0xe6e8eb);
		t._muted = QColor(0x9aa4af);
		t._border = QColor(0x353b43);
		t._borderSoft = QColor(0x2a2f36);
		t._borderStrong = QColor(0x4b535d);
		t._hover = QColor(0x2b3038);
		t._pressed = QColor(0x353b44);
		t._button = QColor(0x262a31);
		t._accent = QColor(0x3b82f6);
		t._accentHover = QColor(0x5b97f8);
		t._accentText = QColor(0xffffff);
		t._accentSoft = QColor(0x1d3157);
		t._accentBorder = QColor(0x2f5aa8);
		t._danger = QColor(0xf2555a);
		t._dangerSoft = QColor(0x4a1f24);
		t._warn = QColor(0xf0b429);
		t._disabled = QColor(0x5c646e);
		t._tooltip = QColor(0x2d3239);
		t._tooltipText = QColor(0xe6e8eb);
		t._scrollHandle = QColor(0x3e454e);
		t._scrollHandleHover = QColor(0x5a626c);
		t._menu = QColor(0x272a30);
		t._menuHover = QColor(0x363a42);
		t._menuBorder = QColor(0x3d424b);
		t._iconFg = QColor(0xc9d1d9);
		t._iconMuted = QColor(0x8b949e);
		t._iconAccent = QColor(0x60a5fa);
		return t;
	}

	// upstream dark mode tones (Preferences > Dark Mode): the colours of NppDarkMode::Colors on the NotePadLX dark
	// palette; the black tone is the NotePadLX palette itself
	Tokens tonedDarkTokens()
	{
		Tokens t = darkTokens();
		const DarkModeConf& darkMode = NppParameters::getInstance().getNppGUI()._darkmode;
		if (darkMode._colorTone == NppDarkMode::blackTone)
			return t;
		const NppDarkMode::Colors c = (darkMode._colorTone == NppDarkMode::customizedTone)
			? darkMode._customColors : NppDarkMode::getDarkModeDefaultColors(darkMode._colorTone);
		auto q = [](COLORREF rgb) { return QColor(GetRValue(rgb), GetGValue(rgb), GetBValue(rgb)); };
		t._base = q(c.background);
		t._alt = q(c.softerBackground);
		t._button = q(c.softerBackground);
		t._hover = q(c.hotBackground);
		t._pressed = q(c.hotBackground).lighter(115);
		t._window = q(c.pureBackground);
		t._menu = q(c.softerBackground);
		t._menuHover = q(c.hotBackground);
		t._tooltip = q(c.softerBackground);
		t._text = q(c.text);
		t._tooltipText = q(c.text);
		t._iconFg = q(c.text);
		t._muted = q(c.darkerText);
		t._iconMuted = q(c.darkerText);
		t._disabled = q(c.disabledText);
		t._border = q(c.edge);
		t._borderSoft = q(c.disabledEdge);
		t._borderStrong = q(c.hotEdge);
		t._menuBorder = q(c.edge);
		t._dangerSoft = q(c.errorBackground);
		t._scrollHandle = q(c.edge);
		t._scrollHandleHover = q(c.hotEdge);
		return t;
	}

	Tokens g_tokens = lightTokens();
	quint64 g_generation = 0; // bumped on theme change: part of the icon cache keys

	// the toolbar icons (Preferences > Toolbar): upstream Fluent colorization and the "filled" sets
	struct ToolbarIconStyle
	{
		QColor _tint;       // invalid: the theme colours
		bool _mono = false; // the whole icon in the tint (upstream "Complete"), else only its accents ("Partial")
		bool _bold = false; // thicker strokes (upstream "Filled Fluent UI" sets)
	};
	ToolbarIconStyle g_toolbarStyle;

	enum class IconKind { Plain, Toolbar, TabAlternate };

	bool schemeIsDark()
	{
		const QByteArray forced = qgetenv("NPLX_THEME");
		if (forced == "dark")
			return true;
		if (forced == "light")
			return false;
		// upstream dark mode setting (config.xml DarkMode); "follow Windows" follows the desktop colour scheme
		return NppDarkMode::isEnabled();
	}

	// --- SVG templates

	const QByteArray& iconTemplate(const QString& name)
	{
		static QHash<QString, QByteArray> templates;
		auto it = templates.find(name);
		if (it == templates.end())
		{
			QFile f(QStringLiteral(":/nplx-icons/") + name + QStringLiteral(".svg"));
			it = templates.insert(name, f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray());
		}
		return *it;
	}

	QByteArray colourize(QByteArray svg, const Tokens& t, bool disabled, const QColor& fgOverride = QColor(),
		IconKind kind = IconKind::Plain)
	{
		auto hex = [](const QColor& c) { return c.name(QColor::HexRgb).toLatin1(); };
		QColor fg = fgOverride.isValid() ? fgOverride : t._iconFg;
		QColor muted = t._iconMuted;
		QColor accent = t._iconAccent;
		QColor warn = t._warn;
		if (kind == IconKind::Toolbar)
		{
			if (g_toolbarStyle._tint.isValid())
			{
				accent = g_toolbarStyle._tint;
				if (g_toolbarStyle._mono)
					fg = muted = g_toolbarStyle._tint;
			}
			if (g_toolbarStyle._bold)
				svg.replace("stroke-width=\"1.75\"", "stroke-width=\"2.25\"");
		}
		else if (kind == IconKind::TabAlternate)
		{
			// upstream alternate tab icons: coloured documents
			muted = t._iconAccent;
			warn = t._danger;
		}

		if (disabled)
		{
			const QByteArray d = hex(t._disabled);
			for (const char* token : { "@fg", "@muted", "@accent", "@danger", "@warn" })
				svg.replace(token, d);
		}
		else
		{
			svg.replace("@fg", hex(fg));
			svg.replace("@muted", hex(muted));
			svg.replace("@accent", hex(accent));
			svg.replace("@danger", hex(t._danger));
			svg.replace("@warn", hex(warn));
		}
		svg.replace("@bg", hex(t._base));
		return svg;
	}

	QImage renderSvg(const QByteArray& svg, const QSize& pixelSize)
	{
		// the Qt SVG image format plugin (qt6-svg-plugins) renders it: no link-time dependency on Qt Svg
		QByteArray data = svg;
		QBuffer buffer(&data);
		buffer.open(QIODevice::ReadOnly);
		QImageReader reader(&buffer, "svg");
		reader.setScaledSize(pixelSize);
		return reader.read();
	}

	class NplxIconEngine : public QIconEngine
	{
	public:
		explicit NplxIconEngine(const QString& name, IconKind kind = IconKind::Plain) : _name(name), _kind(kind) {}

		QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
		{
			return scaledPixmap(size, mode, state, 1.0);
		}

		QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State /*state*/, qreal scale) override
		{
			const QSize pixelSize = size * scale;
			const bool disabled = (mode == QIcon::Disabled);
			const QString key = QStringLiteral("nplx:%1:%2:%3x%4:%5:%6").arg(_name).arg(int(disabled))
				.arg(pixelSize.width()).arg(pixelSize.height()).arg(g_generation).arg(int(_kind));
			QPixmap pm;
			if (!QPixmapCache::find(key, &pm))
			{
				const QImage img = renderSvg(colourize(iconTemplate(_name), g_tokens, disabled, QColor(), _kind), pixelSize);
				pm = QPixmap::fromImage(img);
				QPixmapCache::insert(key, pm);
			}
			pm.setDevicePixelRatio(scale);
			return pm;
		}

		void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override
		{
			const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
			painter->drawPixmap(rect, scaledPixmap(rect.size(), mode, state, dpr));
		}

		QSize actualSize(const QSize& size, QIcon::Mode, QIcon::State) override { return size; }
		QList<QSize> availableSizes(QIcon::Mode, QIcon::State) override
		{
			return { QSize(16, 16), QSize(18, 18), QSize(22, 22), QSize(24, 24), QSize(32, 32), QSize(48, 48) };
		}
		bool isNull() override { return iconTemplate(_name).isEmpty(); }
		QString key() const override { return QStringLiteral("nplx"); }
		QIconEngine* clone() const override { return new NplxIconEngine(_name, _kind); }

	private:
		QString _name;
		IconKind _kind = IconKind::Plain;
	};

	// --- stylesheet images: the templates written, coloured, to the cache folder (QSS needs file names)

	QString assetPath(const QString& name, const QColor& fgOverride = QColor(), bool disabled = false)
	{
		const QByteArray svg = colourize(iconTemplate(name), g_tokens, disabled, fgOverride);
		QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
		if (dir.isEmpty())
			dir = QDir::tempPath() + QStringLiteral("/notepadlx");
		dir += QStringLiteral("/ui");
		QDir().mkpath(dir);
		const QString path = dir + QLatin1Char('/') + name + QLatin1Char('-')
			+ QString::number(qHash(svg), 16) + QStringLiteral(".svg");
		if (!QFile::exists(path))
		{
			QSaveFile f(path);
			if (f.open(QIODevice::WriteOnly))
			{
				f.write(svg);
				f.commit();
			}
		}
		return path;
	}

	// --- application style

	class NplxProxyStyle : public QProxyStyle
	{
	public:
		NplxProxyStyle() : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))) {}

		int styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget, QStyleHintReturn* returnData) const override
		{
			switch (hint)
			{
				case SH_DialogButtonBox_ButtonsHaveIcons:
					return 0; // no stock icons (green check, red cross...) on OK / Cancel
				case SH_MessageBox_CenterButtons:
					return 0;
				case SH_Menu_Scrollable:
					return 1; // a menu taller than the screen scrolls instead of splitting into columns
				default:
					return QProxyStyle::styleHint(hint, option, widget, returnData);
			}
		}

		int pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget) const override
		{
			switch (metric)
			{
				case PM_MessageBoxIconSize:
					return 40;
				case PM_ToolBarIconSize:
					return 24;
				case PM_SmallIconSize:
					// the menu actions shared with the toolbar carry icons that menus do not show
					// (setIconVisibleInMenu(false)), but QMenu still reserves their column: keep a column only for the
					// check marks of the menus that have checkable items
					if (const auto* menu = qobject_cast<const QMenu*>(widget))
					{
						for (const QAction* action : menu->actions())
							if (action->isCheckable() && action->isVisible())
								return 16;
						return 0;
					}
					return QProxyStyle::pixelMetric(metric, option, widget);
				default:
					return QProxyStyle::pixelMetric(metric, option, widget);
			}
		}

		QIcon standardIcon(StandardPixmap sp, const QStyleOption* option, const QWidget* widget) const override
		{
			switch (sp)
			{
				case SP_MessageBoxInformation: return icon(QStringLiteral("msg_info"));
				case SP_MessageBoxWarning:     return icon(QStringLiteral("msg_warning"));
				case SP_MessageBoxCritical:    return icon(QStringLiteral("msg_error"));
				case SP_MessageBoxQuestion:    return icon(QStringLiteral("msg_question"));
				case SP_DialogOkButton:
				case SP_DialogCancelButton:
				case SP_DialogHelpButton:
				case SP_DialogOpenButton:
				case SP_DialogSaveButton:
				case SP_DialogCloseButton:
				case SP_DialogApplyButton:
				case SP_DialogResetButton:
				case SP_DialogDiscardButton:
				case SP_DialogYesButton:
				case SP_DialogNoButton:
				case SP_DialogYesToAllButton:
				case SP_DialogNoToAllButton:
				case SP_DialogSaveAllButton:
				case SP_DialogAbortButton:
				case SP_DialogRetryButton:
				case SP_DialogIgnoreButton:
					return QIcon();
				case SP_TitleBarCloseButton:
				case SP_DockWidgetCloseButton:
					return icon(QStringLiteral("tab_close"));
				case SP_TitleBarNormalButton:
					return icon(QStringLiteral("restore"));
				default:
					return QProxyStyle::standardIcon(sp, option, widget);
			}
		}
	};

	QPalette paletteFor(const Tokens& t)
	{
		QPalette p;
		p.setColor(QPalette::Window, t._window);
		p.setColor(QPalette::WindowText, t._text);
		p.setColor(QPalette::Base, t._base);
		p.setColor(QPalette::AlternateBase, t._alt);
		p.setColor(QPalette::Text, t._text);
		p.setColor(QPalette::Button, t._button);
		p.setColor(QPalette::ButtonText, t._text);
		p.setColor(QPalette::BrightText, t._accentText);
		p.setColor(QPalette::Highlight, t._accent);
		p.setColor(QPalette::HighlightedText, t._accentText);
		p.setColor(QPalette::ToolTipBase, t._tooltip);
		p.setColor(QPalette::ToolTipText, t._tooltipText);
		p.setColor(QPalette::PlaceholderText, t._muted);
		p.setColor(QPalette::Link, t._accent);
		p.setColor(QPalette::LinkVisited, t._accentHover);
		p.setColor(QPalette::Light, t._dark ? t._borderStrong : t._base);
		p.setColor(QPalette::Midlight, t._borderSoft);
		p.setColor(QPalette::Mid, t._border);
		p.setColor(QPalette::Dark, t._borderStrong);
		p.setColor(QPalette::Shadow, t._dark ? QColor(Qt::black) : QColor(0x8c959f));
		for (QPalette::ColorRole role : { QPalette::WindowText, QPalette::Text, QPalette::ButtonText })
			p.setColor(QPalette::Disabled, role, t._disabled);
		p.setColor(QPalette::Disabled, QPalette::Base, t._window);
		p.setColor(QPalette::Disabled, QPalette::Highlight, t._border);
		return p;
	}

	void applyTheme(QApplication& app)
	{
		g_tokens = schemeIsDark() ? tonedDarkTokens() : lightTokens();
		++g_generation;
		QApplication::setPalette(paletteFor(g_tokens));
		app.setStyleSheet(styleSheet(g_tokens));
	}
}

const Tokens& tokens()
{
	return g_tokens;
}

bool isDark()
{
	return g_tokens._dark;
}

QIcon icon(const QString& name)
{
	return QIcon(new NplxIconEngine(name));
}

QIcon toolbarIcon(const QString& name)
{
	return QIcon(new NplxIconEngine(name, IconKind::Toolbar));
}

QIcon tabIcon(const QString& name, bool alternate)
{
	return QIcon(new NplxIconEngine(name, alternate ? IconKind::TabAlternate : IconKind::Plain));
}

void setToolbarIconStyle(const QColor& tint, bool mono, bool bold)
{
	g_toolbarStyle = ToolbarIconStyle{ tint, mono, bold };
	++g_generation; // the icons are drawn again
}

void initDarkMode()
{
	// upstream NppDarkMode::initDarkMode: the DarkMode setting of config.xml; NPLX: "enableWindowsMode" follows the
	// desktop colour scheme
	const DarkModeConf& darkMode = NppParameters::getInstance().getNppGUI()._darkmode;
	const bool systemIsDark = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
	NppDarkMode::setEnabled(darkMode._advOptions._enableWindowsMode ? systemIsDark : darkMode._isEnabled);
}

void apply(QApplication& app)
{
	QApplication::setStyle(new NplxProxyStyle);
	applyTheme(app);
	QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, &app, [&app]() {
		initDarkMode();
		applyTheme(app);
	});
}

void refresh()
{
	if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance()))
		applyTheme(*app);
}

QString styleSheet(const Tokens& t)
{
	QString qss = QStringLiteral(R"(
QMainWindow::separator { background: {border}; width: 1px; height: 1px; }

QToolBar { background: {window}; border: none; border-bottom: 1px solid {border}; padding: 3px 6px; spacing: 1px; }
QToolBar::separator { background: {border}; width: 1px; margin: 7px 4px; }
QToolBar QToolButton { background: transparent; border: 1px solid transparent; border-radius: 6px; padding: 3px; }
QToolBar QToolButton#qt_toolbar_ext_button { qproperty-icon: url({chevron_right_fg}); min-width: 18px; }
QToolBar QToolButton:hover { background: {hover}; }
QToolBar QToolButton:pressed { background: {pressed}; }
QToolBar QToolButton:checked { background: {accentSoft}; border-color: {accentBorder}; }

QMenuBar { background: {window}; padding: 3px 6px; spacing: 2px; }
QMenuBar::item { padding: 5px 10px; border-radius: 6px; background: transparent; }
QMenuBar::item:selected { background: {hover}; }
QMenuBar::item:pressed { background: {menuHover}; }
QMenu { background: {menu}; border: 1px solid {menuBorder}; border-radius: 10px; padding: 6px; }
QMenu::item { padding: 6px 22px 6px 12px; border-radius: 6px; color: {text}; background: transparent; }
QMenu::item:selected { background: {menuHover}; color: {text}; }
QMenu::item:disabled { color: {disabled}; background: transparent; }
QMenu::separator { height: 1px; background: {menuBorder}; margin: 5px 6px; }
QMenu::indicator { width: 14px; height: 14px; left: 10px; }
QMenu::indicator:non-exclusive:checked { image: url({menu_check}); }
QMenu::indicator:non-exclusive:checked:selected { image: url({menu_check}); }
QMenu::indicator:exclusive:checked { image: url({menu_dot}); }
QMenu::indicator:exclusive:checked:selected { image: url({menu_dot}); }
QMenu::right-arrow { image: url({chevron_right}); width: 12px; height: 12px; right: 10px; }
QMenu::right-arrow:selected { image: url({chevron_right_fg}); }

QPushButton { background: {button}; color: {text}; border: 1px solid {border}; border-radius: 6px; padding: 3px 14px; min-height: 18px; }
QPushButton:hover { background: {hover}; border-color: {borderStrong}; }
QPushButton:pressed { background: {pressed}; }
QPushButton:checked { background: {accentSoft}; border-color: {accent}; }
QPushButton:focus { border-color: {accent}; }
QPushButton:default { background: {accent}; color: {accentText}; border-color: {accent}; }
QPushButton:default:hover { background: {accentHover}; border-color: {accentHover}; }
QPushButton:disabled { color: {disabled}; background: {window}; border-color: {borderSoft}; }
QPushButton[compact="true"] { padding: 0px 2px; min-width: 0px; }
QPushButton::menu-indicator { image: url({chevron_down}); width: 10px; subcontrol-position: right center; right: 6px; }

QToolButton { border-radius: 5px; }

QMessageBox QLabel#qt_msgbox_label { min-width: 260px; padding: 4px 12px 10px 2px; }
QMessageBox QLabel#qt_msgbox_informativelabel { color: {muted}; padding: 0 6px 8px 2px; }

QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {
	background: {base}; color: {text}; border: 1px solid {border}; border-radius: 5px; padding: 2px 6px;
	selection-background-color: {accent}; selection-color: {accentText};
}
QPlainTextEdit, QTextEdit, QListView, QTreeView, QTableView {
	background: {base}; color: {text}; border: 1px solid {border}; border-radius: 5px;
	selection-background-color: {accent}; selection-color: {accentText};
}
QLineEdit:hover, QSpinBox:hover, QComboBox:hover, QPlainTextEdit:hover, QTextEdit:hover { border-color: {borderStrong}; }
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus, QPlainTextEdit:focus, QTextEdit:focus,
QListView:focus, QTreeView:focus, QTableView:focus { border-color: {accent}; }
QLineEdit:disabled, QSpinBox:disabled, QComboBox:disabled, QPlainTextEdit:disabled, QTextEdit:disabled,
QListView:disabled, QTreeView:disabled { color: {disabled}; background: {window}; border-color: {borderSoft}; }
QLineEdit[readOnly="true"] { background: {alt}; }
QListView::item, QTreeView::item { padding: 2px; }
QListView::item:hover, QTreeView::item:hover { background: {hover}; }
QListView::item:selected, QTreeView::item:selected { background: {accent}; color: {accentText}; }

QComboBox::drop-down { border: none; width: 20px; subcontrol-origin: padding; subcontrol-position: center right; }
QComboBox::down-arrow { image: url({chevron_down}); width: 12px; height: 12px; }
QComboBox::down-arrow:disabled { image: url({chevron_down_disabled}); }
QComboBox QAbstractItemView { background: {base}; border: 1px solid {border}; border-radius: 6px; padding: 3px; outline: 0;
	selection-background-color: {accent}; selection-color: {accentText}; }
QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { border: none; width: 16px; background: transparent; }
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: url({chevron_up}); width: 10px; height: 10px; }
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: url({chevron_down}); width: 10px; height: 10px; }

QCheckBox, QRadioButton { spacing: 7px; }
QCheckBox::indicator, QRadioButton::indicator { width: 15px; height: 15px; }
QCheckBox::indicator { border: 1px solid {borderStrong}; border-radius: 4px; background: {base}; }
QCheckBox::indicator:hover { border-color: {accent}; }
QCheckBox::indicator:checked { background: {accent}; border-color: {accent}; image: url({check}); }
QCheckBox::indicator:indeterminate { background: {accent}; border-color: {accent}; image: url({dash}); }
QCheckBox::indicator:disabled { background: {window}; border-color: {borderSoft}; }
QCheckBox::indicator:checked:disabled { background: {disabled}; border-color: {disabled}; }
QRadioButton::indicator { border: 1px solid {borderStrong}; border-radius: 8px; background: {base}; }
QRadioButton::indicator:hover { border-color: {accent}; }
QRadioButton::indicator:checked { border-color: {accent};
	background: qradialgradient(cx:0.5, cy:0.5, radius:0.5, fx:0.5, fy:0.5, stop:0 {accentText}, stop:0.38 {accentText}, stop:0.48 {accent}, stop:1 {accent}); }
QRadioButton::indicator:disabled { background: {window}; border-color: {borderSoft}; }
QRadioButton::indicator:checked:disabled { border-color: {disabled};
	background: qradialgradient(cx:0.5, cy:0.5, radius:0.5, fx:0.5, fy:0.5, stop:0 {window}, stop:0.38 {window}, stop:0.48 {disabled}, stop:1 {disabled}); }

QGroupBox { border: 1px solid {border}; border-radius: 8px; margin-top: 8px; padding-top: 4px; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: {muted}; }

QTabBar { background: transparent; }
QTabBar::tab { background: transparent; color: {muted}; padding: 6px 12px; border: none; border-bottom: 2px solid transparent; }
QTabBar::tab:hover { background: {hover}; color: {text}; }
QTabBar::tab:selected { background: {base}; color: {text}; border-bottom: 2px solid {accent}; }
QTabBar[nplxReduced="false"]::tab { padding: 9px 14px; }
QTabBar[nplxDrawInactive="true"]::tab:!selected { background: {alt}; }
QTabBar[nplxDrawInactive="true"]::tab:!selected:hover { background: {hover}; }
QTabBar[nplxTopBar="false"]::tab:selected { border-bottom-color: transparent; }
QTabBar::tab:left { border-bottom: none; border-right: 2px solid transparent; }
QTabBar::tab:left:selected { border-right: 2px solid {accent}; }
QTabBar[nplxTopBar="false"]::tab:left:selected { border-right-color: transparent; }
QTabBar::tab:disabled { color: {disabled}; }
QTabBar::close-button { image: url({tab_close}); subcontrol-position: right; border-radius: 4px; margin: 2px; }
QTabBar::close-button:hover { image: url({tab_close_hover}); background: {dangerSoft}; }
QTabBar QToolButton#nplxPinButton { border: none; border-radius: 4px; margin: 2px; padding: 0px; background: transparent; }
QTabBar QToolButton#nplxPinButton:hover { background: {hover}; }
QTabBar QToolButton { background: {window}; border: 1px solid {border}; border-radius: 4px; }
QTabWidget::pane { border: 1px solid {border}; border-radius: 6px; top: -1px; background: {window}; }

QScrollBar:vertical { background: {window}; width: 12px; margin: 0; border: none; }
QScrollBar:horizontal { background: {window}; height: 12px; margin: 0; border: none; }
QScrollBar::handle:vertical { background: {scrollHandle}; min-height: 28px; border-radius: 3px; margin: 2px 3px; }
QScrollBar::handle:horizontal { background: {scrollHandle}; min-width: 28px; border-radius: 3px; margin: 3px 2px; }
QScrollBar::handle:hover { background: {scrollHandleHover}; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; border: none; background: none; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }
QAbstractScrollArea::corner { background: {window}; border: none; }

QSlider::groove:horizontal { height: 4px; background: {border}; border-radius: 2px; }
QSlider::sub-page:horizontal { background: {accent}; border-radius: 2px; }
QSlider::handle:horizontal { background: {base}; border: 2px solid {accent}; width: 10px; height: 10px; margin: -5px 0; border-radius: 7px; }
QSlider::handle:horizontal:disabled { border-color: {disabled}; }
QSlider::sub-page:horizontal:disabled { background: {disabled}; }

QProgressBar { background: {base}; border: 1px solid {border}; border-radius: 5px; text-align: center; color: {text}; }
QProgressBar::chunk { background: {accent}; border-radius: 4px; }

QHeaderView::section { background: {window}; color: {muted}; border: none; border-bottom: 1px solid {border}; border-right: 1px solid {borderSoft}; padding: 4px 6px; }

QSplitter::handle { background: {border}; }
QSplitter::handle:hover { background: {accent}; }

QStatusBar { background: {window}; border-top: 1px solid {border}; color: {muted}; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: {muted}; padding: 2px 6px; border-left: 1px solid {borderSoft}; }

QDockWidget { titlebar-close-icon: url({tab_close}); titlebar-normal-icon: url({restore}); font-weight: 600; }
QDockWidget::title { background: {window}; padding: 5px 8px; border-bottom: 1px solid {border}; text-align: left; }
QDockWidget::close-button, QDockWidget::float-button { border: none; border-radius: 4px; padding: 3px; background: transparent; icon-size: 14px; }
QDockWidget::close-button:hover, QDockWidget::float-button:hover { background: {hover}; }

QToolTip { background: {tooltip}; color: {tooltipText}; border: 1px solid {border}; border-radius: 5px; padding: 4px 7px; }
)");

	const std::pair<const char*, QColor> colours[] = {
		{ "window", t._window }, { "base", t._base }, { "alt", t._alt }, { "text", t._text }, { "muted", t._muted },
		{ "borderSoft", t._borderSoft }, { "borderStrong", t._borderStrong }, { "border", t._border },
		{ "hover", t._hover }, { "pressed", t._pressed }, { "button", t._button },
		{ "accentHover", t._accentHover }, { "accentText", t._accentText }, { "accentSoft", t._accentSoft },
		{ "accentBorder", t._accentBorder }, { "accent", t._accent },
		{ "dangerSoft", t._dangerSoft }, { "danger", t._danger }, { "warn", t._warn }, { "disabled", t._disabled },
		{ "tooltipText", t._tooltipText }, { "tooltip", t._tooltip },
		{ "scrollHandleHover", t._scrollHandleHover }, { "scrollHandle", t._scrollHandle },
		{ "menuHover", t._menuHover }, { "menuBorder", t._menuBorder }, { "menu", t._menu },
	};
	// images first (their names contain colour names, e.g. "menu_check_selected")
	const std::pair<const char*, QString> images[] = {
		{ "{menu_check_selected}", assetPath(QStringLiteral("menu_check"), t._accentText) },
		{ "{menu_check}", assetPath(QStringLiteral("menu_check"), t._accent) },
		{ "{menu_dot_selected}", assetPath(QStringLiteral("menu_dot"), t._accentText) },
		{ "{menu_dot}", assetPath(QStringLiteral("menu_dot"), t._accent) },
		{ "{chevron_right_selected}", assetPath(QStringLiteral("chevron_right"), t._accentText) },
		{ "{chevron_right_fg}", assetPath(QStringLiteral("chevron_right"), t._text) },
		{ "{chevron_right}", assetPath(QStringLiteral("chevron_right"), t._muted) },
		{ "{chevron_down_disabled}", assetPath(QStringLiteral("chevron_down"), QColor(), true) },
		{ "{chevron_down}", assetPath(QStringLiteral("chevron_down"), t._muted) },
		{ "{chevron_up}", assetPath(QStringLiteral("chevron_up"), t._muted) },
		{ "{check}", assetPath(QStringLiteral("check")) },
		{ "{dash}", assetPath(QStringLiteral("dash")) },
		{ "{tab_close_hover}", assetPath(QStringLiteral("tab_close"), t._danger) },
		{ "{tab_close}", assetPath(QStringLiteral("tab_close"), t._muted) },
		{ "{restore}", assetPath(QStringLiteral("restore"), t._muted) },
	};
	for (const auto& [token, path] : images)
		qss.replace(QLatin1String(token), path);
	for (const auto& [name, colour] : colours)
		qss.replace(QLatin1Char('{') + QLatin1String(name) + QLatin1Char('}'), colour.name(QColor::HexRgb));
	return qss;
}
}
