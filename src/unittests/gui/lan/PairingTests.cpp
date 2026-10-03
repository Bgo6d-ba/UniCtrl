/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "PairingTests.h"

#include "gui/lan/Pairing.h"
#include "net/Spake2.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtEndian>

#include <array>
#include <memory>
#include <vector>

using namespace deskflow::gui::lan;
using deskflow::Spake2;

namespace {

const auto kCode = QStringLiteral("482915");

PeerIdentity hostIdentity()
{
  return {QStringLiteral("Office PC"), QStringLiteral("office-pc"), QString(64, u'a'), 24800};
}

PeerIdentity clientIdentity()
{
  return {QStringLiteral("Laptop"), QStringLiteral("laptop.local"), QString(64, u'b'), 0};
}

QJsonObject toObject(const PeerIdentity &identity)
{
  return QJsonDocument::fromJson(identity.toJson()).object();
}

QByteArray frame(const QByteArray &payload, quint32 size)
{
  QByteArray header(4, '\0');
  qToBigEndian(size, header.data());
  return header + payload;
}

QByteArray frame(const QJsonObject &message)
{
  const auto json = QJsonDocument(message).toJson(QJsonDocument::Compact);
  return frame(json, static_cast<quint32>(json.size()));
}

QString shareOf(Spake2::Role role)
{
  Spake2 spake(role, Spake2::passwordScalar(kCode));
  return QString::fromLatin1(spake.share().toBase64());
}

QJsonObject hello(int version = 1, const QJsonObject &id = toObject(clientIdentity()))
{
  return {{"t", "hello"}, {"v", version}, {"id", id}, {"share", shareOf(Spake2::Role::A)}};
}

// The addresses that connections pretend to come from: all on the one subnet that the host knows
const QList<LocalNetwork> kNetworks{{QHostAddress(QStringLiteral("192.168.1.1")), 24}};
const auto kAddressA = QStringLiteral("192.168.1.10");
const auto kAddressB = QStringLiteral("192.168.1.11");
const auto kAddressC = QStringLiteral("192.168.1.12");

struct Host
{
  Host()
  {
    server.setIdentity(hostIdentity());
    server.setPassword(kCode);
  }

  ~Host()
  {
    setLocalNetworksForTest(std::nullopt);
  }

  // Connections count as coming from this address from now on, until the next call
  void comeFrom(const QString &address)
  {
    install();
    from = QHostAddress(address);
  }

  // The connection that this port of the client made counts as coming from this address, whatever else is set
  void comeFrom(quint16 clientPort, const QString &address)
  {
    install();
    byPort.insert(clientPort, QHostAddress(address));
  }

  PairingServer server;
  QSignalSpy paired{&server, &PairingServer::paired};
  QSignalSpy failed{&server, &PairingServer::attemptFailed};
  QSignalSpy locked{&server, &PairingServer::lockedOut};
  QHostAddress from{QHostAddress::LocalHost};
  QHash<quint16, QHostAddress> byPort;

private:
  // Tests that don't pretend use the peer address of the connection, as the application does
  void install()
  {
    if (m_installed) {
      return;
    }
    m_installed = true;
    setLocalNetworksForTest(kNetworks);
    server.setPeerAddressForTest([this](const QTcpSocket *socket) { return byPort.value(socket->peerPort(), from); });
  }

