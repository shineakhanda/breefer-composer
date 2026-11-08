#include "ui/Theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

namespace bc::theme {

void apply(QApplication& app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette pal;
    pal.setColor(QPalette::Window, panelBg());
    pal.setColor(QPalette::WindowText, text());
    pal.setColor(QPalette::Base, QColor(0x16, 0x16, 0x16));
    pal.setColor(QPalette::AlternateBase, panelAlt());
    pal.setColor(QPalette::Text, text());
    pal.setColor(QPalette::Button, panelAlt());
    pal.setColor(QPalette::ButtonText, text());
    pal.setColor(QPalette::Highlight, selection());
    pal.setColor(QPalette::HighlightedText, Qt::white);
    pal.setColor(QPalette::ToolTipBase, QColor(0x2b, 0x2b, 0x2b));
    pal.setColor(QPalette::ToolTipText, text());
    pal.setColor(QPalette::PlaceholderText, textFaint());
    pal.setColor(QPalette::Link, hotText());
    pal.setColor(QPalette::Disabled, QPalette::Text, textFaint());
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, textFaint());
    pal.setColor(QPalette::Disabled, QPalette::WindowText, textFaint());
    app.setPalette(pal);

    QFont font(QStringLiteral("Segoe UI"));
    font.setPointSizeF(8.5);
    app.setFont(font);

