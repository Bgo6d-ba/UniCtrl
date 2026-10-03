/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "arch/Arch.h"
#include "base/Log.h"
#include "net/Fingerprint.h"

#include <QObject>
#include <QTemporaryDir>

class SecureSocketTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void init();
  void trustedPeers_exchangeDataBothWays();
  void untrustedServer_clientFailsClosed();
  void unknownClient_serverRejects();
  void close_whilePeerSends_doesNotDeadlock();
  void destroyed_leavesNoConnectHandler();
  void largeWrite_slowReadingPeer_isKept();

private:
  Arch m_arch;
  Log m_log;
  QTemporaryDir m_dir;
  Fingerprint m_fingerprint;
  Fingerprint m_otherFingerprint;
};
