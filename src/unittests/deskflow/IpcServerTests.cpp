/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "IpcServerTests.h"

#include "common/VersionInfo.h"
#include "deskflow/ipc/IpcServer.h"

#include <QCoreApplication>
#include <QLocalSocket>

#include <memory>

using deskflow::core::ipc::IpcServer;

namespace {

class TestIpcServer : public IpcServer
{
public:
  explicit TestIpcServer(const QString &name)
      : IpcServer(nullptr, name, QStringLiteral("test"), QLocalServer::UserAccessOption)
  {
  }

  QList<QStringList> m_commands;
  QStringList m_versionMismatchCommands;

private:
  void processCommand(QLocalSocket *, const QString &, const QStringList &parts) override
  {
    m_commands.append(parts);
  }

  QStringList versionMismatchCommands() const override
  {
    return m_versionMismatchCommands;
  }
};

QString uniqueServerName()
{
  static int counter = 0;
  return QStringLiteral("deskflow-ipc-test-%1-%2").arg(QCoreApplication::applicationPid()).arg(++counter);
}

std::unique_ptr<TestIpcServer> startServer(const QString &name)
{
  auto server = std::make_unique<TestIpcServer>(name);
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
    return lines.size() >= count;
  });
  return lines;
}

QString helloMessage()
{
  return QStringLiteral("hello=%1+%2").arg(kVersion, kVersionGitSha);
}

// A round trip guarantees the server has registered the client.
bool connectWithoutHello(QLocalSocket &client, const QString &name)
{
  client.connectToServer(name);
  if (!client.waitForConnected(1000)) {
    return false;
  }
  client.write("noop\n");
  return readLines(client, 1) == QStringList{QStringLiteral("ok")};
}

bool connectClient(QLocalSocket &client, const QString &name)
{
  if (!connectWithoutHello(client, name)) {
    return false;
  }
  client.write(helloMessage().toUtf8() + '\n');
  return readLines(client, 1) == QStringList{helloMessage()};
}

} // namespace

void IpcServerTests::argsKeepEquals()
{
  const auto name = uniqueServerName();
  const auto server = startServer(name);
  QLocalSocket client;
  QVERIFY(connectClient(client, name));

  client.write("custom=a=b\n");
  QTRY_COMPARE(server->m_commands.size(), 1);
  QCOMPARE(server->m_commands.at(0), (QStringList{"custom", "a=b"}));
}

void IpcServerTests::partialLineIsBuffered()
{
  const auto name = uniqueServerName();
  const auto server = startServer(name);
  QLocalSocket client;
  QVERIFY(connectClient(client, name));

  client.write("cus");
  client.flush();
  QTest::qWait(100);
  QVERIFY(server->m_commands.isEmpty());

  client.write("tom=x\n");
  QTRY_COMPARE(server->m_commands.size(), 1);
  QCOMPARE(server->m_commands.at(0), (QStringList{"custom", "x"}));
}

void IpcServerTests::oversizedLineDisconnects()
{
  const auto name = uniqueServerName();
  const auto server = startServer(name);
  QLocalSocket client;
  QVERIFY(connectClient(client, name));

  client.write(QByteArray(128 * 1024, 'x'));
  QTRY_COMPARE(client.state(), QLocalSocket::UnconnectedState);
  QVERIFY(server->m_commands.isEmpty());
}

void IpcServerTests::lineBreakInArgsIsNotSent()
{
  const auto name = uniqueServerName();
  const auto server = startServer(name);
  QLocalSocket client;
  QVERIFY(connectClient(client, name));

  server->broadcastCommand(QStringLiteral("unrecognisedClient"), QStringLiteral("evil\nconnectionState=Connected"));
  server->broadcastCommand(QStringLiteral("unrecognisedClient"), QStringLiteral("evil\rconnectionState=Connected"));
  server->broadcastCommand(QStringLiteral("connectionState"), QStringLiteral("Listening"));

  QCOMPARE(readLines(client, 1), QStringList{QStringLiteral("connectionState=Listening")});
}

void IpcServerTests::trailingLineBreakIsTrimmed()
{
  const auto name = uniqueServerName();
  const auto server = startServer(name);
  QLocalSocket client;
  QVERIFY(connectClient(client, name));

  server->broadcastCommand(QStringLiteral("secureSocket"), QStringLiteral("TLSv1.3\r\n"));

  QCOMPARE(readLines(client, 1), QStringList{QStringLiteral("secureSocket=TLSv1.3")});
}

void IpcServerTests::lineBreakDoesNotEvictPendingState()
{
  const auto name = uniqueServerName();
  const auto server = startServer(name);

  server->broadcastCommand(QStringLiteral("connectedClients"), QStringLiteral("laptop"));
  server->broadcastCommand(QStringLiteral("connectedClients"), QStringLiteral("evil\nconnectionState=Connected"));

  QLocalSocket client;
  client.connectToServer(name);
  QVERIFY(client.waitForConnected(1000));
  client.write(helloMessage().toUtf8() + '\n');

  const QStringList expected = {helloMessage(), QStringLiteral("connectedClients=laptop")};
  auto lines = readLines(client, expected.size());
  QTest::qWait(100);
  lines += readLines(client, 0);
  QCOMPARE(lines, expected);
}

