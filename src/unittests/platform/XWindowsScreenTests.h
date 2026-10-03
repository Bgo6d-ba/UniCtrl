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

class XWindowsScreenTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void cleanupTestCase();
  void registerHotKey_keyWithModifiers_grabsAndReusesId();
  void registerHotKey_modifiersOnly_grabs();
  void registerHotKey_toggleModifier_rejected();

private:
  Arch m_arch;
  Log m_log;
  // held open for the whole run so Xvfb does not reset between the screens' own connections
  Display *m_display = nullptr;
};
