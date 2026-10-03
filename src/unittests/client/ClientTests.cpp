/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ClientTests.h"

#include "../StubScreen.h"
#include "../TestSettings.h"
#include "base/EventQueue.h"
#include "client/Client.h"
#include "common/Settings.h"
#include "deskflow/Screen.h"
#include "net/NetworkAddress.h"
#include "net/SocketMultiplexer.h"
#include "net/TCPSocketFactory.h"

#include <QElapsedTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

void ClientTests::initTestCase()
{
  m_arch.init();
  QVERIFY(m_dir.isValid());
  useTestSettings(m_dir);
  Settings::setValue(Settings::Security::TlsEnabled, false);
}

void ClientTests::oversizePacket_disconnectsWithError()
{
  QTcpServer server;
  QVERIFY(server.listen(QHostAddress::LocalHost, 0));
  NetworkAddress address("127.0.0.1", server.serverPort());
  address.resolve();

  EventQueue events;
  SocketMultiplexer multiplexer;
  deskflow::Screen screen{new StubScreen(&events), &events};
  Client client(&events, "client", address, new TCPSocketFactory(&events, &multiplexer), &screen);

  QString failure;
  events.addHandler(EventTypes::ClientConnectionFailed, client.getEventTarget(), [&](const Event &event) {
    failure = static_cast<const Client::FailInfo *>(event.getDataObject())->m_what;
    events.addEvent(Event(EventTypes::Quit));
  });

  // the server answers with a length prefix far over the protocol limit
  QTcpSocket *peer = nullptr;
  QElapsedTimer elapsed;
  elapsed.start();
  auto *timer = events.newTimer(0.01, nullptr);
  events.addHandler(EventTypes::Timer, timer, [&](const Event &) {
    if (peer == nullptr && server.waitForNewConnection(0)) {
      peer = server.nextPendingConnection();
      peer->write(QByteArray(
          "\xff\xff\xff\xff"
          "junk",
          8
      ));
      peer->waitForBytesWritten(1000);
    }
    if (elapsed.elapsed() > 10000) {
      events.addEvent(Event(EventTypes::Quit));
    }
  });

  client.connect();
  events.loop();
  events.removeHandler(EventTypes::Timer, timer);
  events.deleteTimer(timer);
  events.removeHandler(EventTypes::ClientConnectionFailed, client.getEventTarget());

  // without a handler the client sat on the stalled stream until its connect timeout
  QCOMPARE(failure, QStringLiteral("server sent a malformed packet"));
}

QTEST_MAIN(ClientTests)