void IpcServerTests::pendingStateKeepsLatest()
{
  const auto name = uniqueServerName();
  const auto server = startServer(name);

  server->broadcastCommand(QStringLiteral("connectionState"), QStringLiteral("Connecting"));
  server->broadcastCommand(QStringLiteral("unrecognisedClient"), QStringLiteral("laptop"));
  server->broadcastCommand(QStringLiteral("connectionState"), QStringLiteral("Connected"));
  server->broadcastCommand(QStringLiteral("unrecognisedClient"), QStringLiteral("laptop"));
  server->broadcastCommand(QStringLiteral("unrecognisedClient"), QStringLiteral("desktop"));

  QLocalSocket client;
  client.connectToServer(name);
  QVERIFY(client.waitForConnected(1000));
  client.write(helloMessage().toUtf8() + '\n');

  const QStringList expected = {
      helloMessage(), QStringLiteral("connectionState=Connected"), QStringLiteral("unrecognisedClient=laptop"),
      QStringLiteral("unrecognisedClient=desktop")
  };
  auto lines = readLines(client, expected.size());
  QTest::qWait(100);
  lines += readLines(client, 0);
  QCOMPARE(lines, expected);
}

void IpcServerTests::pendingMessagesAreCapped()
{
  const auto name = uniqueServerName();
  const auto server = startServer(name);

  for (int i = 0; i < 150; ++i) {
    server->broadcastCommand(QStringLiteral("event"), QString::number(i));
  }

  QLocalSocket client;
  client.connectToServer(name);
  QVERIFY(client.waitForConnected(1000));
  client.write(helloMessage().toUtf8() + '\n');

  auto lines = readLines(client, 101);
  QTest::qWait(100);
  lines += readLines(client, 0);
  QCOMPARE(lines.size(), 101);
  QCOMPARE(lines.at(1), QStringLiteral("event=50"));
  QCOMPARE(lines.last(), QStringLiteral("event=149"));
}

void IpcServerTests::commandBeforeHelloIsRefused()
{
  const auto name = uniqueServerName();
  const auto server = startServer(name);
  QLocalSocket client;
  QVERIFY(connectWithoutHello(client, name));

  client.write("custom=early\n");
  QCOMPARE(readLines(client, 1), QStringList{QStringLiteral("error=hello required")});

  client.write(helloMessage().toUtf8() + "\ncustom=late\n");
  QTRY_COMPARE(server->m_commands.size(), 1);
  QCOMPARE(server->m_commands.at(0), (QStringList{"custom", "late"}));
}

void IpcServerTests::versionMismatchDisconnects()
{
  const auto name = uniqueServerName();
  const auto server = startServer(name);
  QLocalSocket client;
  QVERIFY(connectWithoutHello(client, name));

  client.write("hello=0.0.0+other\ncustom=x\n");
  const auto lines = readLines(client, 1);
  QVERIFY(!lines.isEmpty());
  QVERIFY(lines.at(0).startsWith(QStringLiteral("versionMismatch=")));
  QTRY_COMPARE(client.state(), QLocalSocket::UnconnectedState);
  QVERIFY(server->m_commands.isEmpty());
}

void IpcServerTests::versionMismatchKeepsAllowedCommands()
{
  const auto name = uniqueServerName();
  const auto server = startServer(name);
  server->m_versionMismatchCommands = {QStringLiteral("stop")};
  QLocalSocket client;
  QVERIFY(connectWithoutHello(client, name));

  client.write("hello=0.0.0+other\ncustom=x\nstop\n");
  const auto lines = readLines(client, 2);
  QCOMPARE(lines.size(), 2);
  QVERIFY(lines.at(0).startsWith(QStringLiteral("versionMismatch=")));
  QCOMPARE(lines.at(1), QStringLiteral("error=hello required"));
  QTRY_COMPARE(server->m_commands.size(), 1);
  QCOMPARE(server->m_commands.at(0), QStringList{QStringLiteral("stop")});
  QCOMPARE(client.state(), QLocalSocket::ConnectedState);

  // That GUI waits for this before it starts its own core.
  server->broadcastCommand(QStringLiteral("bye"));
  QCOMPARE(readLines(client, 1), QStringList{QStringLiteral("bye")});
}

void IpcServerTests::broadcastSkipsClientsWithoutHello()
{
  const auto name = uniqueServerName();
  const auto server = startServer(name);
  QLocalSocket ready;
  QVERIFY(connectClient(ready, name));
  QLocalSocket pending;
  QVERIFY(connectWithoutHello(pending, name));

  server->broadcastCommand(QStringLiteral("connectionState"), QStringLiteral("Listening"));
  QCOMPARE(readLines(ready, 1), QStringList{QStringLiteral("connectionState=Listening")});

  // Anything broadcast to it would have arrived before the reply to its hello.
  pending.write(helloMessage().toUtf8() + '\n');
  QCOMPARE(readLines(pending, 1), QStringList{helloMessage()});
}

QTEST_MAIN(IpcServerTests)
