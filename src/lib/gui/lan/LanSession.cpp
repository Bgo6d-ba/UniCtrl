/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "LanSession.h"

#include "gui/TlsUtility.h"
#include "net/Fingerprint.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace deskflow::gui::lan {

namespace {

using CoreMode = Settings::CoreMode;
using JoinState = HostJoinWidget::JoinState;
using deskflow::core::ConnectionState;
using deskflow::core::ProcessState;

// A restart of the core stops it and starts it again at once; this is how long it may stay stopped before the
// session takes it for stopped
constexpr int kCoreStopGraceMs = 2000;

// The most a list setting keeps, oldest first out: whoever has the code can pair over and over
constexpr int kMaxSavedEntries = 256;

CoreMode coreMode()
{
  return Settings::value(Settings::Core::CoreMode).value<CoreMode>();
}

quint16 corePort()
{
  return static_cast<quint16>(Settings::value(Settings::Core::Port).toInt());
}

// The maps of a list setting are told apart by their value under key
QVariantMap findEntry(const QString &setting, const QString &key, const QString &value)
{
  const auto entries = Settings::value(setting).toList();
  const auto entry =
      std::ranges::find_if(entries, [&](const QVariant &item) { return item.toMap().value(key).toString() == value; });
  return entry == entries.cend() ? QVariantMap() : entry->toMap();
}

void saveEntry(const QString &setting, const QString &key, const QVariantMap &map)
{
  auto entries = Settings::value(setting).toList();
  const auto entry =
      std::ranges::find_if(entries, [&](const QVariant &item) { return item.toMap().value(key) == map.value(key); });
  if (entry == entries.end()) {
    entries.append(map);
  } else {
    *entry = map;
  }
  while (entries.size() > kMaxSavedEntries)
    entries.removeFirst();
  Settings::setValue(setting, entries);
}

// The host is not joined without a code any more
void forgetPairedHost(const QString &id)
{
  auto entries = Settings::value(Settings::Lan::PairedHosts).toList();
  entries.removeIf([&id](const QVariant &item) { return item.toMap().value(QStringLiteral("id")).toString() == id; });
  Settings::setValue(Settings::Lan::PairedHosts, entries.isEmpty() ? QVariant() : QVariant(entries));
}

// The address part of the "address:port" that a join saves as the host of the client
QString addressOf(const QString &remoteHost)
{
  const auto colon = remoteHost.indexOf(u':');
  return colon > 0 && colon == remoteHost.lastIndexOf(u':') ? remoteHost.left(colon) : remoteHost;
}

// The fingerprint of a peer is trusted by the core once it is in the database, which is a file that may not have a
// folder yet
bool trust(const QString &database, const QString &fingerprint)
{
  QDir().mkpath(QFileInfo(database).absolutePath());
  return TlsUtility::addTrusted(database, {QCryptographicHash::Sha256, QByteArray::fromHex(fingerprint.toLatin1())});
}

// Pairing only protects the host when the core checks certificates. True when a setting had to be changed.
bool forceSecurity()
{
  bool changed = false;
  for (const auto &key : {Settings::Security::TlsEnabled, Settings::Security::CheckPeers}) {
    if (!Settings::value(key).toBool()) {
      Settings::setValue(key, true);
      changed = true;
    }
  }
  return changed;
}

} // namespace

LanSession::LanSession(QObject *parent)
    : QObject(parent),
      m_announcer(new LanAnnouncer(this)),
      m_browser(new LanBrowser(this)),
      m_server(new PairingServer(this)),
      m_client(new PairingClient(this)),
      m_coreStopTimer(new QTimer(this)),
      m_joinWatchdog(new QTimer(this))
{
  m_coreStopTimer->setSingleShot(true);
  m_coreStopTimer->setInterval(kCoreStopGraceMs);
  connect(m_coreStopTimer, &QTimer::timeout, this, &LanSession::coreStopped);

  m_joinWatchdog->setSingleShot(true);
  m_joinWatchdog->setInterval(kJoinWatchdogMs);
  connect(m_joinWatchdog, &QTimer::timeout, this, &LanSession::joinTimedOut);

  connect(m_browser, &LanBrowser::hostsChanged, this, &LanSession::publishHosts);

  connect(m_server, &PairingServer::paired, this, &LanSession::pairedWithClient);
  connect(m_server, &PairingServer::attemptFailed, this, [this](const QHostAddress &from) {
    Q_EMIT error(tr("A computer at %1 entered a wrong code.").arg(from.toString()));
  });
  connect(m_server, &PairingServer::lockedOut, this, &LanSession::pairingLockedOut);

  connect(m_client, &PairingClient::paired, this, &LanSession::pairedWithHost);
  connect(m_client, &PairingClient::failed, this, &LanSession::pairingFailed);
}

