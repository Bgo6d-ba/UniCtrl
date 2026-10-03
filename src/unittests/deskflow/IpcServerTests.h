/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/Log.h"

#include <QTest>

class IpcServerTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void argsKeepEquals();
  void partialLineIsBuffered();
  void oversizedLineDisconnects();
  void lineBreakInArgsIsNotSent();
  void trailingLineBreakIsTrimmed();
  void lineBreakDoesNotEvictPendingState();
  void pendingStateKeepsLatest();
  void pendingMessagesAreCapped();
  void commandBeforeHelloIsRefused();
  void versionMismatchDisconnects();
  void versionMismatchKeepsAllowedCommands();
  void broadcastSkipsClientsWithoutHello();

private:
  Log m_log;
};
