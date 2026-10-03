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
#include <optional>

class QTimer;
class QUdpSocket;

namespace deskflow::gui::lan {

struct HostAnnouncement
{
  QString id; //!< stable per host, 1 to 64 of [A-Za-z0-9-]
  QString name;
  quint16 pairPort = kPairingPort;
  quint16 corePort = 0;

  //! Compact JSON: {"svc":"deskflow-plus","v":1,"id":..,"name":..,"pair":..,"core":..}
  QByteArray toDatagram() const;

  //! Strict: anything but a valid version 1 announcement of at most kMaxBeaconSize bytes is rejected
  static std::optional<HostAnnouncement> parse(const QByteArray &datagram);

  bool operator==(const HostAnnouncement &other) const = default;
};

struct DiscoveredHost : HostAnnouncement
{
  QHostAddress address;
  qint64 lastSeenMs = 0; //!< monotonic, only meaningful relative to other hosts' values
};

/**
 * @brief Broadcasts this host's announcement every kBeaconIntervalMs
 *
 * Sent to 255.255.255.255 and the broadcast address of every up, running, broadcast capable,
 * non-loopback IPv4 interface. Discovery is unauthenticated; joining requires pairing.
 */
class LanAnnouncer : public QObject
{
  Q_OBJECT

public:
  explicit LanAnnouncer(QObject *parent = nullptr);

  //! Sent right away when announcing already
  void setAnnouncement(const HostAnnouncement &announcement);
  void start();
  void stop();

  //! Test hook: send to these addresses and port instead of the broadcast addresses
  void setTargetsForTest(const QList<QHostAddress> &targets, quint16 port);

private:
  QList<QHostAddress> targets() const;
  void announce();

  QUdpSocket *m_socket;
  QTimer *m_timer;
  HostAnnouncement m_announcement;
  QList<QHostAddress> m_testTargets;
  quint16 m_port = kBeaconPort;
  bool m_loggedSendFailure = false;
};

/**
 * @brief Collects the hosts announcing on the LAN, forgetting those not heard from for kHostExpiryMs
 *
 * Only announcements from computers on this computer's subnets are heard (isLocalPeer()), an address may announce
 * at most kMaxHostsPerAddress hosts, and there are at most kMaxHosts in all. A flood of announcements can't fill the
 * list, nor make the application rebuild it more than once per kHostsChangedIntervalMs.
 */
class LanBrowser : public QObject
{
  Q_OBJECT

public:
  explicit LanBrowser(QObject *parent = nullptr);

  //! @param port the beacon port; tests pass 0 and read port()
  bool start(quint16 port = kBeaconPort);
  //! Forgets the hosts, which is signalled right away
  void stop();
  quint16 port() const;

  //! Announcements with this id (this computer's own) are ignored
  void setOwnId(const QString &id);

  //! Sorted by name, always up to date, whether hostsChanged() came yet or not
  QList<DiscoveredHost> hosts() const;

  //! Test hook: forget hosts after this long instead of kHostExpiryMs; call before start()
  void setExpiryForTest(int ms);
  //! Test hook: emit hostsChanged() this often at most instead of every kHostsChangedIntervalMs
  void setChangeIntervalForTest(int ms);
  //! Test hook: the address an announcement counts as coming from, in place of the address it was sent from
  void setSenderForTest(const std::function<QHostAddress(const QHostAddress &sender, quint16 port)> &sender);

Q_SIGNALS:
  //! When the hosts changed. Comes at once when the last one was long enough ago, otherwise one is held back to
  //! follow: at most one per kHostsChangedIntervalMs, however many announcements arrive.
  void hostsChanged();

private:
  void readDatagrams();
  void expire();
  void notifyChanged();
  void emitChanged();
  int hostsFrom(const QHostAddress &address, const QString &exceptId) const;

  QUdpSocket *m_socket;
  QTimer *m_expiryTimer;
  QTimer *m_changeTimer;
  QElapsedTimer m_clock;
  QHash<QString, DiscoveredHost> m_hosts;
  QString m_ownId;
  std::function<QHostAddress(const QHostAddress &, quint16)> m_sender;
  std::optional<qint64> m_lastChangeMs;
  int m_expiryMs = kHostExpiryMs;
  int m_changeIntervalMs = kHostsChangedIntervalMs;
};

} // namespace deskflow::gui::lan
