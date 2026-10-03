/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "SecureSocketTests.h"

#include "../TestSettings.h"

#include "base/EventQueue.h"
#include "base/FunctionJob.h"
#include "common/Settings.h"
#include "mt/Thread.h"
#include "net/FingerprintDatabase.h"
#include "net/NetworkAddress.h"
#include "net/SecureListenSocket.h"
#include "net/SecureSocket.h"
#include "net/SecureUtils.h"
#include "net/SocketException.h"
#include "net/SocketMultiplexer.h"

#include <QDir>
#include <QFile>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QThread>

#include <openssl/pem.h>
#include <openssl/ssl.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <utility>
#include <vector>

namespace {

// an event queue that knows which handlers are registered
class HandlerTrackingQueue : public EventQueue
{
public:
  void addHandler(EventTypes type, void *target, const EventHandler &handler) override
  {
    {
      std::scoped_lock lock{m_mutex};
      m_registered.emplace(type, target);
    }
    EventQueue::addHandler(type, target, handler);
  }

  void removeHandler(EventTypes type, void *target) override
  {
    {
      std::scoped_lock lock{m_mutex};
      m_registered.erase({type, target});
    }
    EventQueue::removeHandler(type, target);
  }

  void removeHandlers(void *target) override
  {
    {
      std::scoped_lock lock{m_mutex};
      std::erase_if(m_registered, [target](const auto &entry) { return entry.second == target; });
    }
    EventQueue::removeHandlers(target);
  }

  bool hasHandler(EventTypes type, void *target) const
  {
    std::scoped_lock lock{m_mutex};
    return m_registered.contains({type, target});
  }

private:
  mutable std::mutex m_mutex;
  std::set<std::pair<EventTypes, void *>> m_registered;
};

Fingerprint certFingerprint(const QString &path)
{
  BIO *bio = BIO_new_file(qPrintable(path), "r");
  X509 *cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
  BIO_free(bio);
  auto fingerprint = deskflow::sslCertFingerprint(cert, QCryptographicHash::Sha256);
  X509_free(cert);
  return fingerprint;
}

bool trust(const QString &dbPath, const Fingerprint &fingerprint)
{
  FingerprintDatabase db;
  db.addTrusted(fingerprint);
  return db.write(dbPath);
}

bool runUntil(EventQueue &events, const std::function<bool()> &done, double seconds = 10.0)
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

QByteArray readAll(IDataSocket &socket)
{
  QByteArray data(static_cast<qsizetype>(socket.getSize()), '\0');
  socket.read(data.data(), static_cast<uint32_t>(data.size()));
  return data;
}

// a server and a client secure socket connected over loopback through one multiplexer
struct TlsPair
{
  EventQueue events;
  SocketMultiplexer multiplexer;
  SecureListenSocket listen;
  std::unique_ptr<IDataSocket> server;
  SecureSocket client;

  bool serverAccepted = false;
  bool serverRejected = false;
  bool clientConnected = false;
  bool clientFailed = false;
  QByteArray serverReceived;
  QByteArray clientReceived;

  explicit TlsPair(SecurityLevel level)
      : listen(&events, &multiplexer, IArchNetwork::AddressFamily::INet, level),
        client(&events, &multiplexer, IArchNetwork::AddressFamily::INet, level)
  {
    using enum EventTypes;
    events.addHandler(ListenSocketConnecting, listen.getEventTarget(), [this](const Event &) {
      server = listen.accept();
      if (!server) {
        return;
      }
      auto *target = server->getEventTarget();
      events.addHandler(ClientListenerAccepted, target, [this](const Event &) { serverAccepted = true; });
      events.addHandler(ClientListenerDisconnectedOnAccept, target, [this](const Event &) { serverRejected = true; });
      events.addHandler(StreamInputReady, target, [this](const Event &) { serverReceived += readAll(*server); });
    });

    auto *target = client.getEventTarget();
    events.addHandler(DataSocketSecureConnected, target, [this](const Event &) { clientConnected = true; });
    events.addHandler(DataSocketConnectionFailed, target, [this](const Event &) { clientFailed = true; });
    events.addHandler(StreamInputReady, target, [this](const Event &) { clientReceived += readAll(client); });

    client.initSsl(false);
    client.connect(bindToFreePort());
  }

  ~TlsPair()
  {
    events.removeHandlers(listen.getEventTarget());
    events.removeHandlers(client.getEventTarget());
    if (server) {
      events.removeHandlers(server->getEventTarget());
    }
  }

  TlsPair(const TlsPair &) = delete;
  TlsPair &operator=(const TlsPair &) = delete;

  NetworkAddress bindToFreePort()
  {
    for (int attempt = 0; attempt < 50; ++attempt) {
      NetworkAddress address("127.0.0.1", QRandomGenerator::global()->bounded(20000, 60000));
      address.resolve();
      try {
        listen.bind(address);
        return address;
      } catch (const SocketAddressInUseException &) {
        continue;
      }
    }
    qFatal("no free port to listen on");
  }

