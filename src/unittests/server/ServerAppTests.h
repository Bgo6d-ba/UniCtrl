/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "arch/Arch.h"
#include "base/Log.h"

#include <QTemporaryDir>
#include <QTest>

class ServerAppTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void resume_failedRestart_retriesThenQuitsWithError();
  void resume_screenBackOnRetry_startsServer();
  void destroy_whileStarted_configOutlivesServer();

private:
  Arch m_arch;
  Log m_log;
  QTemporaryDir m_settingsDir;
  QString m_configPath;
};
