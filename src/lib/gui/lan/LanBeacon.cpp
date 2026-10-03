/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "LanBeacon.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QTimer>
#include <QUdpSocket>

namespace deskflow::gui::lan {

namespace {

const auto kService = QStringLiteral("deskflow-plus");
constexpr int kVersion = 1;
constexpr int kMaxIdLength = 64;
constexpr int kMaxDatagramsPerPass = 256;

bool isValidId(const QString &id)
{
  return !id.isEmpty() && id.size() <= kMaxIdLength && std::ranges::all_of(id, [](QChar c) {
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9') || c == u'-';
  });
}

std::optional<quint16> portFrom(const QJsonValue &value)
{
  // not a whole number gives 0
  const auto port = value.toInteger(0);
  if (port < 1 || port > 65535) {
    return std::nullopt;
  }
  return static_cast<quint16>(port);
}

} // namespace

QByteArray HostAnnouncement::toDatagram() const
{
  const QJsonObject object{{"svc", kService}, {"v", kVersion},    {"id", id},
                           {"name", name},    {"pair", pairPort}, {"core", corePort}};
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

std::optional<HostAnnouncement> HostAnnouncement::parse(const QByteArray &datagram)
{
  if (datagram.size() > kMaxBeaconSize) {
    return std::nullopt;
  }
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(datagram, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    return std::nullopt;
  }

  const auto object = document.object();
  const auto id = object.value("id").toString();
  const auto name = object.value("name").toString().trimmed();
  const auto pairPort = portFrom(object.value("pair"));
  const auto corePort = portFrom(object.value("core"));
  if (object.value("svc").toString() != kService || object.value("v").toInteger() != kVersion || !isValidId(id) ||
      !isValidDisplayName(name) || !pairPort || !corePort) {
    return std::nullopt;
  }
  return HostAnnouncement{id, name, *pairPort, *corePort};
}

LanAnnouncer::LanAnnouncer(QObject *parent) : QObject(parent), m_socket(new QUdpSocket(this)), m_timer(new QTimer(this))
{
  m_timer->setInterval(kBeaconIntervalMs);
  connect(m_timer, &QTimer::timeout, this, &LanAnnouncer::announce);
}

void LanAnnouncer::setAnnouncement(const HostAnnouncement &announcement)
{
  if (!HostAnnouncement::parse(announcement.toDatagram())) {
    qWarning().noquote() << "lan announcement is invalid, other computers will ignore it:" << announcement.toDatagram();
  }
  m_announcement = announcement;
  if (m_timer->isActive()) {
    announce();
  }
}

void LanAnnouncer::start()
{
  m_loggedSendFailure = false;
  announce();
  m_timer->start();
}

void LanAnnouncer::stop()
{
  m_timer->stop();
}

void LanAnnouncer::setTargetsForTest(const QList<QHostAddress> &targets, quint16 port)
{
  m_testTargets = targets;
  m_port = port;
}

QList<QHostAddress> LanAnnouncer::targets() const
{
  if (!m_testTargets.isEmpty()) {
    return m_testTargets;
  }

  QList<QHostAddress> targets{QHostAddress(QHostAddress::Broadcast)};
  const auto interfaces = QNetworkInterface::allInterfaces();
  for (const auto &networkInterface : interfaces) {
    const auto flags = networkInterface.flags();
    if (!flags.testFlags(QNetworkInterface::IsUp | QNetworkInterface::IsRunning | QNetworkInterface::CanBroadcast) ||
        flags.testFlag(QNetworkInterface::IsLoopBack)) {
      continue;
    }
    const auto entries = networkInterface.addressEntries();
    for (const auto &entry : entries) {
      const auto broadcast = entry.broadcast();
      if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol && !broadcast.isNull() &&
          !targets.contains(broadcast)) {
        targets.append(broadcast);
      }
    }
  }
  return targets;
}

void LanAnnouncer::announce()
{
  const auto datagram = m_announcement.toDatagram();
  const auto addresses = targets();
  for (const auto &address : addresses) {
    if (m_socket->writeDatagram(datagram, address, m_port) < 0 && !m_loggedSendFailure) {
      m_loggedSendFailure = true;
      qDebug().noquote() << "lan announcement to" << address.toString() << "failed:" << m_socket->errorString();
    }
  }
}

LanBrowser::LanBrowser(QObject *parent)
    : QObject(parent),
      m_socket(new QUdpSocket(this)),
      m_expiryTimer(new QTimer(this)),
      m_changeTimer(new QTimer(this))
{
  m_clock.start();
  m_changeTimer->setSingleShot(true);
  m_changeTimer->setTimerType(Qt::PreciseTimer);
  connect(m_socket, &QUdpSocket::readyRead, this, &LanBrowser::readDatagrams);
  connect(m_expiryTimer, &QTimer::timeout, this, &LanBrowser::expire);
  connect(m_changeTimer, &QTimer::timeout, this, &LanBrowser::emitChanged);
}

bool LanBrowser::start(quint16 port)
{
  stop();
  if (!m_socket->bind(QHostAddress::AnyIPv4, port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
    qWarning().noquote() << "lan browser could not listen on udp port" << port << m_socket->errorString();
    return false;
  }
  m_expiryTimer->start(std::clamp(m_expiryMs / 4, 1, 1000));
  return true;
}

void LanBrowser::stop()
{
  m_socket->close();
  m_expiryTimer->stop();
  // a change that was held back is told with this one
  const bool held = m_changeTimer->isActive();
  m_changeTimer->stop();
  if (held || !m_hosts.isEmpty()) {
    m_hosts.clear();
    emitChanged();
  }
}

quint16 LanBrowser::port() const
{
  return m_socket->localPort();
}

void LanBrowser::setOwnId(const QString &id)
{
  m_ownId = id;
  if (m_hosts.remove(id)) {
    notifyChanged();
  }
}

QList<DiscoveredHost> LanBrowser::hosts() const
{
  auto hosts = m_hosts.values();
  std::ranges::sort(hosts, [](const DiscoveredHost &a, const DiscoveredHost &b) {
    const auto order = a.name.compare(b.name, Qt::CaseInsensitive);
    return order != 0 ? order < 0 : a.id < b.id;
  });
  return hosts;
}

void LanBrowser::setExpiryForTest(int ms)
{
  m_expiryMs = ms;
}

void LanBrowser::setChangeIntervalForTest(int ms)
{
  m_changeIntervalMs = ms;
}

void LanBrowser::setSenderForTest(const std::function<QHostAddress(const QHostAddress &, quint16)> &sender)
{
  m_sender = sender;
}

// The cheap checks come first, so that what doesn't change the list costs little
void LanBrowser::readDatagrams()
{
  // one byte over the limit, so an oversized datagram is rejected rather than cut down to size
  char buffer[kMaxBeaconSize + 1];
  const auto now = m_clock.elapsed();
  bool changed = false;
  int count = 0;
  for (; count < kMaxDatagramsPerPass && m_socket->hasPendingDatagrams(); ++count) {
    QHostAddress sender;
    quint16 port = 0;
    const auto size = m_socket->readDatagram(buffer, sizeof(buffer), &sender, &port);
    if (size < 0) {
      break;
    }
    const auto from = m_sender ? m_sender(sender, port) : sender;
    if (!isLocalPeer(from)) {
      continue;
    }
    const auto announcement = HostAnnouncement::parse(QByteArray::fromRawData(buffer, static_cast<qsizetype>(size)));
    if (!announcement || announcement->id == m_ownId) {
      continue;
    }

    const auto existing = m_hosts.constFind(announcement->id);
    const bool isNew = existing == m_hosts.cend();
    if (isNew && m_hosts.size() >= kMaxHosts) {
      continue;
    }
    if ((isNew || existing->address != from) && hostsFrom(from, announcement->id) >= kMaxHostsPerAddress) {
      continue;
    }
    changed = changed || isNew || static_cast<const HostAnnouncement &>(*existing) != *announcement ||
              existing->address != from;
    m_hosts.insert(announcement->id, DiscoveredHost{*announcement, from, now});
  }
  if (changed) {
    notifyChanged();
  }
  // a flood is read a little at a time, so that the rest of the application gets its turn
  if (count == kMaxDatagramsPerPass && m_socket->hasPendingDatagrams()) {
    QMetaObject::invokeMethod(this, &LanBrowser::readDatagrams, Qt::QueuedConnection);
  }
}

void LanBrowser::expire()
{
  const auto now = m_clock.elapsed();
  const auto expired =
      m_hosts.removeIf([this, now](decltype(m_hosts)::iterator host) { return now - host->lastSeenMs > m_expiryMs; });
  if (expired > 0) {
    notifyChanged();
  }
}

// Emits hostsChanged() at once when the last one is long enough ago, otherwise one to follow that
void LanBrowser::notifyChanged()
{
  if (m_changeTimer->isActive()) {
    return;
  }
  const auto wait = m_lastChangeMs ? *m_lastChangeMs + m_changeIntervalMs - m_clock.elapsed() : 0;
  if (wait > 0) {
    m_changeTimer->start(static_cast<int>(wait));
  } else {
    emitChanged();
  }
}

void LanBrowser::emitChanged()
{
  m_lastChangeMs = m_clock.elapsed();
  Q_EMIT hostsChanged();
}

// How many hosts other than this one announce from this address
int LanBrowser::hostsFrom(const QHostAddress &address, const QString &exceptId) const
{
  int count = 0;
  for (const auto &host : m_hosts) {
    if (host.address == address && host.id != exceptId) {
      ++count;
    }
  }
  return count;
}

} // namespace deskflow::gui::lan