  bool m_installed = false;
};

struct Result
{
  std::optional<PairingError> error;
  PeerIdentity host;
};

// One pairing attempt on localhost; no error means the client paired
Result join(quint16 port, const QString &code, const PeerIdentity &me = clientIdentity(), int deadlineMs = 10000)
{
  PairingClient client;
  client.setDeadlineForTest(deadlineMs);
  QEventLoop loop;
  std::optional<Result> result;
  QObject::connect(&client, &PairingClient::paired, &loop, [&loop, &result](const PeerIdentity &host) {
    result = Result{std::nullopt, host};
    loop.quit();
  });
  QObject::connect(&client, &PairingClient::failed, &loop, [&loop, &result](PairingError error) {
    result = Result{error, {}};
    loop.quit();
  });
  client.pair(QHostAddress(QHostAddress::LocalHost), port, code, me);
  if (!result) {
    // the client's deadline ends every attempt; this only guards against a client that never answers
    QTimer::singleShot(deadlineMs + 2000, &loop, &QEventLoop::quit);
    loop.exec();
  }
  return result.value_or(Result{PairingError::Timeout, {}});
}

// A raw connection to the server; true when the server closed it without an answer
bool closedAfterSending(quint16 port, const QByteArray &data)
{
  QTcpSocket socket;
  socket.connectToHost(QHostAddress(QHostAddress::LocalHost), port);
  if (!socket.waitForConnected(2000)) {
    return false;
  }
  socket.write(data);
  QByteArray received;
  const bool closed = QTest::qWaitFor(
      [&socket, &received] {
        received += socket.readAll();
        return socket.state() == QAbstractSocket::UnconnectedState;
      },
      5000
  );
  return closed && received.isEmpty();
}

// Many pairing attempts at once; the results are in the order the attempts were started
QList<Result> joinAll(quint16 port, const QString &code, int count)
{
  QList<std::optional<Result>> results(count);
  int done = 0;
  std::vector<std::unique_ptr<PairingClient>> clients;
  for (int i = 0; i < count; ++i) {
    auto &client = clients.emplace_back(std::make_unique<PairingClient>());
    client->setDeadlineForTest(10000);
    QObject::connect(
        client.get(), &PairingClient::paired, client.get(),
        [&results, &done, i](const PeerIdentity &host) {
          results[i] = Result{std::nullopt, host};
          ++done;
        }
    );
    QObject::connect(client.get(), &PairingClient::failed, client.get(), [&results, &done, i](PairingError error) {
      results[i] = Result{error, {}};
      ++done;
    });
  }
  for (const auto &client : clients) {
    client->pair(QHostAddress(QHostAddress::LocalHost), port, code, clientIdentity());
  }
  static_cast<void>(QTest::qWaitFor([&done, count] { return done == count; }, 20000));

  QList<Result> all;
  for (const auto &result : std::as_const(results)) {
    all.append(result.value_or(Result{PairingError::Timeout, {}}));
  }
  return all;
}

// The next frame from the host, or nothing when the connection ends before one is complete
std::optional<QJsonObject> readFrame(QTcpSocket &socket, int timeoutMs = 5000)
{
  const auto complete = [&socket] {
    char header[4];
    return socket.peek(header, 4) == 4 &&
           socket.bytesAvailable() >= 4 + static_cast<qint64>(qFromBigEndian<quint32>(header));
  };
  static_cast<void>(QTest::qWaitFor(
      [&socket, &complete] { return complete() || socket.state() == QAbstractSocket::UnconnectedState; }, timeoutMs
  ));
  if (!complete()) {
    return std::nullopt;
  }
  char header[4];
  socket.read(header, 4);
  const auto document = QJsonDocument::fromJson(socket.read(qFromBigEndian<quint32>(header)));
  return document.isObject() ? std::optional(document.object()) : std::nullopt;
}

// A client that does what PairingClient does, but a step at a time, so that a test decides when the next one comes
struct ManualClient
{
  explicit ManualClient(const QString &code) : spake(Spake2::Role::A, Spake2::passwordScalar(code))
  {
  }

  // Connects and sends the hello; true when the host answers with its share, after which the client is ready to confirm
  bool sendHello(quint16 port)
  {
    socket.connectToHost(QHostAddress(QHostAddress::LocalHost), port);
    if (!socket.waitForConnected(2000)) {
      return false;
    }
    socket.write(frame(
        {{"t", "hello"},
         {"v", 1},
         {"id", toObject(clientIdentity())},
         {"share", QString::fromLatin1(spake.share().toBase64())}}
    ));
    const auto share = readFrame(socket);
    if (!share || share->value("t").toString() != u"share") {
      return false;
    }
    const auto host = PeerIdentity::fromJson(share->value("id").toObject());
    const auto peerShare = QByteArray::fromBase64(share->value("share").toString().toLatin1());
    return host && spake.finish(peerShare, clientIdentity().toJson(), host->toJson());
  }

  void confirm()
  {
    socket.write(frame({{"t", "confirm"}, {"mac", QString::fromLatin1(spake.confirmation().toBase64())}}));
  }

  // The error code of the host's answer, or "confirm" when it confirmed too, or nothing when the host closed
  QString answer()
  {
    const auto reply = readFrame(socket);
    if (!reply) {
      return {};
    }
    return reply->value("t").toString() == u"error" ? reply->value("code").toString() : reply->value("t").toString();
  }

  QTcpSocket socket;
  Spake2 spake;
};

// Sits between a client and the host and changes one field of the identity in every message of one type
class Relay : public QObject
{
public:
  Relay(quint16 hostPort, const QString &type, const QString &field, const QJsonValue &value)
      : m_hostPort(hostPort),
        m_type(type),
        m_field(field),
        m_value(value)
  {
    connect(&m_server, &QTcpServer::newConnection, this, &Relay::accept);
  }

  // The sockets close as the members go, and what they signal then is not for the slots below any more
  ~Relay() override
  {
    if (m_client) {
      m_client->disconnect(this);
    }
    m_host.disconnect(this);
  }

  bool listen()
  {
    return m_server.listen(QHostAddress(QHostAddress::LocalHost));
  }

