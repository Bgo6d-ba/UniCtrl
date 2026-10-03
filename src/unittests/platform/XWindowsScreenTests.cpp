/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "XWindowsScreenTests.h"

#include "base/EventQueue.h"
#include "deskflow/AppUtil.h"
#include "platform/XWindowsScreen.h"

namespace {
class TestAppUtil : public AppUtil
{
public:
  int run() override
  {
    return 0;
  }

  std::vector<std::string> getKeyboardLayoutList() override
  {
    return {"en"};
  }

  std::string getCurrentLanguageCode() override
  {
    return "en";
  }
};
} // namespace

void XWindowsScreenTests::initTestCase()
{
  m_arch.init();
  m_log.setFilter(LogLevel::Level::Error);
  m_display = XOpenDisplay(nullptr);
  if (m_display == nullptr) {
    QSKIP("needs an X server");
  }
}

void XWindowsScreenTests::cleanupTestCase()
{
  if (m_display != nullptr) {
    XCloseDisplay(m_display);
  }
}

void XWindowsScreenTests::registerHotKey_keyWithModifiers_grabsAndReusesId()
{
  TestAppUtil appUtil;
  EventQueue events;
  XWindowsScreen screen(nullptr, true, &events);
  screen.updateKeyMap();

  const auto id = screen.registerHotKey('a', KeyModifierControl | KeyModifierShift);
  QVERIFY(id != 0);
  screen.unregisterHotKey(id);
  QCOMPARE(screen.registerHotKey('a', KeyModifierControl | KeyModifierShift), id);
}

void XWindowsScreenTests::registerHotKey_modifiersOnly_grabs()
{
  TestAppUtil appUtil;
  EventQueue events;
  XWindowsScreen screen(nullptr, true, &events);
  screen.updateKeyMap();

  QVERIFY(screen.registerHotKey(kKeyNone, KeyModifierControl | KeyModifierShift) != 0);
}

void XWindowsScreenTests::registerHotKey_toggleModifier_rejected()
{
  TestAppUtil appUtil;
  EventQueue events;
  XWindowsScreen screen(nullptr, true, &events);
  screen.updateKeyMap();

  QCOMPARE(screen.registerHotKey('a', KeyModifierCapsLock), 0u);
}

QTEST_MAIN(XWindowsScreenTests)
