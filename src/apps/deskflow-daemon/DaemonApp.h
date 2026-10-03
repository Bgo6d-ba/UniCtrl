/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2025 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/ipc/DaemonPolicy.h"

#include <QObject>
#include <QThread>

#include <optional>

class Event;
class IEventQueue;
class FileLogOutputter;
class QLocalServer;
class QCoreApplication;

namespace deskflow::core::ipc {
class DaemonIpcServer;
}

#if defined(Q_OS_WIN)
class MSWindowsWatchdog;
#endif

extern const char *const kLogFilename;

class DaemonApp : public QObject
{
  Q_OBJECT

public:
  explicit DaemonApp(IEventQueue &events);
  ~DaemonApp() override;

  void run(QThread &daemonThread);
  void setForeground();
  void initLogging();
  void connectIpcServer(deskflow::core::ipc::DaemonIpcServer *ipcServer);
  //! When the machine-wide folder could not be made admin-only, nothing in it is used.
  void setSystemDirTrusted(bool trusted);

  QString logFilename() const;

private:
  void daemonize();
  void handleError(const char *message);
  int mainLoop();
  int daemonLoop();
  void saveLogLevel(const QString &logLevel) const;
  void setConfig(const deskflow::core::ipc::DaemonConfig &config);
  void startProcess(const deskflow::core::ipc::IpcCaller &caller) const;
  //! @param starter Who asked for the start, or nothing when the boot config is re-applied.
  void applyWatchdogCommand(const std::optional<deskflow::core::ipc::IpcCaller> &starter) const;
  void clearWatchdogCommand();
  void clearSettings();

  static void showConsole();

#if defined(Q_OS_WIN)
  std::unique_ptr<MSWindowsWatchdog> m_pWatchdog;
#endif

  IEventQueue &m_events;
  FileLogOutputter *m_pFileLogOutputter = nullptr;
  deskflow::core::ipc::DaemonIpcServer *m_ipcServer = nullptr;
  deskflow::core::ipc::DaemonConfig m_config;
  QString m_untrustedDir;
  bool m_foreground = false;
};
