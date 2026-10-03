/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "IpcServer.h"

#include "base/Log.h"
#include "common/IpcUtils.h"
#include "common/VersionInfo.h"

#include <QLocalServer>
#include <QLocalSocket>

#if defined(Q_OS_WIN)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <sddl.h>

#include <vector>
#endif

namespace deskflow::core::ipc {

namespace {

// Commands that describe current state, so only the latest one is worth replaying.
const QStringList kStateCommands = {
    QStringLiteral("connectionState"), QStringLiteral("connectedClients"), QStringLiteral("secureSocket"),
    QStringLiteral("retryIn"), QStringLiteral("missingKeyboardLayouts")
};
const qsizetype kMaxPendingMessages = 100;
const qint64 kMaxMessageSize = 64 * 1024;

#if defined(Q_OS_WIN)
bool hasEnabledAdminGroup(HANDLE token)
{
  alignas(DWORD) BYTE adminSid[SECURITY_MAX_SID_SIZE];
  DWORD sidSize = sizeof(adminSid);
  if (!CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, adminSid, &sidSize)) {
    return false;
  }

  DWORD size = 0;
  GetTokenInformation(token, TokenGroups, nullptr, 0, &size);
  std::vector<BYTE> buffer(size);
  if (size == 0 || !GetTokenInformation(token, TokenGroups, buffer.data(), size, &size)) {
    return false;
  }

  const auto *groups = reinterpret_cast<const TOKEN_GROUPS *>(buffer.data());
  for (DWORD i = 0; i < groups->GroupCount; ++i) {
    if ((groups->Groups[i].Attributes & SE_GROUP_ENABLED) && EqualSid(groups->Groups[i].Sid, adminSid)) {
      return true;
    }
  }
  return false;
}

// UAC gives admins a filtered token where Administrators is deny-only; such a caller is not elevated.
bool isFilteredAdminToken(HANDLE token)
{
  TOKEN_ELEVATION_TYPE elevationType{};
  DWORD size = 0;
  if (!GetTokenInformation(token, TokenElevationType, &elevationType, sizeof(elevationType), &size) ||
      elevationType != TokenElevationTypeLimited) {
    return false;
  }

  TOKEN_LINKED_TOKEN linked{};
  if (!GetTokenInformation(token, TokenLinkedToken, &linked, sizeof(linked), &size)) {
    return false;
  }

  const auto isAdmin = hasEnabledAdminGroup(linked.LinkedToken);
  CloseHandle(linked.LinkedToken);
  return isAdmin;
}

QString tokenUserSid(HANDLE token)
{
  DWORD size = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  std::vector<BYTE> buffer(size);
  LPWSTR sid = nullptr;
  if (size == 0 || !GetTokenInformation(token, TokenUser, buffer.data(), size, &size) ||
      !ConvertSidToStringSidW(reinterpret_cast<const TOKEN_USER *>(buffer.data())->User.Sid, &sid)) {
    return {};
  }
  const auto result = QString::fromWCharArray(sid);
  LocalFree(sid);
  return result;
}
#endif

} // namespace

IpcServer::IpcServer(
    QObject *parent, const QString &serverName, const QString &typeName, QLocalServer::SocketOptions socketOptions
)
    : QObject(parent),
      m_server{new QLocalServer(this)}, // NOSONAR - Qt memory
      m_serverName(serverName),
      m_socketOptions(socketOptions),
      m_typeName(typeName.toUtf8())
{
  // do nothing
}

IpcServer::~IpcServer()
{
  m_server->close();
}

void IpcServer::listen()
{
  m_server->setSocketOptions(m_socketOptions);

  connect(m_server, &QLocalServer::newConnection, this, &IpcServer::handleNewConnection);
  QLocalServer::removeServer(m_serverName);
  if (m_server->listen(m_serverName)) {
    LOG_DEBUG("%s ipc server listening on: %s", m_typeName.constData(), m_serverName.toUtf8().constData());
  } else {
    LOG_ERR("%s ipc server failed to listen on: %s", m_typeName.constData(), m_serverName.toUtf8().constData());
  }
}

void IpcServer::handleNewConnection()
{
  QLocalSocket *clientSocket = m_server->nextPendingConnection();
  if (!clientSocket) {
    LOG_ERR("%s ipc server failed to get new connection", m_typeName.constData());
    return;
  }

  if (!isClientAllowed(clientSocket)) {
    LOG_WARN("%s ipc server rejected connection", m_typeName.constData());
    clientSocket->abort();
    clientSocket->deleteLater();
    return;
  }

  LOG_DEBUG("%s ipc server got new connection", m_typeName.constData());
  m_clients.insert(clientSocket, Handshake::Pending);

  connect(clientSocket, &QLocalSocket::readyRead, this, &IpcServer::handleReadyRead);
  connect(clientSocket, &QLocalSocket::disconnected, this, &IpcServer::handleDisconnected);
  connect(clientSocket, &QLocalSocket::errorOccurred, this, &IpcServer::handleErrorOccurred);
}

