/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ClientListenerTests.h"

#include "../StubScreen.h"
#include "../TestSettings.h"
#include "base/EventQueue.h"
#include "common/Settings.h"
#include "deskflow/Screen.h"
#include "net/IDataSocket.h"
#include "net/IListenSocket.h"
#include "net/ISocketFactory.h"
#include "net/NetworkAddress.h"
#include "net/SecureUtils.h"
#include "net/SocketException.h"
#include "net/SocketMultiplexer.h"
#include "net/TCPSocketFactory.h"
#include "server/ClientListener.h"
#include "server/ClientProxy.h"
#include "server/PrimaryClient.h"
#include "server/Server.h"

#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QTest>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>

namespace {

struct Sockets
{
  int live = 0;
  int listens = 0;
  int accepted = 0;
  // binds that throw before the next one succeeds
  int failBinds = 0;
  void *listenTarget = nullptr;
  void *lastAccepted = nullptr;
  // peer address of the next accepted socket; each gets its own when empty
  std::string peer;
};

class FakeDataSocket : public IDataSocket
{
public:
  explicit FakeDataSocket(Sockets &sockets) : IDataSocket(nullptr), m_sockets(sockets)
  {
    ++m_sockets.live;
  }

  ~FakeDataSocket() override
  {
    --m_sockets.live;
  }

  void bind(const NetworkAddress &) override
  {
    if (m_sockets.failBinds > 0) {
      --m_sockets.failBinds;
      throw SocketBindException("injected bind failure");
    }
  }

  void close() override
  {
  }

  void *getEventTarget() const override
  {
    return const_cast<FakeDataSocket *>(this);
  }

  void connect(const NetworkAddress &) override
  {
  }

  bool isFatal() const override
  {
    return false;
  }

  uint32_t read(void *, uint32_t) override
  {
    return 0;
  }

  void write(const void *, uint32_t) override
  {
  }

  void flush() override
  {
  }

  void shutdownInput() override
  {
  }

  void shutdownOutput() override
  {
  }

  bool isReady() const override
  {
    return false;
  }

  uint32_t getSize() const override
  {
    return 0;
  }

private:
  Sockets &m_sockets;
};

class FakeListenSocket : public IListenSocket
{
public:
  explicit FakeListenSocket(Sockets &sockets) : m_sockets(sockets)
  {
    m_sockets.listenTarget = this;
    ++m_sockets.listens;
  }

  void bind(const NetworkAddress &) override
  {
    if (m_sockets.failBinds > 0) {
      --m_sockets.failBinds;
      throw SocketBindException("injected bind failure");
    }
  }

  void close() override
  {
  }

  void *getEventTarget() const override
  {
    return const_cast<FakeListenSocket *>(this);
  }

  std::unique_ptr<IDataSocket> accept() override
  {
    auto socket = std::make_unique<FakeDataSocket>(m_sockets);
    ++m_sockets.accepted;
    socket->setPeerAddress(m_sockets.peer.empty() ? "10.0.0." + std::to_string(m_sockets.accepted) : m_sockets.peer);
    m_sockets.lastAccepted = socket->getEventTarget();
    return socket;
  }

private:
  Sockets &m_sockets;
};

class FakeSocketFactory : public ISocketFactory
{
public:
  explicit FakeSocketFactory(Sockets &sockets) : m_sockets(sockets)
  {
  }

  IDataSocket *create(IArchNetwork::AddressFamily, SecurityLevel) const override
  {
    return new FakeDataSocket(m_sockets);
  }

