/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "CoreIpcClient.h"

#include "common/IpcUtils.h"

#include <QString>

#if defined(Q_OS_WIN)
#include <QLocalSocket>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <aclapi.h>
#include <sddl.h>

#include <vector>
#endif

namespace deskflow::gui::ipc {

CoreIpcClient::CoreIpcClient(QObject *parent)
    : IpcClient(parent, deskflow::ipc::coreServerName(), QStringLiteral("core"))
{
  // do nothing
}

void CoreIpcClient::sendStop()
{
  sendMessage(QStringLiteral("stop"));
}

void CoreIpcClient::processCommand(const QString &command, const QStringList &parts)
{
  const auto args = parts.size() >= 2 ? parts.at(1) : QString();
  Q_EMIT commandReceived(command, args);
}

bool CoreIpcClient::isTrustedServer(
    quint32 serverSession, const QString &pipeOwnerSid, quint32 ownSession, const QString &ownSid
)
{
  // Qt makes the pipe owner the server's token user: this user, or SYSTEM when the daemon elevates the core.
  // Every SYSTEM core shares that owner, so the session check is what keeps other desktops' cores out.
  const auto isOwnerTrusted = pipeOwnerSid == ownSid || pipeOwnerSid == QStringLiteral("S-1-5-18");
  return !ownSid.isEmpty() && serverSession == ownSession && isOwnerTrusted;
}

#if defined(Q_OS_WIN)
namespace {

QString sidToString(PSID sid)
{
  LPWSTR text = nullptr;
  if (sid == nullptr || !ConvertSidToStringSidW(sid, &text)) {
    return {};
  }
  const auto result = QString::fromWCharArray(text);
  LocalFree(text);
  return result;
}

QString ownUserSid()
{
  QString result;
  if (HANDLE token = nullptr; OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<BYTE> buffer(size);
    if (size != 0 && GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
      result = sidToString(reinterpret_cast<const TOKEN_USER *>(buffer.data())->User.Sid);
    }
    CloseHandle(token);
  }
  return result;
}

QString pipeOwnerSid(HANDLE pipe)
{
  PSID owner = nullptr;
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (GetSecurityInfo(
          pipe, SE_KERNEL_OBJECT, OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr, nullptr, &descriptor
      ) != ERROR_SUCCESS) {
    return {};
  }
  const auto result = sidToString(owner);
  LocalFree(descriptor);
  return result;
}

} // namespace

bool CoreIpcClient::isServerTrusted(const QLocalSocket &socket) const
{
  // Session and owner come from the pipe itself, since a core running as SYSTEM cannot be opened by this user.
  const auto pipe = reinterpret_cast<HANDLE>(socket.socketDescriptor());
  ULONG serverSession = 0;
  DWORD ownSession = 0;
  if (!GetNamedPipeServerSessionId(pipe, &serverSession) || !ProcessIdToSessionId(GetCurrentProcessId(), &ownSession)) {
    qWarning("core ipc could not get the pipe server session, error: %lu", GetLastError());
    return false;
  }

  const auto owner = pipeOwnerSid(pipe);
  if (!isTrustedServer(serverSession, owner, ownSession, ownUserSid())) {
    qWarning("core ipc refused pipe server in session %lu owned by %s", serverSession, qUtf8Printable(owner));
    return false;
  }
  return true;
}
#endif

} // namespace deskflow::gui::ipc
