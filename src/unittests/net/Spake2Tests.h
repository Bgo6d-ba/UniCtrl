/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTest>

class Spake2Tests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void rfc9382Vectors_data();
  void rfc9382Vectors();
  void matchingPasswords_bothConfirm();
  void wrongPassword_bothFail();
  void differentIdentities_bothFail_data();
  void differentIdentities_bothFail();
  void invalidPeerShare_rejected_data();
  void invalidPeerShare_rejected();
  void identityK_rejected();
  void invalidW_neverShares_data();
  void invalidW_neverShares();
  void passwordScalar_knownAnswer();
  void finish_onlyOnce();
};
