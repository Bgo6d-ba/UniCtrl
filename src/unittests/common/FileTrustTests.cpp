/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "FileTrustTests.h"

#include "common/FileTrust.h"
#include "common/Settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <algorithm>

using namespace deskflow::filetrust;

namespace {

const quint32 kFullControl = 0x1F01FF;
const quint32 kReadAndExecute = 0x1200A9;

// What the installer and the daemon apply: SYSTEM and Administrators full control, Users read and execute.
Security installerSecurity()
{
  return {
      Principal::System,
      true,
      {{Principal::System, kFullControl},
       {Principal::Administrators, kFullControl},
       {Principal::Other, kReadAndExecute}}
  };
}

} // namespace

void FileTrustTests::readAndExecuteIsNotChange()
{
  QVERIFY(!grantsChange(kReadAndExecute));
  QVERIFY(!grantsChange(0x80000000)); // GENERIC_READ
}

void FileTrustTests::writeRightsAreChange()
{
  QVERIFY(grantsChange(kFullControl));
  QVERIFY(grantsChange(0x2));        // add a file to a folder
  QVERIFY(grantsChange(0x4));        // add a subfolder
  QVERIFY(grantsChange(0x40000));    // WRITE_DAC
  QVERIFY(grantsChange(0x80000));    // WRITE_OWNER
  QVERIFY(grantsChange(0x10000000)); // GENERIC_ALL
  QVERIFY(grantsChange(0x40000000)); // GENERIC_WRITE
}

void FileTrustTests::installerDaclIsAdminOnly()
{
  auto security = installerSecurity();
  QVERIFY(isAdminOnly(security));
  security.owner = Principal::Administrators;
  QVERIFY(isAdminOnly(security));
}

void FileTrustTests::userOwnerIsNotAdminOnly()
{
  // An owner may always rewrite the DACL, so a planted file is never trusted.
  auto security = installerSecurity();
  security.owner = Principal::Other;
  QVERIFY(!isAdminOnly(security));
}

void FileTrustTests::userWriteIsNotAdminOnly()
{
  // The default ProgramData DACL lets Users add files and folders.
  auto security = installerSecurity();
  security.aces.append({Principal::Other, 0x6});
  QVERIFY(!isAdminOnly(security));
}

void FileTrustTests::inheritOnlyAndDenyAreIgnored()
{
  auto security = installerSecurity();
  security.aces.append({Principal::Other, kFullControl, true, true});
  security.aces.append({Principal::Other, 0, false, false});
  QVERIFY(isAdminOnly(security));
}

void FileTrustTests::nullDaclIsNotAdminOnly()
{
  auto security = installerSecurity();
  security.hasDacl = false;
  security.aces.clear();
  QVERIFY(!isAdminOnly(security));
}

void FileTrustTests::machineWideCertificateIsAdminOnly()
{
  // What an elevated GUI creates in the machine-wide folder: owner Administrators, D:P(OW FA)(SY FA)(BA FA)(BU FR).
  Security certificate{
      Principal::Administrators,
      true,
      {{Principal::OwnerRights, kFullControl},
       {Principal::System, kFullControl},
       {Principal::Administrators, kFullControl},
       {Principal::Other, 0x120089}}
  };
  QVERIFY(isAdminOnly(certificate));

  // The creating user's own ACE would let that user's unelevated processes replace the key.
  certificate.aces.append({Principal::Other, kFullControl});
  QVERIFY(!isAdminOnly(certificate));

  // OWNER RIGHTS is only as trusted as the owner.
  certificate.aces.removeLast();
  certificate.owner = Principal::Other;
  QVERIFY(!isAdminOnly(certificate));
}

