/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "IpcUtilsTests.h"

#include "common/Constants.h"
#include "common/IpcUtils.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

#if !defined(Q_OS_WIN)
#include <unistd.h>
#endif

void IpcUtilsTests::splitMessage_data()
{
  QTest::addColumn<QString>("message");
  QTest::addColumn<QStringList>("parts");

  QTest::newRow("command only") << "stop" << QStringList{"stop"};
  QTest::newRow("command and args") << "logLevel=DEBUG" << QStringList{"logLevel", "DEBUG"};
  QTest::newRow("args containing =") << "configFile=C:/a=b/c.conf" << QStringList{"configFile", "C:/a=b/c.conf"};
  QTest::newRow("empty args") << "connectedClients=" << QStringList{"connectedClients", ""};
}

void IpcUtilsTests::splitMessage()
{
  QFETCH(QString, message);
  QFETCH(QStringList, parts);
  QCOMPARE(deskflow::ipc::splitMessage(message), parts);
}

void IpcUtilsTests::coreServerNameIsPrivate()
{
#if defined(Q_OS_WIN)
  QSKIP("Windows uses a named pipe with access checks instead of a private directory");
#else
  const QFileInfo name(deskflow::ipc::coreServerName());
  QVERIFY(name.isAbsolute());
  QCOMPARE(name.fileName(), QString::fromLatin1(kCoreIpcName));

  const QFileInfo dir(name.absolutePath());
  QVERIFY(dir.isDir());
  QCOMPARE(dir.ownerId(), static_cast<uint>(getuid()));
  QCOMPARE(dir.permissions() & (QFileDevice::WriteGroup | QFileDevice::WriteOther), QFileDevice::Permissions{});
#endif
}

void IpcUtilsTests::coreServerNameFitsSocketPath()
{
#if !defined(Q_OS_LINUX)
  QSKIP("only Linux lets the runtime dir be overridden");
#else
  QTemporaryDir temp;
  const auto longDir = QStringLiteral("%1/%2").arg(temp.path(), QString(100, 'x'));
  QVERIFY(QDir().mkpath(longDir));
  QVERIFY(QFile::setPermissions(longDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

  const auto oldRuntimeDir = qgetenv("XDG_RUNTIME_DIR");
  qputenv("XDG_RUNTIME_DIR", longDir.toUtf8());
  const auto name = deskflow::ipc::coreServerName();
  if (oldRuntimeDir.isNull()) {
    qunsetenv("XDG_RUNTIME_DIR");
  } else {
    qputenv("XDG_RUNTIME_DIR", oldRuntimeDir);
  }

  QVERIFY(!name.isEmpty());
  QVERIFY(name.toUtf8().size() < 104);
#endif
}

QTEST_MAIN(IpcUtilsTests)