  IListenSocket *createListen(IArchNetwork::AddressFamily, SecurityLevel) const override
  {
    return new FakeListenSocket(m_sockets);
  }

private:
  Sockets &m_sockets;
};

std::unique_ptr<ClientListener> makeListener(Sockets &sockets, EventQueue &events)
{
  NetworkAddress address("127.0.0.1", 24800);
  address.resolve();
  // TLS level so accepted sockets stay pending until their handshake completes
  return std::make_unique<ClientListener>(
      address, std::make_unique<FakeSocketFactory>(sockets), &events, SecurityLevel::Encrypted
  );
}

// a real server and client listener on a loopback port
struct LiveServer
{
  EventQueue events;
  SocketMultiplexer multiplexer;
  deskflow::server::Config config{&events};
  deskflow::Screen screen{new StubScreen(&events), &events};
  PrimaryClient primary{"server", &screen};
  std::unique_ptr<Server> server;
  std::unique_ptr<ClientListener> listener;
  quint16 port = 0;
  std::atomic_bool accepted = false;
  std::string announced;

  explicit LiveServer(SecurityLevel level, double handshakeTimeout = 10.0)
  {
    config.addScreen("server");
    server = std::make_unique<Server>(config, &primary, &screen, &events);

    for (int attempt = 0; !listener && attempt < 50; ++attempt) {
      port = static_cast<quint16>(QRandomGenerator::global()->bounded(20000, 60000));
      NetworkAddress address("127.0.0.1", port);
      address.resolve();
      try {
        listener = std::make_unique<ClientListener>(
            address, std::make_unique<TCPSocketFactory>(&events, &multiplexer), &events, level, handshakeTimeout
        );
      } catch (const SocketAddressInUseException &) {
        continue;
      }
    }
    if (!listener) {
      qFatal("no free port to listen on");
    }
    listener->setServer(server.get());
    // take the announced client the way ServerApp does
    events.addHandler(EventTypes::ClientListenerAccepted, listener.get(), [this](const Event &) {
      if (std::unique_ptr<ClientProxy> client{listener->getNextClient()}; client) {
        announced = client->getName();
        accepted = true;
      }
    });
  }

  ~LiveServer()
  {
    events.removeHandler(EventTypes::ClientListenerAccepted, listener.get());
    listener.reset();
    server.reset();
  }

  LiveServer(const LiveServer &) = delete;
  LiveServer &operator=(const LiveServer &) = delete;

  bool runUntil(const std::function<bool()> &done, double seconds = 10.0)
  {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    auto *timer = events.newTimer(0.01, nullptr);
    events.addHandler(EventTypes::Timer, timer, [&](const Event &) {
      if (done() || std::chrono::steady_clock::now() > deadline) {
        events.addEvent(Event(EventTypes::Quit));
      }
    });
    events.loop();
    events.removeHandler(EventTypes::Timer, timer);
    events.deleteTimer(timer);
    return done();
  }

  // runs a blocking client on its own thread while this thread drives the server's event queue
  void runPeer(const std::function<void(QTcpSocket &)> &peer)
  {
    std::atomic_bool done = false;
    std::thread thread([&] {
      QTcpSocket socket;
      socket.connectToHost(QHostAddress::LocalHost, port);
      if (socket.waitForConnected(5000)) {
        peer(socket);
      }
      done = true;
    });
    runUntil([&done] { return done.load(); }, 30.0);
    thread.join();
  }
};

bool waitForBytes(QTcpSocket &socket, qint64 n)
{
  while (socket.bytesAvailable() < n) {
    if (!socket.waitForReadyRead(5000)) {
      return false;
    }
  }
  return true;
}

// protocol messages are framed by a 4-byte big-endian length
QByteArray readPacket(QTcpSocket &socket)
{
  if (!waitForBytes(socket, 4)) {
    return {};
  }
  quint32 length = 0;
  QDataStream(socket.read(4)) >> length;
  if (!waitForBytes(socket, length)) {
    return {};
  }
  return socket.read(length);
}

void writePacket(QTcpSocket &socket, const QByteArray &payload)
{
  QByteArray packet;
  QDataStream out(&packet, QIODevice::WriteOnly);
  out << quint32(payload.size());
  socket.write(packet + payload);
  socket.waitForBytesWritten(5000);
}

// replies to the server hello the way a 1.6 client does, returning the server's next message
QByteArray sayHello(QTcpSocket &socket, const QByteArray &name)
{
  const auto hello = readPacket(socket);
  QByteArray reply;
  QDataStream out(&reply, QIODevice::WriteOnly);
  out.writeRawData(hello.constData(), 7);
  out << qint16(1) << qint16(6) << quint32(name.size());
  out.writeRawData(name.constData(), static_cast<int>(name.size()));
  writePacket(socket, reply);
  return readPacket(socket);
}

} // namespace

