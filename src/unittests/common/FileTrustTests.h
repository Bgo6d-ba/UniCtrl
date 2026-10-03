/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTest>

class FileTrustTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void readAndExecuteIsNotChange();
  void writeRightsAreChange();
  void installerDaclIsAdminOnly();
  void userOwnerIsNotAdminOnly();
  void userWriteIsNotAdminOnly();
  void inheritOnlyAndDenyAreIgnored();
  void nullDaclIsNotAdminOnly();
  void machineWideCertificateIsAdminOnly();
  void privateFileSddlByFolder();
  void repairDecision();
  void untrustedEntriesAreFound();
};