void LanSession::setNetworkForTest(const Network &network)
{
  m_network = network;
}

quint16 LanSession::browsePort() const
{
  return m_browser->port();
}

void LanSession::setCoreStopGraceForTest(int ms)
{
  m_coreStopTimer->setInterval(ms);
}

void LanSession::setJoinWatchdogForTest(int ms)
{
  m_joinWatchdog->setInterval(ms);
}

//
// Hosting
//

void LanSession::startHosting()
{
  beginHosting(false);
}

// An adopted server runs already: nothing is forced or restarted, and the core is not asked to start. What Host
// asks for is a server with the security that pairing relies on, run again when it runs.
bool LanSession::beginHosting(bool adopt)
{
  if (m_hosting)
    return true;

  const auto me = ownIdentity(corePort());
  if (!me)
    return false;

  const auto saved = Settings::value(Settings::Lan::Password).toString();
  m_customPassword = saved.size() >= HostJoinWidget::MinCustomPasswordLength;
  if (!saved.isEmpty() && !m_customPassword)
    Settings::setValue(Settings::Lan::Password); // one of before the minimum, too easy to guess to be kept
  m_password = m_customPassword ? saved : randomCode();

  m_server->setIdentity(*me);
  m_server->setPassword(m_password);
  if (!m_server->listen(m_network.pairingPort)) {
    m_server->setPassword({});
    m_password.clear();
    m_notice.clear();
    Q_EMIT error(
        tr("Computers can't join: port %1 is in use. Is another copy of this app hosting?").arg(m_network.pairingPort)
    );
    return false;
  }

  // Hosting wins over joining
  cancelJoin();
  if (!adopt) {
    forceSecurity();
    Settings::setValue(Settings::Core::CoreMode, CoreMode::Server);
    Settings::setValue(Settings::Lan::HostingEnabled, true);
  }
  m_fingerprint = me->fingerprint;
  m_hosting = true;
  m_pairedCount = 0;
  m_capped = false;
  setLegacy(false);
  updateBrowser();

  if (!m_network.announceTargets.isEmpty())
    m_announcer->setTargetsForTest(m_network.announceTargets, m_network.announcePort);
  m_announcer->setAnnouncement({Settings::hostId(), me->displayName, m_server->port(), me->corePort});
  m_announcer->start();
  qInfo().noquote() << (adopt ? "hosting again as" : "hosting as") << me->displayName;

  Q_EMIT codeChanged(m_customPassword ? QString() : m_password);
  Q_EMIT joinedComputersChanged(displayNames(m_connectedClients));
  Q_EMIT hostingChanged(true);
  flushNotice();

  if (!adopt)
    Q_EMIT startCoreRequested(CoreMode::Server);
  return true;
}

void LanSession::stopHosting()
{
  if (!m_hosting)
    return;
  endHosting();
  requestCoreStop();
}

// Ends hosting without touching the core, which may have stopped or changed mode on its own. A server that runs
// after this is not one that Host started.
void LanSession::endHosting()
{
  qInfo("hosting ended");
  m_hosting = false;
  m_announcer->stop();
  m_server->close();
  m_server->setPassword({});
  m_password.clear();
  Settings::setValue(Settings::Lan::HostingEnabled);

  Q_EMIT codeChanged({});
  Q_EMIT joinedComputersChanged({});
  Q_EMIT hostingChanged(false);
  updateBrowser();
}

void LanSession::newCode()
{
  if (!m_hosting)
    return;
  Settings::setValue(Settings::Lan::Password);
  usePassword(randomCode(), false);
}

void LanSession::setCustomPassword(const QString &password)
{
  if (!m_hosting)
    return;
  if (password.size() < HostJoinWidget::MinCustomPasswordLength) {
    qWarning("password is too short, keeping the current one");
    Q_EMIT error(tr("A password needs at least %n characters.", nullptr, HostJoinWidget::MinCustomPasswordLength));
    return;
  }
  Settings::setValue(Settings::Lan::Password, password);
  usePassword(password, true);
}