  bool waitForHandshake()
  {
    return runUntil(events, [this] { return serverAccepted && clientConnected; });
  }
};

} // namespace

void SecureSocketTests::initTestCase()
{
  m_arch.init();
  QVERIFY(m_dir.isValid());

  // the certificate path and trusted fingerprint databases all come from settings
  useTestSettings(m_dir);
  QVERIFY(QDir().mkpath(Settings::tlsDir()));

  const auto certPath = QStringLiteral("%1/deskflow.pem").arg(Settings::tlsDir());
  deskflow::generatePemSelfSignedCert(certPath);
  Settings::setValue(Settings::Security::Certificate, certPath);
  m_fingerprint = certFingerprint(certPath);
  QVERIFY(m_fingerprint.isValid());

  const auto otherPath = m_dir.filePath(QStringLiteral("other.pem"));
  deskflow::generatePemSelfSignedCert(otherPath);
  m_otherFingerprint = certFingerprint(otherPath);
  QVERIFY(m_otherFingerprint.isValid());
  QVERIFY(m_otherFingerprint != m_fingerprint);
}

void SecureSocketTests::init()
{
  QFile::remove(Settings::tlsTrustedServersDb());
  QFile::remove(Settings::tlsTrustedClientsDb());
}

void SecureSocketTests::trustedPeers_exchangeDataBothWays()
{
  QVERIFY(trust(Settings::tlsTrustedServersDb(), m_fingerprint));
  QVERIFY(trust(Settings::tlsTrustedClientsDb(), m_fingerprint));

  TlsPair pair(SecurityLevel::PeerAuth);
  QVERIFY(pair.waitForHandshake());
  QCOMPARE(pair.server->getPeerAddress(), std::string("127.0.0.1"));

  pair.client.write("ping", 4);
  QVERIFY(runUntil(pair.events, [&pair] { return pair.serverReceived == "ping"; }));

  pair.server->write("pong", 4);
  QVERIFY(runUntil(pair.events, [&pair] { return pair.clientReceived == "pong"; }));
}

void SecureSocketTests::untrustedServer_clientFailsClosed()
{
  QVERIFY(trust(Settings::tlsTrustedServersDb(), m_otherFingerprint));

  TlsPair pair(SecurityLevel::Encrypted);
  QVERIFY(runUntil(pair.events, [&pair] { return pair.clientFailed; }));
  QVERIFY(!pair.clientConnected);
}

void SecureSocketTests::unknownClient_serverRejects()
{
  QVERIFY(trust(Settings::tlsTrustedServersDb(), m_fingerprint));

  TlsPair pair(SecurityLevel::PeerAuth);
  QVERIFY(runUntil(pair.events, [&pair] { return pair.serverRejected; }));
  QVERIFY(!pair.serverAccepted);
}

void SecureSocketTests::close_whilePeerSends_doesNotDeadlock()
{
  QVERIFY(trust(Settings::tlsTrustedServersDb(), m_fingerprint));

  struct Sender
  {
    IDataSocket *socket;
    std::atomic_bool sending = true;
  };

  for (int round = 0; round < 50; ++round) {
    TlsPair pair(SecurityLevel::Encrypted);
    QVERIFY(pair.waitForHandshake());

    // a deskflow Thread rather than std::thread, since the multiplexer tracks its callers' threads
    Sender state{&pair.client};
    Thread sender(new FunctionJob(
        [](void *arg) {
          auto *s = static_cast<Sender *>(arg);
          const QByteArray chunk(64 * 1024, 'x');
          while (s->sending) {
            s->socket->write(chunk.constData(), static_cast<uint32_t>(chunk.size()));
            QThread::usleep(100);
          }
        },
        &state
    ));
    for (int i = 0; pair.server->getSize() == 0 && i < 5000; ++i) {
      QTest::qSleep(1);
    }
    QVERIFY(pair.server->getSize() > 0);

    // close() used to take the socket lock before the multiplexer's job list lock, the reverse
    // of the multiplexer thread servicing the incoming data
    Thread closer(new FunctionJob([](void *s) { static_cast<IDataSocket *>(s)->close(); }, pair.server.get()));
    const bool closed = closer.wait(5.0);
    state.sending = false;
    // a deadlocked multiplexer also blocks the sender's write
    const bool stopped = sender.wait(5.0);
    if (!closed || !stopped) {
      // the threads can't be unwound, so abort rather than hang the suite
      qFatal("SecureSocket::close deadlocked with the socket multiplexer on round %d", round);
    }
  }
}