  quint16 port() const
  {
    return m_server.serverPort();
  }

private:
  void accept()
  {
    m_client = m_server.nextPendingConnection();
    connect(m_client, &QTcpSocket::readyRead, this, &Relay::pump);
    connect(m_client, &QTcpSocket::disconnected, this, [this] { m_host.disconnectFromHost(); });
    connect(&m_host, &QTcpSocket::connected, this, &Relay::pump);
    connect(&m_host, &QTcpSocket::readyRead, this, &Relay::pump);
    connect(&m_host, &QTcpSocket::disconnected, this, [this] { m_client->disconnectFromHost(); });
    m_host.connectToHost(QHostAddress(QHostAddress::LocalHost), m_hostPort);
  }

  // Messages go on to the host once it is connected
  void pump()
  {
    forward(m_client, &m_host, m_fromClient);
    forward(&m_host, m_client, m_fromHost);
  }

  void forward(QTcpSocket *from, QTcpSocket *to, QByteArray &buffer)
  {
    buffer += from->readAll();
    while (to->state() == QAbstractSocket::ConnectedState && buffer.size() >= 4) {
      const auto size = qFromBigEndian<quint32>(buffer.constData());
      if (buffer.size() < 4 + static_cast<qsizetype>(size)) {
        return;
      }
      auto message = QJsonDocument::fromJson(buffer.mid(4, size)).object();
      buffer.remove(0, 4 + size);
      if (message.value("t").toString() == m_type) {
        auto id = message.value("id").toObject();
        id.insert(m_field, m_value);
        message.insert("id", id);
      }
      to->write(frame(message));
    }
  }

  quint16 m_hostPort;
  QString m_type;
  QString m_field;
  QJsonValue m_value;
  QTcpServer m_server;
  QTcpSocket *m_client = nullptr;
  QTcpSocket m_host;
  QByteArray m_fromClient;
  QByteArray m_fromHost;
};

} // namespace

void PairingTests::identity_roundTrip()
{
  QCOMPARE(
      hostIdentity().toJson(), QByteArray(R"({"fingerprint":")") + QByteArray(64, 'a') +
                                   R"(","name":"Office PC","port":24800,"screen":"office-pc"})"
  );
  QVERIFY(PeerIdentity::fromJson(toObject(hostIdentity())) == hostIdentity());
  QVERIFY(PeerIdentity::fromJson(toObject(clientIdentity())) == clientIdentity());

  for (const auto &screen :
       {QStringLiteral("a"), QStringLiteral("_"), QStringLiteral("my-pc.local."), QStringLiteral("A_b-C.d"),
        QString(255, u'x')}) {
    auto identity = clientIdentity();
    identity.screenName = screen;
    QVERIFY2(identity.isValid(), qPrintable(screen));
  }
}

void PairingTests::identity_rejects_data()
{
  QTest::addColumn<QString>("key");
  QTest::addColumn<QJsonValue>("value");

  const QJsonValue missing(QJsonValue::Undefined);
  QTest::newRow("no name") << "name" << missing;
  QTest::newRow("empty name") << "name" << QJsonValue("");
  QTest::newRow("name too long") << "name" << QJsonValue(QString(kMaxDisplayNameLength + 1, u'n'));
  QTest::newRow("name with line break") << "name" << QJsonValue("Office\nPC");
  QTest::newRow("name not a string") << "name" << QJsonValue(7);
  QTest::newRow("name with right-to-left override") << "name" << QJsonValue(QStringLiteral("abc\u202etxt.exe"));
  QTest::newRow("name with bidi isolate") << "name" << QJsonValue(QStringLiteral("abc\u2067txt.exe"));
  QTest::newRow("name with zero width space") << "name" << QJsonValue(QStringLiteral("Office\u200bPC"));
  QTest::newRow("name of zero width spaces") << "name" << QJsonValue(QStringLiteral("\u200b\u200b"));
  QTest::newRow("name of blanks") << "name" << QJsonValue(QStringLiteral("\u00a0\u3164\u2800"));
  QTest::newRow("no screen") << "screen" << missing;
  QTest::newRow("screen with space") << "screen" << QJsonValue("office pc");
  QTest::newRow("screen with comma") << "screen" << QJsonValue("office,pc");
  QTest::newRow("screen with control") << "screen" << QJsonValue("office\x7fpc");
  QTest::newRow("screen starts with dash") << "screen" << QJsonValue("-office");
  QTest::newRow("screen ends with dash") << "screen" << QJsonValue("office-");
  QTest::newRow("screen empty part") << "screen" << QJsonValue("office..pc");
  QTest::newRow("screen leading dot") << "screen" << QJsonValue(".office");
  QTest::newRow("screen only dot") << "screen" << QJsonValue(".");
  QTest::newRow("screen not ascii") << "screen" << QJsonValue(QStringLiteral("café"));
  QTest::newRow("screen too long") << "screen" << QJsonValue(QString(256, u'x'));
  QTest::newRow("no fingerprint") << "fingerprint" << missing;
  QTest::newRow("fingerprint short") << "fingerprint" << QJsonValue(QString(63, u'a'));
  QTest::newRow("fingerprint long") << "fingerprint" << QJsonValue(QString(65, u'a'));
  QTest::newRow("fingerprint uppercase") << "fingerprint" << QJsonValue(QString(64, u'A'));
  QTest::newRow("fingerprint not hex") << "fingerprint" << QJsonValue(QString(64, u'g'));
  QTest::newRow("no port") << "port" << missing;
  QTest::newRow("port negative") << "port" << QJsonValue(-1);
  QTest::newRow("port too big") << "port" << QJsonValue(65536);
  QTest::newRow("port fraction") << "port" << QJsonValue(1.5);
  QTest::newRow("port as string") << "port" << QJsonValue("24800");
}