void ClientListenerTests::initTestCase()
{
  m_arch.init();
  QVERIFY(m_dir.isValid());

  // keep the server away from the user's settings; it also reads the tls certificate from here
  useTestSettings(m_dir);
  QVERIFY(QDir().mkpath(Settings::tlsDir()));
  const auto certPath = QStringLiteral("%1/deskflow.pem").arg(Settings::tlsDir());
  deskflow::generatePemSelfSignedCert(certPath);
  Settings::setValue(Settings::Security::Certificate, certPath);
}

void ClientListenerTests::handshakeTimeout_deletesPendingSocket()
{
  EventQueue events;
  Sockets sockets;
  auto listener = makeListener(sockets, events);

  events.dispatchEvent(Event(EventTypes::ListenSocketConnecting, sockets.listenTarget));
  QCOMPARE(sockets.live, 1);

  events.dispatchEvent(Event(EventTypes::Timer, sockets.lastAccepted));
  QCOMPARE(sockets.live, 0);
}

void ClientListenerTests::pendingLimit_refusesExtraSockets()
{
  EventQueue events;
  Sockets sockets;
  auto listener = makeListener(sockets, events);

  constexpr int cap = ClientListener::s_maxPendingClients;
  for (int i = 0; i < cap + 4; ++i) {
    events.dispatchEvent(Event(EventTypes::ListenSocketConnecting, sockets.listenTarget));
  }
  QCOMPARE(sockets.live, cap);

  listener.reset();
  QCOMPARE(sockets.live, 0);
}

void ClientListenerTests::handshakeDeadline_closesStalledTlsConnection()
{
  LiveServer live(SecurityLevel::Encrypted, 0.2);

  bool closed = false;
  qint64 elapsed = 0;
  live.runPeer([&](QTcpSocket &socket) {
    // never start the TLS handshake
    QElapsedTimer timer;
    timer.start();
    closed = socket.waitForDisconnected(5000);
    elapsed = timer.elapsed();
  });

  QVERIFY(closed);
  QVERIFY(elapsed < 3000);
}

void ClientListenerTests::hello_validName_announcesClient()
{
  LiveServer live(SecurityLevel::PlainText);

  QByteArray query;
  live.runPeer([&](QTcpSocket &socket) {
    query = sayHello(socket, "laptop");
    if (query != "QINF") {
      return;
    }
    QByteArray info("DINF");
    QDataStream out(&info, QIODevice::Append);
    out << qint16(0) << qint16(0) << qint16(1920) << qint16(1080) << qint16(0) << qint16(0) << qint16(0);
    writePacket(socket, info);

    // stay connected until the server has announced the client
    for (int i = 0; !live.accepted && i < 500; ++i) {
      socket.waitForReadyRead(10);
    }
  });

  QCOMPARE(query, QByteArray("QINF"));
  QVERIFY(live.accepted);
  QCOMPARE(live.announced, std::string("laptop"));
}

void ClientListenerTests::hello_invalidName_rejected_data()
{
  QTest::addColumn<QByteArray>("name");
  QTest::newRow("empty") << QByteArray();
  QTest::newRow("control character") << QByteArray("lap\x1btop");
  QTest::newRow("delete") << QByteArray("lap\x7ftop");
}

