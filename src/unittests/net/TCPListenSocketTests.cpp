/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "TCPListenSocketTests.h"

#include "base/EventQueue.h"
#include "base/FunctionJob.h"
#include "mt/Thread.h"
#include "net/IDataSocket.h"
#include "net/ISocketMultiplexerJob.h"
#include "net/NetworkAddress.h"
#include "net/SocketException.h"
#include "net/SocketMultiplexer.h"
#include "net/TCPListenSocket.h"

#include <QDir>
#include <QRandomGenerator>
#include <QTest>

#if defined(Q_OS_UNIX)
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

NetworkAddress bindToFreePort(TCPListenSocket &listen)
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

ArchSocket connectTo(const NetworkAddress &address)
{
  auto socket = ARCH->newSocket(IArchNetwork::AddressFamily::INet, IArchNetwork::SocketType::Stream);
  ARCH->connectSocket(socket, address.getAddress());
  return socket;
}

bool waitForEvent(EventQueue &events, TCPListenSocket &listen, EventTypes type)
{
  bool received = false;
  events.addHandler(type, listen.getEventTarget(), [&](const Event &) {
    received = true;
    events.addEvent(Event(EventTypes::Quit));
  });
  auto *timer = events.newOneShotTimer(5.0, nullptr);
  events.addHandler(EventTypes::Timer, timer, [&events](const Event &) { events.addEvent(Event(EventTypes::Quit)); });

  events.loop();

  events.removeHandler(EventTypes::Timer, timer);
  events.deleteTimer(timer);
  events.removeHandler(type, listen.getEventTarget());
  return received;
}

bool waitForConnecting(EventQueue &events, TCPListenSocket &listen)
{
  return waitForEvent(events, listen, EventTypes::ListenSocketConnecting);
}

// hands the listen socket an error revent from the multiplexer thread, as poll() does on POLLERR
class ErrorJob : public ISocketMultiplexerJob
{
public:
  ErrorJob(TCPListenSocket &listen, ArchSocket ready) : m_listen(listen), m_socket(ARCH->copySocket(ready))
  {
  }
  ErrorJob(const ErrorJob &) = delete;
  ErrorJob &operator=(const ErrorJob &) = delete;
  ~ErrorJob() override
  {
    ARCH->closeSocket(m_socket);
  }

  ISocketMultiplexerJob *run(bool, bool, bool) override
  {
    return m_listen.serviceListening(this, false, false, true);
  }
  ArchSocket getSocket() const override
  {
    return m_socket;
  }
  bool isReadable() const override
  {
    return false;
  }
  bool isWritable() const override
  {
    return true;
  }

private:
  TCPListenSocket &m_listen;
  ArchSocket m_socket;
};

#if defined(Q_OS_UNIX)
qsizetype openDescriptors()
{
  return QDir(QStringLiteral("/dev/fd")).entryList(QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot).size();
}
#endif

} // namespace

void TCPListenSocketTests::initTestCase()
{
  m_arch.init();
}

void TCPListenSocketTests::accept_nothingPending_returnsNullAndKeepsListening()
{
  EventQueue events;
  SocketMultiplexer multiplexer;
  TCPListenSocket listen(&events, &multiplexer, IArchNetwork::AddressFamily::INet);
  const auto address = bindToFreePort(listen);

  QVERIFY(listen.accept() == nullptr);

  auto client = connectTo(address);
  QVERIFY(waitForConnecting(events, listen));
  auto accepted = listen.accept();
  QVERIFY(accepted != nullptr);
  QCOMPARE(accepted->getPeerAddress(), std::string("127.0.0.1"));

  // the accepted socket must be polled, or nothing the client sends is ever read
  ARCH->writeSocket(client, "hi", 2);
  QTRY_COMPARE_WITH_TIMEOUT(accepted->getSize(), 2u, 5000);
  ARCH->closeSocket(client);
}