void PairingTests::identity_rejects()
{
  QFETCH(QString, key);
  QFETCH(QJsonValue, value);

  auto object = toObject(hostIdentity());
  if (value.isUndefined()) {
    object.remove(key);
  } else {
    object.insert(key, value);
  }
  QVERIFY(!PeerIdentity::fromJson(object));
}

void PairingTests::pair_succeeds()
{
  Host host;
  QVERIFY(host.server.listen(0));

  const auto result = join(host.server.port(), kCode);
  QVERIFY(!result.error);
  QVERIFY(result.host == hostIdentity());

  QTRY_COMPARE(host.paired.count(), 1);
  QVERIFY(host.paired.first().at(0).value<PeerIdentity>() == clientIdentity());
  QCOMPARE(host.paired.first().at(1).value<QHostAddress>(), QHostAddress(QHostAddress::LocalHost));
  QCOMPARE(host.failed.count(), 0);
}

void PairingTests::pair_wrongCode()
{
  Host host;
  QVERIFY(host.server.listen(0));

  QVERIFY(join(host.server.port(), QStringLiteral("482916")).error == PairingError::WrongCode);
  QTRY_COMPARE(host.failed.count(), 1);
  QCOMPARE(host.failed.first().first().value<QHostAddress>(), QHostAddress(QHostAddress::LocalHost));
  QCOMPARE(host.paired.count(), 0);
  QCOMPARE(host.locked.count(), 0);
}

void PairingTests::pair_locksAddressOutAfterFailures()
{
  Host host;
  QVERIFY(host.server.listen(0));
  const auto port = host.server.port();

  for (int i = 0; i < kMaxFailedAttemptsPerAddress; ++i) {
    QVERIFY(join(port, QStringLiteral("000000")).error == PairingError::WrongCode);
  }
  QCOMPARE(host.failed.count(), kMaxFailedAttemptsPerAddress);
  // that address only: the budget of all addresses is another one
  QCOMPARE(host.locked.count(), 0);

  // refused even with the right code, and that isn't counted as a failure
  QVERIFY(join(port, kCode).error == PairingError::LockedOut);
  QCOMPARE(host.failed.count(), kMaxFailedAttemptsPerAddress);
  QCOMPARE(host.paired.count(), 0);

  // another address is served
  host.comeFrom(kAddressB);
  QVERIFY(!join(port, kCode).error);
  QCOMPARE(host.paired.count(), 1);
  QCOMPARE(host.paired.first().at(1).value<QHostAddress>(), QHostAddress(kAddressB));

  // and the lockout of the first ends
  host.comeFrom(QStringLiteral("127.0.0.1"));
  host.server.setTimingForTest(kPairingDeadlineMs, kFailureWindowMs, 0);
  QVERIFY(!join(port, kCode).error);
  QCOMPARE(host.paired.count(), 2);
}

void PairingTests::pair_failuresExpire()
{
  Host host;
  QVERIFY(host.server.listen(0));
  const auto port = host.server.port();

  for (int i = 1; i < kMaxFailedAttemptsPerAddress; ++i) {
    QVERIFY(join(port, QStringLiteral("000000")).error == PairingError::WrongCode);
  }
  // the failures so far fall out of the window
  host.server.setTimingForTest(kPairingDeadlineMs, 0, kLockoutMs);
  QVERIFY(join(port, QStringLiteral("000000")).error == PairingError::WrongCode);
  QCOMPARE(host.locked.count(), 0);
  QVERIFY(!join(port, kCode).error);
}

void PairingTests::pair_parallelGuessesShareTheBudget()
{
  static_assert(kMaxPairingSocketsPerAddress == 2);
  Host host;
  QVERIFY(host.server.listen(0));
  const auto port = host.server.port();
  host.comeFrom(kAddressA);

  // one guess short of the budget of the address
  for (int i = 1; i < kMaxFailedAttemptsPerAddress; ++i) {
    QVERIFY(join(port, QStringLiteral("000000")).error == PairingError::WrongCode);
  }
  QCOMPARE(host.failed.count(), kMaxFailedAttemptsPerAddress - 1);

  // two connections get past the hello, so that both are ready to guess before either one has
  ManualClient first(QStringLiteral("000000"));
  ManualClient second(QStringLiteral("000000"));
  QVERIFY(first.sendHello(port));
  QVERIFY(second.sendHello(port));
  first.confirm();
  second.confirm();

  // one of them uses up the budget and the other is turned away, whichever the host got to first
  auto answers = QStringList{first.answer(), second.answer()};
  answers.sort();
  QCOMPARE(answers, (QStringList{QStringLiteral("code"), QStringLiteral("locked")}));
  QCOMPARE(host.failed.count(), kMaxFailedAttemptsPerAddress);
  QCOMPARE(host.paired.count(), 0);

  QVERIFY(join(port, kCode).error == PairingError::LockedOut);
  host.comeFrom(kAddressB);
  QVERIFY(!join(port, kCode).error);
  QCOMPARE(host.paired.count(), 1);
}