void IpcServer::handleReadyRead()
{
  const auto clientSocket = qobject_cast<QLocalSocket *>(sender());
  LOG_VERBOSE("%s ipc server ready to read data", m_typeName.constData());

  // Each message is delimited by a newline; an incomplete line stays in the socket buffer until the rest arrives.
  while (m_clients.contains(clientSocket) && clientSocket->canReadLine()) {
    auto messageData = clientSocket->readLine();
    messageData.chop(1);
    processMessage(clientSocket, QString::fromUtf8(messageData));
  }

  if (m_clients.contains(clientSocket) && clientSocket->bytesAvailable() > kMaxMessageSize) {
    LOG_WARN("%s ipc server got oversized message, disconnecting client", m_typeName.constData());
    m_clients.remove(clientSocket);
    clientSocket->abort();
    clientSocket->deleteLater();
  }
}

void IpcServer::handleDisconnected()
{
  const auto clientSocket = qobject_cast<QLocalSocket *>(sender());
  LOG_DEBUG("%s ipc server client disconnected", m_typeName.constData());
  m_clients.remove(clientSocket);
  clientSocket->deleteLater();
}

void IpcServer::handleErrorOccurred()
{
  const auto clientSocket = qobject_cast<QLocalSocket *>(sender());
  if (clientSocket->error() == QLocalSocket::PeerClosedError) {
    LOG_DEBUG("%s ipc server client closed connection", m_typeName.constData());
  } else {
    LOG_ERR("%s ipc server client error: %s", m_typeName.constData(), clientSocket->errorString().toUtf8().constData());
  }

  m_clients.remove(clientSocket);
  clientSocket->deleteLater();
}

void IpcServer::dropClient(QLocalSocket *clientSocket)
{
  // Forgotten first, so nothing more it sent is processed and nothing more is sent to it.
  m_clients.remove(clientSocket);
  clientSocket->flush();
  clientSocket->disconnectFromServer();
}

void IpcServer::processMessage(QLocalSocket *clientSocket, const QString &message)
{
  LOG_VERBOSE("%s ipc server got message: %s", m_typeName.constData(), message.toUtf8().constData());
  const auto parts = deskflow::ipc::splitMessage(message);
  const auto &command = parts.at(0);
  const auto handshake = m_clients.value(clientSocket);
  if (command == QStringLiteral("hello")) {
    processHello(clientSocket, parts);
    return;
  }

  if (command == QStringLiteral("noop")) {
    LOG_DEBUG("%s ipc server got noop message", m_typeName.constData());
    writeToClientSocket(clientSocket, QStringLiteral("ok"));
  } else if (handshake == Handshake::Done ||
             (handshake == Handshake::VersionMismatch && versionMismatchCommands().contains(command))) {
    processCommand(clientSocket, command, parts);
  } else {
    LOG_WARN(
        "%s ipc server refused command before a matching hello: %s", m_typeName.constData(),
        command.toUtf8().constData()
    );
    writeToClientSocket(clientSocket, QStringLiteral("error=hello required"));
  }

  clientSocket->flush();
}

void IpcServer::processHello(QLocalSocket *clientSocket, const QStringList &parts)
{
  if (parts.size() < 2) {
    LOG_ERR("%s ipc client hello missing version", m_typeName.constData());
    writeToClientSocket(clientSocket, "error=missing version");
    dropClient(clientSocket);
    return;
  }

  const auto versionId = QStringLiteral("%1+%2").arg(kVersion, kVersionGitSha);
  const auto &clientVersion = parts.at(1);
  LOG_DEBUG("%s ipc server got hello message (version: %s)", m_typeName.constData(), versionId.toUtf8().constData());

  if (clientVersion != versionId) {
    LOG_WARN(
        "%s ipc client version mismatch (client: %s, server: %s)", m_typeName.constData(),
        clientVersion.toUtf8().constData(), versionId.toUtf8().constData()
    );
    writeToClientSocket(clientSocket, QStringLiteral("versionMismatch=%1").arg(versionId));
    if (versionMismatchCommands().isEmpty()) {
      dropClient(clientSocket);
    } else {
      m_clients.insert(clientSocket, Handshake::VersionMismatch);
      clientSocket->flush();
    }
    return;
  }

  LOG_DEBUG("%s ipc server sending hello back", m_typeName.constData());
  m_clients.insert(clientSocket, Handshake::Done);
  writeToClientSocket(clientSocket, QStringLiteral("hello=%1").arg(versionId));

  // Replay messages that were queued before any clients connected.
  LOG_VERBOSE("ipc server replaying %lld pending messages", m_pendingMessages.size());
  for (const auto &pending : std::as_const(m_pendingMessages)) {
    LOG_VERBOSE("%s ipc server replaying: %s", m_typeName.constData(), pending.toUtf8().constData());
    writeToClientSocket(clientSocket, pending);
  }
  m_pendingMessages.clear();
  clientSocket->flush();
}

