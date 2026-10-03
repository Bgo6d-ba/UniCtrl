/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "IpcServer.h"

#include "common/Constants.h"

#include <QObject>
#include <QString>

class QLocalSocket;

namespace deskflow::core::ipc {

class DaemonIpcServer : public IpcServer
{
  Q_OBJECT

public:
  explicit DaemonIpcServer(
      QObject *parent, const QString &logFilename, const QString &serverName = QString::fromLatin1(kDaemonIpcName)
  );

  //! Tells only the client whose start is being handled, from a slot of startProcessRequested.
  void replyToStarter(const QString &command, const QString &args);

protected:
  //! What the OS says about the process at the other end; nothing where it cannot tell.
  virtual std::optional<IpcCaller> callerOf(const QLocalSocket *clientSocket) const;
  virtual quint32 consoleSessionId() const;

private:
  void processCommand(QLocalSocket *clientSocket, const QString &command, const QStringList &parts) override;
  bool isClientAllowed(QLocalSocket *clientSocket) override;
  void processConfigFile(QLocalSocket *&clientSocket, const QStringList &messageParts);

private:
  const QString m_logFilename;
  QLocalSocket *m_starter = nullptr;
};

} // namespace deskflow::core::ipc
