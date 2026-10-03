/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "DaemonIpcClient.h"

#include "common/Constants.h"

#include <QDebug>

#if defined(Q_OS_WIN)
#include <QLocalSocket>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <aclapi.h>
#include <winsvc.h>
#endif

namespace deskflow::gui::ipc {

DaemonIpcClient::DaemonIpcClient(QObject *parent) : IpcClient(parent, kDaemonIpcName, QStringLiteral("daemon"))
{
}

void DaemonIpcClient::sendLogLevel(const QString &logLevel)
{
  sendMessage(QStringLiteral("logLevel=%1").arg(logLevel));
}

void DaemonIpcClient::sendConfigFile(const QString &configFile)
{
  sendMessage(QStringLiteral("configFile=%1").arg(configFile));
}

void DaemonIpcClient::sendStartProcess()
{
  sendMessage(QStringLiteral("start"));
}

void DaemonIpcClient::sendStopProcess()
{
  sendMessage(QStringLiteral("stop"));
}

void DaemonIpcClient::sendClearSettings()
{
  sendMessage(QStringLiteral("clearSettings"));
}

void DaemonIpcClient::requestLogPath()
{
  sendMessage(QStringLiteral("logPath"));
}

void DaemonIpcClient::processCommand(const QString &command, const QStringList &parts)
{
  if (command == QStringLiteral("logPath") && parts.size() == 2) {
    Q_EMIT logPathReceived(parts.at(1));
  } else if (command == QStringLiteral("elevationRefused") && parts.size() == 2) {
    qWarning().noquote() << "the daemon started the core without elevation:" << parts.at(1);
  }
}

#if defined(Q_OS_WIN)
namespace {

DWORD runningServicePid()
{
  DWORD pid = 0;
  SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (manager == nullptr) {
    qWarning("daemon ipc could not open service manager, error: %lu", GetLastError());
    return 0;
  }
  if (SC_HANDLE service = OpenServiceW(manager, kAppNameW, SERVICE_QUERY_STATUS); service == nullptr) {
    qWarning("daemon ipc could not open service, error: %lu", GetLastError());
  } else {
    SERVICE_STATUS_PROCESS status{};
    DWORD needed = 0;
    if (!QueryServiceStatusEx(
            service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status), sizeof(status), &needed
        )) {
      qWarning("daemon ipc could not query service, error: %lu", GetLastError());
    } else if (status.dwCurrentState == SERVICE_RUNNING) {
      pid = status.dwProcessId;
    }
    CloseServiceHandle(service);
  }
  CloseServiceHandle(manager);
  return pid;
}

bool isOwnedByLocalSystem(HANDLE pipe)
{
  alignas(DWORD) BYTE systemSid[SECURITY_MAX_SID_SIZE];
  DWORD sidSize = sizeof(systemSid);
  PSID owner = nullptr;
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  const auto result = GetSecurityInfo(
      pipe, SE_KERNEL_OBJECT, OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr, nullptr, &descriptor
  );
  const auto owned = result == ERROR_SUCCESS && CreateWellKnownSid(WinLocalSystemSid, nullptr, systemSid, &sidSize) &&
                     EqualSid(owner, systemSid);
  if (descriptor != nullptr) {
    LocalFree(descriptor);
  }
  return owned;
}

} // namespace

bool DaemonIpcClient::isServerTrusted(const QLocalSocket &socket) const
{
  // The pipe owner alone is not enough: the daemon's pipe grants everyone FILE_CREATE_PIPE_INSTANCE, so any session
  // can add its own server instance, and every instance shares the daemon's security descriptor. Only the server
  // process id identifies the instance this socket connected to, so it must be the running daemon service's.
  const auto pipe = reinterpret_cast<HANDLE>(socket.socketDescriptor());
  ULONG serverPid = 0;
  if (!GetNamedPipeServerProcessId(pipe, &serverPid)) {
    qWarning("daemon ipc could not get pipe server pid, error: %lu", GetLastError());
    return false;
  }
  const auto servicePid = runningServicePid();
  if (servicePid == 0 || serverPid != servicePid || !isOwnedByLocalSystem(pipe)) {
    qWarning(
        "daemon ipc pipe server pid %lu is not the running daemon service (pid %lu) or not owned by LocalSystem; "
        "a daemon run with --foreground is not trusted",
        serverPid, servicePid
    );
    return false;
  }
  return true;
}
#endif

} // namespace deskflow::gui::ipc
