/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "DaemonIpcServer.h"

#include "base/Log.h"

#include <QLocalSocket>
#include <QVariant>

#if defined(Q_OS_WIN)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace deskflow::core::ipc {

const auto kAckMessage = "ok";
const auto kErrorMessage = "error";
const auto kAdminProperty = "deskflowIpcAdmin";
const auto kFilteredAdminProperty = "deskflowIpcFilteredAdmin";
const auto kSessionProperty = "deskflowIpcSession";
const auto kUserSidProperty = "deskflowIpcUserSid";

namespace {

IpcCaller callerProperties(const QLocalSocket *clientSocket)
{
  return {
      clientSocket->property(kAdminProperty).toBool(), clientSocket->property(kFilteredAdminProperty).toBool(),
      clientSocket->property(kSessionProperty).toUInt(), clientSocket->property(kUserSidProperty).toString()
  };
}

} // namespace

// The daemon runs as SYSTEM but the GUI runs as a regular user, so the pipe is open to all and callers are checked.
DaemonIpcServer::DaemonIpcServer(QObject *parent, const QString &logFilename, const QString &serverName)
    : IpcServer(parent, serverName, QStringLiteral("daemon"), QLocalServer::WorldAccessOption),
      m_logFilename(logFilename)
{
  // do nothing
}

void DaemonIpcServer::processCommand(QLocalSocket *clientSocket, const QString &command, const QStringList &parts)
{
  const auto caller = callerProperties(clientSocket);
  if (!policy::daemonAcceptsCommand(caller, consoleSessionId())) {
    LOG_WARN("daemon ipc server refused command from non-admin client no longer on the console session");
    writeToClientSocket(clientSocket, kErrorMessage);
    return;
  }

  const auto arg = parts.value(1);
  if (command == QStringLiteral("logLevel") && !arg.isEmpty()) {
    LOG_DEBUG("daemon ipc server got new log level: %s", arg.toUtf8().constData());
    Q_EMIT logLevelChanged(arg);
    writeToClientSocket(clientSocket, kAckMessage);
  } else if (command == QStringLiteral("configFile")) {
    processConfigFile(clientSocket, parts);
  } else if (command == QStringLiteral("start")) {
    LOG_DEBUG("daemon ipc server got start message");
    m_starter = clientSocket;
    Q_EMIT startProcessRequested(caller);
    m_starter = nullptr;
    writeToClientSocket(clientSocket, kAckMessage);
  } else if (command == QStringLiteral("stop")) {
    LOG_DEBUG("daemon ipc server got stop message");
    Q_EMIT stopProcessRequested();
    writeToClientSocket(clientSocket, kAckMessage);
  } else if (command == QStringLiteral("logPath")) {
    LOG_DEBUG("daemon ipc server got log path request");
    writeToClientSocket(clientSocket, QStringLiteral("logPath=%1").arg(m_logFilename));
  } else if (command == QStringLiteral("clearSettings")) {
    LOG_DEBUG("daemon ipc server got clear settings message");
    Q_EMIT clearSettingsRequested();
    writeToClientSocket(clientSocket, kAckMessage);
  } else {
    LOG_WARN("daemon ipc server got invalid command: %s", command.toUtf8().constData());
    writeToClientSocket(clientSocket, kErrorMessage);
  }
}

void DaemonIpcServer::replyToStarter(const QString &command, const QString &args)
{
  if (m_starter != nullptr) {
    writeToClientSocket(m_starter, QStringLiteral("%1=%2").arg(command, args));
  }
}

void DaemonIpcServer::processConfigFile(QLocalSocket *&clientSocket, const QStringList &messageParts)
{
  const auto file = messageParts.value(1);
  const auto config = policy::configFromCaller(file, callerProperties(clientSocket));
  if (!config) {
    LOG_ERR("daemon ipc server refused config file path: %s", file.toUtf8().constData());
    writeToClientSocket(clientSocket, kErrorMessage);
    return;
  }

  LOG_DEBUG("daemon ipc server got config file: %s", file.toUtf8().constData());
  Q_EMIT configChanged(*config);
  writeToClientSocket(clientSocket, kAckMessage);
}

bool DaemonIpcServer::isClientAllowed(QLocalSocket *clientSocket)
{
  const auto caller = callerOf(clientSocket);
  if (!caller || !policy::daemonAcceptsConnection(*caller, consoleSessionId())) {
    LOG_WARN("daemon ipc server refused client (session %u)", caller ? caller->sessionId : 0);
    return false;
  }

  clientSocket->setProperty(kAdminProperty, caller->isAdmin);
  clientSocket->setProperty(kFilteredAdminProperty, caller->isFilteredAdmin);
  clientSocket->setProperty(kSessionProperty, caller->sessionId);
  clientSocket->setProperty(kUserSidProperty, caller->userSid);
  return true;
}

std::optional<IpcCaller> DaemonIpcServer::callerOf(const QLocalSocket *clientSocket) const
{
#if defined(Q_OS_WIN)
  return pipeCaller(clientSocket);
#else
  Q_UNUSED(clientSocket)
  return std::nullopt;
#endif
}

quint32 DaemonIpcServer::consoleSessionId() const
{
#if defined(Q_OS_WIN)
  return WTSGetActiveConsoleSessionId();
#else
  return 0;
#endif
}

} // namespace deskflow::core::ipc