void FileTrustTests::privateFileSddlByFolder()
{
  const auto base = QStringLiteral("D:P(A;;FA;;;OW)(A;;FA;;;SY)(A;;FA;;;BA)");
  const auto sid = QStringLiteral("S-1-5-21-1-2-3-1001");
  QCOMPARE(
      privateFileSddl(QStringLiteral("%1/tls/Deskflow.pem").arg(Settings::SystemDir), sid),
      base + QStringLiteral("(A;;FR;;;BU)")
  );
  QCOMPARE(
      privateFileSddl(QStringLiteral("%1/tls/Deskflow.pem").arg(Settings::UserDir), sid),
      QStringLiteral("%1(A;;FA;;;%2)").arg(base, sid)
  );
  QCOMPARE(privateFileSddl(QStringLiteral("%1/tls/Deskflow.pem").arg(Settings::UserDir), QString()), base);
  // Neither a sibling folder nor a way out of the folder counts as inside it.
  QCOMPARE(
      privateFileSddl(QStringLiteral("%1-other/Deskflow.pem").arg(Settings::SystemDir), sid),
      QStringLiteral("%1(A;;FA;;;%2)").arg(base, sid)
  );
  QCOMPARE(
      privateFileSddl(QStringLiteral("%1/../Deskflow.pem").arg(Settings::SystemDir), sid),
      QStringLiteral("%1(A;;FA;;;%2)").arg(base, sid)
  );
}

void FileTrustTests::repairDecision()
{
  QCOMPARE(systemDirRepair(OpenResult::NotFound, false, Principal::Other), SystemDirRepair::Create);
  QCOMPARE(systemDirRepair(OpenResult::Opened, true, Principal::System), SystemDirRepair::Secure);
  QCOMPARE(systemDirRepair(OpenResult::Opened, true, Principal::Administrators), SystemDirRepair::Secure);
  // A folder a user created before the installer did, or a link, could redirect what SYSTEM writes.
  QCOMPARE(systemDirRepair(OpenResult::Opened, true, Principal::Other), SystemDirRepair::Replace);
  QCOMPARE(systemDirRepair(OpenResult::Opened, false, Principal::System), SystemDirRepair::Replace);
  // A tool holding the admin's folder open must not get it, and the settings deployed in it, moved aside.
  QCOMPARE(systemDirRepair(OpenResult::Failed, false, Principal::Other), SystemDirRepair::Leave);
}

void FileTrustTests::untrustedEntriesAreFound()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const auto root = QFileInfo(dir.path()).canonicalFilePath();
  for (const auto &folder : QStringList{"tls", "planted", QStringLiteral("old%1-1").arg(kAsideMarker)}) {
    QVERIFY(QDir(root).mkpath(folder));
  }
  for (const auto &file :
       QStringList{"Deskflow.conf", "tls/Deskflow.pem", "tls/trusted-clients", "planted/Deskflow.pem"}) {
    QFile f(QStringLiteral("%1/%2").arg(root, file));
    QVERIFY(f.open(QFile::WriteOnly));
  }

  // A user planted the folder "planted" and the trust database.
  QStringList read;
  const auto reader = [&](const QString &path) -> std::optional<Security> {
    read.append(QDir(root).relativeFilePath(path));
    auto security = installerSecurity();
    if (path.endsWith(QStringLiteral("planted")) || path.endsWith(QStringLiteral("trusted-clients"))) {
      security.owner = Principal::Other;
    }
    return security;
  };

  auto untrusted = untrustedEntries(root, reader);
  untrusted.sort();
  QCOMPARE(
      untrusted,
      QStringList({QStringLiteral("%1/planted").arg(root), QStringLiteral("%1/tls/trusted-clients").arg(root)})
  );
  // Nothing is looked at inside an untrusted folder or one already moved aside.
  QVERIFY(!read.contains(QStringLiteral("planted/Deskflow.pem")));
  QVERIFY(std::none_of(read.cbegin(), read.cend(), [](const QString &p) { return p.contains(kAsideMarker); }));

  // What cannot be read, such as a link, is not trusted either.
  QCOMPARE(untrustedEntries(root, [](const QString &) { return std::optional<Security>(); }).size(), 3);
}

QTEST_MAIN(FileTrustTests)