void SecureSocketTests::destroyed_leavesNoConnectHandler()
{
  QTcpServer server;
  QVERIFY(server.listen(QHostAddress::LocalHost, 0));
  NetworkAddress address("127.0.0.1", server.serverPort());
  address.resolve();

  HandlerTrackingQueue events;
  SocketMultiplexer multiplexer;
  void *target = nullptr;
  {
    SecureSocket socket(&events, &multiplexer, IArchNetwork::AddressFamily::INet);
    target = socket.getEventTarget();
    socket.connect(address);
    QVERIFY(events.hasHandler(EventTypes::DataSocketConnected, target));
  }
  // a connect event still queued for the dead socket would otherwise reach it
  QVERIFY(!events.hasHandler(EventTypes::DataSocketConnected, target));
}

void SecureSocketTests::largeWrite_slowReadingPeer_isKept()
{
  QVERIFY(trust(Settings::tlsTrustedServersDb(), m_fingerprint));

  // keeps the accepted descriptor away from Qt, the TLS peer thread owns it
  class RawServer : public QTcpServer
  {
  public:
    qintptr descriptor = -1;

  protected:
    void incomingConnection(qintptr handle) override
    {
      descriptor = handle;
    }
  };

  struct ImpatientSocket : SecureSocket
  {
    ImpatientSocket(IEventQueue *events, SocketMultiplexer *multiplexer)
        : SecureSocket(events, multiplexer, IArchNetwork::AddressFamily::INet, SecurityLevel::Encrypted)
    {
      m_outputStallTimeout = std::chrono::seconds(1);
    }
    ImpatientSocket(const ImpatientSocket &) = delete;
    ImpatientSocket &operator=(const ImpatientSocket &) = delete;
    // off the multiplexer before our vtable goes away
    ~ImpatientSocket() override
    {
      close();
    }
  };

  // a TLS peer that reads steadily but slower than the backlog could go out in one piece
  struct Peer
  {
    qintptr descriptor;
    QByteArray certPath;
    qint64 expected;
    std::atomic<qint64> received = 0;
    std::atomic_bool stop = false;

    void run()
    {
      constexpr double bytesPerSecond = 16.0 * 1024 * 1024;
      SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
      SSL_CTX_use_certificate_chain_file(ctx, certPath.constData());
      SSL_CTX_use_PrivateKey_file(ctx, certPath.constData(), SSL_FILETYPE_PEM);
      SSL *ssl = SSL_new(ctx);
      SSL_set_fd(ssl, static_cast<int>(descriptor));

      const auto retry = [ssl](int result) {
        const int error = SSL_get_error(ssl, result);
        if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) {
          return false;
        }
        QThread::msleep(1);
        return true;
      };

      bool accepted = false;
      while (!stop && !accepted) {
        const int result = SSL_accept(ssl);
        accepted = result == 1;
        if (!accepted && !retry(result)) {
          break;
        }
      }

      std::vector<char> buffer(64 * 1024);
      const auto start = std::chrono::steady_clock::now();
      while (accepted && !stop && received < expected) {
        const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
        if (static_cast<double>(received) > elapsed.count() * bytesPerSecond) {
          QThread::msleep(1);
          continue;
        }
        const int result = SSL_read(ssl, buffer.data(), static_cast<int>(buffer.size()));
        if (result > 0) {
          received += result;
        } else if (!retry(result)) {
          break;
        }
      }
      SSL_free(ssl);
      SSL_CTX_free(ctx);
    }
  };

  RawServer server;
  QVERIFY(server.listen(QHostAddress::LocalHost, 0));
  NetworkAddress address("127.0.0.1", server.serverPort());
  address.resolve();

  EventQueue events;
  SocketMultiplexer multiplexer;
  ImpatientSocket client(&events, &multiplexer);
  bool dropped = false;
  events.addHandler(EventTypes::StreamOutputError, client.getEventTarget(), [&dropped](const Event &) {
    dropped = true;
  });
  client.initSsl(false);
  client.connect(address);
  QTRY_VERIFY_WITH_TIMEOUT(server.descriptor != -1, 5000);

  // queued whole before the handshake, like a clipboard larger than the output cap
  const QByteArray chunk(1024 * 1024, 'x');
  const auto chunks = static_cast<int>(TCPSocket::s_maxOutputBufferSize / chunk.size()) + 4;
  for (int i = 0; i < chunks; ++i) {
    client.write(chunk.constData(), static_cast<uint32_t>(chunk.size()));
  }

  Peer peer{
      server.descriptor, Settings::value(Settings::Security::Certificate).toString().toLocal8Bit(),
      qint64{chunks} * chunk.size()
  };
  std::thread reader([&peer] { peer.run(); });
  // keep-alives while the backlog drains check the peer is still taking data
  const bool done = runUntil(
      events,
      [&] {
        client.write("k", 1);
        return dropped || peer.received >= peer.expected;
      },
      60.0
  );
  peer.stop = true;
  reader.join();
  events.removeHandlers(client.getEventTarget());
  QTcpSocket closer;
  closer.setSocketDescriptor(peer.descriptor);

  QVERIFY(done);
  QVERIFY(!dropped);
  QCOMPARE(peer.received.load(), peer.expected);
}

QTEST_MAIN(SecureSocketTests)