void IpcServer::broadcastCommand(const QString &command, const QString &args)
{
  // Trailing line breaks are harmless (e.g. an SSL cipher description), so drop them rather than the message.
  auto trimmedArgs = args;
  while (trimmedArgs.endsWith('\n') || trimmedArgs.endsWith('\r')) {
    trimmedArgs.chop(1);
  }

  const auto message = trimmedArgs.isEmpty() ? command : QStringLiteral("%1=%2").arg(command, trimmedArgs);
  if (hasLineBreak(message)) {
    return;
  }

  // State is only for clients that completed the handshake, but any client may be waiting for the server to stop,
  // such as a GUI of another version that asked this core to stop.
  const auto toAll = command == QStringLiteral("bye");
  QList<QLocalSocket *> ready;
  for (const auto [client, handshake] : m_clients.asKeyValueRange()) {
    if (handshake == Handshake::Done || toAll) {
      ready.append(client);
    }
  }

  if (ready.isEmpty()) {
    LOG_VERBOSE(
        "%s ipc server has no clients, message queued: %s", m_typeName.constData(), message.toUtf8().constData()
    );
    // Nothing may drain the queue for a long time, so drop what a late client would not need.
    m_pendingMessages.removeIf([&](const QString &pending) {
      return pending == message ||
             (kStateCommands.contains(command) && deskflow::ipc::splitMessage(pending).at(0) == command);
    });
    m_pendingMessages.append(message);
    if (m_pendingMessages.size() > kMaxPendingMessages) {
      m_pendingMessages.removeFirst();
    }
    return;
  }

  LOG_VERBOSE(
      "%s ipc server broadcasting message to %lld clients: %s", m_typeName.constData(), ready.size(),
      message.toUtf8().constData()
  );
  for (auto *client : std::as_const(ready)) {
    writeToClientSocket(client, message);
    client->flush();
  }
}

bool IpcServer::hasLineBreak(const QString &message) const
{
  // Messages can carry peer-controlled text; a line break would let it inject a second message.
  if (message.contains('\n') || message.contains('\r')) {
    LOG_WARN("%s ipc server refused to send message containing a line break", m_typeName.constData());
    return true;
  }
  return false;
}

void IpcServer::writeToClientSocket(QLocalSocket *&clientSocket, const QString &message) const
{
  if (hasLineBreak(message)) {
    return;
  }

  QByteArray messageData = message.toUtf8() + '\n';
  qint64 bytesWritten = clientSocket->write(messageData);
  if (bytesWritten != messageData.size()) {
    LOG_ERR("%s ipc server failed to write full message to client socket", m_typeName.constData());
  } else {
    LOG_VERBOSE(
        "%s ipc server wrote message to client socket: %s", m_typeName.constData(), message.toUtf8().constData()
    );
  }
}

#if defined(Q_OS_WIN)
std::optional<IpcCaller> IpcServer::pipeCaller(const QLocalSocket *clientSocket)
{
  // On Windows the socket descriptor is the server end of the named pipe.
  const auto pipe = reinterpret_cast<HANDLE>(clientSocket->socketDescriptor());
  ULONG processId = 0;
  ULONG sessionId = 0;
  if (!GetNamedPipeClientProcessId(pipe, &processId) || !GetNamedPipeClientSessionId(pipe, &sessionId)) {
    LOG_ERR("failed to identify ipc client, error: %lu", GetLastError());
    return std::nullopt;
  }

  IpcCaller caller;
  caller.sessionId = sessionId;
  if (HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId); process != nullptr) {
    if (HANDLE token = nullptr; OpenProcessToken(process, TOKEN_QUERY, &token)) {
      // SYSTEM and elevated admins have Administrators enabled in their own token.
      caller.isAdmin = hasEnabledAdminGroup(token);
      caller.isFilteredAdmin = !caller.isAdmin && isFilteredAdminToken(token);
      caller.userSid = tokenUserSid(token);
      CloseHandle(token);
    }
    CloseHandle(process);
  }
  return caller;
}
#endif

} // namespace deskflow::core::ipc
