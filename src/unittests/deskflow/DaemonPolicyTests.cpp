/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "DaemonPolicyTests.h"

#include "../TestSettings.h"

#include "common/Settings.h"
#include "common/VersionInfo.h"
#include "deskflow/ipc/DaemonIpcServer.h"
#include "deskflow/ipc/DaemonPolicy.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalSocket>
#include <QSettings>

#include <filesystem>
#include <memory>

using namespace deskflow::core::ipc;

namespace {

const auto kUserSid = QStringLiteral("S-1-5-21-1-2-3-1001");
const IpcCaller kUser{false, false, 1, kUserSid};
const IpcCaller kFilteredAdmin{false, true, 1, kUserSid};
const IpcCaller kAdmin{true, false, 1, kUserSid};

class TestDaemonIpcServer : public DaemonIpcServer
{
public:
  explicit TestDaemonIpcServer(const QString &name)
      : DaemonIpcServer(nullptr, QStringLiteral("daemon.log"), name),
        m_name(name)
  {
    connect(this, &IpcServer::configChanged, this, [this](const DaemonConfig &config) { m_configs.append(config); });
    connect(this, &IpcServer::startProcessRequested, this, [this](const IpcCaller &caller) {
      m_starters.append(caller);
    });
  }

  const QString m_name;
  std::optional<IpcCaller> m_caller;
  quint32 m_consoleSession = 1;
  QList<DaemonConfig> m_configs;
  QList<IpcCaller> m_starters;

protected:
  std::optional<IpcCaller> callerOf(const QLocalSocket *) const override
  {
    return m_caller;
  }

  quint32 consoleSessionId() const override
  {
    return m_consoleSession;
  }
};

std::unique_ptr<TestDaemonIpcServer> startServer(const std::optional<IpcCaller> &caller)
{
  static int counter = 0;
  auto server = std::make_unique<TestDaemonIpcServer>(
      QStringLiteral("deskflow-daemon-test-%1-%2").arg(QCoreApplication::applicationPid()).arg(++counter)
  );
  server->m_caller = caller;
  server->listen();
  return server;
}

QStringList readLines(QLocalSocket &socket, qsizetype count)
{
  QStringList lines;
  (void)QTest::qWaitFor([&] {
    while (socket.canReadLine()) {
      lines.append(QString::fromUtf8(socket.readLine()).chopped(1));
    }
    return lines.size() >= count || socket.state() == QLocalSocket::UnconnectedState;
  });
  return lines;
}

// Connects and says hello; false when the server refused the client.
bool handshake(QLocalSocket &client, const TestDaemonIpcServer &server)
{
  const auto hello = QStringLiteral("hello=%1+%2").arg(kVersion, kVersionGitSha);
  client.connectToServer(server.m_name);
  if (!client.waitForConnected(1000)) {
    return false;
  }
  client.write(hello.toUtf8() + '\n');
  return readLines(client, 1) == QStringList{hello};
}

// Sends one command and returns the reply.
QString send(QLocalSocket &client, const QString &message)
{
  client.write(message.toUtf8() + '\n');
  return readLines(client, 1).value(0);
}

} // namespace

QString DaemonPolicyTests::writeConfig(const QString &name, int coreMode, bool elevate) const
{
  const auto file = QStringLiteral("%1/%2").arg(m_dir.path(), name);
  QSettings settings(file, QSettings::IniFormat);
  settings.setValue(Settings::Core::CoreMode, coreMode);
  settings.setValue(Settings::Daemon::Elevate, elevate);
  settings.sync();
  return file;
}

void DaemonPolicyTests::initTestCase()
{
  QVERIFY(m_settingsDir.isValid());
  useTestSettings(m_settingsDir);
}

void DaemonPolicyTests::remoteAndOddPathsAreRefused()
{
  QVERIFY(policy::isAcceptableConfigPath(QStringLiteral("%1/Deskflow.conf").arg(QDir::tempPath())));
  QVERIFY(!policy::isAcceptableConfigPath(QString()));
  QVERIFY(!policy::isAcceptableConfigPath(QStringLiteral("relative/Deskflow.conf")));
  QVERIFY(!policy::isAcceptableConfigPath(QStringLiteral("//attacker/share/Deskflow.conf")));
  QVERIFY(!policy::isAcceptableConfigPath(QStringLiteral("/?\?/UNC/attacker/share/Deskflow.conf")));
  QVERIFY(!policy::isAcceptableConfigPath(QStringLiteral("%1/a\" --log \"x").arg(QDir::tempPath())));
#if defined(Q_OS_WIN)
  QVERIFY(policy::isAcceptableConfigPath(QStringLiteral("C:\\Users\\me\\Deskflow.conf")));
  QVERIFY(!policy::isAcceptableConfigPath(QStringLiteral("\\\\attacker\\share\\Deskflow.conf")));
  QVERIFY(!policy::isAcceptableConfigPath(QStringLiteral("\\\\?\\UNC\\attacker\\share\\Deskflow.conf")));
  QVERIFY(!policy::isAcceptableConfigPath(QStringLiteral("\\??\\UNC\\attacker\\share\\Deskflow.conf")));
#endif
}

