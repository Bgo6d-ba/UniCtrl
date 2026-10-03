/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "DaemonPolicy.h"

#include <QHash>
#include <QLocalServer>
#include <QObject>

#include <optional>

class QLocalSocket;

namespace deskflow::core::ipc {

class IpcServer : public QObject
{
  Q_OBJECT

public:
  explicit IpcServer(
      QObject *parent, const QString &serverName, const QString &typeName, QLocalServer::SocketOptions socketOptions
  );
  ~IpcServer() override;

  void listen();
  void broadcastCommand(const QString &command, const QString &args = "");

Q_SIGNALS:
  void logLevelChanged(const QString &logLevel);
  void configChanged(const deskflow::core::ipc::DaemonConfig &config);
  void startProcessRequested(const deskflow::core::ipc::IpcCaller &caller);
  void stopProcessRequested();
  void clearSettingsRequested();

protected:
  /**!
   * Write a message to the client socket and append a newline character.
   *
   * \param clientSocket The client socket to write to.
   * \param message The message to write (without trailing newline).
   */
  void writeToClientSocket(QLocalSocket *&clientSocket, const QString &message) const;

  virtual bool isClientAllowed(QLocalSocket *clientSocket)
  {
    Q_UNUSED(clientSocket)
    return true;
  }

  //! Commands still accepted from a client whose version does not match; with none, it is disconnected.
  virtual QStringList versionMismatchCommands() const
  {
    return {};
  }

#if defined(Q_OS_WIN)
  static std::optional<IpcCaller> pipeCaller(const QLocalSocket *clientSocket);
#endif

private:
  enum class Handshake
  {
    Pending,
    Done,
    VersionMismatch
  };

  bool hasLineBreak(const QString &message) const;
  void processMessage(QLocalSocket *clientSocket, const QString &message);
  void processHello(QLocalSocket *clientSocket, const QStringList &parts);
  virtual void processCommand(QLocalSocket *clientSocket, const QString &command, const QStringList &parts) = 0;
  void dropClient(QLocalSocket *clientSocket);
  void handleNewConnection();
  void handleReadyRead();
  void handleDisconnected();
  void handleErrorOccurred();

  QLocalServer *m_server;
  QHash<QLocalSocket *, Handshake> m_clients;
  QString m_serverName;
  QLocalServer::SocketOptions m_socketOptions;
  QStringList m_pendingMessages;
  QByteArray m_typeName;
};

} // namespace deskflow::core::ipc
