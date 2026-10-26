#include "theme.h"

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QStyleFactory>
#include <QStyleHints>
#include <utility>

namespace {

struct Colors {
    const char *window, *header, *sidebar, *surface, *field;
    const char *text, *muted, *disabledText, *divider, *controlBorder;
    const char *button, *buttonHover, *buttonPressed, *buttonDisabled;
    const char *accent, *accentHover, *accentPressed, *onAccent, *focus;
    const char *selected, *onSelected;
};

constexpr Colors dark{
    "#18191C", "#222327", "#202125", "#26272C", "#1C1D21",
    "#F3F4F6", "#B9BDC6", "#858A94", "#3B3D44", "#777C87",
    "#303238", "#3D4048", "#27292F", "#25262B",
    "#FF785A", "#FF947D", "#EA6247", "#211A18", "#FFB39E",
    "#48342F", "#FFF5F1"
};

constexpr Colors light{
    "#F5F7FA", "#FFFFFF", "#FFFFFF", "#F0F2F5", "#FFFFFF",
    "#23262D", "#505661", "#727782", "#D2D6DD", "#7A828F",
    "#F1F3F6", "#E4E7ED", "#D6DBE3", "#ECEEF2",
    "#BD4025", "#A6341C", "#8F2D17", "#FFFFFF", "#A9351D",
    "#FFE7DF", "#542313"
};

void setColors(QApplication &app, const Colors &c)
{
    const auto color = [](const char *hex) { return QColor(QString::fromLatin1(hex)); };
    QPalette palette = app.palette();
    palette.setColor(QPalette::All, QPalette::Window, color(c.window));
    palette.setColor(QPalette::All, QPalette::WindowText, color(c.text));
    palette.setColor(QPalette::All, QPalette::Base, color(c.field));
    palette.setColor(QPalette::All, QPalette::AlternateBase, color(c.surface));
    palette.setColor(QPalette::All, QPalette::Text, color(c.text));
    palette.setColor(QPalette::All, QPalette::Button, color(c.button));
    palette.setColor(QPalette::All, QPalette::ButtonText, color(c.text));
    palette.setColor(QPalette::All, QPalette::Highlight, color(c.accent));
    palette.setColor(QPalette::All, QPalette::HighlightedText, color(c.onAccent));
    palette.setColor(QPalette::All, QPalette::ToolTipBase, color(c.surface));
    palette.setColor(QPalette::All, QPalette::ToolTipText, color(c.text));
    palette.setColor(QPalette::All, QPalette::PlaceholderText, color(c.muted));
    palette.setColor(QPalette::All, QPalette::Link, color(c.accent));
    palette.setColor(QPalette::Disabled, QPalette::Text, color(c.disabledText));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, color(c.disabledText));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, color(c.disabledText));
    app.setPalette(palette);

    QString css = QStringLiteral(R"QSS(
        QMainWindow { background: @window@; }
        QWidget#appHeader { background: @header@; border-bottom: 1px solid @divider@; }
        QWidget#workspace, QWidget#viewerPanel, QWidget#timelinePanel { background: @window@; }
        QWidget#sidebar, QWidget#mediaPanel, QWidget#inspectorPanel { background: @sidebar@; }
        QWidget#panelHeader, QWidget#viewerToolbar, QWidget#timelineToolbar {
            background: @sidebar@; border-bottom: 1px solid @divider@;
        }
        QWidget#panelFooter { background: @sidebar@; border-top: 1px solid @divider@; }
        QLabel#brandTitle, QLabel#projectTitle, QLabel#panelTitle,
        QLabel#sectionTitle, QLabel#selectedClipName { color: @text@; font-weight: 600; }
        QLabel#brandSubtitle, QLabel#dropHint, QLabel#saveState, QLabel#format,
        QLabel#panelHint, QLabel#selectedClipMeta, QLabel#timelineHint { color: @muted@; }
        QScrollArea { border: none; background: transparent; }
        QScrollArea > QWidget > QWidget { background: @sidebar@; }

        QGroupBox { background: transparent; border: none;
                    border-top: 1px solid @divider@; border-radius: 0;
                    margin-top: 18px; padding: 12px 0 4px; font-weight: 600; color: @text@; }
        QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left;
                           left: 0; padding: 0 8px 0 0; color: @text@; }

        QPushButton { background: @button@; color: @text@; border: 2px solid @controlBorder@;
                      border-radius: 5px; min-height: 28px; padding: 2px 10px; font-weight: 500; }
        QPushButton:hover { background: @buttonHover@; }
        QPushButton:pressed { background: @buttonPressed@; }
        QPushButton:focus { border-color: @focus@; }
        QPushButton:disabled { background: @buttonDisabled@; border-color: @divider@;
                               color: @disabledText@; }
        QPushButton#primaryButton { background: @accent@; color: @onAccent@;
                                    border-color: @accent@; font-weight: 600; }
        QPushButton#primaryButton:hover { background: @accentHover@; border-color: @accentHover@; }
        QPushButton#primaryButton:pressed { background: @accentPressed@; border-color: @accentPressed@; }
        QPushButton#primaryButton:focus { border-color: @focus@; }
        QPushButton#primaryButton:disabled { background: @buttonDisabled@; border-color: @divider@;
                                            color: @disabledText@; }
        QPushButton#secondaryButton { background: @surface@; }
        QPushButton#secondaryButton:hover { background: @buttonHover@; }
        QPushButton#quietButton { background: transparent; border-color: transparent; color: @muted@; }
        QPushButton#quietButton:hover { background: @buttonHover@; color: @text@; }
        QPushButton#quietButton:pressed { background: @buttonPressed@; }
        QPushButton#quietButton:checked { background: @selected@; color: @onSelected@; border-color: @accent@; }
        QPushButton#quietButton:focus { border-color: @focus@; }
        QPushButton#quietButton:disabled { color: @disabledText@; background: transparent; border-color: transparent; }

        QLineEdit, QDoubleSpinBox { background: @field@; color: @text@;
                                    border: 2px solid @controlBorder@; border-radius: 5px;
                                    min-height: 28px; padding: 1px 7px;
                                    selection-background-color: @accent@;
                                    selection-color: @onAccent@; }
        QLineEdit:focus, QDoubleSpinBox:focus { border-color: @focus@; }
        QLineEdit:read-only { background: @buttonDisabled@; color: @muted@; }
        QLineEdit:disabled, QDoubleSpinBox:disabled { background: @buttonDisabled@;
                                                      border-color: @divider@;
                                                      color: @disabledText@; }

        QListWidget { background: @sidebar@; color: @text@; border: 2px solid transparent;
                      border-radius: 0; padding: 6px; outline: 0; }
        QListWidget:focus { border-color: @focus@; }
        QListWidget[dropActive="true"] { border: 2px dashed @focus@; background: @selected@; }
        QListWidget::item { padding: 10px 8px; margin-bottom: 4px; border: 1px solid transparent; border-radius: 5px; }
        QListWidget::item:hover { background: @buttonHover@; }
        QListWidget::item:selected { background: @selected@; color: @onSelected@; border-color: @accent@; }

        QTabWidget::pane { border: none; background: @sidebar@; }
        QTabWidget::tab-bar { alignment: left; }
        QTabBar { background: @sidebar@; }
        QTabBar::tab { background: transparent; color: @muted@;
                      border: 2px solid transparent; min-height: 26px; padding: 5px 8px; }
        QTabBar::tab:hover { background: @buttonHover@; color: @text@; }
        QTabBar::tab:selected { color: @text@; border-bottom-color: @accent@; font-weight: 600; }
        QTabBar::tab:focus { border-color: @focus@; }
        QTabBar::tab:disabled { color: @disabledText@; }
        QSplitter::handle { background: @divider@; }
        QSplitter::handle:hover, QSplitter::handle:pressed { background: @controlBorder@; }

        QLabel#preview { background: #0D0E10; color: #F3F4F6; border: none; border-radius: 0; }
        QSlider { border: 2px solid transparent; border-radius: 4px; }
        QSlider:focus { border-color: @focus@; }
        QSlider::groove:horizontal { height: 4px; background: @controlBorder@; border-radius: 2px; }
        QSlider::sub-page:horizontal { background: @accent@; border-radius: 2px; }
        QSlider::handle:horizontal { background: @accent@; border: 2px solid @onAccent@;
                                     width: 14px; height: 14px; margin: -7px 0; border-radius: 8px; }
        QSlider:disabled { color: @disabledText@; }
        QScrollBar:vertical { background: @sidebar@; width: 12px; margin: 0; }
        QScrollBar:horizontal { background: @sidebar@; height: 12px; margin: 0; }
        QScrollBar::handle:vertical { background: @controlBorder@; min-height: 28px; border: 3px solid @sidebar@; border-radius: 5px; }
        QScrollBar::handle:horizontal { background: @controlBorder@; min-width: 28px; border: 3px solid @sidebar@; border-radius: 5px; }
        QScrollBar::handle:hover { background: @muted@; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
        QProgressBar { border: 1px solid @controlBorder@; border-radius: 4px;
                       background: @field@; text-align: center; color: @text@; }
        QProgressBar::chunk { background: @accent@; border-radius: 3px; }
        QMenuBar { background: @header@; color: @text@; }
        QMenuBar::item { padding: 6px 10px; }
        QMenuBar::item:selected { background: @buttonHover@; }
        QMenu { background: @surface@; color: @text@; border: 1px solid @controlBorder@; padding: 5px; }
        QMenu::item { padding: 6px 24px; }
        QMenu::item:selected { background: @selected@; color: @onSelected@; }
        QStatusBar { background: @header@; color: @muted@; border-top: 1px solid @divider@; }
        QToolTip { background: @surface@; color: @text@; border: 1px solid @controlBorder@; padding: 5px; }
    )QSS");

    const std::pair<const char *, const char *> tokens[] = {
        {"@window@", c.window}, {"@header@", c.header}, {"@sidebar@", c.sidebar},
        {"@surface@", c.surface}, {"@field@", c.field}, {"@text@", c.text},
        {"@muted@", c.muted}, {"@disabledText@", c.disabledText},
        {"@divider@", c.divider}, {"@controlBorder@", c.controlBorder},
        {"@button@", c.button}, {"@buttonHover@", c.buttonHover},
        {"@buttonPressed@", c.buttonPressed}, {"@buttonDisabled@", c.buttonDisabled},
        {"@accent@", c.accent}, {"@accentHover@", c.accentHover},
        {"@accentPressed@", c.accentPressed}, {"@onAccent@", c.onAccent},
        {"@focus@", c.focus}, {"@selected@", c.selected},
        {"@onSelected@", c.onSelected}
    };
    for (const auto &[name, value] : tokens)
        css.replace(QString::fromLatin1(name), QString::fromLatin1(value));
    app.setStyleSheet(css);
}

} // namespace

void applyAppTheme(QApplication &app)
{
    const bool fallbackDark = app.palette().color(QPalette::Window).lightness() < 128;
    app.setStyle(QStyleFactory::create("Fusion"));
    const auto refresh = [&app, fallbackDark](Qt::ColorScheme scheme) {
        const bool useDark = scheme == Qt::ColorScheme::Dark ||
                             (scheme == Qt::ColorScheme::Unknown && fallbackDark);
        setColors(app, useDark ? dark : light);
    };
    refresh(app.styleHints()->colorScheme());
    QObject::connect(app.styleHints(), &QStyleHints::colorSchemeChanged,
                     &app, refresh);
}

void setAppColorScheme(QApplication &app, Qt::ColorScheme scheme) {
    const bool useDark = scheme == Qt::ColorScheme::Dark ||
        (scheme == Qt::ColorScheme::Unknown && app.palette().color(QPalette::Window).lightness() < 128);
    setColors(app,useDark ? dark : light);
}
