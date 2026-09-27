#include "Theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

namespace Theme {
QColor background() { return QColor(0x17, 0x19, 0x1e); }
QColor surface() { return QColor(0x21, 0x24, 0x2b); }
QColor raised() { return QColor(0x2d, 0x31, 0x3a); }
QColor text() { return QColor(0xe8, 0xe4, 0xda); }
QColor muted() { return QColor(0x8c, 0x91, 0x9c); }
QColor accent() { return QColor(0xf2, 0xa3, 0x3a); }

void apply(QApplication& app)
{
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette p;
    p.setColor(QPalette::Window, background());
    p.setColor(QPalette::WindowText, text());
    p.setColor(QPalette::Base, surface());
    p.setColor(QPalette::AlternateBase, raised());
    p.setColor(QPalette::ToolTipBase, raised());
    p.setColor(QPalette::ToolTipText, text());
    p.setColor(QPalette::Text, text());
    p.setColor(QPalette::PlaceholderText, muted());
    p.setColor(QPalette::Button, raised());
    p.setColor(QPalette::ButtonText, text());
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, accent());
    p.setColor(QPalette::HighlightedText, QColor(0x1a, 0x14, 0x0a));
    p.setColor(QPalette::Link, accent());
    p.setColor(QPalette::Mid, QColor(0x3a, 0x3e, 0x48));
    p.setColor(QPalette::Dark, QColor(0x10, 0x11, 0x15));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, muted().darker(125));
    app.setPalette(p);

    app.setStyleSheet(QStringLiteral(R"(
QToolTip { color: #e8e4da; background: #2d313a; border: 1px solid #454a55; padding: 4px 6px; }
QMainWindow::separator { background: #101115; width: 2px; height: 2px; }
QDockWidget { color: #e8e4da; font-weight: 600; }
QDockWidget::title { background: #1c1f25; padding: 8px 10px; text-align: left; }
QTabWidget::pane { border: 0; background: #1c1f25; }
QTabBar::tab { background: transparent; color: #8c919c; padding: 7px 14px; border: 0; border-bottom: 2px solid transparent; }
QTabBar::tab:selected { color: #e8e4da; border-bottom: 2px solid #f2a33a; }
QTabBar::tab:hover { color: #e8e4da; }
QScrollArea, QScrollArea > QWidget > QWidget { background: #1c1f25; }
QGroupBox { border: 0; border-top: 1px solid #30343d; margin-top: 18px; padding-top: 10px; font-weight: 600; color: #cfcabf; }
QGroupBox::title { subcontrol-origin: margin; left: 0; padding: 0 0 2px 0; }
QPushButton { background: #2d313a; border: 1px solid #3b404b; border-radius: 6px; padding: 5px 12px; }
QPushButton:hover { border-color: #f2a33a; }
QPushButton:pressed { background: #3a3f4a; }
QPushButton:disabled { color: #5d626c; border-color: #2d313a; }
QPushButton:checked { background: #4a3a22; border-color: #f2a33a; color: #ffd9a1; }
QComboBox, QSpinBox, QDoubleSpinBox, QLineEdit { background: #2a2d35; border: 1px solid #3b404b; border-radius: 6px; padding: 4px 8px; min-height: 20px; }
QComboBox:hover, QSpinBox:hover, QLineEdit:focus { border-color: #f2a33a; }
QComboBox QAbstractItemView { background: #2a2d35; selection-background-color: #f2a33a; selection-color: #1a140a; border: 1px solid #3b404b; }
QListWidget { background: #1c1f25; border: 0; outline: 0; }
QListWidget::item { padding: 6px 8px; border-radius: 5px; }
QListWidget::item:selected { background: #3a3122; color: #ffd9a1; }
QListWidget::item:hover { background: #272a31; }
QSlider::groove:horizontal { height: 4px; background: #3a3e48; border-radius: 2px; }
QSlider::sub-page:horizontal { background: #f2a33a; border-radius: 2px; }
QSlider::handle:horizontal { width: 14px; height: 14px; margin: -5px 0; border-radius: 7px; background: #e8e4da; }
QSlider::handle:horizontal:hover { background: #ffffff; }
QSlider::sub-page:horizontal:disabled { background: #555a64; }
QCheckBox::indicator { width: 16px; height: 16px; border-radius: 4px; border: 1px solid #4a4f5a; background: #2a2d35; }
QCheckBox::indicator:checked { background: #f2a33a; border-color: #f2a33a; }
QMenu { background: #252830; border: 1px solid #3b404b; padding: 4px; }
QMenu::item { padding: 6px 22px 6px 12px; border-radius: 4px; }
QMenu::item:selected { background: #3a3122; color: #ffd9a1; }
QMenu::item:disabled { color: #5d626c; }
QMenu::separator { height: 1px; background: #3b404b; margin: 4px 6px; }
QMenu::indicator { width: 12px; height: 12px; }
QScrollBar:vertical { background: transparent; width: 10px; }
QScrollBar::handle:vertical { background: #3a3e48; border-radius: 4px; min-height: 30px; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; }

#controlBar { background: rgba(20, 22, 27, 0.90); border-radius: 12px; }
#controlBar QToolButton { background: transparent; border: 0; border-radius: 8px; padding: 5px; }
#controlBar QToolButton:hover { background: rgba(255, 255, 255, 0.08); }
#controlBar QToolButton:checked { background: rgba(242, 163, 58, 0.16); }
#controlBar QToolButton::menu-indicator { image: none; width: 0; }
#controlBar QLabel { color: #cfcabf; }
#controlBar #timeLabel { color: #e8e4da; }
#controlBar QComboBox { background: rgba(255,255,255,0.06); border: 1px solid rgba(255,255,255,0.10); }
#seekSlider::groove:horizontal { height: 5px; background: rgba(255,255,255,0.18); border-radius: 2px; }
#seekSlider::handle:horizontal { width: 14px; height: 14px; margin: -5px 0; border-radius: 7px; background: #f2a33a; }
#osd { background: rgba(20, 22, 27, 0.88); color: #e8e4da; border-radius: 10px; padding: 8px 14px; font-size: 14px; }
#infoOverlay { background: rgba(20, 22, 27, 0.85); color: #e8e4da; border-radius: 10px; padding: 10px 14px; font-family: monospace; font-size: 12px; }
#compareLabel { background: rgba(20, 22, 27, 0.75); color: #ffd9a1; border-radius: 6px; padding: 3px 8px; font-weight: 600; }
#emptyHint { color: #8c919c; font-size: 15px; background: transparent; }
#paramValue { color: #8c919c; min-width: 38px; }
#presetState { color: #f2a33a; }
)"));
}
} // namespace Theme
