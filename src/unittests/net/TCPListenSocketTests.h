/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "arch/Arch.h"
#include "base/Log.h"

#include <QObject>

class TCPListenSocketTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void accept_nothingPending_returnsNullAndKeepsListening();
  void accept_acceptFails_keepsListening();
  void accept_peerAlreadyReset_leaksNoDescriptor();
  void serviceListening_error_reportsWithoutDeadlock();

private:
  Arch m_arch;
  Log m_log;
};