void PairingTests::pair_parallelFloodMakesFewGuesses()
{
  Host host;
  QVERIFY(host.server.listen(0));
  const auto port = host.server.port();
  host.comeFrom(kAddressA);

  // waves of 16 attempts at once, until the address is locked out
  int wrong = 0;
  int locked = 0;
  for (int wave = 0; wave < 30 && locked == 0; ++wave) {
    const auto results = joinAll(port, QStringLiteral("000000"), 16);
    for (const auto &result : results) {
      QVERIFY(result.error.has_value());
      wrong += *result.error == PairingError::WrongCode ? 1 : 0;
      locked += *result.error == PairingError::LockedOut ? 1 : 0;
    }
  }
  QVERIFY(locked > 0);
  QCOMPARE(wrong, kMaxFailedAttemptsPerAddress);
  QTRY_COMPARE(host.failed.count(), kMaxFailedAttemptsPerAddress);
  QCOMPARE(host.paired.count(), 0);

  // it made no difference to the others
  host.comeFrom(kAddressB);
  QVERIFY(!join(port, kCode).error);
  QCOMPARE(host.paired.count(), 1);
}

void PairingTests::pair_globalBudget()
{
  static_assert(kMaxFailedAttempts % kMaxFailedAttemptsPerAddress == 0);
  Host host;
  QVERIFY(host.server.listen(0));
  const auto port = host.server.port();

  // every address stays within its own budget, all together they don't
  const auto addresses = kMaxFailedAttempts / kMaxFailedAttemptsPerAddress;
  for (int address = 0; address < addresses; ++address) {
    host.comeFrom(QStringLiteral("192.168.1.%1").arg(20 + address));
    for (int i = 0; i < kMaxFailedAttemptsPerAddress; ++i) {
      QVERIFY(join(port, QStringLiteral("000000")).error == PairingError::WrongCode);
    }
  }
  QCOMPARE(host.failed.count(), kMaxFailedAttempts);
  QCOMPARE(host.locked.count(), 1);
  QCOMPARE(host.locked.first().first().toInt(), kLockoutMs / 1000);

  // everyone is refused, an address that never tried as well, and that isn't counted as a failure
  host.comeFrom(kAddressC);
  QVERIFY(join(port, kCode).error == PairingError::LockedOut);
  QCOMPARE(host.failed.count(), kMaxFailedAttempts);
  QCOMPARE(host.locked.count(), 1);
  QCOMPARE(host.paired.count(), 0);

  // a new password starts all over
  host.server.setPassword(kCode);
  QVERIFY(!join(port, kCode).error);
  host.comeFrom(QStringLiteral("192.168.1.20"));
  QVERIFY(!join(port, kCode).error);
  QCOMPARE(host.paired.count(), 2);
}

// What the application does when everyone is locked out: a new code, which starts all over
void PairingTests::pair_passwordRotatedOnLockout()
{
  Host host;
  QVERIFY(host.server.listen(0));
  const auto port = host.server.port();
  const auto rotated = QStringLiteral("654321");
  QObject::connect(&host.server, &PairingServer::lockedOut, &host.server, [&host, rotated] {
    host.server.setPassword(rotated);
  });

  for (int address = 0; address < kMaxFailedAttempts / kMaxFailedAttemptsPerAddress; ++address) {
    host.comeFrom(QStringLiteral("192.168.1.%1").arg(20 + address));
    for (int i = 0; i < kMaxFailedAttemptsPerAddress; ++i) {
      QVERIFY(join(port, QStringLiteral("000000")).error == PairingError::WrongCode);
    }
  }
  QCOMPARE(host.locked.count(), 1);

  // nobody is locked out any more: the old code is a wrong one, and the new code pairs
  host.comeFrom(kAddressC);
  QVERIFY(join(port, kCode).error == PairingError::WrongCode);
  QVERIFY(!join(port, rotated).error);
  QCOMPARE(host.paired.count(), 1);
  QCOMPARE(host.locked.count(), 1);
}

void PairingTests::pair_resetsBudgets_data()
{
  QTest::addColumn<bool>("close");

  QTest::newRow("new password") << false;
  QTest::newRow("close and listen again") << true;
}

