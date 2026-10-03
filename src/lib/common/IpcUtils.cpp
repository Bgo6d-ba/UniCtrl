/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "IpcUtils.h"

#include "common/Constants.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#if !defined(Q_OS_WIN)
#include <unistd.h>
#endif

namespace deskflow::ipc {

#if !defined(Q_OS_WIN)
const qsizetype kMaxSocketPath = 104;
#endif

QStringList splitMessage(const QString &message)
{
  const auto index = message.indexOf('=');
  if (index < 0) {
    return {message};
  }
  return {message.left(index), message.mid(index + 1)};
}

QString coreServerName()
{
#if defined(Q_OS_WIN)
  return QString::fromLatin1(kCoreIpcName);
#else
  // A bare name would put the socket in the shared temp dir, where another user could squat it.
  auto dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
  // sun_path holds only 104 bytes on macOS (108 on Linux), which a long home path can exceed.
  if (dir.isEmpty() || QStringLiteral("%1/%2").arg(dir, kCoreIpcName).toUtf8().size() >= kMaxSocketPath) {
    dir = QStringLiteral("%1/%2-%3").arg(QDir::tempPath(), kAppId, QString::number(getuid()));
    QDir().mkdir(dir);
    const QFileInfo info(dir);
    if (info.isSymLink() || !info.isDir() || info.ownerId() != getuid() ||
        !QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner)) {
      return {};
    }
  }
  return QStringLiteral("%1/%2").arg(dir, kCoreIpcName);
#endif
}

} // namespace deskflow::ipc
