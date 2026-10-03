/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "SocketTests.h"

#include "../deskflow/MockEventQueue.h"
#include "arch/ArchException.h"
#include "base/FunctionJob.h"
#include "mt/Thread.h"
#include "net/NetworkAddress.h"
#include "net/SecureSocket.h"
#include "net/SecureUtils.h"
#include "net/SocketMultiplexer.h"
#include "net/TCPSocket.h"

#include <QDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>

#include <openssl/ssl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>

namespace {

// records the events sockets post, from whichever thread posts them
class RecordingEventQueue : public MockEventQueue
{
public:
  void addEvent(Event &&event) override
  {
    {
      std::scoped_lock lock{m_mutex};
      m_types.push_back(event.getType());
    }
    Event::deleteData(event);
  }

  bool received(EventTypes type) const
  {
    std::scoped_lock lock{m_mutex};
    return std::ranges::find(m_types, type) != m_types.end();
  }

private:
  mutable std::mutex m_mutex;
  std::vector<EventTypes> m_types;
};

// IArchNetwork has no getsockname, so probe for a free port and retry if it is taken before the bind
ArchSocket listenOnFreePort(quint16 &port)
{
  for (int attempt = 0;; ++attempt) {
    QTcpServer probe;
    probe.listen(QHostAddress::LocalHost, 0);
    port = probe.serverPort();
    probe.close();

    NetworkAddress address("127.0.0.1", port);
    address.resolve();
    ArchSocket listener = ARCH->newSocket(IArchNetwork::AddressFamily::INet, IArchNetwork::SocketType::Stream);
    ARCH->setReuseAddrOnSocket(listener, true);
    try {
      ARCH->bindSocket(listener, address.getAddress());
      ARCH->listenOnSocket(listener);
      return listener;
    } catch (const ArchNetworkException &) {
      ARCH->closeSocket(listener);
      if (attempt == 9) {
        throw;
      }
    }
  }
}

} // namespace

void SocketTests::initTestCase()
{
  m_arch.init();
}

void SocketTests::unblockPollSocket_beforeFirstPoll_wakesThread()
{
  // SocketMultiplexer unblocks its service thread whenever the job list changes, even before that thread's first poll
  struct Poller
  {
    ArchSocket listener = nullptr;
    std::atomic_bool unblocked = false;
    std::chrono::steady_clock::duration waited{};
  };
  // poll a real socket that never becomes readable: winsock returns at once from a poll with no sockets
  quint16 port = 0;
  Poller state;
  state.listener = listenOnFreePort(port);
  Thread poller(new FunctionJob(
      [](void *arg) {
        auto *s = static_cast<Poller *>(arg);
        while (!s->unblocked) {
          QThread::usleep(100);
        }
        IArchNetwork::PollEntry entry{};
        entry.m_socket = s->listener;
        entry.m_events = IArchNetwork::PollEventMask::In;
        const auto start = std::chrono::steady_clock::now();
        ARCH->pollSocket(&entry, 1, 5.0);
        s->waited = std::chrono::steady_clock::now() - start;
      },
      &state
  ));

  poller.unblockPollSocket();
  state.unblocked = true;
  const bool finished = poller.wait(10.0);
  ARCH->closeSocket(state.listener);
  QVERIFY(finished);
  QVERIFY(state.waited < std::chrono::seconds(2));
}

void SocketTests::flush_peerNotReading_returns()
{
  // the server never accepts, so the connection sits in the backlog and nothing drains it
  QTcpServer server;
  QVERIFY(server.listen(QHostAddress::LocalHost, 0));

  MockEventQueue events;
  SocketMultiplexer multiplexer;
  TCPSocket socket(&events, &multiplexer);
  NetworkAddress address("127.0.0.1", server.serverPort());
  address.resolve();
  socket.connect(address);

  const QByteArray chunk(1024 * 1024, 'x');
  for (int i = 0; i < 32; ++i) {
    socket.write(chunk.constData(), chunk.size());
  }

  Thread flusher(new FunctionJob([](void *s) { static_cast<TCPSocket *>(s)->flush(); }, &socket));
  const bool returned = flusher.wait(10.0);
  if (!returned) {
    // closing marks the socket flushed, which releases the stuck flush
    socket.close();
    flusher.wait();
  }
  QVERIFY(returned);
}

