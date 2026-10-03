/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "IpcServer.h"

#include "common/StatusSink.h"

#include <QObject>
#include <QSet>

class QLocalSocket;

namespace deskflow::core::ipc {

// Installs itself as the status sink while alive and forwards status to connected GUI clients.
class CoreIpcServer : public IpcServer, public IStatusSink
{
  Q_OBJECT

public:
  explicit CoreIpcServer(QObject *parent);
  ~CoreIpcServer() override;

  void sendStatus(const QString &command, const QString &args) override;

private:
  void processCommand(QLocalSocket *clientSocket, const QString &command, const QStringList &parts) override;
  QStringList versionMismatchCommands() const override;
#if defined(Q_OS_WIN)
  bool isClientAllowed(QLocalSocket *clientSocket) override;
#endif
};

} // namespace deskflow::core::ipc
