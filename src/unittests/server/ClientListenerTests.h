/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "arch/Arch.h"
#include "base/Log.h"

#include <QObject>
#include <QTemporaryDir>

class ClientListenerTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void handshakeTimeout_deletesPendingSocket();
  void pendingLimit_refusesExtraSockets();
  void handshakeDeadline_closesStalledTlsConnection();
  void hello_validName_announcesClient();
  void hello_invalidName_rejected_data();
  void hello_invalidName_rejected();
  void pendingLimit_refusesConnectionsBeyondCap();
  void pendingLimit_oneAddress_leavesRoomForOthers();
  void listenError_listensAgain();

private:
  Arch m_arch;
  Log m_log;
  QTemporaryDir m_dir;
};
