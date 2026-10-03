/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTest>

class IpcClientTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void trustedServer_getsHello();
  void untrustedServer_refusedBeforeHello();
  void failedAttempts_sendOneHello();
  void refusedHandshake_dropsRest();
  void coreServer_onlyOwnSessionAndOwner();
};
