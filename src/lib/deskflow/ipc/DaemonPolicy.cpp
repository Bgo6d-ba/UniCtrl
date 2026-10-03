/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "DaemonPolicy.h"

#include "common/Settings.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace deskflow::core::ipc::policy {

bool daemonAcceptsConnection(const IpcCaller &caller, quint32 consoleSessionId)
{
  if (caller.isAdmin) {
    return true;
  }
  return !caller.userSid.isEmpty() && caller.sessionId == consoleSessionId;
}

bool daemonAcceptsCommand(const IpcCaller &caller, quint32 consoleSessionId)
{
  return caller.isAdmin || caller.sessionId == consoleSessionId;
}

bool coreAcceptsConnection(const IpcCaller &caller, quint32 coreSessionId)
{
  return caller.isAdmin || caller.sessionId == coreSessionId;
}

bool isAcceptableConfigPath(const QString &path)
{
  // Checked before anything touches the file: even a stat of a UNC path authenticates to the host.
  // Win32 also passes "\\?\UNC\host" and "\??\UNC\host" to the network redirector.
  const auto normalised = QDir::fromNativeSeparators(path);
  return !path.isEmpty() && !normalised.startsWith(QStringLiteral("//")) &&
         !normalised.startsWith(QStringLiteral("/?\?/")) && QDir::isAbsolutePath(path) && !path.contains('"');
}

std::optional<DaemonConfig> configFromCaller(const QString &file, const IpcCaller &caller)
{
  if (!isAcceptableConfigPath(file)) {
    return std::nullopt;
  }
  return DaemonConfig{file, caller.isAdmin, caller.isAdmin ? QString() : caller.userSid};
}

std::optional<DaemonConfig> bootConfig(const QString &file, bool fromAdmin)
{
  if (!fromAdmin || file.isEmpty()) {
    return std::nullopt;
  }
  return DaemonConfig{file, true, {}};
}

bool isInside(const QString &path, const QString &dir)
{
  if (dir.isEmpty()) {
    return false;
  }
  // A file that does not exist yet, such as a log, is resolved through its folder.
  const auto resolve = [](const QString &p) {
    const QFileInfo info(p);
    if (const auto canonical = info.canonicalFilePath(); !canonical.isEmpty()) {
      return canonical;
    }
    const auto folder = QFileInfo(info.absolutePath()).canonicalFilePath();
    return folder.isEmpty() ? info.absoluteFilePath() : QStringLiteral("%1/%2").arg(folder, info.fileName());
  };
  const auto relative = QDir(resolve(dir)).relativeFilePath(resolve(path));
  return relative != QStringLiteral("..") && !relative.startsWith(QStringLiteral("../")) &&
         !QDir::isAbsolutePath(relative);
}

QString daemonLogFile(const QString &logFile, const QString &untrustedDir, const QString &fallbackDir)
{
  return isInside(logFile, untrustedDir) ? QStringLiteral("%1/%2").arg(fallbackDir, QFileInfo(logFile).fileName())
                                         : logFile;
}

CoreStartPlan planCoreStart(
    const DaemonConfig &config, const std::optional<IpcCaller> &starter, const QString &corePath,
    const QString &untrustedDir
)
{
  CoreStartPlan plan;
  if (!isAcceptableConfigPath(config.file)) {
    plan.error = QStringLiteral("config file path is not allowed: %1").arg(config.file);
    return plan;
  }
  if (!QFileInfo::exists(config.file)) {
    plan.error = QStringLiteral("config file does not exist: %1").arg(config.file);
    return plan;
  }
  if (isInside(config.file, untrustedDir)) {
    plan.error = QStringLiteral("config file is in a folder users may have written to: %1").arg(config.file);
    return plan;
  }
  if (!config.fromAdmin && config.ownerSid.isEmpty()) {
    plan.error = QStringLiteral("config file owner is unknown");
    return plan;
  }

  const QSettings settings(config.file, QSettings::IniFormat);
  const auto coreMode = settings.value(Settings::Core::CoreMode).toInt();
  QString modeArg;
  if (coreMode == Settings::CoreMode::Server) {
    modeArg = QStringLiteral("server");
  } else if (coreMode == Settings::CoreMode::Client) {
    modeArg = QStringLiteral("client");
  } else {
    plan.error = QStringLiteral("invalid core mode in config: %1").arg(coreMode);
    return plan;
  }

  // The file is chosen by an IPC caller, so its elevate setting is only trusted when both the caller
  // that set it and the caller that starts it are elevated admins.
  const auto wantsElevate =
      settings.value(Settings::Daemon::Elevate, Settings::defaultValue(Settings::Daemon::Elevate)).toBool();
  const auto allowElevate = config.fromAdmin && (!starter.has_value() || starter->isAdmin);
  plan.elevate = wantsElevate && allowElevate;
  // Elevate is on by default, so a standard user is not told on every start about what only an admin can change.
  if (wantsElevate && !allowElevate && starter.has_value() && starter->isFilteredAdmin) {
    plan.elevationRefused = QStringLiteral("run Deskflow as administrator to start an elevated core");
  } else if (wantsElevate && !allowElevate && starter.has_value() && starter->isAdmin) {
    plan.elevationRefused = QStringLiteral("save the settings as an elevated administrator to start an elevated core");
  }

  plan.command = QStringLiteral("\"%1\" %2 --settings \"%3\"").arg(corePath, modeArg, config.file).toStdWString();
  return plan;
}

} // namespace deskflow::core::ipc::policy