void DaemonPolicyTests::configFromNonAdminIsBoundToOwner()
{
  const auto file = QStringLiteral("%1/Deskflow.conf").arg(QDir::tempPath());
  const auto config = policy::configFromCaller(file, kUser);
  QVERIFY(config.has_value());
  QCOMPARE(config->file, file);
  QVERIFY(!config->fromAdmin);
  QCOMPARE(config->ownerSid, kUserSid);

  const auto adminConfig = policy::configFromCaller(file, kAdmin);
  QVERIFY(adminConfig.has_value());
  QVERIFY(adminConfig->fromAdmin);
  QVERIFY(adminConfig->ownerSid.isEmpty());

  QVERIFY(!policy::configFromCaller(QStringLiteral("//attacker/share/Deskflow.conf"), kAdmin).has_value());
}

void DaemonPolicyTests::configFromFilteredAdminIsNotAdmin()
{
  const auto config =
      policy::configFromCaller(QStringLiteral("%1/Deskflow.conf").arg(QDir::tempPath()), kFilteredAdmin);
  QVERIFY(config.has_value());
  QVERIFY(!config->fromAdmin);
  QCOMPARE(config->ownerSid, kUserSid);
}

void DaemonPolicyTests::onlyAdminConfigStartsAtBoot()
{
  QVERIFY(!policy::bootConfig(QStringLiteral("/x/Deskflow.conf"), false).has_value());
  QVERIFY(!policy::bootConfig(QString(), true).has_value());
  const auto config = policy::bootConfig(QStringLiteral("/x/Deskflow.conf"), true);
  QVERIFY(config.has_value());
  QVERIFY(config->fromAdmin);
}

void DaemonPolicyTests::legacyAdminFlagIsIgnored()
{
  // Earlier versions also set it for admins whose token UAC filtered, which must not elevate at boot.
  const auto original = Settings::settingsFile();
  // Written as text, as an earlier version left it on disk.
  const auto file = m_settingsDir.filePath(QStringLiteral("legacy.conf"));
  QFile legacy(file);
  QVERIFY(legacy.open(QFile::WriteOnly));
  legacy.write("[daemon]\nconfigFile=/x/Deskflow.conf\nconfigFromAdmin=true\n");
  legacy.close();
  Settings::setSettingsFile(file);
  const auto fromAdmin = Settings::value(Settings::Daemon::ConfigFromAdmin).toBool();
  const auto config = Settings::value(Settings::Daemon::ConfigFile).toString();
  Settings::setSettingsFile(original);

  QCOMPARE(config, QStringLiteral("/x/Deskflow.conf"));
  QVERIFY(!fromAdmin);
  QVERIFY(!policy::bootConfig(config, fromAdmin).has_value());
}

void DaemonPolicyTests::insideResolvesPaths()
{
  const auto dir = m_dir.path();
  QVERIFY(policy::isInside(dir, dir));
  QVERIFY(policy::isInside(QStringLiteral("%1/Deskflow.conf").arg(dir), dir));
  QVERIFY(policy::isInside(QStringLiteral("%1/tls/../Deskflow.conf").arg(dir), dir));
  QVERIFY(!policy::isInside(QStringLiteral("%1/../Deskflow.conf").arg(dir), dir));
  QVERIFY(!policy::isInside(QStringLiteral("%1-other/Deskflow.conf").arg(dir), dir));
  QVERIFY(!policy::isInside(QStringLiteral("%1/Deskflow.conf").arg(dir), QString()));

  // A link elsewhere that leads into the folder; not QFile::link, which makes a shortcut file on Windows.
  const auto link = QStringLiteral("%1/link").arg(m_settingsDir.path());
  std::error_code error;
  std::filesystem::create_directory_symlink(
      QDir(dir).filesystemAbsolutePath(), QFileInfo(link).filesystemFilePath(), error
  );
  if (error) {
    QSKIP("Creating a symbolic link needs a privilege this account lacks");
  }
  QVERIFY(policy::isInside(QStringLiteral("%1/Deskflow.conf").arg(link), dir));
}

void DaemonPolicyTests::logLeavesUntrustedDir()
{
  const auto log = QStringLiteral("%1/Deskflow-daemon.log").arg(m_dir.path());
  QCOMPARE(policy::daemonLogFile(log, QString(), QStringLiteral("/fallback")), log);
  QCOMPARE(policy::daemonLogFile(log, m_dir.path(), QStringLiteral("/fallback")), "/fallback/Deskflow-daemon.log");
  QCOMPARE(policy::daemonLogFile(log, m_settingsDir.path(), QStringLiteral("/fallback")), log);
}