void TCPListenSocketTests::accept_peerAlreadyReset_leaksNoDescriptor()
{
#if defined(Q_OS_UNIX)
  EventQueue events;
  SocketMultiplexer multiplexer;
  TCPListenSocket listen(&events, &multiplexer, IArchNetwork::AddressFamily::INet);
  const auto address = bindToFreePort(listen);

  sockaddr_in peer{};
  peer.sin_family = AF_INET;
  peer.sin_port = htons(static_cast<uint16_t>(address.getPort()));
  peer.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  // the first round is uncounted: the multiplexer thread opens its unblock pipe lazily
  qsizetype before = 0;
  for (int i = 0; i < 6; ++i) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    QVERIFY(fd >= 0);
    QCOMPARE(::connect(fd, reinterpret_cast<const sockaddr *>(&peer), sizeof(peer)), 0);
    QVERIFY(waitForConnecting(events, listen));

    // a zero linger close sends RST, so on macOS setting up the accepted socket fails;
    // Linux accepts the reset socket, so there this only guards the success path
    const linger reset{1, 0};
    setsockopt(fd, SOL_SOCKET, SO_LINGER, &reset, sizeof(reset));
    close(fd);
    QTest::qSleep(50);

    [[maybe_unused]] const bool refused = listen.accept() == nullptr;
#if defined(Q_OS_MACOS)
    QVERIFY(refused);
#endif
    if (i == 0) {
      before = openDescriptors();
    }
  }
  QCOMPARE(openDescriptors(), before);
#else
  QSKIP("descriptors are only countable on Unix");
#endif
}

void TCPListenSocketTests::accept_acceptFails_keepsListening()
{
#if defined(DESKFLOW_UBSAN)
  QSKIP("UBSan's vptr check opens a pipe, so it fails once no descriptor is left");
#elif defined(Q_OS_UNIX)
  EventQueue events;
  SocketMultiplexer multiplexer;
  TCPListenSocket listen(&events, &multiplexer, IArchNetwork::AddressFamily::INet);
  const auto address = bindToFreePort(listen);

  auto client = connectTo(address);
  QVERIFY(waitForConnecting(events, listen));

  // make accept() fail with EMFILE by capping descriptors at the lowest free one
  rlimit original{};
  QCOMPARE(getrlimit(RLIMIT_NOFILE, &original), 0);
  const int lowestFree = open("/dev/null", O_RDONLY);
  QVERIFY(lowestFree >= 0);
  close(lowestFree);
  rlimit capped = original;
  capped.rlim_cur = static_cast<rlim_t>(lowestFree);
  QCOMPARE(setrlimit(RLIMIT_NOFILE, &capped), 0);

  bool threw = false;
  std::unique_ptr<IDataSocket> accepted;
  try {
    accepted = listen.accept();
  } catch (...) {
    threw = true;
  }
  setrlimit(RLIMIT_NOFILE, &original);

  QVERIFY(!threw);
  QVERIFY(accepted == nullptr);

  // macOS drops the refused connection while Linux keeps it queued, so connect again
  auto secondClient = connectTo(address);
  QVERIFY(waitForConnecting(events, listen));
  QVERIFY(listen.accept() != nullptr);
  ARCH->closeSocket(secondClient);
  ARCH->closeSocket(client);
#else
  QSKIP("descriptor limits are only adjustable on Unix");
#endif
}

void TCPListenSocketTests::serviceListening_error_reportsWithoutDeadlock()
{
  EventQueue events;
  SocketMultiplexer multiplexer;
  TCPListenSocket listen(&events, &multiplexer, IArchNetwork::AddressFamily::INet);
  const auto address = bindToFreePort(listen);

  // a connected socket is writable at once, so the multiplexer runs the job straight away
  auto client = connectTo(address);
  // added once the loop runs, so the job's log line follows the loop's own; TSan cannot see the
  // time zone lock the logger takes inside Qt
  auto *start = events.newOneShotTimer(0.01, nullptr);
  events.addHandler(EventTypes::Timer, start, [&](const Event &) {
    multiplexer.addSocket(&listen, new ErrorJob(listen, client));
  });
  QVERIFY(waitForEvent(events, listen, EventTypes::ListenSocketError));
  events.removeHandler(EventTypes::Timer, start);
  events.deleteTimer(start);

  // closing from the service thread used to relock the job list it was holding
  Thread closer(new FunctionJob([](void *l) { static_cast<TCPListenSocket *>(l)->close(); }, &listen));
  if (!closer.wait(5.0)) {
    qFatal("the socket multiplexer deadlocked servicing a listen socket error");
  }
  ARCH->closeSocket(client);
}

QTEST_MAIN(TCPListenSocketTests)