// A new secret is a new start: the pairings so far do not count against it, and a notice about the old one is not
// true any more
void LanSession::usePassword(const QString &password, bool custom)
{
  m_customPassword = custom;
  m_password = password;
  m_pairedCount = 0;
  m_server->setPassword(password);
  Q_EMIT codeChanged(custom ? QString() : password);
  if (std::exchange(m_capped, false))
    Q_EMIT error({});
}

// Somebody guessed wrong too often. A code that was made for the user is replaced by a new one, so that what was
// learnt about it is worth nothing; a password of the user's own stays, and only the pause protects it.
void LanSession::pairingLockedOut(int seconds)
{
  if (m_customPassword) {
    Q_EMIT error(tr("Too many wrong codes. Joining is paused for %n second(s).", nullptr, seconds));
    return;
  }
  newCode();
  Q_EMIT error(tr("Too many wrong codes — a new code was generated."));
}

void LanSession::refresh()
{
  if (!m_hosting)
    return;

  if (forceSecurity())
    Q_EMIT error(tr("Encryption stays on while this computer hosts, so that only computers that joined can connect."));

  const auto fingerprint = QString::fromLatin1(TlsUtility::certFingerprint().toHex());
  const PeerIdentity me{
      Settings::displayName(), Settings::value(Settings::Core::ComputerName).toString(),
      fingerprint.isEmpty() ? m_fingerprint : fingerprint, corePort()
  };
  if (!me.isValid()) {
    qWarning("name, port or certificate can't be used for hosting, keeping the old ones");
    return;
  }
  m_fingerprint = me.fingerprint;
  m_server->setIdentity(me);
  m_announcer->setAnnouncement({Settings::hostId(), me.displayName, m_server->port(), me.corePort});
}

// This computer's identity for pairing, with an error shown when it can't be used. A certificate that exists is
// kept, valid or not: replacing it would end every pairing that pinned it, so a new one is told about.
std::optional<PeerIdentity> LanSession::ownIdentity(quint16 corePort)
{
  const bool created = !QFile::exists(Settings::value(Settings::Security::Certificate).toString());
  const auto fingerprint = QString::fromLatin1(TlsUtility::ensureCertificate(false).toHex());
  if (fingerprint.isEmpty()) {
    Q_EMIT error(tr("Could not create or read the certificate that identifies this computer."));
    return std::nullopt;
  }

  PeerIdentity me{
      Settings::displayName(), Settings::value(Settings::Core::ComputerName).toString(), fingerprint, corePort
  };
  if (!me.isValid()) {
    Q_EMIT error(
        tr("The name of this computer can only use letters, numbers, - and _ to host or join. Change it "
           "under Advanced.")
    );
    return std::nullopt;
  }
  if (created)
    m_notice =
        tr("A new certificate was created for this computer, so the computers that joined before have to join "
           "again.");
  return me;
}

// A notice is shown once the view has changed to the page it belongs on, which clears what was shown before
void LanSession::flushNotice()
{
  if (!m_notice.isEmpty())
    Q_EMIT error(std::exchange(m_notice, {}));
}

// What the core reports until it has stopped is about what was, and is not asked about
void LanSession::requestCoreStop()
{
  m_stopRequested = true;
  Q_EMIT stopCoreRequested();
}

void LanSession::pairedWithClient(const PeerIdentity &client, const QHostAddress &from)
{
  if (!trust(Settings::tlsTrustedClientsDb(), client.fingerprint)) {
    Q_EMIT error(tr("Could not save the certificate of %1, so it can't join.").arg(client.displayName));
    return;
  }

  qInfo().noquote() << "computer joined:" << client.displayName << "as" << client.screenName << "from"
                    << from.toString();
  saveEntry(
      Settings::Lan::ClientNames, QStringLiteral("screen"),
      {{QStringLiteral("screen"), client.screenName}, {QStringLiteral("name"), client.displayName}}
  );
  Q_EMIT addClientScreenRequested(client);
  Q_EMIT clientJoined(client.displayName);
  Q_EMIT joinedComputersChanged(displayNames(m_connectedClients));

  // Trust does not end, so whoever has the code can only add so many computers to it
  if (++m_pairedCount >= kMaxPairingsPerSession) {
    m_server->setPassword({});
    m_capped = true;
    Q_EMIT error(
        tr("%1 computers have joined. Nobody else can join until you choose New code.").arg(kMaxPairingsPerSession)
    );
  }
}

