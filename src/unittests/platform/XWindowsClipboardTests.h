/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "base/Log.h"

#include <QTest>

// after Qt: X11 defines macros such as Status and Bool
#include "platform/XWindowsClipboard.h"

class XWindowsClipboardTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  // Test are run in order top to bottom
  void defaultCtor();
  void initTestCase();
  void cleanupTestCase();
  void open();
  void singleFormat();

private:
  Log m_log;
  const std::string m_testString = "deskflow test string";
  const std::string m_testString2 = "Another String";
  Display *m_display = nullptr;
  Window m_window = None;
  XWindowsClipboard &getClipboard();
  std::unique_ptr<XWindowsClipboard> m_clipboard;
};
