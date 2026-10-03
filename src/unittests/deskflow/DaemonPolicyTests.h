/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/Log.h"

#include <QTemporaryDir>
#include <QTest>

class DaemonPolicyTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void remoteAndOddPathsAreRefused();
  void configFromNonAdminIsBoundToOwner();
  void configFromFilteredAdminIsNotAdmin();
  void onlyAdminConfigStartsAtBoot();
  void legacyAdminFlagIsIgnored();
  void insideResolvesPaths();
  void logLeavesUntrustedDir();
  void planRefusesUntrustedDir();
  void coreAcceptsOwnSessionOrAdmin();
  void planBuildsWideCommand();
  void planRefusesBadConfigs();
  void planElevatesOnlyForElevatedAdmins();

  void serverRefusesOtherSessions();
  void serverAcceptsAdminFromAnySession();
  void serverRefusesCommandsAfterConsoleChange();
  void serverBindsConfigToOwner();
  void serverRefusesRemoteConfig();
  void serverPassesStarter();
  void serverRepliesOnlyToStarter();
  void serverRejectsUnknownCommand();

private:
  QString writeConfig(const QString &name, int coreMode, bool elevate) const;

  QTemporaryDir m_dir;
  QTemporaryDir m_settingsDir;
  Log m_log;
};
