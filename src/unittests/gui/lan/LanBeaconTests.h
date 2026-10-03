/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTest>

class LanBeaconTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void cleanup();
  void randomCode_isSixDigits();
  void displayName_validation();
  void displayName_formatCharacters_data();
  void displayName_formatCharacters();
  void displayName_invisibleOnly_data();
  void displayName_invisibleOnly();
  void displayName_acceptsWhatIsVisible_data();
  void displayName_acceptsWhatIsVisible();
  void fingerprint_validation();
  void localPeer_subnets();
  void localPeer_loopbackAlways();
  void localPeer_followsInterfaces();
  void announcement_roundTrip();
  void parse_trimsName();
  void parse_rejects_data();
  void parse_rejects();
  void browser_receivesAnnouncer();
  void browser_ignoresOwnId();
  void browser_sortsByName();
  void browser_expiresHosts();
  void browser_capsHosts();
  void browser_capsHostsPerAddress();
  void browser_dropsOtherSubnets();
  void browser_coalescesChanges();
  void browser_ignoresNoise();
};
