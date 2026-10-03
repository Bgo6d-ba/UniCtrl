/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTemporaryDir>
#include <QTest>

class LanSessionTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void init();
  void hosting_announcesAndForcesSecurity();
  void hosting_portInUse();
  void hosting_codeAndPassword();
  void hosting_keepsSecurityOn();
  void hosting_refreshAnnouncesNewIdentity();
  void hosting_setsAndClearsTheFlag();
  void hosting_capsThePairings();
  void hosting_lastPairingStillCompletes();
  void hosting_keepsACertificateThatIsInvalid();
  void hosting_tellsAboutANewCertificate();
  void lockout_replacesAnAutomaticCode();
  void lockout_keepsACustomPassword();
  void browsing_pausesWhileHosting();
  void browsing_startsOnlyWhenAsked();
  void join_trustsBothWays();
  void join_wrongCode();
  void join_customPassword();
  void join_hostNotNearby();
  void join_hostUnreachable();
  void join_givesUpWithoutAConnection();
  void join_connectionStopsTheWatchdog();
  void rejoin_skipsPairing();
  void rejoin_needsPairedNearbyHost();
  void rejoin_failureForgetsTheHost();
  void rejoin_invalidFingerprintIsForgotten();
  void untrustedPeer_isNeverAskedAbout();
  void hostsList_marksPairedHosts();
  void joinedComputers_useDisplayNames();
  void core_restartKeepsHosting();
  void core_stopEndsHosting();
  void core_modeChangeEndsHosting();
  void core_joinFollowsConnection();
  void core_stopEndsJoin();
  void simpleView_hostsFlaggedServer();
  void simpleView_takesUpServerOnceStarted();
  void simpleView_leavesManualServerAlone();
  void simpleView_doesNotForceSecurityToTakeUp();
  void simpleView_showsRunningClient();
  void simpleView_adoptedClientHasNoWatchdog();
  void screenQueue_holdsClientsWhileEditing();
  void screenQueue_nestsAndSkipsWhatTheLayoutHas();

private:
  QTemporaryDir m_dir;
};
