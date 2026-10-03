/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "IpcClient.h"

#include <QObject>

namespace deskflow::gui::ipc {

class CoreIpcClient : public IpcClient
{
  Q_OBJECT

public:
  explicit CoreIpcClient(QObject *parent = nullptr);

  void sendStop();

  //! Whether the core pipe server this GUI connected to is its own session's core.
  static bool
  isTrustedServer(quint32 serverSession, const QString &pipeOwnerSid, quint32 ownSession, const QString &ownSid);

Q_SIGNALS:
  void commandReceived(const QString &command, const QString &args);

protected:
  void processCommand(const QString &command, const QStringList &parts) override;
#if defined(Q_OS_WIN)
  bool isServerTrusted(const QLocalSocket &socket) const override;
#endif
};

} // namespace deskflow::gui::ipc
