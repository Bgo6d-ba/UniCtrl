/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "common/Settings.h"

#include <QTemporaryDir>
#include <QTest>

class SettingsTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  // Test are run in order top to bottom
  void setSettingsFile();
  void setStateFile();
  void settingsFile();
  void settingsPath();
  void tlsDir();
  void tlsTrustedServersDb();
  void tlsTrustedClientsDb();
  void checkValidSettings();
  void checkCleanScreenName();
  void checkCleanScreenName_LongName();
  void checkDisplayName();
  void checkHostId();
  void checkHostingEnabled();
  void checkLanSettingsKept();
  void checkClearState();
  void concurrentAccess();

private:
  QTemporaryDir m_dir;
  const QString m_settingsPathTemp = m_dir.path();
  const QString m_settingsFile = QStringLiteral("%1/Deskflow.conf").arg(m_settingsPathTemp);
  const QString m_stateFile = QStringLiteral("%1/Deskflow.state").arg(m_settingsPathTemp);

  // Users may not write the machine-wide folder, so TLS files stay next to the settings file on Windows too.
  const QString m_settingsPath = m_settingsPathTemp;

  const QString m_expectedTlsDir = QStringLiteral("%1/tls").arg(m_settingsPath);
  const QString m_expectedTlsServerDB = QStringLiteral("%1/trusted-servers").arg(m_expectedTlsDir);
  const QString m_expectedTlsClientDB = QStringLiteral("%1/trusted-clients").arg(m_expectedTlsDir);
};
