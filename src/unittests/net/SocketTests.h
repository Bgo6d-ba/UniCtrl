/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "arch/Arch.h"
#include "base/Log.h"

#include <QTest>

class SocketTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void unblockPollSocket_beforeFirstPoll_wakesThread();
  void flush_peerNotReading_returns();
  void write_peerStopsReading_dropsConnection();
  void write_beyondCapToReadingPeer_isKept();
  void serviceConnected_readError_disconnects();
  void acceptedSocket_notPolledUntilJobStarts();
  void sharedContext_reusedUntilCertChanges();
  void socketMultiplexer_destroyed_closesUnblockPipe();
  void networkAddress_selfAssign_keepsAddress();

private:
  Arch m_arch;
  Log m_log;
};
