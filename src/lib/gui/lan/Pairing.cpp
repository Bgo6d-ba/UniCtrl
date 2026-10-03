/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "Pairing.h"

#include "net/Spake2.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtEndian>

#include <openssl/crypto.h>

#include <algorithm>
#include <utility>

namespace deskflow::gui::lan {

namespace {

constexpr int kVersion = 1;
constexpr int kHeaderSize = 4;
constexpr int kMaxScreenNameLength = 255;

bool isScreenNameChar(QChar c)
{
  return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9') || c == u'_' || c == u'-';
}

// As server::Config::isValidScreenName: dot separated parts of [A-Za-z0-9_-] that don't start or end with '-',
// where a trailing dot is allowed
bool isValidScreenName(const QString &name)
{
  if (name.isEmpty() || name.size() > kMaxScreenNameLength) {
    return false;
  }
  const auto parts = QStringView(name).split(u'.');
  for (qsizetype i = 0; i < parts.size(); ++i) {
    const auto part = parts.at(i);
    if (part.isEmpty() && i > 0 && i == parts.size() - 1) {
      continue;
    }
    if (part.isEmpty() || part.front() == u'-' || part.back() == u'-' || !std::ranges::all_of(part, isScreenNameChar)) {
      return false;
    }
  }
  return true;
}

QJsonObject identityObject(const PeerIdentity &identity)
{
  return {
      {"fingerprint", identity.fingerprint},
      {"name", identity.displayName},
      {"port", identity.corePort},
      {"screen", identity.screenName}
  };
}

QByteArray frame(const QJsonObject &message)
{
  const auto json = QJsonDocument(message).toJson(QJsonDocument::Compact);
  QByteArray bytes(kHeaderSize, Qt::Uninitialized);
  qToBigEndian(static_cast<quint32>(json.size()), bytes.data());
  return bytes + json;
}

enum class FrameStatus
{
  Incomplete,
  Ready,
  Invalid
};

// Takes the next message off the socket once its frame is complete
FrameStatus takeFrame(QTcpSocket &socket, QJsonObject &message)
{
  char header[kHeaderSize];
  if (socket.peek(header, kHeaderSize) < kHeaderSize) {
    return FrameStatus::Incomplete;
  }
  const auto size = qFromBigEndian<quint32>(header);
  if (size == 0 || size > kMaxFrameSize) {
    return FrameStatus::Invalid;
  }
  if (socket.bytesAvailable() < kHeaderSize + static_cast<qint64>(size)) {
    return FrameStatus::Incomplete;
  }
  socket.skip(kHeaderSize);
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(socket.read(size), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    return FrameStatus::Invalid;
  }
  message = document.object();
  return FrameStatus::Ready;
}

QString toBase64(const QByteArray &data)
{
  return QString::fromLatin1(data.toBase64());
}

QByteArray fromBase64(const QJsonValue &value)
{
  const auto result =
      QByteArray::fromBase64Encoding(value.toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
  return result ? *result : QByteArray();
}

QJsonObject errorMessage(const QString &code)
{
  return {{"t", "error"}, {"code", code}};
}

void cleanse(QByteArray &secret)
{
  OPENSSL_cleanse(secret.data(), secret.size());
}

} // namespace

bool PeerIdentity::isValid() const
{
  return isValidDisplayName(displayName) && isValidScreenName(screenName) && isValidFingerprint(fingerprint);
}

QByteArray PeerIdentity::toJson() const
{
  return QJsonDocument(identityObject(*this)).toJson(QJsonDocument::Compact);
}

std::optional<PeerIdentity> PeerIdentity::fromJson(const QJsonObject &object)
{
  const auto port = object.value("port").toInteger(-1);
  const PeerIdentity identity{
      object.value("name").toString(), object.value("screen").toString(), object.value("fingerprint").toString(),
      static_cast<quint16>(port)
  };
  if (port < 0 || port > 65535 || !identity.isValid()) {
    return std::nullopt;
  }
  return identity;
}

struct PairingServer::Session
{
  enum class Step
  {
    AwaitingHello,
    AwaitingConfirm,
    Closing
  };