    app.setStyleSheet(QStringLiteral(R"(
QMainWindow { background: #101010; }
QDialog { background: #1d1d1d; }
QWidget { color: #e3e3e3; }

/* ---- panel frames (Premiere: rounded panels on a black gutter) -------- */
QFrame#PanelFrame { background: #1d1d1d; border: 1px solid #1d1d1d; border-radius: 4px; }
QFrame#PanelFrame[active="true"] { border: 1px solid #378ef0; }
QMainWindow::separator { background: #101010; width: 4px; height: 4px; }
QMainWindow::separator:hover { background: #2a2a2a; }
QToolBar#HeaderToolBar { background: #101010; border: none; spacing: 0; padding: 0; margin: 0; }
QWidget#DockTitle { background: #1d1d1d; border-top-left-radius: 4px; border-top-right-radius: 4px; }
QWidget#DockTitle[compact="true"] { background: #2a2a2a; border-radius: 3px; }
QDockWidget { color: #f0f0f0; }
QMainWindow > QTabBar { background: transparent; qproperty-drawBase: 0; }
QMainWindow > QTabBar::tab {
    background: #1d1d1d; color: #9b9b9b; border: none; border-top-left-radius: 4px; border-top-right-radius: 4px;
    padding: 5px 10px 4px 10px; margin: 0 1px 0 0; font-weight: 600;
}
QMainWindow > QTabBar::tab:selected { color: #f0f0f0; background: #262626; }
QMainWindow > QTabBar::tab:hover:!selected { color: #d0d0d0; }
QSplitter#Gutter::handle { background: #101010; }
QSplitter#Gutter::handle:horizontal { width: 4px; }
QSplitter#Gutter::handle:vertical { height: 4px; }
QSplitter#Gutter::handle:hover { background: #2a2a2a; }

/* ---- panel tabs ------------------------------------------------------ */
QTabWidget#PanelTabs::pane { border: none; background: #1d1d1d; top: 0px; }
QTabWidget#PanelTabs > QTabBar { background: transparent; }
QTabBar#PanelTabBar { background: transparent; qproperty-drawBase: 0; }
QTabBar#PanelTabBar::tab {
    background: transparent; color: #9b9b9b; border: none;
    padding: 6px 9px 5px 9px; margin: 0; font-weight: 600;
}
QTabBar#PanelTabBar::tab:selected { color: #f0f0f0; }
QTabBar#PanelTabBar::tab:hover:!selected { color: #d0d0d0; }
QTabBar#PanelTabBar QToolButton { background: #1d1d1d; border: none; }
QLabel#PanelTitle { color: #f0f0f0; font-weight: 600; padding: 6px 9px 5px 9px; }

/* ---- header bar (Home / Import / Edit / Export) ----------------------- */
QWidget#HeaderBar { background: #101010; }
QToolButton#ModeButton {
    color: #9b9b9b; background: transparent; border: none; font-size: 10pt; font-weight: 600;
    padding: 4px 12px;
}
QToolButton#ModeButton:hover { color: #e3e3e3; }
QToolButton#ModeButton:checked { color: #ffffff; border-bottom: 2px solid #378ef0; }
QLabel#HeaderStatus { color: #8a8a8a; }

/* ---- menus ------------------------------------------------------------ */
QMenuBar { background: #101010; padding: 0px; }
QMenuBar::item { padding: 3px 8px; background: transparent; color: #cfcfcf; }
QMenuBar::item:selected { background: #2b2b2b; color: #ffffff; }
QMenu { background: #262626; border: 1px solid #3a3a3a; padding: 4px 0; }
QMenu::item { padding: 4px 28px 4px 26px; }
QMenu::item:selected { background: #378ef0; color: #ffffff; }
QMenu::item:disabled { color: #5c5c5c; }
QMenu::separator { height: 1px; background: #3a3a3a; margin: 4px 0; }
QMenu::indicator { width: 12px; height: 12px; left: 8px; }

/* ---- tool buttons: icon-only, Premiere-flat ---------------------------- */
QToolButton {
    background: transparent; border: none; border-radius: 3px; padding: 3px; color: #cfcfcf;
}
QToolButton:hover { background: #2e2e2e; }
QToolButton:pressed { background: #262626; }
QToolButton:checked { background: transparent; }
QToolButton::menu-indicator { image: none; width: 0; }
QToolButton[popupMode="1"] { padding-right: 3px; }

/* ---- buttons / inputs ------------------------------------------------- */
QPushButton {
    background: #2b2b2b; border: 1px solid #3c3c3c; border-radius: 12px;
    padding: 4px 14px; min-height: 16px; color: #e3e3e3;
}
QPushButton:hover { background: #343434; }
QPushButton:pressed { background: #262626; }
QPushButton:default { background: #378ef0; border-color: #378ef0; color: #ffffff; }
QPushButton:default:hover { background: #4b9df5; }
QPushButton:disabled { color: #5c5c5c; background: #222222; border-color: #2c2c2c; }

QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit {
    background: #141414; border: 1px solid #333333; border-radius: 3px;
    padding: 2px 6px; selection-background-color: #378ef0; selection-color: #ffffff;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus, QPlainTextEdit:focus { border-color: #378ef0; }
QComboBox { padding-right: 18px; }
QComboBox::drop-down { border: none; width: 16px; }
QComboBox QAbstractItemView { background: #262626; border: 1px solid #3a3a3a; selection-background-color: #378ef0; outline: none; }
QComboBox#Flat { background: transparent; border: none; color: #4ba3ff; padding: 0 4px 0 2px; }
QComboBox#Flat::drop-down { width: 0px; border: none; }
QComboBox#Flat::down-arrow { image: none; width: 0px; height: 0px; }
QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 0; border: none; }

QListWidget, QTreeWidget, QTableWidget, QAbstractScrollArea { background: #1d1d1d; border: none; outline: none; }
QListWidget::item { padding: 2px; border-radius: 2px; }
QListWidget::item:selected { background: #2a5fa6; color: #ffffff; }
QListWidget::item:hover:!selected { background: #262626; }
QHeaderView::section { background: #1d1d1d; color: #9b9b9b; border: none; border-bottom: 1px solid #2c2c2c; padding: 3px 6px; }

QScrollBar:vertical { background: transparent; width: 9px; margin: 0; }
QScrollBar:horizontal { background: transparent; height: 9px; margin: 0; }
QScrollBar::handle:vertical { background: #3d3d3d; min-height: 24px; border-radius: 4px; margin: 1px; }
QScrollBar::handle:horizontal { background: #3d3d3d; min-width: 24px; border-radius: 4px; margin: 1px; }
QScrollBar::handle:hover { background: #575757; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QSlider::groove:horizontal { height: 2px; background: #3d3d3d; border-radius: 1px; }
QSlider::sub-page:horizontal { background: #9b9b9b; border-radius: 1px; }
QSlider::handle:horizontal { background: #d8d8d8; width: 10px; height: 10px; margin: -4px 0; border-radius: 5px; }
QSlider::handle:horizontal:hover { background: #ffffff; }
QSlider::groove:vertical { width: 3px; background: #3d3d3d; border-radius: 1px; }
QSlider::handle:vertical { background: #d8d8d8; height: 14px; width: 22px; margin: 0 -10px; border-radius: 2px; }

QStatusBar { background: #101010; color: #8a8a8a; }
QStatusBar::item { border: none; }

QTabWidget::pane { border: 1px solid #2c2c2c; }
QTabBar::tab { background: transparent; color: #9b9b9b; padding: 5px 12px; border: none; }
QTabBar::tab:selected { color: #ffffff; border-bottom: 2px solid #378ef0; }

QGroupBox { border: 1px solid #2c2c2c; border-radius: 3px; margin-top: 14px; padding-top: 6px; }
QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; color: #9b9b9b; font-weight: 600; }

QCheckBox::indicator, QRadioButton::indicator { width: 12px; height: 12px; border: 1px solid #5a5a5a; background: #141414; }
QCheckBox::indicator { border-radius: 2px; }
QRadioButton::indicator { border-radius: 6px; }
QCheckBox::indicator:checked, QRadioButton::indicator:checked { background: #378ef0; border-color: #378ef0; }

QToolTip { background: #2b2b2b; color: #e3e3e3; border: 1px solid #444444; padding: 3px 6px; }
QProgressBar { background: #141414; border: none; border-radius: 2px; text-align: center; color: #e3e3e3; height: 10px; }
QProgressBar::chunk { background: #378ef0; border-radius: 2px; }
QDial { background: transparent; }
)"));
}

} // namespace bc::theme
