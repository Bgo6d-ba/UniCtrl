/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTest>

class PairingTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void identity_roundTrip();
  void identity_rejects_data();
  void identity_rejects();
  void pair_succeeds();
  void pair_wrongCode();
  void pair_locksAddressOutAfterFailures();
  void pair_failuresExpire();
  void pair_parallelGuessesShareTheBudget();
  void pair_parallelFloodMakesFewGuesses();
  void pair_globalBudget();
  void pair_passwordRotatedOnLockout();
  void pair_resetsBudgets_data();
  void pair_resetsBudgets();
  void pair_screenNameClash();
  void pair_answerSurvivesChangesInPairedSlot_data();
  void pair_answerSurvivesChangesInPairedSlot();
  void pair_newPasswordDropsPairingsInProgress();
  void pair_tamperedIdentity_data();
  void pair_tamperedIdentity();
  void server_needsValidIdentity();
  void pair_noPasswordRefused();
  void server_closesBadFrames_data();
  void server_closesBadFrames();
  void server_acceptsLargestFrame();
  void server_closesIdleConnection();
  void server_helloDeadline();
  void server_limitsConnections();
  void server_refusesOtherSubnets();
  void client_rejectsBadReplies_data();
  void client_rejectsBadReplies();
  void client_timeout();
  void client_unreachable();
};