QStringList LanSession::displayNames(const QStringList &screenNames) const
{
  QStringList names;
  for (const auto &screen : screenNames) {
    const auto name = findEntry(Settings::Lan::ClientNames, QStringLiteral("screen"), screen)
                          .value(QStringLiteral("name"))
                          .toString();
    names.append(name.isEmpty() ? screen : name);
  }
  return names;
}

//
// Browsing
//

void LanSession::startBrowsing()
{
  m_browsing = true;
  updateBrowser();
}

void LanSession::stopBrowsing()
{
  m_browsing = false;
  updateBrowser();
}

void LanSession::updateBrowser()
{
  const bool run = m_browsing && !m_hosting;
  if (run == m_browserRunning)
    return;

  m_browserRunning = run;
  if (!run) {
    m_browser->stop();
    return;
  }
  m_browser->setOwnId(Settings::hostId());
  if (!m_browser->start(m_network.browsePort)) {
    m_browserRunning = false;
    Q_EMIT error(tr("Could not listen for hosts on this network."));
  }
}

void LanSession::publishHosts()
{
  QSet<QString> pairedIds;
  const auto pairedHosts = Settings::value(Settings::Lan::PairedHosts).toList();
  for (const auto &entry : pairedHosts)
    pairedIds.insert(entry.toMap().value(QStringLiteral("id")).toString());

  QList<HostListEntry> entries;
  const auto hosts = m_browser->hosts();
  for (const auto &host : hosts) {
    const bool paired = pairedIds.contains(host.id);
    entries.append({host.id, host.name, paired, !paired});
  }
  Q_EMIT hostsChanged(entries);
}

std::optional<DiscoveredHost> LanSession::nearbyHost(const QString &hostId) const
{
  const auto hosts = m_browser->hosts();
  const auto host = std::ranges::find(hosts, hostId, &DiscoveredHost::id);
  if (host == hosts.cend())
    return std::nullopt;
  return *host;
}

//
// Joining
//

void LanSession::join(const QString &hostId, const QString &code)
{
  if (m_hosting)
    return;

  const auto host = nearbyHost(hostId);
  if (!host) {
    Q_EMIT error(tr("That computer is not nearby any more."));
    return;
  }
  const auto me = ownIdentity(0);
  if (!me)
    return;

  leave();
  m_target = *host;
  m_pairing = true;
  setJoinState(JoinState::Joining);
  flushNotice();
  m_client->pair(host->address, host->pairPort, code, *me);
}

// The address and the ports are the ones the host announces now, which are not to be trusted: the certificate that
// the client checks against is the one this computer paired with, so a host that is not the same fails to connect
void LanSession::rejoin(const QString &hostId)
{
  if (m_hosting)
    return;

  const auto paired = findEntry(Settings::Lan::PairedHosts, QStringLiteral("id"), hostId);
  if (paired.isEmpty()) {
    Q_EMIT error(tr("This computer has to join with a code first."));
    return;
  }
  const auto host = nearbyHost(hostId);
  if (!host) {
    Q_EMIT error(tr("Host not found on the network"));
    return;
  }

  const auto name = paired.value(QStringLiteral("name")).toString();
  const auto fingerprint = paired.value(QStringLiteral("fingerprint")).toString();
  if (!isValidFingerprint(fingerprint)) {
    forgetPairedHost(hostId);
    publishHosts();
    Q_EMIT error(tr("What this computer saved of %1 is not valid. Join again with its code.").arg(name));
    return;
  }
  if (!ownIdentity(0))
    return;
  if (!trust(Settings::tlsTrustedServersDb(), fingerprint)) {
    m_notice.clear();
    Q_EMIT error(tr("Could not save the certificate of %1.").arg(name));
    return;
  }

  leave();
  m_target = *host;
  m_target.name = name;
  m_rejoin = true;
  saveEntry(
      Settings::Lan::PairedHosts, QStringLiteral("id"),
      {{QStringLiteral("id"), hostId},
       {QStringLiteral("name"), name},
       {QStringLiteral("address"), host->address.toString()},
       {QStringLiteral("fingerprint"), fingerprint}}
  );
  startClient();
  flushNotice();
}

