/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/Log.h"

#include <QTemporaryDir>
#include <QTest>

class PlatformScreenTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void needsInputFilterBeforeEnable_default_false();
  void screenLeave_secondaryButtonHeld_releasesButton();
  void screenReconfigure_logsCurrentSides();

private:
  QTemporaryDir m_dir;
  Log m_log;
};