void PairingTests::pair_resetsBudgets()
{
  QFETCH(bool, close);

  Host host;
  QVERIFY(host.server.listen(0));
  for (int i = 0; i < kMaxFailedAttemptsPerAddress; ++i) {
    QVERIFY(join(host.server.port(), QStringLiteral("000000")).error == PairingError::WrongCode);
  }
  QVERIFY(join(host.server.port(), kCode).error == PairingError::LockedOut);

  if (close) {
    host.server.close();
    QVERIFY(host.server.listen(0));
  } else {
    host.server.setPassword(kCode);
  }
  QVERIFY(!join(host.server.port(), kCode).error);
  QCOMPARE(host.paired.count(), 1);
}

void PairingTests::pair_screenNameClash()
{
  Host host;
  QVERIFY(host.server.listen(0));

  auto me = clientIdentity();
  me.screenName = QStringLiteral("OFFICE-PC");
  QVERIFY(join(host.server.port(), kCode, me).error == PairingError::Protocol);
  QCOMPARE(host.paired.count(), 0);
  QCOMPARE(host.failed.count(), 0);
}

void PairingTests::pair_answerSurvivesChangesInPairedSlot_data()
{
  QTest::addColumn<bool>("close");

  QTest::newRow("new password") << false;
  QTest::newRow("close") << true;
}

// A slot of paired() that starts over must not stop the host's answer from reaching the computer it has trusted
void PairingTests::pair_answerSurvivesChangesInPairedSlot()
{
  QFETCH(bool, close);

  Host host;
  QVERIFY(host.server.listen(0));
  QObject::connect(&host.server, &PairingServer::paired, &host.server, [&host, close] {
    if (close) {
      host.server.close();
    } else {
      host.server.setPassword(QStringLiteral("654321"));
    }
  });

  const auto result = join(host.server.port(), kCode);
  QVERIFY(!result.error);
  QVERIFY(result.host == hostIdentity());
  QCOMPARE(host.paired.count(), 1);
}

void PairingTests::pair_newPasswordDropsPairingsInProgress()
{
  Host host;
  QVERIFY(host.server.listen(0));

  // ready to confirm the old code
  ManualClient client(kCode);
  QVERIFY(client.sendHello(host.server.port()));
  host.server.setPassword(QStringLiteral("654321"));
  QTRY_COMPARE(client.socket.state(), QAbstractSocket::UnconnectedState);
  QCOMPARE(host.paired.count(), 0);
}

void PairingTests::pair_tamperedIdentity_data()
{
  QTest::addColumn<QString>("message");
  QTest::addColumn<QString>("field");
  QTest::addColumn<QJsonValue>("value");
  QTest::addColumn<bool>("changed");

  const QJsonValue otherFingerprint(QString(64, u'c'));
  QTest::newRow("nothing changed") << "hello" << "name" << QJsonValue("Laptop") << false;
  QTest::newRow("client's fingerprint") << "hello" << "fingerprint" << otherFingerprint << true;
  QTest::newRow("client's name") << "hello" << "name" << QJsonValue("Mallory") << true;
  QTest::newRow("client's screen") << "hello" << "screen" << QJsonValue("mallory") << true;
  QTest::newRow("host's fingerprint") << "share" << "fingerprint" << otherFingerprint << true;
  QTest::newRow("host's name") << "share" << "name" << QJsonValue("Mallory") << true;
  QTest::newRow("host's screen") << "share" << "screen" << QJsonValue("mallory") << true;
  QTest::newRow("host's core port") << "share" << "port" << QJsonValue(24801) << true;
}

// Whoever relays the messages can't put another identity, and with it another certificate, in them: the identities
// are part of what the two sides confirm, so the confirmation fails and neither side trusts the other
void PairingTests::pair_tamperedIdentity()
{
  QFETCH(QString, message);
  QFETCH(QString, field);
  QFETCH(QJsonValue, value);
  QFETCH(bool, changed);

  Host host;
  QVERIFY(host.server.listen(0));
  Relay relay(host.server.port(), message, field, value);
  QVERIFY(relay.listen());

  const auto result = join(relay.port(), kCode);
  if (!changed) {
    QVERIFY(!result.error);
    QVERIFY(result.host == hostIdentity());
    QTRY_COMPARE(host.paired.count(), 1);
    return;
  }

  // the host can't tell a changed identity from a wrong code
  QVERIFY(result.error == PairingError::WrongCode);
  QTRY_COMPARE(host.failed.count(), 1);
  QCOMPARE(host.paired.count(), 0);

  // it is the relay that made the difference
  const auto direct = join(host.server.port(), kCode);
  QVERIFY(!direct.error);
  QVERIFY(direct.host == hostIdentity());
  QTRY_COMPARE(host.paired.count(), 1);
  QVERIFY(host.paired.first().at(0).value<PeerIdentity>() == clientIdentity());
}