void LanSession::leave()
{
  if (m_joinState == JoinState::Idle)
    return;
  const bool clientAsked = !m_pairing;
  cancelJoin();
  if (clientAsked)
    requestCoreStop();
}

bool LanSession::rejectUntrustedPeer()
{
  if (m_hosting || m_stopRequested) {
    qInfo("ignoring a computer that did not pair, its certificate is not trusted");
    return true;
  }
  // A client that runs since before is only shown, and asks the user as it always did
  if (m_joinState == JoinState::Idle || m_pairing || m_adopted)
    return false;

  qWarning().noquote() << "the certificate of" << m_target.name << "is not the one it paired with";
  failJoin(tr("This host's identity changed. Join again with its code."), true);
  return true;
}

void LanSession::pairedWithHost(const PeerIdentity &host)
{
  m_pairing = false;
  if (!trust(Settings::tlsTrustedServersDb(), host.fingerprint)) {
    cancelJoin();
    Q_EMIT error(tr("Could not save the certificate of %1.").arg(host.displayName));
    return;
  }

  // What the host proved is used rather than what it announced
  m_target.name = host.displayName;
  m_target.corePort = host.corePort;
  saveEntry(
      Settings::Lan::PairedHosts, QStringLiteral("id"),
      {{QStringLiteral("id"), m_target.id},
       {QStringLiteral("name"), host.displayName},
       {QStringLiteral("address"), m_target.address.toString()},
       {QStringLiteral("fingerprint"), host.fingerprint}}
  );
  startClient();
}

void LanSession::pairingFailed(PairingError reason, const QString &detail)
{
  qWarning().noquote() << "pairing with" << m_target.name << "failed:" << detail;
  const auto name = m_target.name;
  cancelJoin();

  using enum PairingError;
  switch (reason) {
  case WrongCode:
    Q_EMIT error(tr("Wrong code. Check the code shown on %1.").arg(name));
    break;
  case LockedOut:
    Q_EMIT error(tr("%1 is not accepting codes for a minute, because too many wrong ones were entered.").arg(name));
    break;
  case Unreachable:
    Q_EMIT error(tr("Could not reach %1. Both computers need to be on the same network, and a firewall must allow "
                    "this app.")
                     .arg(name));
    break;
  case Timeout:
    Q_EMIT error(tr("%1 did not answer.").arg(name));
    break;
  case Protocol:
    Q_EMIT error(
        tr("Could not join %1: its answer was not valid, or it has the same computer name as this one.").arg(name)
    );
    break;
  }
}

// Points the client at m_target and asks for it to start. The host is saved with its port, so that Core::Port stays
// the one of the server that this computer may host.
void LanSession::startClient()
{
  Settings::setValue(
      Settings::Client::RemoteHost, QStringLiteral("%1:%2").arg(m_target.address.toString()).arg(m_target.corePort)
  );
  if (!Settings::value(Settings::Security::TlsEnabled).toBool())
    Settings::setValue(Settings::Security::TlsEnabled, true);
  Settings::setValue(Settings::Core::CoreMode, CoreMode::Client);
  setJoinState(JoinState::Joining);
  publishHosts();
  m_joinWatchdog->start();
  Q_EMIT startCoreRequested(CoreMode::Client);
}

// A client that runs already is shown as joined to the host with its address, or else to that address
void LanSession::adoptClient()
{
  const auto remoteHost = Settings::value(Settings::Client::RemoteHost).toString();
  if (remoteHost.isEmpty())
    return;

  const auto host = findEntry(Settings::Lan::PairedHosts, QStringLiteral("address"), addressOf(remoteHost));
  m_target = {};
  m_target.id = host.value(QStringLiteral("id")).toString();
  m_target.name = host.isEmpty() ? remoteHost : host.value(QStringLiteral("name")).toString();
  m_pairing = false;
  m_adopted = true;
  setJoinState(m_connection == ConnectionState::Connected ? JoinState::Connected : JoinState::Joining);
}

void LanSession::cancelJoin()
{
  m_client->cancel();
  m_joinWatchdog->stop();
  m_pairing = false;
  m_adopted = false;
  m_rejoin = false;
  if (m_joinState != JoinState::Idle)
    setJoinState(JoinState::Idle);
}

