/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/Log.h"

#include <QTemporaryDir>
#include <QTest>

class ScreenCommandTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void disabledCommand_doesNotRun();
  void enterCommand_runsOnEnterOnly();
  void exitCommand_runsOnExitOnly();

private:
  QString touchCommand(const QString &marker) const;

  Log m_log;
  QTemporaryDir m_dir;
};