void ClientListenerTests::hello_invalidName_rejected()
{
  QFETCH(QByteArray, name);
  LiveServer live(SecurityLevel::PlainText);

  QByteArray reply;
  bool closed = false;
  live.runPeer([&](QTcpSocket &socket) {
    reply = sayHello(socket, name);
    closed = socket.state() == QAbstractSocket::UnconnectedState || socket.waitForDisconnected(5000);
  });

  QCOMPARE(reply, QByteArray("EBAD"));
  QVERIFY(closed);
  QVERIFY(!live.accepted);
}

void ClientListenerTests::pendingLimit_oneAddress_leavesRoomForOthers()
{
  EventQueue events;
  Sockets sockets;
  auto listener = makeListener(sockets, events);

  constexpr int perAddress = ClientListener::s_maxPendingPerAddress;
  sockets.peer = "10.0.0.1";
  void *first = nullptr;
  for (int i = 0; i < perAddress + 2; ++i) {
    events.dispatchEvent(Event(EventTypes::ListenSocketConnecting, sockets.listenTarget));
    first = first ? first : sockets.lastAccepted;
  }
  QCOMPARE(sockets.live, perAddress);

  sockets.peer = "10.0.0.2";
  events.dispatchEvent(Event(EventTypes::ListenSocketConnecting, sockets.listenTarget));
  QCOMPARE(sockets.live, perAddress + 1);

  // the address gets a slot back once one of its pending sockets goes
  events.dispatchEvent(Event(EventTypes::Timer, first));
  QCOMPARE(sockets.live, perAddress);
  sockets.peer = "10.0.0.1";
  events.dispatchEvent(Event(EventTypes::ListenSocketConnecting, sockets.listenTarget));
  QCOMPARE(sockets.live, perAddress + 1);
}

void ClientListenerTests::listenError_listensAgain()
{
  EventQueue events;
  Sockets sockets;
  auto listener = makeListener(sockets, events);
  QCOMPARE(sockets.listens, 1);

  // not at once, a socket that keeps failing would spin through the event queue
  sockets.failBinds = 1;
  events.dispatchEvent(Event(EventTypes::ListenSocketError, sockets.listenTarget));
  QCOMPARE(sockets.listens, 1);

  // a failed rebind is retried too
  events.dispatchEvent(Event(EventTypes::Timer, listener.get()));
  QCOMPARE(sockets.listens, 2);
  events.dispatchEvent(Event(EventTypes::Timer, listener.get()));
  QCOMPARE(sockets.listens, 3);

  events.dispatchEvent(Event(EventTypes::ListenSocketConnecting, sockets.listenTarget));
  QCOMPARE(sockets.live, 1);
}

void ClientListenerTests::pendingLimit_refusesConnectionsBeyondCap()
{
  // every loopback peer shares one address, so the per-address cap is the one that binds
  constexpr int cap = ClientListener::s_maxPendingPerAddress;
  constexpr int attempts = ClientListener::s_maxPendingClients + 4;
  LiveServer live(SecurityLevel::PlainText);

  int greeted = 0;
  int refused = 0;
  live.runPeer([&](QTcpSocket &first) {
    std::vector<std::unique_ptr<QTcpSocket>> others;
    for (int i = 0; i < attempts - 1; ++i) {
      auto &socket = others.emplace_back(std::make_unique<QTcpSocket>());
      socket->connectToHost(QHostAddress::LocalHost, live.port);
      socket->waitForConnected(5000);
    }
    // none of them says hello, so all stay unauthenticated
    auto count = [&](QTcpSocket &socket) {
      if (socket.bytesAvailable() > 0 || socket.waitForReadyRead(5000)) {
        ++greeted;
      } else {
        ++refused;
      }
    };
    count(first);
    for (auto &socket : others) {
      count(*socket);
    }
  });

  QCOMPARE(greeted, cap);
  QCOMPARE(refused, attempts - cap);
}

QTEST_MAIN(ClientListenerTests)
