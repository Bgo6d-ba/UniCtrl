/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>

namespace deskflow::fuzz {

//! Every input ends with this command, so the fuzzer knows the peer has handled the input
inline const QString kSentinel = QStringLiteral("fuzzSentinel");

inline void initQt(const char *appName)
{
  static int argc = 1;
  static char *argv[] = {const_cast<char *>(appName), nullptr};
  static QCoreApplication app(argc, argv);
  qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &) {});
}

inline QString socketName(const char *role)
{
  return QStringLiteral("%1/deskflow-fuzz-%2-%3")
      .arg(QDir::tempPath())
      .arg(QCoreApplication::applicationPid())
      .arg(role);
}

//! Runs the event loop until \p done returns true; false on timeout
template <typename F> bool processEventsUntil(F done)
{
  const QDeadlineTimer deadline(10000);
  while (!done()) {
    if (deadline.hasExpired()) {
      return false;
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
  }
  return true;
}

} // namespace deskflow::fuzz
