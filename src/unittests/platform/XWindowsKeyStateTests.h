/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "arch/Arch.h"
#include "base/Log.h"

#include <QTest>

// not <X11/Xlib.h>: its None/Bool macros break headers included after this one
using Display = struct _XDisplay;

class XWindowsKeyStateTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void cleanupTestCase();
  void sensitivityFromKeysyms_letter_shiftAndCapsLock();
  void sensitivityFromKeysyms_lowercaseOnly_fillsUppercase();
  void sensitivityFromKeysyms_altGrLevels_altGr();
  void sensitivityFromKeysyms_keypad_numLockAndShift();
  void sensitivityFromKeysyms_digit_shiftOnly();
  void levelKeyID_modifierWithoutLevelKeysym_fallsBackToLevelZero();
  void levelKeyID_plainKeyWithoutLevelKeysym_none();
  void updateKeyMap_coreProtocol_letterNeedsShiftOrCapsLock();
  void updateKeyMap_xkb_letterNeedsShiftOrCapsLock();

private:
  Arch m_arch;
  Log m_log;
  // one connection for the whole run: Xvfb resets when its last client leaves, and a reopen can race that reset
  Display *m_display = nullptr;
};