// The join is over and the client is not wanted. The host is forgotten when it can't be joined without a code any
// more: the certificate it answers with is not the one that was trusted, or it can't be reached at all.
void LanSession::failJoin(const QString &text, bool forgetHost)
{
  const auto hostId = m_target.id;
  const bool clientAsked = !m_pairing;
  cancelJoin();
  if (clientAsked)
    requestCoreStop();
  if (forgetHost && !hostId.isEmpty()) {
    forgetPairedHost(hostId);
    publishHosts();
  }
  Q_EMIT error(text);
}

// The client did not connect in time: the host is not there, or not the one that was joined, and would not tell
void LanSession::joinTimedOut()
{
  if (m_joinState != JoinState::Joining || m_pairing || m_adopted)
    return;

  qWarning().noquote() << "joining" << m_target.name << "timed out";
  if (m_rejoin) {
    failJoin(tr("Could not connect to %1. Join it again with its code.").arg(m_target.name), true);
  } else {
    failJoin(
        tr("Could not connect to %1. Both computers need to be on the same network, and a firewall must allow this "
           "app.")
            .arg(m_target.name),
        false
    );
  }
}

void LanSession::setJoinState(JoinState state)
{
  m_joinState = state;
  Q_EMIT joinStateChanged(state, state == JoinState::Idle ? QString() : m_target.name);
}

//
// The core
//

void LanSession::coreProcessStateChanged(ProcessState state)
{
  m_coreState = state;

  if (state == ProcessState::Stopped || state == ProcessState::Starting) {
    m_stopRequested = false;
    m_connectedClients.clear();
    if (m_hosting)
      Q_EMIT joinedComputersChanged({});
  }

  if (state == ProcessState::Stopped) {
    setLegacy(false);
    if (m_hosting || (m_joinState != JoinState::Idle && !m_pairing))
      m_coreStopTimer->start();
    return;
  }
  if (state == ProcessState::Stopping)
    return;

  m_coreStopTimer->stop();
  reconcile();
}

void LanSession::coreConnectionStateChanged(ConnectionState state)
{
  m_connection = state;
  if (m_joinState == JoinState::Idle || m_pairing)
    return;

  if (state == ConnectionState::Connected)
    m_joinWatchdog->stop();
  const auto joinState = state == ConnectionState::Connected ? JoinState::Connected : JoinState::Joining;
  if (joinState != m_joinState)
    setJoinState(joinState);
}

void LanSession::connectedClientsChanged(const QStringList &screenNames)
{
  m_connectedClients = screenNames;
  if (m_hosting)
    Q_EMIT joinedComputersChanged(displayNames(screenNames));
}

void LanSession::setSimpleView(bool simple)
{
  m_simple = simple;
  if (!simple)
    stopBrowsing();
  reconcile();
}

// Makes the session agree with a core that started, or changed mode, without it
void LanSession::reconcile()
{
  const auto mode = coreMode();
  if (m_hosting && mode != CoreMode::Server)
    endHosting();
  if (m_joinState != JoinState::Idle && !m_pairing && mode != CoreMode::Client)
    cancelJoin();

  if (!m_simple || m_hosting || mode != CoreMode::Server)
    setLegacy(false);
  if (!m_simple || m_coreState != ProcessState::Started || m_hosting || m_joinState != JoinState::Idle)
    return;

  if (mode == CoreMode::Client) {
    adoptClient();
  } else if (mode == CoreMode::Server) {
    // Only what Host started is hosted again, and as it is. Anything else was set up by hand and is left alone.
    const bool secure = Settings::value(Settings::Security::TlsEnabled).toBool() &&
                        Settings::value(Settings::Security::CheckPeers).toBool();
    if (secure && Settings::value(Settings::Lan::HostingEnabled).toBool() && beginHosting(true))
      return;
    Settings::setValue(Settings::Lan::HostingEnabled);
    setLegacy(true);
  }
}

void LanSession::setLegacy(bool legacy)
{
  if (legacy == m_legacy)
    return;
  m_legacy = legacy;
  Q_EMIT legacySessionChanged(legacy);
}

// The core stopped by itself, or was stopped elsewhere, and did not start again
void LanSession::coreStopped()
{
  if (m_hosting) {
    endHosting();
    Q_EMIT error(tr("Hosting ended because the server stopped. The log shows why."));
  }
  if (m_joinState != JoinState::Idle && !m_pairing) {
    cancelJoin();
    Q_EMIT error(tr("Joining ended because the client stopped. The log shows why."));
  }
}

} // namespace deskflow::gui::lan