void DaemonPolicyTests::planRefusesUntrustedDir()
{
  const auto core = QStringLiteral("/opt/unictrl-core");
  const auto file = writeConfig(QStringLiteral("deployed.conf"), Settings::CoreMode::Server, true);
  QVERIFY(policy::planCoreStart({file, true, {}}, std::nullopt, core, m_settingsDir.path()).error.isEmpty());
  const auto plan = policy::planCoreStart({file, true, {}}, std::nullopt, core, m_dir.path());
  QVERIFY(plan.error.contains(QStringLiteral("users may have written")));
  QVERIFY(plan.command.empty());
}

void DaemonPolicyTests::coreAcceptsOwnSessionOrAdmin()
{
  QVERIFY(policy::coreAcceptsConnection(kUser, 1));
  QVERIFY(!policy::coreAcceptsConnection(kUser, 2));
  QVERIFY(!policy::coreAcceptsConnection(kFilteredAdmin, 2));
  QVERIFY(policy::coreAcceptsConnection(kAdmin, 2));
}

void DaemonPolicyTests::planBuildsWideCommand()
{
  // A Cyrillic profile name, as the settings path of many users here has.
  const auto profile = QString::fromUtf16(u"\u0411\u0430\u0442\u043c\u04e9\u043d\u0445");
  QVERIFY(QDir(m_dir.path()).mkpath(profile));
  const auto file = writeConfig(QStringLiteral("%1/Deskflow.conf").arg(profile), Settings::CoreMode::Client, false);

  const auto plan = policy::planCoreStart({file, true, {}}, kAdmin, QStringLiteral("/opt/unictrl-core"));
  QVERIFY2(plan.error.isEmpty(), qPrintable(plan.error));
  QVERIFY(plan.command.starts_with(L"\"/opt/unictrl-core\" client --settings \""));
  QVERIFY(plan.command.find(L"\u0411\u0430\u0442\u043c\u04e9\u043d\u0445/Deskflow.conf\"") != std::wstring::npos);
}

void DaemonPolicyTests::planRefusesBadConfigs()
{
  const auto core = QStringLiteral("/opt/unictrl-core");
  const auto missing = QStringLiteral("%1/missing.conf").arg(m_dir.path());
  QVERIFY(!policy::planCoreStart({missing, true, {}}, kAdmin, core).error.isEmpty());

  const auto remote = QStringLiteral("//attacker/share/Deskflow.conf");
  QVERIFY(!policy::planCoreStart({remote, true, {}}, kAdmin, core).error.isEmpty());

  const auto file = writeConfig(QStringLiteral("server.conf"), Settings::CoreMode::Server, false);
  QVERIFY(!policy::planCoreStart({file, false, {}}, kUser, core).error.isEmpty());

  const auto noMode = writeConfig(QStringLiteral("none.conf"), Settings::CoreMode::None, false);
  QVERIFY(!policy::planCoreStart({noMode, true, {}}, kAdmin, core).error.isEmpty());
}

void DaemonPolicyTests::planElevatesOnlyForElevatedAdmins()
{
  const auto core = QStringLiteral("/opt/unictrl-core");
  const auto file = writeConfig(QStringLiteral("elevated.conf"), Settings::CoreMode::Server, true);

  const auto admin = policy::planCoreStart({file, true, {}}, kAdmin, core);
  QVERIFY(admin.elevate);
  QVERIFY(admin.elevationRefused.isEmpty());

  const auto boot = policy::planCoreStart({file, true, {}}, std::nullopt, core);
  QVERIFY(boot.elevate);

  // A non-elevated process of an admin must not get a SYSTEM core without a UAC prompt.
  const auto filtered = policy::planCoreStart({file, true, {}}, kFilteredAdmin, core);
  QVERIFY(filtered.error.isEmpty());
  QVERIFY(!filtered.elevate);
  QVERIFY(filtered.elevationRefused.contains(QStringLiteral("administrator")));

  const auto userConfig = policy::planCoreStart({file, false, kUserSid}, kAdmin, core);
  QVERIFY(!userConfig.elevate);
  QVERIFY(!userConfig.elevationRefused.isEmpty());

  // Elevate defaults to on, which a standard user can neither use nor change.
  const auto user = policy::planCoreStart({file, false, kUserSid}, kUser, core);
  QVERIFY(!user.elevate);
  QVERIFY(user.error.isEmpty());
  QVERIFY(user.elevationRefused.isEmpty());
  QVERIFY(policy::planCoreStart({file, false, kUserSid}, std::nullopt, core).elevationRefused.isEmpty());

  const auto notWanted = writeConfig(QStringLiteral("plain.conf"), Settings::CoreMode::Server, false);
  const auto plain = policy::planCoreStart({notWanted, false, kUserSid}, kUser, core);
  QVERIFY(!plain.elevate);
  QVERIFY(plain.elevationRefused.isEmpty());
}