void PairingTests::server_needsValidIdentity()
{
  const auto warning = "pairing server needs a valid identity with a core port";
  PairingServer server;
  QTest::ignoreMessage(QtWarningMsg, warning);
  QVERIFY(!server.listen(0));

  auto identity = hostIdentity();
  identity.corePort = 0;
  server.setIdentity(identity);
  QTest::ignoreMessage(QtWarningMsg, warning);
  QVERIFY(!server.listen(0));

  server.setIdentity(hostIdentity());
  QVERIFY(server.listen(0));
}

void PairingTests::pair_noPasswordRefused()
{
  Host host;
  host.server.setPassword(QString());
  QVERIFY(host.server.listen(0));

  QVERIFY(join(host.server.port(), QString()).error == PairingError::Protocol);
  QCOMPARE(host.paired.count(), 0);
}

void PairingTests::server_closesBadFrames_data()
{
  QTest::addColumn<QByteArray>("data");

  auto noFingerprint = toObject(clientIdentity());
  noFingerprint.remove("fingerprint");
  auto badShare = hello();
  badShare.insert("share", QString::fromLatin1(QByteArray(Spake2::kShareSize, '\0').toBase64()));
  auto notBase64 = hello();
  notBase64.insert("share", "not base64!");

  QTest::newRow("oversized frame") << frame("{}", kMaxFrameSize + 1);
  QTest::newRow("empty frame") << frame(QByteArray(), 0);
  QTest::newRow("not json") << frame("hello", 5);
  QTest::newRow("json array") << frame("[]", 2);
  QTest::newRow("confirm before hello") << frame(QJsonObject{{"t", "confirm"}, {"mac", "AAAA"}});
  QTest::newRow("unknown version") << frame(hello(2));
  QTest::newRow("invalid identity") << frame(hello(1, noFingerprint));
  QTest::newRow("invalid share") << frame(badShare);
  QTest::newRow("share not base64") << frame(notBase64);
}

void PairingTests::server_closesBadFrames()
{
  QFETCH(QByteArray, data);

  Host host;
  QVERIFY(host.server.listen(0));
  QVERIFY(closedAfterSending(host.server.port(), data));
  QCOMPARE(host.failed.count(), 0);
  QCOMPARE(host.paired.count(), 0);
}

void PairingTests::server_acceptsLargestFrame()
{
  Host host;
  QVERIFY(host.server.listen(0));

  auto payload = QJsonDocument(hello()).toJson(QJsonDocument::Compact);
  payload += QByteArray(kMaxFrameSize - payload.size(), ' ');
  QTcpSocket socket;
  socket.connectToHost(QHostAddress(QHostAddress::LocalHost), host.server.port());
  QVERIFY(socket.waitForConnected(2000));
  socket.write(frame(payload, kMaxFrameSize));

  // answered with the host's share
  QVERIFY(QTest::qWaitFor([&socket] { return socket.bytesAvailable() > 4; }, 5000));
  QVERIFY(socket.readAll().contains(R"("t":"share")"));
}

void PairingTests::server_closesIdleConnection()
{
  Host host;
  host.server.setTimingForTest(200, kFailureWindowMs, kLockoutMs);
  QVERIFY(host.server.listen(0));

  // half a frame, and then nothing
  QVERIFY(closedAfterSending(host.server.port(), frame(QByteArray(), 10)));
}

void PairingTests::server_helloDeadline()
{
  constexpr int helloDeadlineMs = 300;
  Host host;
  host.server.setTimingForTest(kPairingDeadlineMs, kFailureWindowMs, kLockoutMs, helloDeadlineMs);
  QVERIFY(host.server.listen(0));
  const auto port = host.server.port();

  // a connection that says nothing is closed at the hello deadline, and not at the one for the whole pairing
  QElapsedTimer silent;
  silent.start();
  QVERIFY(closedAfterSending(port, QByteArray()));
  QVERIFY(silent.elapsed() >= helloDeadlineMs - 100);
  QVERIFY(silent.elapsed() < kPairingDeadlineMs / 2);

  // the hello deadline is for the hello only
  ManualClient slow(kCode);
  QVERIFY(slow.sendHello(port));
  QTest::qWait(3 * helloDeadlineMs);
  QCOMPARE(slow.socket.state(), QAbstractSocket::ConnectedState);
  slow.confirm();
  QCOMPARE(slow.answer(), QStringLiteral("confirm"));
  QTRY_COMPARE(host.paired.count(), 1);
}

