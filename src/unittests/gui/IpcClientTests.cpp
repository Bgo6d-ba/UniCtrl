/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "IpcClientTests.h"

#include "gui/ipc/CoreIpcClient.h"
#include "gui/ipc/IpcClient.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>

using deskflow::gui::ipc::IpcClient;

namespace {

class TestIpcClient : public IpcClient
{
public:
  TestIpcClient(const QString &name, bool trusted)
      : IpcClient(nullptr, name, QStringLiteral("test")),
        m_trusted(trusted)
  {
  }

  QStringList commands;

protected:
  void processCommand(const QString &command, const QStringList &) override
  {
    commands.append(command);
  }

private:
  bool isServerTrusted(const QLocalSocket &) const override
  {
    return m_trusted;
  }

  bool m_trusted;
};

// Returns what the client sent before the server gave up waiting.
QByteArray receivedFrom(bool trusted, int &failures)
{
  QLocalServer server;
  const auto name = QStringLiteral("deskflow-ipc-client-test-%1").arg(QCoreApplication::applicationPid());
  QLocalServer::removeServer(name);
  if (!server.listen(name)) {
    return "listen failed";
  }

  TestIpcClient client(name, trusted);
  QSignalSpy failed(&client, &IpcClient::connectionFailed);
  client.connectToServer();

  QByteArray data;
  if (server.waitForNewConnection(1000)) {
    auto *socket = server.nextPendingConnection();
    (void)QTest::qWaitFor([&] {
      data += socket->readAll();
      return data.contains('\n') || failed.count() > 0;
    });
  }
  failures = static_cast<int>(failed.count());
  return data;
}

} // namespace

void IpcClientTests::trustedServer_getsHello()
{
  int failures = 0;
  QVERIFY(receivedFrom(true, failures).startsWith("hello="));
  QCOMPARE(failures, 0);
}

void IpcClientTests::untrustedServer_refusedBeforeHello()
{
  int failures = 0;
  QCOMPARE(receivedFrom(false, failures), QByteArray());
  QCOMPARE(failures, 1);
}

void IpcClientTests::failedAttempts_sendOneHello()
{
  const auto name = QStringLiteral("deskflow-ipc-client-retry-test-%1").arg(QCoreApplication::applicationPid());
  QLocalServer::removeServer(name);
  TestIpcClient client(name, true);
  QSignalSpy failed(&client, &IpcClient::connectionFailed);

  // No server yet, so every attempt fails.
  client.connectToServer();
  QTRY_COMPARE(failed.count(), 1);

  QLocalServer server;
  QVERIFY(server.listen(name));
  client.connectToServer();
  QVERIFY(server.waitForNewConnection(1000));
  auto *socket = server.nextPendingConnection();

  QByteArray data;
  QVERIFY(QTest::qWaitFor([&] {
    data += socket->readAll();
    return data.contains('\n');
  }));
  QTest::qWait(100);
  data += socket->readAll();
  QCOMPARE(data.count("hello="), 1);
}

void IpcClientTests::refusedHandshake_dropsRest()
{
  const auto name = QStringLiteral("deskflow-ipc-client-refused-test-%1").arg(QCoreApplication::applicationPid());
  QLocalServer::removeServer(name);
  QLocalServer server;
  QVERIFY(server.listen(name));
  TestIpcClient client(name, true);
  QSignalSpy failed(&client, &IpcClient::connectionFailed);
  client.connectToServer();
  QVERIFY(server.waitForNewConnection(1000));
  auto *socket = server.nextPendingConnection();
  // The server answers only once the client said hello.
  QVERIFY(QTest::qWaitFor([&] { return socket->readAll().contains("hello="); }));

  socket->write("error=refused\nconnectionState=connected\n");
  socket->flush();
  QTRY_COMPARE(failed.count(), 1);
  QTest::qWait(100);
  QCOMPARE(client.commands, QStringList());
}

void IpcClientTests::coreServer_onlyOwnSessionAndOwner()
{
  using deskflow::gui::ipc::CoreIpcClient;
  const auto me = QStringLiteral("S-1-5-21-1-2-3-1001");
  const auto other = QStringLiteral("S-1-5-21-1-2-3-1002");
  QVERIFY(CoreIpcClient::isTrustedServer(1, me, 1, me));
  QVERIFY(CoreIpcClient::isTrustedServer(1, QStringLiteral("S-1-5-18"), 1, me));

  // A pipe another session's user created or squatted.
  QVERIFY(!CoreIpcClient::isTrustedServer(2, me, 1, me));
  QVERIFY(!CoreIpcClient::isTrustedServer(2, QStringLiteral("S-1-5-18"), 1, me));
  QVERIFY(!CoreIpcClient::isTrustedServer(1, other, 1, me));
  QVERIFY(!CoreIpcClient::isTrustedServer(1, QStringLiteral("S-1-5-32-544"), 1, me));
  QVERIFY(!CoreIpcClient::isTrustedServer(1, QString(), 1, QString()));
}

QTEST_MAIN(IpcClientTests)