void DaemonPolicyTests::serverRefusesOtherSessions()
{
  // Another session's user, an admin whose token UAC filtered, and a user the OS could not name.
  for (const auto &caller :
       {IpcCaller{false, false, 2, kUserSid}, IpcCaller{false, true, 2, kUserSid},
        IpcCaller{false, false, 1, QString()}}) {
    const auto server = startServer(caller);
    QLocalSocket client;
    QVERIFY(!handshake(client, *server));
    QTRY_COMPARE(client.state(), QLocalSocket::UnconnectedState);
  }

  const auto unknown = startServer(std::nullopt);
  QLocalSocket client;
  QVERIFY(!handshake(client, *unknown));
}

void DaemonPolicyTests::serverAcceptsAdminFromAnySession()
{
  const auto server = startServer(IpcCaller{true, false, 2, kUserSid});
  QLocalSocket client;
  QVERIFY(handshake(client, *server));
  QCOMPARE(send(client, QStringLiteral("stop")), QStringLiteral("ok"));
}

void DaemonPolicyTests::serverRefusesCommandsAfterConsoleChange()
{
  const auto server = startServer(kUser);
  QLocalSocket client;
  QVERIFY(handshake(client, *server));
  QCOMPARE(send(client, QStringLiteral("logPath")), QStringLiteral("logPath=daemon.log"));

  server->m_consoleSession = 2;
  QCOMPARE(send(client, QStringLiteral("start")), QStringLiteral("error"));
  QVERIFY(server->m_starters.isEmpty());
}

void DaemonPolicyTests::serverBindsConfigToOwner()
{
  const auto file = QStringLiteral("%1/Deskflow.conf").arg(QDir::tempPath());
  const auto server = startServer(kFilteredAdmin);
  QLocalSocket client;
  QVERIFY(handshake(client, *server));
  QCOMPARE(send(client, QStringLiteral("configFile=%1").arg(file)), QStringLiteral("ok"));

  QCOMPARE(server->m_configs.size(), 1);
  QCOMPARE(server->m_configs.at(0).file, file);
  QVERIFY(!server->m_configs.at(0).fromAdmin);
  QCOMPARE(server->m_configs.at(0).ownerSid, kUserSid);
}

void DaemonPolicyTests::serverRefusesRemoteConfig()
{
  const auto server = startServer(kAdmin);
  QLocalSocket client;
  QVERIFY(handshake(client, *server));
  QCOMPARE(send(client, QStringLiteral("configFile=//attacker/share/Deskflow.conf")), QStringLiteral("error"));
  QCOMPARE(send(client, QStringLiteral("configFile")), QStringLiteral("error"));
  QVERIFY(server->m_configs.isEmpty());
}

void DaemonPolicyTests::serverPassesStarter()
{
  const auto server = startServer(kFilteredAdmin);
  QLocalSocket client;
  QVERIFY(handshake(client, *server));
  QCOMPARE(send(client, QStringLiteral("start")), QStringLiteral("ok"));

  QCOMPARE(server->m_starters.size(), 1);
  QVERIFY(!server->m_starters.at(0).isAdmin);
  QVERIFY(server->m_starters.at(0).isFilteredAdmin);
  QCOMPARE(server->m_starters.at(0).userSid, kUserSid);
}

void DaemonPolicyTests::serverRepliesOnlyToStarter()
{
  const auto server = startServer(kAdmin);
  QObject::connect(server.get(), &IpcServer::startProcessRequested, server.get(), [&server] {
    server->replyToStarter(QStringLiteral("elevationRefused"), QStringLiteral("why"));
  });
  QLocalSocket starter;
  QLocalSocket other;
  QVERIFY(handshake(starter, *server));
  QVERIFY(handshake(other, *server));

  starter.write("start\n");
  QCOMPARE(readLines(starter, 2), QStringList({QStringLiteral("elevationRefused=why"), QStringLiteral("ok")}));
  QTest::qWait(100);
  QVERIFY(!other.canReadLine());

  // Outside a start there is no one to reply to.
  server->replyToStarter(QStringLiteral("elevationRefused"), QStringLiteral("why"));
  QTest::qWait(100);
  QVERIFY(!starter.canReadLine());
}

void DaemonPolicyTests::serverRejectsUnknownCommand()
{
  const auto server = startServer(kUser);
  QLocalSocket client;
  QVERIFY(handshake(client, *server));
  QCOMPARE(send(client, QStringLiteral("elevate")), QStringLiteral("error"));
  QCOMPARE(send(client, QStringLiteral("logLevel")), QStringLiteral("error"));
}

QTEST_MAIN(DaemonPolicyTests)
