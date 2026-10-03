/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "LanProtocol.h"

#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QObject>

#include <functional>
#include <map>
#include <memory>
#include <optional>

class QJsonObject;
class QTcpServer;
class QTcpSocket;
class QTimer;

namespace deskflow {
class Spake2;
}

namespace deskflow::gui::lan {

struct PeerIdentity
{
  QString displayName;
  QString screenName;
  QString fingerprint;  //!< SHA-256 of the TLS certificate, 64 lowercase hex digits
  quint16 corePort = 0; //!< the host's core server port, 0 for a client

  //! displayName per isValidDisplayName(), screenName valid in the core's server config, fingerprint per
  //! isValidFingerprint()
  bool isValid() const;

  //! Compact JSON with a stable key order; these bytes are the identity in the SPAKE2 transcript
  QByteArray toJson() const;
  static std::optional<PeerIdentity> fromJson(const QJsonObject &object);

  bool operator==(const PeerIdentity &other) const = default;
};

enum class PairingError
{
  WrongCode,   //!< the host rejected the code
  LockedOut,   //!< the host refuses pairing for a while after too many wrong codes
  Unreachable, //!< could not connect to the host
  Timeout,     //!< no result within kPairingDeadlineMs
  Protocol     //!< invalid or unexpected data, the connection closed early, or the host failed key confirmation
};

/**
 * @brief The host side of pairing, which proves both sides know the code with SPAKE2 (net/Spake2.h)
 *
 * A frame is a 4 byte big-endian length and a compact UTF-8 JSON object of at most kMaxFrameSize bytes:
 *   client: {"t":"hello","v":1,"id":<identity>,"share":<base64 pA>}
 *   host:   {"t":"share","id":<identity>,"share":<base64 pB>} or {"t":"error","code":"locked"|"name"}
 *   client: {"t":"confirm","mac":<base64 cA>}
 *   host:   {"t":"confirm","mac":<base64 cB>} or {"t":"error","code":"code"|"locked"}
 * The SPAKE2 identities A and B are the toJson() bytes of the client's and the host's identity.
 *
 * Only computers on this computer's subnets (isLocalPeer()) may connect. Wrong codes are counted per address and
 * over all addresses: kMaxFailedAttemptsPerAddress of them within kFailureWindowMs lock that address out for
 * kLockoutMs, kMaxFailedAttempts lock everyone out (lockedOut()). The lockout is checked when the confirmation
 * arrives too, so connections opened in parallel can't guess more than the budget. A connection has
 * kPairingHelloDeadlineMs to deliver its hello and kPairingDeadlineMs in all, an address holds at most
 * kMaxPairingSocketsPerAddress of the kMaxPairingSockets connections.
 */
class PairingServer : public QObject
{
  Q_OBJECT

public:
  explicit PairingServer(QObject *parent = nullptr);
  ~PairingServer() override;

  //! Listens on every IPv4 address; set a valid identity (with a core port) and the password first.
  //! Tests pass port 0 and read port().
  bool listen(quint16 port = kPairingPort);
  //! Stops listening, drops the pairings in progress (but not those being answered) and forgets the wrong codes
  void close();
  quint16 port() const;

  //! The code or password joining computers must enter; empty refuses everyone. Drops the pairings in progress
  //! (but not those being answered: a computer that was trusted still gets the answer) and forgets the wrong codes
  //! and lockouts.
  void setPassword(const QString &password);
  //! This host's identity, with its core port
  void setIdentity(const PeerIdentity &identity);

  //! Test hook: replaces kPairingDeadlineMs, kFailureWindowMs, kLockoutMs and kPairingHelloDeadlineMs, also for
  //! failures and a lockout so far
  void
  setTimingForTest(int deadlineMs, int failureWindowMs, int lockoutMs, int helloDeadlineMs = kPairingHelloDeadlineMs);
  //! Test hook: the address a connection counts as coming from, in place of its peer address
  void setPeerAddressForTest(const std::function<QHostAddress(const QTcpSocket *)> &peerAddress);

Q_SIGNALS:
  //! The client proved it knows the code; trust its fingerprint
  void paired(const deskflow::gui::lan::PeerIdentity &client, const QHostAddress &from);
  //! Someone tried a wrong code
  void attemptFailed(const QHostAddress &from);
  //! Too many wrong codes from all addresses together; pairing is refused for this long
  void lockedOut(int seconds);

private:
  struct Session;

  //! The wrong codes of one address, or of all of them
  struct Budget
  {
    QList<qint64> failures; //!< when the wrong codes within the window came
    std::optional<qint64> lockedAtMs;

    bool isLocked(qint64 now, int lockoutMs) const;
    //! Counts a wrong code; true when that used the budget up, which locks it
    bool fail(qint64 now, int limit, int windowMs);
    //! Nothing to remember any more
    bool isIdle(qint64 now, int windowMs, int lockoutMs) const;
  };

  void accept();
  void read(QTcpSocket *socket);
  bool handle(QTcpSocket *socket, Session &session, const QJsonObject &message);
  void reply(QTcpSocket *socket, Session &session, const QJsonObject &message, bool last);
  void drop(QTcpSocket *socket);
  void dropAll();
  QHostAddress peerKey(const QTcpSocket *socket) const;
  int sessionsFrom(const QHostAddress &address) const;
  bool isLockedOut(const QHostAddress &address) const;
  bool countFailure(const QHostAddress &address);
  void forgetFailures();

  QTcpServer *m_server;
  std::map<QTcpSocket *, std::unique_ptr<Session>> m_sessions;
  QByteArray m_w;
  PeerIdentity m_identity;
  QElapsedTimer m_clock;
  Budget m_budget;
  QHash<QHostAddress, Budget> m_addressBudgets;
  std::function<QHostAddress(const QTcpSocket *)> m_peerAddress;
  int m_deadlineMs = kPairingDeadlineMs;
  int m_helloDeadlineMs = kPairingHelloDeadlineMs;
  int m_failureWindowMs = kFailureWindowMs;
  int m_lockoutMs = kLockoutMs;
};

/**
 * @brief The joining side of pairing, see PairingServer for the protocol
 */
class PairingClient : public QObject
{
  Q_OBJECT

public:
  explicit PairingClient(QObject *parent = nullptr);
  ~PairingClient() override;

  //! Cancels any pairing in progress and starts a new one, which ends with paired() or failed()
  //! @param me this computer's identity, which must be valid (the host drops invalid ones)
  void pair(const QHostAddress &address, quint16 port, const QString &password, const PeerIdentity &me);
  //! Stops the pairing in progress, without a signal
  void cancel();

  //! Test hook: replaces kPairingDeadlineMs
  void setDeadlineForTest(int ms);

Q_SIGNALS:
  //! The host proved it knows the code; trust its fingerprint
  void paired(const deskflow::gui::lan::PeerIdentity &host);
  void failed(deskflow::gui::lan::PairingError error, const QString &detail);

private:
  enum class Step
  {
    Idle,
    Connecting,
    AwaitingShare,
    AwaitingConfirm
  };

  void read();
  void handle(const QJsonObject &message);
  void socketFailed();
  void fail(PairingError error, const QString &detail);

  QTcpSocket *m_socket = nullptr;
  QTimer *m_deadline;
  std::unique_ptr<Spake2> m_spake;
  PeerIdentity m_me;
  PeerIdentity m_host;
  Step m_step = Step::Idle;
  int m_deadlineMs = kPairingDeadlineMs;
};

} // namespace deskflow::gui::lan
