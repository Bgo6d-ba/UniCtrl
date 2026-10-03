/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTemporaryDir>
#include <QTest>

class TlsUtilityTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void init();
  void enforceMinKeySize_raisesSmallKeySize();
  void enforceMinKeySize_keepsLargerKeySize();
  void ensureCertificate_generatesMissingCert();
  void ensureCertificate_keepsInvalidCertUnlessReplacing();
  void ensureCertificate_keepsValidCert();
  void trust_roundTripsThroughDatabase();

private:
  QString m_certPath;
  QTemporaryDir m_dir;
};