  Step step = Step::AwaitingHello;
  QHostAddress address; //!< what the peer counts as, see peerKey()
  std::unique_ptr<Spake2> spake;
  PeerIdentity client;
};

PairingServer::PairingServer(QObject *parent) : QObject(parent), m_server(new QTcpServer(this))
{
  m_clock.start();
  connect(m_server, &QTcpServer::newConnection, this, &PairingServer::accept);
}

PairingServer::~PairingServer()
{
  close();
  cleanse(m_w);
}

bool PairingServer::listen(quint16 port)
{
  close();
  if (!m_identity.isValid() || m_identity.corePort == 0) {
    qWarning() << "pairing server needs a valid identity with a core port";
    return false;
  }
  if (!m_server->listen(QHostAddress::AnyIPv4, port)) {
    qWarning().noquote() << "pairing server could not listen on tcp port" << port << m_server->errorString();
    return false;
  }
  return true;
}

void PairingServer::close()
{
  m_server->close();
  dropAll();
  forgetFailures();
}

quint16 PairingServer::port() const
{
  return m_server->serverPort();
}

void PairingServer::setPassword(const QString &password)
{
  cleanse(m_w);
  m_w = password.isEmpty() ? QByteArray() : Spake2::passwordScalar(password);
  dropAll();
  forgetFailures();
}

void PairingServer::setIdentity(const PeerIdentity &identity)
{
  m_identity = identity;
}

void PairingServer::setTimingForTest(int deadlineMs, int failureWindowMs, int lockoutMs, int helloDeadlineMs)
{
  m_deadlineMs = deadlineMs;
  m_failureWindowMs = failureWindowMs;
  m_lockoutMs = lockoutMs;
  m_helloDeadlineMs = helloDeadlineMs;
}

void PairingServer::setPeerAddressForTest(const std::function<QHostAddress(const QTcpSocket *)> &peerAddress)
{
  m_peerAddress = peerAddress;
}

void PairingServer::accept()
{
  while (auto *socket = m_server->nextPendingConnection()) {
    const auto address = peerKey(socket);
    if (!isLocalPeer(address) || m_sessions.size() >= kMaxPairingSockets ||
        sessionsFrom(address) >= kMaxPairingSocketsPerAddress) {
      socket->abort();
      socket->deleteLater();
      continue;
    }
    auto session = std::make_unique<Session>();
    session->address = address;
    m_sessions.emplace(socket, std::move(session));
    socket->setReadBufferSize(kHeaderSize + kMaxFrameSize);
    connect(socket, &QTcpSocket::readyRead, this, [this, socket] { read(socket); });
    connect(socket, &QTcpSocket::disconnected, this, [this, socket] { drop(socket); });
    QTimer::singleShot(m_deadlineMs, socket, [this, socket] { drop(socket); });
    QTimer::singleShot(m_helloDeadlineMs, socket, [this, socket] {
      const auto pending = m_sessions.find(socket);
      if (pending != m_sessions.end() && pending->second->step == Session::Step::AwaitingHello) {
        drop(socket);
      }
    });
  }
}

void PairingServer::read(QTcpSocket *socket)
{
  // looked up on every pass: a slot connected to one of our signals may have dropped the session
  for (auto it = m_sessions.find(socket); it != m_sessions.end() && it->second->step != Session::Step::Closing;
       it = m_sessions.find(socket)) {
    QJsonObject message;
    const auto status = takeFrame(*socket, message);
    if (status == FrameStatus::Incomplete) {
      return;
    }
    if (status == FrameStatus::Invalid || !handle(socket, *it->second, message)) {
      drop(socket);
      return;
    }
  }
}

// false drops the connection without an answer
bool PairingServer::handle(QTcpSocket *socket, Session &session, const QJsonObject &message)
{
  const auto type = message.value("t").toString();
  if (session.step == Session::Step::AwaitingHello && type == "hello") {
    if (isLockedOut(session.address)) {
      reply(socket, session, errorMessage("locked"), true);
      return true;
    }
    const auto client = PeerIdentity::fromJson(message.value("id").toObject());
    if (m_w.isEmpty() || message.value("v").toInteger() != kVersion || !client) {
      return false;
    }
    if (client->screenName.compare(m_identity.screenName, Qt::CaseInsensitive) == 0) {
      reply(socket, session, errorMessage("name"), true);
      return true;
    }
    session.spake = std::make_unique<Spake2>(Spake2::Role::B, m_w);
    const auto share = session.spake->share();
    if (share.isEmpty() ||
        !session.spake->finish(fromBase64(message.value("share")), client->toJson(), m_identity.toJson())) {
      return false;
    }
    session.client = *client;
    session.step = Session::Step::AwaitingConfirm;
    reply(socket, session, {{"t", "share"}, {"id", identityObject(m_identity)}, {"share", toBase64(share)}}, false);
    return true;
  }

  if (session.step == Session::Step::AwaitingConfirm && type == "confirm") {
    // a reply may end the session, so what the signals need is copied first
    const auto from = session.address;
    // connections in parallel that got past the hello can't make more guesses than the budget allows
    if (isLockedOut(from)) {
      reply(socket, session, errorMessage("locked"), true);
      return true;
    }
    if (!session.spake->verifyPeer(fromBase64(message.value("mac")))) {
      reply(socket, session, errorMessage("code"), true);
      const bool locked = countFailure(from);
      Q_EMIT attemptFailed(from);
      if (locked) {
        Q_EMIT lockedOut((m_lockoutMs + 999) / 1000);
      }
      return true;
    }
    const auto client = session.client;
    reply(socket, session, {{"t", "confirm"}, {"mac", toBase64(session.spake->confirmation())}}, true);
    Q_EMIT paired(client, from);
    return true;
  }

  return false;
}

void PairingServer::reply(QTcpSocket *socket, Session &session, const QJsonObject &message, bool last)
{
  socket->write(frame(message));
  if (last) {
    session.step = Session::Step::Closing;
    socket->disconnectFromHost();
  }
}

void PairingServer::drop(QTcpSocket *socket)
{
  m_sessions.erase(socket);
  socket->disconnect(this);
  socket->abort();
  socket->deleteLater();
}

// Sessions that are only being answered are left to finish: the peer was told the outcome, and the answer of a
// computer that was trusted must arrive even when a slot of paired() changes the password
void PairingServer::dropAll()
{
  QList<QTcpSocket *> sockets;
  for (const auto &[socket, session] : m_sessions) {
    if (session->step != Session::Step::Closing) {
      sockets.append(socket);
    }
  }
  for (auto *socket : std::as_const(sockets)) {
    drop(socket);
  }
}

QHostAddress PairingServer::peerKey(const QTcpSocket *socket) const
{
  return m_peerAddress ? m_peerAddress(socket) : socket->peerAddress();
}

int PairingServer::sessionsFrom(const QHostAddress &address) const
{
  return static_cast<int>(std::ranges::count_if(m_sessions, [&address](const auto &session) {
    return session.second->address == address;
  }));
}

bool PairingServer::isLockedOut(const QHostAddress &address) const
{
  const auto now = m_clock.elapsed();
  const auto budget = m_addressBudgets.constFind(address);
  return m_budget.isLocked(now, m_lockoutMs) ||
         (budget != m_addressBudgets.cend() && budget->isLocked(now, m_lockoutMs));
}

// true when this failure starts a lockout of everyone
bool PairingServer::countFailure(const QHostAddress &address)
{
  const auto now = m_clock.elapsed();
  m_addressBudgets.removeIf([this, now](decltype(m_addressBudgets)::iterator budget) {
    return budget->isIdle(now, m_failureWindowMs, m_lockoutMs);
  });
  m_addressBudgets[address].fail(now, kMaxFailedAttemptsPerAddress, m_failureWindowMs);
  return m_budget.fail(now, kMaxFailedAttempts, m_failureWindowMs);
}

void PairingServer::forgetFailures()
{
  m_budget = {};
  m_addressBudgets.clear();
}

bool PairingServer::Budget::isLocked(qint64 now, int lockoutMs) const
{
  return lockedAtMs && now - *lockedAtMs < lockoutMs;
}

bool PairingServer::Budget::fail(qint64 now, int limit, int windowMs)
{
  failures.removeIf([now, windowMs](qint64 failure) { return now - failure >= windowMs; });
  failures.append(now);
  if (failures.size() < limit) {
    return false;
  }
  failures.clear();
  lockedAtMs = now;
  return true;
}

bool PairingServer::Budget::isIdle(qint64 now, int windowMs, int lockoutMs) const
{
  return !isLocked(now, lockoutMs) &&
         std::ranges::all_of(failures, [now, windowMs](qint64 failure) { return now - failure >= windowMs; });
}

PairingClient::PairingClient(QObject *parent) : QObject(parent), m_deadline(new QTimer(this))
{
  m_deadline->setSingleShot(true);
  connect(m_deadline, &QTimer::timeout, this, [this] {
    fail(PairingError::Timeout, QStringLiteral("no answer from the host"));
  });
}

PairingClient::~PairingClient()
{
  cancel();
}

void PairingClient::pair(const QHostAddress &address, quint16 port, const QString &password, const PeerIdentity &me)
{
  cancel();
  auto w = Spake2::passwordScalar(password);
  m_spake = std::make_unique<Spake2>(Spake2::Role::A, w);
  cleanse(w);
  m_me = me;
  m_step = Step::Connecting;

  m_socket = new QTcpSocket(this);
  m_socket->setReadBufferSize(kHeaderSize + kMaxFrameSize);
  connect(m_socket, &QTcpSocket::connected, this, [this] {
    m_step = Step::AwaitingShare;
    m_socket->write(
        frame({{"t", "hello"}, {"v", kVersion}, {"id", identityObject(m_me)}, {"share", toBase64(m_spake->share())}})
    );
  });
  connect(m_socket, &QTcpSocket::readyRead, this, &PairingClient::read);
  connect(m_socket, &QTcpSocket::errorOccurred, this, &PairingClient::socketFailed);
  m_deadline->start(m_deadlineMs);
  m_socket->connectToHost(address, port);
}

void PairingClient::cancel()
{
  m_deadline->stop();
  m_step = Step::Idle;
  m_spake.reset();
  if (m_socket) {
    m_socket->disconnect(this);
    m_socket->abort();
    m_socket->deleteLater();
    m_socket = nullptr;
  }
}

void PairingClient::setDeadlineForTest(int ms)
{
  m_deadlineMs = ms;
}

void PairingClient::read()
{
  while (m_step == Step::AwaitingShare || m_step == Step::AwaitingConfirm) {
    QJsonObject message;
    const auto status = takeFrame(*m_socket, message);
    if (status == FrameStatus::Incomplete) {
      return;
    }
    if (status == FrameStatus::Invalid) {
      fail(PairingError::Protocol, QStringLiteral("invalid message from the host"));
      return;
    }
    handle(message);
  }
}

void PairingClient::handle(const QJsonObject &message)
{
  const auto type = message.value("t").toString();
  if (type == "error") {
    const auto code = message.value("code").toString();
    if (code == "code") {
      fail(PairingError::WrongCode, QStringLiteral("the host rejected the code"));
    } else if (code == "locked") {
      fail(PairingError::LockedOut, QStringLiteral("the host refuses pairing after too many wrong codes"));
    } else if (code == "name") {
      fail(PairingError::Protocol, QStringLiteral("the host uses the same screen name"));
    } else {
      fail(PairingError::Protocol, QStringLiteral("the host refused pairing"));
    }
    return;
  }

  if (m_step == Step::AwaitingShare && type == "share") {
    const auto host = PeerIdentity::fromJson(message.value("id").toObject());
    if (!host || host->corePort == 0 ||
        !m_spake->finish(fromBase64(message.value("share")), m_me.toJson(), host->toJson())) {
      fail(PairingError::Protocol, QStringLiteral("invalid share from the host"));
      return;
    }
    m_host = *host;
    m_step = Step::AwaitingConfirm;
    m_socket->write(frame({{"t", "confirm"}, {"mac", toBase64(m_spake->confirmation())}}));
    return;
  }

  if (m_step == Step::AwaitingConfirm && type == "confirm") {
    if (!m_spake->verifyPeer(fromBase64(message.value("mac")))) {
      fail(PairingError::Protocol, QStringLiteral("the host failed key confirmation"));
      return;
    }
    const auto host = m_host;
    cancel();
    Q_EMIT paired(host);
    return;
  }

  fail(PairingError::Protocol, QStringLiteral("unexpected message from the host"));
}

void PairingClient::socketFailed()
{
  // the host's last message can arrive together with the end of the connection
  read();
  if (m_step == Step::Idle) {
    return;
  }
  const auto error = m_step == Step::Connecting ? PairingError::Unreachable : PairingError::Protocol;
  fail(error, m_socket->errorString());
}

void PairingClient::fail(PairingError error, const QString &detail)
{
  cancel();
  Q_EMIT failed(error, detail);
}

} // namespace deskflow::gui::lan
