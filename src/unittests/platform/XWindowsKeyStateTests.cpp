/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "XWindowsKeyStateTests.h"

#include "base/EventQueue.h"
#include "deskflow/AppUtil.h"
#include "deskflow/KeyMap.h"
#include "platform/XWindowsKeyState.h"

#include <X11/keysym.h>

#include <algorithm>
#include <vector>

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

using KeyEntries = std::vector<deskflow::KeyMap::KeyItem>;

KeyEntries keyEntries(deskflow::KeyMap &keyMap)
{
  KeyEntries entries;
  keyMap.foreachKey(
      [](KeyID, int32_t, deskflow::KeyMap::KeyItem &item, void *data) {
        static_cast<KeyEntries *>(data)->push_back(item);
      },
      &entries
  );
  return entries;
}

bool hasEntry(const KeyEntries &entries, KeyID id, KeyModifierMask required)
{
  return std::ranges::any_of(entries, [&](const auto &item) {
    return item.m_id == id && item.m_group == 0 && item.m_required == required;
  });
}

void verifyLetterNeedsShiftOrCapsLock(Display *display, bool useXKB)
{
  if (display == nullptr) {
    QSKIP("needs an X server");
  }

  TestAppUtil appUtil;
  EventQueue events;
  deskflow::KeyMap keyMap;
  {
    XWindowsKeyState keyState(display, useXKB, &events, keyMap);
    keyState.updateKeyMap();

    unsigned int shift = 0;
    QVERIFY(keyState.mapModifiersToX(KeyModifierShift, shift));
    QCOMPARE(shift, static_cast<unsigned int>(ShiftMask));

    const auto entries = keyEntries(keyMap);
    QVERIFY(hasEntry(entries, 'a', 0));
    QVERIFY(hasEntry(entries, 'A', KeyModifierShift));
    QVERIFY(hasEntry(entries, 'A', KeyModifierCapsLock));
  }
}
} // namespace

void XWindowsKeyStateTests::initTestCase()
{
  m_arch.init();
  m_log.setFilter(LogLevel::Level::Error);
  m_display = XOpenDisplay(nullptr);
}

void XWindowsKeyStateTests::cleanupTestCase()
{
  if (m_display != nullptr) {
    XCloseDisplay(m_display);
  }
}

void XWindowsKeyStateTests::sensitivityFromKeysyms_letter_shiftAndCapsLock()
{
  XWindowsKeyState::LevelKeysyms keysyms = {XK_a, XK_A, NoSymbol, NoSymbol};
  QCOMPARE(XWindowsKeyState::sensitivityFromKeysyms(keysyms), KeyModifierShift | KeyModifierCapsLock);
}

void XWindowsKeyStateTests::sensitivityFromKeysyms_lowercaseOnly_fillsUppercase()
{
  XWindowsKeyState::LevelKeysyms keysyms = {XK_a, NoSymbol, NoSymbol, NoSymbol};
  QCOMPARE(XWindowsKeyState::sensitivityFromKeysyms(keysyms), KeyModifierShift | KeyModifierCapsLock);
  QCOMPARE(keysyms[1], static_cast<KeySym>(XK_A));
}

void XWindowsKeyStateTests::sensitivityFromKeysyms_altGrLevels_altGr()
{
  XWindowsKeyState::LevelKeysyms keysyms = {XK_q, XK_Q, XK_at, NoSymbol};
  QCOMPARE(
      XWindowsKeyState::sensitivityFromKeysyms(keysyms), KeyModifierAltGr | KeyModifierShift | KeyModifierCapsLock
  );
}

void XWindowsKeyStateTests::sensitivityFromKeysyms_keypad_numLockAndShift()
{
  XWindowsKeyState::LevelKeysyms keysyms = {XK_KP_End, XK_KP_1, NoSymbol, NoSymbol};
  QCOMPARE(XWindowsKeyState::sensitivityFromKeysyms(keysyms), KeyModifierShift | KeyModifierNumLock);
}

void XWindowsKeyStateTests::sensitivityFromKeysyms_digit_shiftOnly()
{
  XWindowsKeyState::LevelKeysyms keysyms = {XK_1, XK_exclam, NoSymbol, NoSymbol};
  QCOMPARE(XWindowsKeyState::sensitivityFromKeysyms(keysyms), KeyModifierShift);
}

void XWindowsKeyStateTests::levelKeyID_modifierWithoutLevelKeysym_fallsBackToLevelZero()
{
  const XWindowsKeyState::LevelKeysyms keysyms = {XK_Shift_L, NoSymbol, NoSymbol, NoSymbol};
  QCOMPARE(XWindowsKeyState::levelKeyID(keysyms, 2, true), kKeyShift_L);
}

void XWindowsKeyStateTests::levelKeyID_plainKeyWithoutLevelKeysym_none()
{
  const XWindowsKeyState::LevelKeysyms keysyms = {XK_Shift_L, NoSymbol, NoSymbol, NoSymbol};
  QCOMPARE(XWindowsKeyState::levelKeyID(keysyms, 2, false), kKeyNone);
}

void XWindowsKeyStateTests::updateKeyMap_coreProtocol_letterNeedsShiftOrCapsLock()
{
  verifyLetterNeedsShiftOrCapsLock(m_display, false);
}

void XWindowsKeyStateTests::updateKeyMap_xkb_letterNeedsShiftOrCapsLock()
{
  verifyLetterNeedsShiftOrCapsLock(m_display, true);
}

QTEST_MAIN(XWindowsKeyStateTests)
