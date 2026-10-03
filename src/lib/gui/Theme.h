/*
 * UniCtrl -- dark emerald theme
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QApplication>
#include <QPalette>
#include <QString>

namespace deskflow::gui {

inline void applyTheme(QApplication &app)
{
  QPalette p;
  const QColor bg(0x05, 0x07, 0x0a), panel(0x0f, 0x13, 0x18), text(0xe2, 0xe8, 0xf0), accent(0x10, 0xb9, 0x81);
  p.setColor(QPalette::Window, bg);
  p.setColor(QPalette::WindowText, text);
  p.setColor(QPalette::Base, panel);
  p.setColor(QPalette::AlternateBase, bg);
  p.setColor(QPalette::Text, text);
  p.setColor(QPalette::Button, panel);
  p.setColor(QPalette::ButtonText, text);
  p.setColor(QPalette::ToolTipBase, panel);
  p.setColor(QPalette::ToolTipText, text);
  p.setColor(QPalette::Highlight, accent);
  p.setColor(QPalette::HighlightedText, Qt::black);
  p.setColor(QPalette::PlaceholderText, QColor(0x64, 0x74, 0x8b));
  p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x47, 0x55, 0x69));
  p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x47, 0x55, 0x69));
  app.setPalette(p);

  app.setStyleSheet(QStringLiteral(R"(
QMainWindow, QDialog, QWidget#topLevelWidget { background: #05070a; }
QWidget { color: #e2e8f0; selection-background-color: #10b981; selection-color: #000; }
QLabel { background: transparent; }
QPushButton {
  background: rgba(255,255,255,0.05); border: 1px solid rgba(255,255,255,0.10);
  border-radius: 14px; padding: 7px 16px; color: #e2e8f0;
}
QPushButton#btnSimpleView, QPushButton#advancedButton {
  padding: 4px 16px; border-radius: 14px; color: #6ee7b7;
  background: rgba(16,185,129,0.10); border: 1px solid rgba(52,211,153,0.35);
}
QPushButton#btnSimpleView:hover, QPushButton#advancedButton:hover { background: rgba(16,185,129,0.25); }
QPlainTextEdit { color: #34d399; font-family: Menlo, monospace; }
QPushButton:hover { background: rgba(16,185,129,0.12); border-color: rgba(52,211,153,0.45); }
QPushButton:pressed { background: rgba(16,185,129,0.25); }
QPushButton:disabled { color: #475569; border-color: rgba(255,255,255,0.05); }
QPushButton:default, QPushButton#hostButton, QPushButton#joinButton {
  background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #10b981, stop:1 #059669);
  border: 1px solid rgba(52,211,153,0.6); color: #021;
}
QPushButton#hostButton, QPushButton#joinButton {
  border-radius: 24px; padding: 18px;
  background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 rgba(16,185,129,0.22), stop:1 rgba(5,150,105,0.06));
  border: 1px solid rgba(52,211,153,0.35); color: #e2e8f0;
}
QPushButton#hostButton:hover, QPushButton#joinButton:hover {
  background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 rgba(16,185,129,0.38), stop:1 rgba(5,150,105,0.14));
  border-color: rgba(52,211,153,0.8);
}
QPushButton#stopHostingButton { border-color: rgba(244,63,94,0.5); color: #fda4af; }
QPushButton#stopHostingButton:hover { background: rgba(244,63,94,0.15); }
QLabel#codeLabel {
  font-family: Menlo, monospace; font-size: 30px; letter-spacing: 6px; color: #6ee7b7;
  background: rgba(16,185,129,0.08); border: 1px solid rgba(52,211,153,0.3); border-radius: 18px; padding: 10px 18px;
}
QWidget#errorBox, QFrame#errorBox { background: rgba(244,63,94,0.10); border: 1px solid rgba(244,63,94,0.4); border-radius: 14px; }
QWidget#legacyNotice { background: rgba(255,255,255,0.03); border: 1px solid rgba(255,255,255,0.08); border-radius: 14px; }
QLineEdit, QSpinBox, QComboBox, QPlainTextEdit, QTextEdit, QListWidget, QListView, QTreeView, QTableView {
  background: rgba(255,255,255,0.04); border: 1px solid rgba(255,255,255,0.10); border-radius: 10px; padding: 4px 8px;
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border-color: rgba(16,185,129,0.6); }
QListWidget#joinedList::item { padding: 8px; border-radius: 8px; }
QListWidget#joinedList::item:selected { background: rgba(16,185,129,0.25); }
QGroupBox { border: 1px solid rgba(255,255,255,0.08); border-radius: 14px; margin-top: 12px; padding-top: 10px; }
QGroupBox::title { subcontrol-origin: margin; left: 12px; color: #6ee7b7; }
QMenuBar, QMenu { background: #0b0e12; border: 1px solid rgba(255,255,255,0.08); }
QMenu::item:selected, QMenuBar::item:selected { background: rgba(16,185,129,0.25); }
QStatusBar { background: #05070a; border-top: 1px solid rgba(255,255,255,0.05); color: #94a3b8; }
QToolTip { background: #0f1318; color: #e2e8f0; border: 1px solid rgba(52,211,153,0.3); }
QScrollBar:vertical { background: transparent; width: 10px; }
QScrollBar::handle:vertical { background: rgba(255,255,255,0.15); border-radius: 5px; min-height: 24px; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QTabWidget::pane { border: 1px solid rgba(255,255,255,0.08); border-radius: 12px; top: 10px; }
QTabBar::tab { padding: 6px 14px; border-radius: 10px; color: #94a3b8; }
QTabBar::tab:selected { background: rgba(16,185,129,0.18); color: #6ee7b7; }
QRadioButton, QCheckBox { spacing: 10px; padding: 4px 2px; }
QRadioButton::indicator, QCheckBox::indicator {
  width: 16px; height: 16px; border: 1px solid rgba(255,255,255,0.25); background: rgba(255,255,255,0.04);
}
QRadioButton::indicator { border-radius: 9px; }
QCheckBox::indicator { border-radius: 5px; }
QRadioButton::indicator:checked { background: qradialgradient(cx:.5,cy:.5,radius:.5,fx:.5,fy:.5, stop:0 #10b981, stop:.55 #10b981, stop:.6 #05070a, stop:1 #05070a); border-color: #10b981; }
QCheckBox::indicator:checked { background: #10b981; border-color: #10b981; }
QPushButton#btnEditName { min-width: 20px; max-width: 28px; padding: 5px; border-radius: 12px; }
)"));
}

} // namespace deskflow::gui