void SocketTests::acceptedSocket_notPolledUntilJobStarts()
{
  quint16 port = 0;
  ArchSocket listener = listenOnFreePort(port);
  QTcpSocket peer;
  peer.connectToHost(QHostAddress::LocalHost, port);
  QVERIFY(peer.waitForConnected(5000));
  peer.write("hello");
  QVERIFY(peer.waitForBytesWritten(5000));

  ArchSocket accepted = nullptr;
  for (int i = 0; accepted == nullptr && i < 500; ++i) {
    accepted = ARCH->acceptSocket(listener, nullptr);
    if (accepted == nullptr) {
      QTest::qSleep(10);
    }
  }
  ARCH->closeSocket(listener);
  QVERIFY(accepted != nullptr);

  struct ArmableSocket : TCPSocket
  {
    using TCPSocket::refreshJob;
    using TCPSocket::TCPSocket;
  };

  MockEventQueue events;
  SocketMultiplexer multiplexer;
  ArmableSocket socket(&events, &multiplexer, accepted);

  // a job installed by the constructor would read before a subclass could take over the socket
  QTest::qSleep(200);
  QCOMPARE(socket.getSize(), 0u);

  socket.refreshJob();
  QTRY_COMPARE_WITH_TIMEOUT(socket.getSize(), 5u, 5000);
}

void SocketTests::write_peerStopsReading_dropsConnection()
{
  // the server never accepts, so nothing drains the connection past the kernel buffers
  QTcpServer server;
  QVERIFY(server.listen(QHostAddress::LocalHost, 0));

  struct ImpatientSocket : TCPSocket
  {
    ImpatientSocket(IEventQueue *events, SocketMultiplexer *multiplexer) : TCPSocket(events, multiplexer)
    {
      m_outputStallTimeout = std::chrono::milliseconds(200);
    }
    ImpatientSocket(const ImpatientSocket &) = delete;
    ImpatientSocket &operator=(const ImpatientSocket &) = delete;
    // off the multiplexer before our vtable goes away
    ~ImpatientSocket() override
    {
      close();
    }
  };

  RecordingEventQueue events;
  SocketMultiplexer multiplexer;
  ImpatientSocket socket(&events, &multiplexer);
  NetworkAddress address("127.0.0.1", server.serverPort());
  address.resolve();
  socket.connect(address);

  const QByteArray chunk(1024 * 1024, 'x');
  // beyond the cap even after the kernel buffers take their share
  const auto chunks = static_cast<int>(TCPSocket::s_maxOutputBufferSize / chunk.size()) + 32;
  for (int i = 0; i < chunks && !events.received(EventTypes::SocketDisconnected); ++i) {
    socket.write(chunk.constData(), chunk.size());
  }
  QTest::qSleep(400);
  socket.write(chunk.constData(), chunk.size());
  QVERIFY(events.received(EventTypes::StreamOutputError));
  QVERIFY(events.received(EventTypes::SocketDisconnected));
}

void SocketTests::write_beyondCapToReadingPeer_isKept()
{
  // a clipboard above the cap is queued at once, far faster than any link drains it
  QTcpServer server;
  QVERIFY(server.listen(QHostAddress::LocalHost, 0));

  RecordingEventQueue events;
  SocketMultiplexer multiplexer;
  TCPSocket socket(&events, &multiplexer);
  NetworkAddress address("127.0.0.1", server.serverPort());
  address.resolve();
  socket.connect(address);
  QVERIFY(server.waitForNewConnection(5000));
  QTcpSocket *peer = server.nextPendingConnection();
  qint64 received = 0;
  QObject::connect(peer, &QTcpSocket::readyRead, [peer, &received] { received += peer->readAll().size(); });

  const QByteArray chunk(512 * 1024, 'x');
  const auto chunks = static_cast<int>(TCPSocket::s_maxOutputBufferSize / chunk.size()) * 3 / 2;
  for (int i = 0; i < chunks; ++i) {
    socket.write(chunk.constData(), chunk.size());
  }
  // a later small write, like a keep-alive, while the backlog drains
  socket.write("k", 1);

  const qint64 total = qint64{chunks} * chunk.size() + 1;
  QTRY_COMPARE_WITH_TIMEOUT(received, total, 60000);
  QVERIFY(!events.received(EventTypes::StreamOutputError));
  QVERIFY(!events.received(EventTypes::SocketDisconnected));
}

