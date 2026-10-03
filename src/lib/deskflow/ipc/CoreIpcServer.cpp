/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "CoreIpcServer.h"

#include "base/Log.h"
#include "common/IpcUtils.h"

#include <QLocalSocket>

#if defined(Q_OS_WIN)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace deskflow::core::ipc {

namespace {

QLocalServer::SocketOptions socketOptions()
{
#if defined(Q_OS_WIN)
  // An elevated core runs as SYSTEM while the GUI does not, so only then is the pipe opened to all and callers checked.
  bool isSystem = false;
  if (HANDLE token = nullptr; OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    alignas(TOKEN_USER) BYTE buffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    DWORD size = 0;
    isSystem = GetTokenInformation(token, TokenUser, buffer, sizeof(buffer), &size) &&
               IsWellKnownSid(reinterpret_cast<const TOKEN_USER *>(buffer)->User.Sid, WinLocalSystemSid);
    CloseHandle(token);
  }
  if (isSystem) {
    return QLocalServer::WorldAccessOption;
  }
#endif
  return QLocalServer::UserAccessOption;
}

} // namespace

CoreIpcServer::CoreIpcServer(QObject *parent)
    : IpcServer(parent, deskflow::ipc::coreServerName(), QStringLiteral("core"), socketOptions())
{
  setStatusSink(this);
}

CoreIpcServer::~CoreIpcServer()
{
  if (statusSink() == this) {
    setStatusSink(nullptr);
  }
}

void CoreIpcServer::sendStatus(const QString &command, const QString &args)
{
  // Queued because callers may not be on the main thread,
  // and QLocalSocket can only be written to from its owning thread.
  QMetaObject::invokeMethod(this, [this, command, args] { broadcastCommand(command, args); }, Qt::QueuedConnection);
}

void CoreIpcServer::processCommand(QLocalSocket *clientSocket, const QString &command, const QStringList &parts)
{
  Q_UNUSED(parts)
  if (command == QStringLiteral("stop")) {
    LOG_DEBUG("core ipc server got stop message");
    writeToClientSocket(clientSocket, QStringLiteral("ok"));
    broadcastCommand(QStringLiteral("bye"));
    Q_EMIT stopProcessRequested();
    return;
  }
  LOG_WARN("core ipc server got unknown command: %s", command.toUtf8().constData());
}

QStringList CoreIpcServer::versionMismatchCommands() const
{
  // A GUI of another version must still be able to stop this core to replace it.
  return {QStringLiteral("stop")};
}

#if defined(Q_OS_WIN)
bool CoreIpcServer::isClientAllowed(QLocalSocket *clientSocket)
{
  const auto caller = pipeCaller(clientSocket);
  DWORD sessionId = 0;
  if (!caller || !ProcessIdToSessionId(GetCurrentProcessId(), &sessionId)) {
    return false;
  }

  if (!policy::coreAcceptsConnection(*caller, sessionId)) {
    LOG_WARN("core ipc server refused non-admin client from session %u", caller->sessionId);
    return false;
  }
  return true;
}
#endif

} // namespace deskflow::core::ipc
