/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "TlsUtilityTests.h"

#include "../TestSettings.h"

#include "common/Settings.h"
#include "gui/TlsUtility.h"
#include "net/Fingerprint.h"

#include <QFile>

using namespace deskflow::gui;

void TlsUtilityTests::initTestCase()
{
  QVERIFY(m_dir.isValid());
  useTestSettings(m_dir);
  m_certPath = m_dir.filePath(QStringLiteral("tls/deskflow.pem"));
  Settings::setValue(Settings::Security::Certificate, m_certPath);
}

void TlsUtilityTests::init()
{
  QFile::remove(m_certPath);
  Settings::setValue(Settings::Security::KeySize, TlsUtility::kMinKeySize);
}

void TlsUtilityTests::enforceMinKeySize_raisesSmallKeySize()
{
  Settings::setValue(Settings::Security::KeySize, 1024);

  QVERIFY(TlsUtility::enforceMinKeySize());
  QCOMPARE(Settings::value(Settings::Security::KeySize).toInt(), TlsUtility::kMinKeySize);
  QVERIFY(!TlsUtility::enforceMinKeySize());
}

void TlsUtilityTests::enforceMinKeySize_keepsLargerKeySize()
{
  Settings::setValue(Settings::Security::KeySize, 4096);

  QVERIFY(!TlsUtility::enforceMinKeySize());
  QCOMPARE(Settings::value(Settings::Security::KeySize).toInt(), 4096);
}

void TlsUtilityTests::ensureCertificate_generatesMissingCert()
{
  const auto fingerprint = TlsUtility::ensureCertificate(false);

  QVERIFY(QFile::exists(m_certPath));
  QVERIFY(TlsUtility::isCertValid(m_certPath));
  QVERIFY(!fingerprint.isEmpty());
  QCOMPARE(fingerprint, TlsUtility::certFingerprint(m_certPath));
}

void TlsUtilityTests::ensureCertificate_keepsInvalidCertUnlessReplacing()
{
  QVERIFY(QDir().mkpath(QFileInfo(m_certPath).absolutePath()));
  QFile file(m_certPath);
  QVERIFY(file.open(QFile::WriteOnly));
  file.write("not a certificate");
  file.close();

  QVERIFY(TlsUtility::ensureCertificate(false).isEmpty());
  QVERIFY(!TlsUtility::isCertValid(m_certPath));

  QVERIFY(!TlsUtility::ensureCertificate(true).isEmpty());
  QVERIFY(TlsUtility::isCertValid(m_certPath));
}

void TlsUtilityTests::ensureCertificate_keepsValidCert()
{
  const auto first = TlsUtility::ensureCertificate(false);
  QVERIFY(!first.isEmpty());

  QCOMPARE(TlsUtility::ensureCertificate(true), first);
}

void TlsUtilityTests::trust_roundTripsThroughDatabase()
{
  const auto db = m_dir.filePath(QStringLiteral("trusted-servers"));
  const Fingerprint peer{QCryptographicHash::Sha256, QByteArray(32, 'a')};
  const Fingerprint other{QCryptographicHash::Sha256, QByteArray(32, 'b')};

  QVERIFY(!TlsUtility::isTrusted(db, peer));
  QVERIFY(TlsUtility::addTrusted(db, peer));
  QVERIFY(TlsUtility::addTrusted(db, other));
  QVERIFY(TlsUtility::isTrusted(db, peer));
  QVERIFY(TlsUtility::isTrusted(db, other));

  QVERIFY(!TlsUtility::addTrusted(m_dir.filePath(QStringLiteral("missing/dir/db")), peer));
}

QTEST_MAIN(TlsUtilityTests)