void SocketTests::serviceConnected_readError_disconnects()
{
  quint16 port = 0;
  ArchSocket listener = listenOnFreePort(port);
  QTcpSocket peer;
  peer.connectToHost(QHostAddress::LocalHost, port);
  QVERIFY(peer.waitForConnected(5000));

  ArchSocket accepted = nullptr;
  for (int i = 0; accepted == nullptr && i < 500; ++i) {
    accepted = ARCH->acceptSocket(listener, nullptr);
    if (accepted == nullptr) {
      QTest::qSleep(10);
    }
  }
  ARCH->closeSocket(listener);
  QVERIFY(accepted != nullptr);

  // a read error that is not a hang-up, which leaves the socket readable
  struct FailingSocket : TCPSocket
  {
    using TCPSocket::refreshJob;
    using TCPSocket::TCPSocket;
    FailingSocket(const FailingSocket &) = delete;
    FailingSocket &operator=(const FailingSocket &) = delete;
    ~FailingSocket() override
    {
      close();
    }
    JobResult doRead() override
    {
      ++reads;
      throw ArchNetworkIOException("injected read error");
    }
    std::atomic<int> reads = 0;
  };

  RecordingEventQueue events;
  SocketMultiplexer multiplexer;
  FailingSocket socket(&events, &multiplexer, accepted);
  socket.refreshJob();
  peer.write("hello");
  QVERIFY(peer.waitForBytesWritten(5000));

  QTRY_VERIFY_WITH_TIMEOUT(events.received(EventTypes::SocketDisconnected), 5000);
  QTest::qSleep(100);
  QCOMPARE(socket.reads.load(), 1);
}

void SocketTests::sharedContext_reusedUntilCertChanges()
{
  QTemporaryDir dir;
  const auto certPath = dir.filePath(QStringLiteral("cert.pem"));
  deskflow::generatePemSelfSignedCert(certPath);

  auto *first = SecureSocket::sharedContext(certPath, true, SecurityLevel::Encrypted);
  auto *second = SecureSocket::sharedContext(certPath, true, SecurityLevel::Encrypted);
  auto *client = SecureSocket::sharedContext(certPath, false, SecurityLevel::Encrypted);
  QVERIFY(first != nullptr);
  QCOMPARE(second, first);
  QVERIFY(client != first);

  deskflow::generatePemSelfSignedCert(certPath);
  QFile cert(certPath);
  QVERIFY(cert.open(QIODevice::ReadWrite));
  QVERIFY(cert.setFileTime(QDateTime::currentDateTime().addSecs(60), QFileDevice::FileModificationTime));
  cert.close();

  auto *reloaded = SecureSocket::sharedContext(certPath, true, SecurityLevel::Encrypted);
  QVERIFY(reloaded != nullptr);
  QVERIFY(reloaded != first);

  QVERIFY(
      SecureSocket::sharedContext(dir.filePath(QStringLiteral("missing.pem")), true, SecurityLevel::Encrypted) ==
      nullptr
  );

  SSL_CTX_free(first);
  SSL_CTX_free(second);
  SSL_CTX_free(client);
  SSL_CTX_free(reloaded);
}

void SocketTests::socketMultiplexer_destroyed_closesUnblockPipe()
{
#if defined(Q_OS_UNIX)
  // stopping the service thread wakes it through its unblock pipe, which must go with the thread
  const auto openDescriptors = [] {
    return QDir(QStringLiteral("/dev/fd")).entryList(QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot).size();
  };
  const auto before = openDescriptors();
  for (int i = 0; i < 3; ++i) {
    SocketMultiplexer multiplexer;
  }
  QTRY_COMPARE(openDescriptors(), before);
#else
  QSKIP("descriptors are only countable on Unix");
#endif
}

void SocketTests::networkAddress_selfAssign_keepsAddress()
{
  NetworkAddress address("127.0.0.1", 24800);
  address.resolve();
  QVERIFY(address.isValid());

  auto &alias = address;
  address = alias;

  QVERIFY(address.isValid());
  QCOMPARE(address.getPort(), 24800);
}

QTEST_MAIN(SocketTests)