void PairingTests::server_limitsConnections()
{
  static_assert(kMaxPairingSockets == 2 * kMaxPairingSocketsPerAddress);
  Host host;
  QVERIFY(host.server.listen(0));
  const auto port = host.server.port();

  // one address too many, then the address that fills the host up, and then the address that finds it full
  const QStringList origins{kAddressA, kAddressA, kAddressA, kAddressB, kAddressB, kAddressC};
  std::array<QTcpSocket, 6> sockets;
  for (qsizetype i = 0; i < origins.size(); ++i) {
    auto &socket = sockets.at(i);
    socket.connectToHost(QHostAddress(QHostAddress::LocalHost), port);
    QVERIFY(socket.waitForConnected(2000));
    host.comeFrom(socket.localPort(), origins.at(i));
  }
  QTRY_COMPARE(sockets.at(2).state(), QAbstractSocket::UnconnectedState);
  QTRY_COMPARE(sockets.at(5).state(), QAbstractSocket::UnconnectedState);
  for (const auto i : {0, 1, 3, 4}) {
    QCOMPARE(sockets.at(i).state(), QAbstractSocket::ConnectedState);
  }

  // a closed connection frees its place
  sockets.front().abort();
  host.comeFrom(kAddressC);
  QVERIFY(QTest::qWaitFor([port] { return !join(port, kCode).error; }, 10000));
}

void PairingTests::server_refusesOtherSubnets()
{
  Host host;
  QVERIFY(host.server.listen(0));
  const auto port = host.server.port();

  // the subnet is 192.168.1.0/24: the next one is out of reach, and the connection is closed without an answer
  host.comeFrom(QStringLiteral("192.168.2.10"));
  QVERIFY(closedAfterSending(port, frame(hello())));
  QCOMPARE(host.failed.count(), 0);
  QCOMPARE(host.paired.count(), 0);
  host.comeFrom(QStringLiteral("10.1.2.3"));
  QVERIFY(closedAfterSending(port, frame(hello())));

  host.comeFrom(kAddressA);
  QVERIFY(!join(port, kCode).error);
  QCOMPARE(host.paired.count(), 1);

  // loopback needs no network, and nothing else is let in without one
  setLocalNetworksForTest(QList<LocalNetwork>{});
  host.comeFrom(QStringLiteral("127.0.0.9"));
  QVERIFY(!join(port, kCode).error);
  QCOMPARE(host.paired.count(), 2);
  host.comeFrom(kAddressA);
  QVERIFY(closedAfterSending(port, frame(hello())));
}

void PairingTests::client_rejectsBadReplies_data()
{
  QTest::addColumn<QByteArray>("reply");

  const auto hostShare = shareOf(Spake2::Role::B);
  auto noCorePort = toObject(hostIdentity());
  noCorePort.insert("port", 0);

  QTest::newRow("oversized frame") << frame("{}", kMaxFrameSize + 1);
  QTest::newRow("not json") << frame("share", 5);
  QTest::newRow("invalid share") << frame(
      QJsonObject{
          {"t", "share"},
          {"id", toObject(hostIdentity())},
          {"share", QString::fromLatin1(QByteArray(Spake2::kShareSize, '\0').toBase64())}
      }
  );
  QTest::newRow("host without core port")
      << frame(QJsonObject{{"t", "share"}, {"id", noCorePort}, {"share", hostShare}});
  QTest::newRow("confirm too early") << frame(QJsonObject{{"t", "confirm"}, {"mac", "AAAA"}});
  QTest::newRow("unknown error") << frame(QJsonObject{{"t", "error"}, {"code", "busy"}});
  QTest::newRow("closed without answer") << QByteArray();
  // as by a host that was told another identity for the client than the one the client has
  QTest::newRow("confirmation of another transcript")
      << frame(QJsonObject{{"t", "share"}, {"id", toObject(hostIdentity())}, {"share", hostShare}}) +
             frame(
                 QJsonObject{
                     {"t", "confirm"}, {"mac", QString::fromLatin1(QByteArray(Spake2::kMacSize, '\0').toBase64())}
                 }
             );
}

void PairingTests::client_rejectsBadReplies()
{
  QFETCH(QByteArray, reply);

  QTcpServer fakeHost;
  QVERIFY(fakeHost.listen(QHostAddress(QHostAddress::LocalHost)));
  QObject::connect(&fakeHost, &QTcpServer::newConnection, [&fakeHost, reply] {
    auto *socket = fakeHost.nextPendingConnection();
    QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, reply] {
      socket->readAll();
      socket->write(reply);
      socket->disconnectFromHost();
    });
  });

  QVERIFY(join(fakeHost.serverPort(), kCode).error == PairingError::Protocol);
}

void PairingTests::client_timeout()
{
  QTcpServer silentHost;
  QVERIFY(silentHost.listen(QHostAddress(QHostAddress::LocalHost)));

  QVERIFY(join(silentHost.serverPort(), kCode, clientIdentity(), 200).error == PairingError::Timeout);
}

void PairingTests::client_unreachable()
{
  quint16 closedPort = 0;
  {
    QTcpServer probe;
    QVERIFY(probe.listen(QHostAddress(QHostAddress::LocalHost)));
    closedPort = probe.serverPort();
  }

  QVERIFY(join(closedPort, kCode).error == PairingError::Unreachable);
}

QTEST_GUILESS_MAIN(PairingTests)
