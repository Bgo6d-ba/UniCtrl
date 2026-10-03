/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "LanSessionTests.h"

#include "../../TestSettings.h"

#include "common/Settings.h"
#include "gui/TlsUtility.h"
#include "gui/config/Screen.h"
#include "gui/lan/ClientScreenQueue.h"
#include "gui/lan/LanSession.h"
#include "net/Fingerprint.h"

#include <QFile>
#include <QHostAddress>
#include <QNetworkDatagram>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>

using namespace deskflow::gui;
using namespace deskflow::gui::lan;
using CoreMode = Settings::CoreMode;
using JoinState = HostJoinWidget::JoinState;
using deskflow::core::ConnectionState;
using deskflow::core::ProcessState;

namespace {

struct Computer
{
  QString computerName;
  QString displayName;
  QString id;
  QString certificate;
  int corePort = 0;
};

// The settings are shared by everything in the process, so a test switches them to the computer that acts. A session
// reads them when it starts hosting or joining, not later.
Computer s_host;
Computer s_joiner;

// Where the beacons of sessions that host without a listener go
QUdpSocket *s_sink = nullptr;

void use(const Computer &computer)
{
  Settings::setValue(Settings::Core::ComputerName, computer.computerName);
  Settings::setValue(Settings::Core::DisplayName, computer.displayName);
  Settings::setValue(Settings::Lan::HostId, computer.id);
  Settings::setValue(Settings::Security::Certificate, computer.certificate);
  Settings::setValue(Settings::Core::Port, computer.corePort);
}

QHostAddress loopback()
{
  return QHostAddress(QHostAddress::LocalHost);
}

// Any port a session picks, and its announcements go to a socket that nobody reads
void isolate(LanSession &session)
{
  session.setNetworkForTest(
      {.browsePort = 0, .announcePort = s_sink->localPort(), .pairingPort = 0, .announceTargets = {loopback()}}
  );
}

CoreMode coreMode()
{
  return Settings::value(Settings::Core::CoreMode).value<CoreMode>();
}

Fingerprint fingerprintOf(const Computer &computer)
{
  return {QCryptographicHash::Sha256, TlsUtility::certFingerprint(computer.certificate)};
}

QString fingerprintText(const Computer &computer)
{
  return QString::fromLatin1(TlsUtility::certFingerprint(computer.certificate).toHex());
}

bool trustedByJoiner(const Computer &host)
{
  return TlsUtility::isTrusted(Settings::tlsTrustedServersDb(), fingerprintOf(host));
}

bool trustedByHost(const Computer &joiner)
{
  return TlsUtility::isTrusted(Settings::tlsTrustedClientsDb(), fingerprintOf(joiner));
}

QVariantList pairedHosts()
{
  return Settings::value(Settings::Lan::PairedHosts).toList();
}

QList<HostListEntry> lastHosts(const QSignalSpy &spy)
{
  return spy.isEmpty() ? QList<HostListEntry>() : spy.last().at(0).value<QList<HostListEntry>>();
}

JoinState lastJoinState(const QSignalSpy &spy)
{
  return spy.last().at(0).value<JoinState>();
}

QString lastText(const QSignalSpy &spy)
{
  return spy.last().at(0).toString();
}

bool isCode(const QString &text)
{
  static const QRegularExpression sixDigits(QStringLiteral("^[0-9]{6}$"));
  return sixDigits.match(text).hasMatch();
}

bool hostingEnabled()
{
  return Settings::value(Settings::Lan::HostingEnabled).toBool();
}

// A computer that could pair, each with a certificate of its own
PeerIdentity guest(int number)
{
  return {
      QStringLiteral("Guest %1").arg(number), QStringLiteral("guest-%1").arg(number),
      QStringLiteral("%1").arg(number, 64, 16, QLatin1Char('0')), 0
  };
}

void drain(QUdpSocket &socket)
{
  while (socket.hasPendingDatagrams())
    socket.receiveDatagram();
}

// What Host leaves behind: the flag, and a server that has the security that pairing relies on
void hostedServerRuns()
{
  Settings::setValue(Settings::Core::CoreMode, CoreMode::Server);
  Settings::setValue(Settings::Lan::HostingEnabled, true);
  Settings::setValue(Settings::Security::TlsEnabled, true);
  Settings::setValue(Settings::Security::CheckPeers, true);
}

// A computer that looks for hosts and one that hosts, in this process and over localhost
struct Rig
{
  LanSession joiner;
  LanSession host;
  QSignalSpy hosts{&joiner, &LanSession::hostsChanged};
  QSignalSpy joinStates{&joiner, &LanSession::joinStateChanged};
  QSignalSpy joinerStarts{&joiner, &LanSession::startCoreRequested};
  QSignalSpy joinerStops{&joiner, &LanSession::stopCoreRequested};
  QSignalSpy joinerErrors{&joiner, &LanSession::error};
  QSignalSpy codes{&host, &LanSession::codeChanged};
  QSignalSpy hostErrors{&host, &LanSession::error};
  QSignalSpy screens{&host, &LanSession::addClientScreenRequested};
  QSignalSpy joined{&host, &LanSession::clientJoined};
  QSignalSpy joinedComputers{&host, &LanSession::joinedComputersChanged};

  // True when the joiner lists the host. Leaves the settings of the joiner in place.
  bool start()
  {
    use(s_joiner);
    isolate(joiner);
    joiner.startBrowsing();
    use(s_host);
    host.setNetworkForTest(
        {.browsePort = 0, .announcePort = joiner.browsePort(), .pairingPort = 0, .announceTargets = {loopback()}}
    );
    host.startHosting();
    use(s_joiner);
    return QTest::qWaitFor([this] { return !lastHosts(hosts).isEmpty(); }, 10000);
  }

  QString code() const
  {
    return lastText(codes);
  }

  // True when the client is asked for
  bool join()
  {
    joiner.join(s_host.id, code());
    return QTest::qWaitFor([this] { return joinerStarts.count() == 1; }, 10000);
  }
};

} // namespace

void LanSessionTests::initTestCase()
{
  QVERIFY(m_dir.isValid());
  useTestSettings(m_dir);
  s_sink = new QUdpSocket(this);
  QVERIFY(s_sink->bind(loopback(), 0));

  s_host = {
      QStringLiteral("host-pc"), QStringLiteral("Host PC"), QStringLiteral("0b6f3f9e-6a3c-4a8e-9d51-3c7a2f1e8b20"),
      m_dir.filePath(QStringLiteral("tls/host.pem")), 24811
  };
  s_joiner = {
      QStringLiteral("join-pc"), QStringLiteral("Joiner"), QStringLiteral("5d0c2f7e-91b4-4c1e-8a3d-7e6f1a2b9c40"),
      m_dir.filePath(QStringLiteral("tls/joiner.pem")), 24822
  };
  for (const auto &computer : {s_host, s_joiner}) {
    use(computer);
    QVERIFY(!TlsUtility::ensureCertificate(false).isEmpty());
  }
}

void LanSessionTests::init()
{
  use(s_host);
  Settings::setValue(Settings::Lan::Password);
  Settings::setValue(Settings::Lan::PairedHosts);
  Settings::setValue(Settings::Lan::ClientNames);
  Settings::setValue(Settings::Client::RemoteHost);
  Settings::setValue(Settings::Core::CoreMode, CoreMode::None);
  Settings::setValue(Settings::Lan::HostingEnabled);
  Settings::setValue(Settings::Security::KeySize);
  Settings::setValue(Settings::Security::TlsEnabled, false);
  Settings::setValue(Settings::Security::CheckPeers, false);
  QFile::remove(Settings::tlsTrustedClientsDb());
  QFile::remove(Settings::tlsTrustedServersDb());
}

void LanSessionTests::hosting_announcesAndForcesSecurity()
{
  QUdpSocket listener;
  QVERIFY(listener.bind(loopback(), 0));
  LanSession session;
  session.setNetworkForTest(
      {.browsePort = 0, .announcePort = listener.localPort(), .pairingPort = 0, .announceTargets = {loopback()}}
  );
  QSignalSpy hosting(&session, &LanSession::hostingChanged);
  QSignalSpy codes(&session, &LanSession::codeChanged);
  QSignalSpy starts(&session, &LanSession::startCoreRequested);
  QSignalSpy stops(&session, &LanSession::stopCoreRequested);
  QSignalSpy errors(&session, &LanSession::error);

  session.startHosting();

  QVERIFY(session.isHosting());
  QCOMPARE(hosting.count(), 1);
  QVERIFY(hosting.last().at(0).toBool());
  QCOMPARE(codes.count(), 1);
  QVERIFY(isCode(lastText(codes)));
  QCOMPARE(starts.count(), 1);
  QCOMPARE(starts.first().at(0).value<CoreMode>(), CoreMode::Server);
  QVERIFY(errors.isEmpty());
  QVERIFY(Settings::value(Settings::Security::TlsEnabled).toBool());
  QVERIFY(Settings::value(Settings::Security::CheckPeers).toBool());
  QCOMPARE(coreMode(), CoreMode::Server);
  QVERIFY(hostingEnabled());

  QTRY_VERIFY(listener.hasPendingDatagrams());
  const auto announced = HostAnnouncement::parse(listener.receiveDatagram().data());
  if (!announced)
    QFAIL("the announcement is not valid");
  QCOMPARE(announced->id, s_host.id);
  QCOMPARE(announced->name, s_host.displayName);
  QCOMPARE(announced->corePort, s_host.corePort);

  // Pairing is where the announcement says
  QTcpSocket pairing;
  pairing.connectToHost(loopback(), announced->pairPort);
  QVERIFY(pairing.waitForConnected(2000));
  pairing.abort();

  session.stopHosting();
  QVERIFY(!session.isHosting());
  QCOMPARE(stops.count(), 1);
  QVERIFY(!hosting.last().at(0).toBool());
  QVERIFY(lastText(codes).isEmpty());
  QVERIFY(!hostingEnabled());

  while (listener.hasPendingDatagrams()) {
    listener.receiveDatagram();
  }
  QTest::qWait(kBeaconIntervalMs + 300);
  QVERIFY(!listener.hasPendingDatagrams());
  pairing.connectToHost(loopback(), announced->pairPort);
  QVERIFY(!pairing.waitForConnected(1000));
}

void LanSessionTests::hosting_portInUse()
{
  QTcpServer busy;
  QVERIFY(busy.listen(QHostAddress(QHostAddress::AnyIPv4), 0));
  LanSession session;
  session.setNetworkForTest(
      {.browsePort = 0, .announcePort = 0, .pairingPort = busy.serverPort(), .announceTargets = {loopback()}}
  );
  QSignalSpy hosting(&session, &LanSession::hostingChanged);
  QSignalSpy starts(&session, &LanSession::startCoreRequested);
  QSignalSpy errors(&session, &LanSession::error);

  QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("pairing server could not listen")));
  session.startHosting();

  QVERIFY(!session.isHosting());
  QVERIFY(hosting.isEmpty());
  QVERIFY(starts.isEmpty());
  QCOMPARE(errors.count(), 1);
  QVERIFY(lastText(errors).contains(QStringLiteral("in use")));
  // Nothing was changed for a hosting that did not start
  QVERIFY(!Settings::value(Settings::Security::TlsEnabled).toBool());
  QCOMPARE(coreMode(), CoreMode::None);
  QVERIFY(!hostingEnabled());
}

void LanSessionTests::hosting_codeAndPassword()
{
  LanSession session;
  isolate(session);
  QSignalSpy codes(&session, &LanSession::codeChanged);
  QSignalSpy errors(&session, &LanSession::error);

  // Not hosting: nothing happens
  session.newCode();
  session.setCustomPassword(QStringLiteral("open sesame"));
  QVERIFY(codes.isEmpty());
  QVERIFY(Settings::value(Settings::Lan::Password).toString().isEmpty());

  session.startHosting();
  QCOMPARE(codes.count(), 1);
  QVERIFY(isCode(lastText(codes)));

  session.newCode();
  QCOMPARE(codes.count(), 2);
  QVERIFY(isCode(lastText(codes)));

  // Eight characters at least, and the user is told why not
  QVERIFY(errors.isEmpty());
  for (const auto &tooShort : {QStringLiteral("12345"), QStringLiteral("1234567")}) {
    QTest::ignoreMessage(QtWarningMsg, "password is too short, keeping the current one");
    session.setCustomPassword(tooShort);
    QCOMPARE(codes.count(), 2);
    QVERIFY(Settings::value(Settings::Lan::Password).toString().isEmpty());
  }
  QCOMPARE(errors.count(), 2);
  QVERIFY(lastText(errors).contains(QStringLiteral("8 characters")));

  session.setCustomPassword(QStringLiteral("12345678"));
  QCOMPARE(codes.count(), 3);
  QVERIFY(lastText(codes).isEmpty());
  QCOMPARE(Settings::value(Settings::Lan::Password).toString(), QStringLiteral("12345678"));

  session.setCustomPassword(QStringLiteral("open sesame"));
  QCOMPARE(codes.count(), 4);
  QVERIFY(lastText(codes).isEmpty());
  QCOMPARE(Settings::value(Settings::Lan::Password).toString(), QStringLiteral("open sesame"));

  session.newCode();
  QCOMPARE(codes.count(), 5);
  QVERIFY(isCode(lastText(codes)));
  QVERIFY(Settings::value(Settings::Lan::Password).toString().isEmpty());

  // A saved password is used from the start, and a saved one that is too short is not, nor kept
  session.stopHosting();
  Settings::setValue(Settings::Lan::Password, QStringLiteral("correct horse"));
  session.startHosting();
  QVERIFY(lastText(codes).isEmpty());

  session.stopHosting();
  Settings::setValue(Settings::Lan::Password, QStringLiteral("1234567"));
  session.startHosting();
  QVERIFY(isCode(lastText(codes)));
  QVERIFY(Settings::value(Settings::Lan::Password).toString().isEmpty());
}

void LanSessionTests::hosting_keepsSecurityOn()
{
  LanSession session;
  isolate(session);
  QSignalSpy errors(&session, &LanSession::error);
  session.startHosting();
  QVERIFY(errors.isEmpty());

  // What the preferences may do while hosting
  Settings::setValue(Settings::Security::CheckPeers, false);
  session.refresh();
  QVERIFY(Settings::value(Settings::Security::CheckPeers).toBool());
  QCOMPARE(errors.count(), 1);

  Settings::setValue(Settings::Security::TlsEnabled, false);
  session.refresh();
  QVERIFY(Settings::value(Settings::Security::TlsEnabled).toBool());
  QCOMPARE(errors.count(), 2);

  session.refresh();
  QCOMPARE(errors.count(), 2);
}

void LanSessionTests::hosting_refreshAnnouncesNewIdentity()
{
  Rig rig;
  QVERIFY(rig.start());
  QCOMPARE(lastHosts(rig.hosts).first().name, s_host.displayName);

  use(s_host);
  Settings::setValue(Settings::Core::DisplayName, QStringLiteral("Renamed PC"));
  Settings::setValue(Settings::Core::Port, 24899);
  rig.host.refresh();
  use(s_joiner);

  QTRY_COMPARE(lastHosts(rig.hosts).first().name, QStringLiteral("Renamed PC"));
  // Pairing tells the new name and port too, which the client is pointed at without changing this computer's own
  QVERIFY(rig.join());
  QCOMPARE(Settings::value(Settings::Client::RemoteHost).toString(), QStringLiteral("127.0.0.1:24899"));
  QCOMPARE(Settings::value(Settings::Core::Port).toInt(), s_joiner.corePort);
  QCOMPARE(pairedHosts().first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Renamed PC"));
}

void LanSessionTests::hosting_setsAndClearsTheFlag()
{
  LanSession session;
  isolate(session);
  QVERIFY(!hostingEnabled());

  session.startHosting();
  QVERIFY(hostingEnabled());
  session.stopHosting();
  QVERIFY(!hostingEnabled());

  // Whatever else ends hosting ends it for the next start too
  session.startHosting();
  session.coreProcessStateChanged(ProcessState::Started);
  QVERIFY(hostingEnabled());
  Settings::setValue(Settings::Core::CoreMode, CoreMode::Client);
  session.coreProcessStateChanged(ProcessState::Starting);
  QVERIFY(!session.isHosting());
  QVERIFY(!hostingEnabled());
}

void LanSessionTests::hosting_capsThePairings()
{
  Rig rig;
  QVERIFY(rig.start());
  auto *server = rig.host.pairingServerForTest();
  const auto from = loopback();

  // A computer that enters the code is one of the sixteen, however it got there
  QVERIFY(rig.join());
  QTRY_COMPARE(rig.screens.count(), 1);
  for (int number = 2; number < kMaxPairingsPerSession; ++number)
    server->paired(guest(number), from);
  QCOMPARE(rig.screens.count(), kMaxPairingsPerSession - 1);
  QVERIFY(rig.hostErrors.isEmpty());

  server->paired(guest(kMaxPairingsPerSession), from);
  QCOMPARE(rig.screens.count(), kMaxPairingsPerSession);
  QCOMPARE(rig.hostErrors.count(), 1);
  QVERIFY(lastText(rig.hostErrors).contains(QStringLiteral("New code")));

  // Nobody else can, whatever code is entered
  rig.joiner.leave();
  rig.joinerStarts.clear();
  QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("pairing with Host PC failed")));
  rig.joiner.join(s_host.id, rig.code());
  QTRY_COMPARE(rig.joinerErrors.count(), 1);
  QCOMPARE(rig.joinerStarts.count(), 0);

  // until there is a new code, which also takes the notice back
  rig.host.newCode();
  QCOMPARE(rig.hostErrors.count(), 2);
  QVERIFY(lastText(rig.hostErrors).isEmpty());
  rig.joiner.join(s_host.id, rig.code());
  QTRY_COMPARE(rig.joinerStarts.count(), 1);

  // The count starts again with it
  for (int number = 100; number < 100 + kMaxPairingsPerSession - 2; ++number)
    server->paired(guest(number), from);
  QCOMPARE(rig.hostErrors.count(), 2);
}

void LanSessionTests::hosting_lastPairingStillCompletes()
{
  // The host stops answering the moment the sixteenth computer is trusted, from inside the slot of `paired`: the
  // sixteenth still has to get the answer that says it joined
  Rig rig;
  QVERIFY(rig.start());
  auto *server = rig.host.pairingServerForTest();
  const auto from = loopback();

  for (int number = 1; number < kMaxPairingsPerSession; ++number)
    server->paired(guest(number), from);
  QCOMPARE(rig.screens.count(), kMaxPairingsPerSession - 1);
  QVERIFY(rig.hostErrors.isEmpty());

  QVERIFY(rig.join());
  QTRY_COMPARE(rig.screens.count(), kMaxPairingsPerSession);
  QCOMPARE(rig.hostErrors.count(), 1);
  QVERIFY(rig.joinerErrors.isEmpty());
}

void LanSessionTests::hosting_keepsACertificateThatIsInvalid()
{
  // A certificate that does not fit the key size is not one to replace: computers that joined have pinned it
  const auto before = fingerprintText(s_host);
  Settings::setValue(Settings::Security::KeySize, 4096);
  QVERIFY(!TlsUtility::isCertValid());

  LanSession session;
  isolate(session);
  QSignalSpy errors(&session, &LanSession::error);
  session.startHosting();

  QVERIFY(session.isHosting());
  QCOMPARE(fingerprintText(s_host), before);
  QVERIFY(errors.isEmpty());
}

void LanSessionTests::hosting_tellsAboutANewCertificate()
{
  Computer fresh = s_host;
  fresh.certificate = m_dir.filePath(QStringLiteral("tls/fresh.pem"));
  QFile::remove(fresh.certificate);
  use(fresh);

  LanSession session;
  isolate(session);
  QSignalSpy hosting(&session, &LanSession::hostingChanged);
  QSignalSpy errors(&session, &LanSession::error);
  session.startHosting();

  // Told once hosting shows, not before: the page it replaces would take the notice with it
  QVERIFY(session.isHosting());
  QVERIFY(QFile::exists(fresh.certificate));
  QCOMPARE(hosting.count(), 1);
  QCOMPARE(errors.count(), 1);
  QVERIFY(lastText(errors).contains(QStringLiteral("have to join again")));

  // and only when it was made
  session.stopHosting();
  session.startHosting();
  QCOMPARE(errors.count(), 1);
}

void LanSessionTests::lockout_replacesAnAutomaticCode()
{
  Rig rig;
  QVERIFY(rig.start());
  QCOMPARE(rig.codes.count(), 1);

  rig.host.pairingServerForTest()->lockedOut(60);

  // What was guessed at is not worth anything now
  QCOMPARE(rig.codes.count(), 2);
  QVERIFY(isCode(rig.code()));
  QVERIFY(lastText(rig.hostErrors).contains(QStringLiteral("new code was generated")));
  QVERIFY(rig.join());
}

void LanSessionTests::lockout_keepsACustomPassword()
{
  Rig rig;
  QVERIFY(rig.start());
  rig.host.setCustomPassword(QStringLiteral("open sesame"));
  const auto codes = rig.codes.count();

  rig.host.pairingServerForTest()->lockedOut(60);

  // Only the pause protects a password of the user's own
  QCOMPARE(rig.codes.count(), codes);
  QCOMPARE(Settings::value(Settings::Lan::Password).toString(), QStringLiteral("open sesame"));
  QVERIFY(lastText(rig.hostErrors).contains(QStringLiteral("60 second")));
  rig.joiner.join(s_host.id, QStringLiteral("open sesame"));
  QTRY_COMPARE(rig.joinerStarts.count(), 1);
}

void LanSessionTests::browsing_pausesWhileHosting()
{
  LanSession session;
  isolate(session);
  QCOMPARE(session.browsePort(), quint16(0));

  session.startBrowsing();
  QVERIFY(session.browsePort() != 0);

  session.startHosting();
  QCOMPARE(session.browsePort(), quint16(0));

  session.stopHosting();
  QVERIFY(session.browsePort() != 0);

  session.stopBrowsing();
  QCOMPARE(session.browsePort(), quint16(0));
}

void LanSessionTests::browsing_startsOnlyWhenAsked()
{
  LanSession session;
  isolate(session);

  // The simple view at the start of the application: nothing listens for hosts
  session.setSimpleView(true);
  session.coreProcessStateChanged(ProcessState::Starting);
  session.coreProcessStateChanged(ProcessState::Started);
  QCOMPARE(session.browsePort(), quint16(0));

  // Nearby hosts show, and go
  session.startBrowsing();
  QVERIFY(session.browsePort() != 0);
  session.stopBrowsing();
  QCOMPARE(session.browsePort(), quint16(0));

  // The advanced view takes the page away
  session.startBrowsing();
  session.setSimpleView(false);
  QCOMPARE(session.browsePort(), quint16(0));
}

void LanSessionTests::join_trustsBothWays()
{
  Rig rig;
  QVERIFY(rig.start());
  QCOMPARE(lastHosts(rig.hosts).size(), 1);
  QCOMPARE(lastHosts(rig.hosts).first().id, s_host.id);
  QVERIFY(!lastHosts(rig.hosts).first().paired);
  QVERIFY(lastHosts(rig.hosts).first().locked);

  QVERIFY(rig.join());

  // The joining computer trusts the host, and is set up as its client
  QVERIFY(rig.joinerErrors.isEmpty());
  QCOMPARE(rig.joinerStarts.first().at(0).value<CoreMode>(), CoreMode::Client);
  QVERIFY(trustedByJoiner(s_host));
  // The host is saved with its port, and the port of the server this computer may host is left as it is
  QCOMPARE(Settings::value(Settings::Client::RemoteHost).toString(), QStringLiteral("127.0.0.1:24811"));
  QCOMPARE(Settings::value(Settings::Core::Port).toInt(), s_joiner.corePort);
  QVERIFY(Settings::value(Settings::Security::TlsEnabled).toBool());
  QCOMPARE(coreMode(), CoreMode::Client);

  const auto paired = pairedHosts();
  QCOMPARE(paired.size(), 1);
  const auto entry = paired.first().toMap();
  QCOMPARE(entry.value(QStringLiteral("id")).toString(), s_host.id);
  QCOMPARE(entry.value(QStringLiteral("name")).toString(), s_host.displayName);
  QCOMPARE(entry.value(QStringLiteral("address")).toString(), QStringLiteral("127.0.0.1"));
  QCOMPARE(entry.value(QStringLiteral("fingerprint")).toString(), fingerprintText(s_host));

  QCOMPARE(lastJoinState(rig.joinStates), JoinState::Joining);
  QCOMPARE(rig.joinStates.last().at(1).toString(), s_host.displayName);
  QVERIFY(lastHosts(rig.hosts).first().paired);
  QVERIFY(!lastHosts(rig.hosts).first().locked);

  // It is connected when the core says so
  rig.joiner.coreConnectionStateChanged(ConnectionState::Connected);
  QCOMPARE(lastJoinState(rig.joinStates), JoinState::Connected);
  QCOMPARE(rig.joinStates.last().at(1).toString(), s_host.displayName);

  // The host trusts the joining computer and asks for its screen
  QTRY_COMPARE(rig.screens.count(), 1);
  const auto client = rig.screens.first().at(0).value<PeerIdentity>();
  QCOMPARE(client.screenName, s_joiner.computerName);
  QCOMPARE(client.displayName, s_joiner.displayName);
  QCOMPARE(client.fingerprint, fingerprintText(s_joiner));
  QVERIFY(trustedByHost(s_joiner));
  QVERIFY(!trustedByHost(s_host));
  QVERIFY(!trustedByJoiner(s_joiner));
  QVERIFY(rig.hostErrors.isEmpty());

  QCOMPARE(rig.joined.count(), 1);
  QCOMPARE(lastText(rig.joined), s_joiner.displayName);
  const auto names = Settings::value(Settings::Lan::ClientNames).toList();
  QCOMPARE(names.size(), 1);
  QCOMPARE(names.first().toMap().value(QStringLiteral("screen")).toString(), s_joiner.computerName);
  QCOMPARE(names.first().toMap().value(QStringLiteral("name")).toString(), s_joiner.displayName);

  // The core tells screen names, the host shows what the computers call themselves
  rig.host.connectedClientsChanged({s_joiner.computerName});
  QCOMPARE(rig.joinedComputers.last().at(0).toStringList(), QStringList{s_joiner.displayName});
}

void LanSessionTests::join_wrongCode()
{
  Rig rig;
  QVERIFY(rig.start());
  const auto wrong = rig.code() == QStringLiteral("000000") ? QStringLiteral("000001") : QStringLiteral("000000");

  QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("pairing with Host PC failed")));
  rig.joiner.join(s_host.id, wrong);
  QCOMPARE(lastJoinState(rig.joinStates), JoinState::Joining);

  QTRY_COMPARE(rig.joinerErrors.count(), 1);
  QVERIFY(lastText(rig.joinerErrors).contains(QStringLiteral("Wrong code")));
  QCOMPARE(lastJoinState(rig.joinStates), JoinState::Idle);
  QTRY_COMPARE(rig.hostErrors.count(), 1);
  QVERIFY(lastText(rig.hostErrors).contains(QStringLiteral("wrong code")));

  // Nothing was trusted or changed
  QVERIFY(!trustedByJoiner(s_host));
  QVERIFY(!trustedByHost(s_joiner));
  QVERIFY(pairedHosts().isEmpty());
  QVERIFY(Settings::value(Settings::Client::RemoteHost).toString().isEmpty());
  QCOMPARE(Settings::value(Settings::Core::Port).toInt(), s_joiner.corePort);
  QCOMPARE(coreMode(), CoreMode::Server);
  QCOMPARE(rig.joinerStarts.count(), 0);
  QCOMPARE(rig.screens.count(), 0);
  QCOMPARE(rig.joined.count(), 0);
}

void LanSessionTests::join_customPassword()
{
  Rig rig;
  QVERIFY(rig.start());
  const auto oldCode = rig.code();
  rig.host.setCustomPassword(QStringLiteral("open sesame"));
  QVERIFY(rig.code().isEmpty());

  QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("pairing with Host PC failed")));
  rig.joiner.join(s_host.id, oldCode);
  QTRY_COMPARE(rig.joinerErrors.count(), 1);
  QCOMPARE(rig.joinerStarts.count(), 0);

  rig.joiner.join(s_host.id, QStringLiteral("open sesame"));
  QTRY_COMPARE(rig.joinerStarts.count(), 1);
  QCOMPARE(rig.joinerErrors.count(), 1);
  QVERIFY(trustedByJoiner(s_host));
}

void LanSessionTests::join_hostNotNearby()
{
  use(s_joiner);
  LanSession joiner;
  isolate(joiner);
  joiner.startBrowsing();
  QSignalSpy states(&joiner, &LanSession::joinStateChanged);
  QSignalSpy starts(&joiner, &LanSession::startCoreRequested);
  QSignalSpy errors(&joiner, &LanSession::error);

  joiner.join(QStringLiteral("nobody"), QStringLiteral("123456"));
  QCOMPARE(errors.count(), 1);

  joiner.rejoin(QStringLiteral("nobody"));
  QCOMPARE(errors.count(), 2);
  QVERIFY(states.isEmpty());
  QVERIFY(starts.isEmpty());
  QVERIFY(pairedHosts().isEmpty());
}

void LanSessionTests::join_hostUnreachable()
{
  use(s_joiner);
  LanSession joiner;
  isolate(joiner);
  joiner.startBrowsing();
  QSignalSpy hosts(&joiner, &LanSession::hostsChanged);
  QSignalSpy states(&joiner, &LanSession::joinStateChanged);
  QSignalSpy errors(&joiner, &LanSession::error);

  quint16 closedPort = 0;
  {
    QTcpServer probe;
    QVERIFY(probe.listen(loopback(), 0));
    closedPort = probe.serverPort();
  }
  const HostAnnouncement ghost{
      QStringLiteral("7a9f4c1e-2b3d-4e5f-8a6b-1c2d3e4f5a6b"), QStringLiteral("Ghost"), closedPort, 24800
  };
  QUdpSocket beacon;
  beacon.writeDatagram(ghost.toDatagram(), loopback(), joiner.browsePort());
  QTRY_COMPARE(hosts.count(), 1);

  QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("pairing with Ghost failed")));
  joiner.join(ghost.id, QStringLiteral("123456"));
  QTRY_COMPARE(errors.count(), 1);
  QVERIFY(lastText(errors).contains(QStringLiteral("Could not reach Ghost")));
  QCOMPARE(lastJoinState(states), JoinState::Idle);
  QVERIFY(pairedHosts().isEmpty());
}

void LanSessionTests::join_givesUpWithoutAConnection()
{
  Rig rig;
  QVERIFY(rig.start());
  rig.joiner.setJoinWatchdogForTest(200);

  QVERIFY(rig.join());
  QCOMPARE(lastJoinState(rig.joinStates), JoinState::Joining);

  // The client never connects: it is not left "joining" for good
  QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("joining Host PC timed out")));
  QTRY_COMPARE(lastJoinState(rig.joinStates), JoinState::Idle);
  QCOMPARE(rig.joinerStops.count(), 1);
  QCOMPARE(rig.joinerErrors.count(), 1);
  QVERIFY(lastText(rig.joinerErrors).contains(QStringLiteral("Could not connect to Host PC")));

  // It did pair, so what was learnt of the host is not lost
  QCOMPARE(pairedHosts().size(), 1);
  QVERIFY(trustedByJoiner(s_host));
}

void LanSessionTests::join_connectionStopsTheWatchdog()
{
  Rig rig;
  QVERIFY(rig.start());
  rig.joiner.setJoinWatchdogForTest(200);
  QVERIFY(rig.join());

  // Connected once, the client has done what the watchdog waits for. Losing the connection again is not a failure to
  // connect: the client keeps trying, and the user can cancel.
  rig.joiner.coreProcessStateChanged(ProcessState::Started);
  rig.joiner.coreConnectionStateChanged(ConnectionState::Connected);
  rig.joiner.coreConnectionStateChanged(ConnectionState::Disconnected);
  QTest::qWait(600);

  QCOMPARE(lastJoinState(rig.joinStates), JoinState::Joining);
  QVERIFY(rig.joinerStops.isEmpty());
  QVERIFY(rig.joinerErrors.isEmpty());
  rig.joiner.coreConnectionStateChanged(ConnectionState::Connected);
  QCOMPARE(lastJoinState(rig.joinStates), JoinState::Connected);
}

void LanSessionTests::rejoin_skipsPairing()
{
  Rig rig;
  QVERIFY(rig.start());
  QVERIFY(rig.join());
  rig.joiner.leave();
  QCOMPARE(rig.joinerStops.count(), 1);
  QCOMPARE(lastJoinState(rig.joinStates), JoinState::Idle);

  // A pairing with the code from before would fail now, and the host would tell
  rig.host.newCode();
  QFile::remove(Settings::tlsTrustedServersDb());
  Settings::setValue(Settings::Client::RemoteHost);
  Settings::setValue(Settings::Core::Port, 24000);
  QVERIFY(!trustedByJoiner(s_host));
  rig.joinerStarts.clear();

  rig.joiner.rejoin(s_host.id);

  // Asked for at once, without a round trip to the host
  QCOMPARE(rig.joinerStarts.count(), 1);
  QCOMPARE(rig.joinerStarts.first().at(0).value<CoreMode>(), CoreMode::Client);
  QCOMPARE(lastJoinState(rig.joinStates), JoinState::Joining);
  QCOMPARE(rig.joinStates.last().at(1).toString(), s_host.displayName);
  QVERIFY(trustedByJoiner(s_host));
  QCOMPARE(Settings::value(Settings::Client::RemoteHost).toString(), QStringLiteral("127.0.0.1:24811"));
  QCOMPARE(Settings::value(Settings::Core::Port).toInt(), 24000);
  QVERIFY(Settings::value(Settings::Security::TlsEnabled).toBool());
  QCOMPARE(coreMode(), CoreMode::Client);

  QTest::qWait(300);
  QCOMPARE(rig.hostErrors.count(), 0);
  QCOMPARE(rig.joined.count(), 1);
  QCOMPARE(rig.screens.count(), 1);
  QVERIFY(rig.joinerErrors.isEmpty());
}

void LanSessionTests::rejoin_needsPairedNearbyHost()
{
  Rig rig;
  QVERIFY(rig.start());

  // Nearby but never paired with
  rig.joiner.rejoin(s_host.id);
  QCOMPARE(rig.joinerErrors.count(), 1);

  // Paired with, but not nearby
  Settings::setValue(
      Settings::Lan::PairedHosts, QVariantList{QVariantMap{
                                      {QStringLiteral("id"), QStringLiteral("gone")},
                                      {QStringLiteral("name"), QStringLiteral("Gone")},
                                      {QStringLiteral("address"), QStringLiteral("127.0.0.1")},
                                      {QStringLiteral("fingerprint"), QString(64, u'a')}
                                  }}
  );
  rig.joiner.rejoin(QStringLiteral("gone"));
  QCOMPARE(rig.joinerErrors.count(), 2);
  QCOMPARE(rig.joinerStarts.count(), 0);
  QVERIFY(rig.joinStates.isEmpty());
}

void LanSessionTests::rejoin_failureForgetsTheHost()
{
  Rig rig;
  QVERIFY(rig.start());
  QVERIFY(rig.join());
  rig.joiner.leave();
  QVERIFY(lastHosts(rig.hosts).first().paired);
  rig.joinerStarts.clear();
  rig.joinerStops.clear();

  // The host does not answer where it announced: a firewall, another certificate, or it stopped
  rig.joiner.setJoinWatchdogForTest(200);
  rig.joiner.rejoin(s_host.id);
  QCOMPARE(rig.joinerStarts.count(), 1);
  QCOMPARE(lastJoinState(rig.joinStates), JoinState::Joining);

  QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("joining Host PC timed out")));
  QTRY_COMPARE(lastJoinState(rig.joinStates), JoinState::Idle);
  QCOMPARE(rig.joinerStops.count(), 1);
  QCOMPARE(rig.joinerErrors.count(), 1);
  QVERIFY(lastText(rig.joinerErrors).contains(QStringLiteral("Join it again with its code")));

  // The list asks for the code again
  QVERIFY(pairedHosts().isEmpty());
  QVERIFY(!lastHosts(rig.hosts).first().paired);
  QVERIFY(lastHosts(rig.hosts).first().locked);
  rig.joiner.rejoin(s_host.id);
  QCOMPARE(rig.joinerErrors.count(), 2);
  QCOMPARE(rig.joinerStarts.count(), 1);
}

void LanSessionTests::rejoin_invalidFingerprintIsForgotten()
{
  Rig rig;
  QVERIFY(rig.start());

  // Only what a pairing saved is trusted: 64 lowercase hex digits
  for (const auto &fingerprint : {QStringLiteral("not a fingerprint"), fingerprintText(s_host).toUpper()}) {
    Settings::setValue(
        Settings::Lan::PairedHosts, QVariantList{QVariantMap{
                                        {QStringLiteral("id"), s_host.id},
                                        {QStringLiteral("name"), s_host.displayName},
                                        {QStringLiteral("address"), QStringLiteral("127.0.0.1")},
                                        {QStringLiteral("fingerprint"), fingerprint}
                                    }}
    );
    const auto errors = rig.joinerErrors.count();

    rig.joiner.rejoin(s_host.id);

    QCOMPARE(rig.joinerErrors.count(), errors + 1);
    QVERIFY(pairedHosts().isEmpty());
    QVERIFY(!trustedByJoiner(s_host));
  }
  QCOMPARE(rig.joinerStarts.count(), 0);
  QVERIFY(rig.joinStates.isEmpty());
}

void LanSessionTests::untrustedPeer_isNeverAskedAbout()
{
  Rig rig;
  QVERIFY(rig.start());

  // Not hosting or joining: the user is asked as always
  QVERIFY(!rig.joiner.rejectUntrustedPeer());

  // Hosting: nobody is asked, the computer is not let in
  QVERIFY(rig.host.rejectUntrustedPeer());
  QVERIFY(rig.host.isHosting());
  QVERIFY(rig.hostErrors.isEmpty());

  // Joining: the host is not the one that was paired with
  QVERIFY(rig.join());
  rig.joiner.coreProcessStateChanged(ProcessState::Started);
  QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("is not the one it paired with")));
  QVERIFY(rig.joiner.rejectUntrustedPeer());

  QCOMPARE(lastJoinState(rig.joinStates), JoinState::Idle);
  QCOMPARE(rig.joinerStops.count(), 1);
  QCOMPARE(rig.joinerErrors.count(), 1);
  QVERIFY(lastText(rig.joinerErrors).contains(QStringLiteral("identity changed. Join again with its code.")));
  QVERIFY(pairedHosts().isEmpty());
  QVERIFY(lastHosts(rig.hosts).first().locked);

  // The client that was told to stop may report once more before it has: that is not a reason to ask either
  QVERIFY(rig.joiner.rejectUntrustedPeer());
  QCOMPARE(rig.joinerErrors.count(), 1);
  rig.joiner.coreProcessStateChanged(ProcessState::Stopped);
  QVERIFY(!rig.joiner.rejectUntrustedPeer());

  // and neither is a server that was told to stop
  rig.host.stopHosting();
  QVERIFY(rig.host.rejectUntrustedPeer());
  rig.host.coreProcessStateChanged(ProcessState::Stopped);
  QVERIFY(!rig.host.rejectUntrustedPeer());
}

void LanSessionTests::hostsList_marksPairedHosts()
{
  {
    Rig rig;
    QVERIFY(rig.start());
    QCOMPARE(lastHosts(rig.hosts).size(), 1);
    QVERIFY(!lastHosts(rig.hosts).first().paired);
    QVERIFY(lastHosts(rig.hosts).first().locked);
  }

  Settings::setValue(
      Settings::Lan::PairedHosts, QVariantList{QVariantMap{
                                      {QStringLiteral("id"), s_host.id},
                                      {QStringLiteral("name"), s_host.displayName},
                                      {QStringLiteral("address"), QStringLiteral("127.0.0.1")},
                                      {QStringLiteral("fingerprint"), fingerprintText(s_host)}
                                  }}
  );
  Rig rig;
  QVERIFY(rig.start());
  QCOMPARE(lastHosts(rig.hosts).size(), 1);
  QCOMPARE(lastHosts(rig.hosts).first().name, s_host.displayName);
  QVERIFY(lastHosts(rig.hosts).first().paired);
  QVERIFY(!lastHosts(rig.hosts).first().locked);
}

void LanSessionTests::joinedComputers_useDisplayNames()
{
  Settings::setValue(
      Settings::Lan::ClientNames,
      QVariantList{QVariantMap{
          {QStringLiteral("screen"), QStringLiteral("laptop")}, {QStringLiteral("name"), QStringLiteral("Laptop")}
      }}
  );
  LanSession session;
  isolate(session);
  QSignalSpy names(&session, &LanSession::joinedComputersChanged);

  session.connectedClientsChanged({QStringLiteral("laptop")});
  QVERIFY(names.isEmpty());

  // The clients of before are shown as soon as hosting starts
  session.startHosting();
  QCOMPARE(names.last().at(0).toStringList(), QStringList{QStringLiteral("Laptop")});

  session.connectedClientsChanged({QStringLiteral("laptop"), QStringLiteral("other-pc")});
  QCOMPARE(names.last().at(0).toStringList(), (QStringList{QStringLiteral("Laptop"), QStringLiteral("other-pc")}));

  // Nobody is connected to a core that starts again
  session.coreProcessStateChanged(ProcessState::Starting);
  QVERIFY(names.last().at(0).toStringList().isEmpty());

  session.stopHosting();
  names.clear();
  session.connectedClientsChanged({QStringLiteral("laptop")});
  QVERIFY(names.isEmpty());
}

void LanSessionTests::core_restartKeepsHosting()
{
  LanSession session;
  isolate(session);
  constexpr int grace = 200;
  session.setCoreStopGraceForTest(grace);
  QSignalSpy hosting(&session, &LanSession::hostingChanged);
  session.startHosting();
  session.coreProcessStateChanged(ProcessState::Started);

  // A restart is over before the event loop runs again, so it does not matter how long the loop takes under load
  session.coreProcessStateChanged(ProcessState::Stopping);
  session.coreProcessStateChanged(ProcessState::Stopped);
  session.coreProcessStateChanged(ProcessState::Starting);
  session.coreProcessStateChanged(ProcessState::Started);

  // A grace that ran on would have ended hosting by now
  QTest::qWait(grace * 3);
  QVERIFY(session.isHosting());
  QCOMPARE(hosting.count(), 1);
}

void LanSessionTests::core_stopEndsHosting()
{
  LanSession session;
  isolate(session);
  session.setCoreStopGraceForTest(300);
  QSignalSpy hosting(&session, &LanSession::hostingChanged);
  QSignalSpy stops(&session, &LanSession::stopCoreRequested);
  QSignalSpy errors(&session, &LanSession::error);
  session.startHosting();
  session.coreProcessStateChanged(ProcessState::Started);

  session.coreProcessStateChanged(ProcessState::Stopped);
  QVERIFY(session.isHosting());

  QTRY_VERIFY(!session.isHosting());
  QVERIFY(!hosting.last().at(0).toBool());
  // The core stopped by itself, and the user is told
  QCOMPARE(stops.count(), 0);
  QCOMPARE(errors.count(), 1);

  // Stopping hosting is not something to tell about
  session.startHosting();
  session.stopHosting();
  session.coreProcessStateChanged(ProcessState::Stopped);
  QTest::qWait(600);
  QCOMPARE(errors.count(), 1);
}

void LanSessionTests::core_modeChangeEndsHosting()
{
  LanSession session;
  isolate(session);
  QSignalSpy stops(&session, &LanSession::stopCoreRequested);
  session.startHosting();
  session.coreProcessStateChanged(ProcessState::Started);

  // The mode buttons of the advanced view
  Settings::setValue(Settings::Core::CoreMode, CoreMode::Client);
  session.coreProcessStateChanged(ProcessState::Starting);

  QVERIFY(!session.isHosting());
  QCOMPARE(stops.count(), 0);
}

void LanSessionTests::core_joinFollowsConnection()
{
  Rig rig;
  QVERIFY(rig.start());
  QVERIFY(rig.join());
  rig.joiner.coreProcessStateChanged(ProcessState::Started);
  const auto changes = rig.joinStates.count();

  rig.joiner.coreConnectionStateChanged(ConnectionState::Connected);
  QCOMPARE(lastJoinState(rig.joinStates), JoinState::Connected);
  rig.joiner.coreConnectionStateChanged(ConnectionState::Connected);
  QCOMPARE(rig.joinStates.count(), changes + 1);

  rig.joiner.coreConnectionStateChanged(ConnectionState::Disconnected);
  QCOMPARE(lastJoinState(rig.joinStates), JoinState::Joining);
  rig.joiner.coreConnectionStateChanged(ConnectionState::Connecting);
  rig.joiner.coreConnectionStateChanged(ConnectionState::Connected);
  QCOMPARE(lastJoinState(rig.joinStates), JoinState::Connected);
  QCOMPARE(rig.joinStates.last().at(1).toString(), s_host.displayName);

  // A server hears about its clients connecting, which is nothing to show
  QSignalSpy hostStates(&rig.host, &LanSession::joinStateChanged);
  rig.host.coreConnectionStateChanged(ConnectionState::Connected);
  QVERIFY(hostStates.isEmpty());
}

void LanSessionTests::core_stopEndsJoin()
{
  Rig rig;
  QVERIFY(rig.start());
  QVERIFY(rig.join());
  rig.joiner.setCoreStopGraceForTest(200);
  rig.joiner.coreProcessStateChanged(ProcessState::Started);
  rig.joiner.coreConnectionStateChanged(ConnectionState::Connected);

  rig.joiner.coreProcessStateChanged(ProcessState::Stopped);
  QTRY_COMPARE(lastJoinState(rig.joinStates), JoinState::Idle);
  QCOMPARE(rig.joinerStops.count(), 0);
  QCOMPARE(rig.joinerErrors.count(), 1);
}

void LanSessionTests::simpleView_hostsFlaggedServer()
{
  hostedServerRuns();
  LanSession session;
  isolate(session);
  QSignalSpy hosting(&session, &LanSession::hostingChanged);
  QSignalSpy codes(&session, &LanSession::codeChanged);
  QSignalSpy starts(&session, &LanSession::startCoreRequested);
  QSignalSpy stops(&session, &LanSession::stopCoreRequested);
  QSignalSpy legacy(&session, &LanSession::legacySessionChanged);

  // The advanced view leaves the core alone
  session.coreProcessStateChanged(ProcessState::Started);
  QVERIFY(!session.isHosting());
  QVERIFY(legacy.isEmpty());

  session.setSimpleView(true);
  QVERIFY(session.isHosting());
  QCOMPARE(hosting.count(), 1);
  QVERIFY(isCode(lastText(codes)));
  // The server is taken up as it is: the core is not asked for anything, and nothing was changed
  QVERIFY(starts.isEmpty());
  QVERIFY(stops.isEmpty());
  QVERIFY(legacy.isEmpty());
  QVERIFY(hostingEnabled());
  QCOMPARE(coreMode(), CoreMode::Server);

  // Switching to the advanced view leaves hosting as it is, and it follows the core from there
  session.setSimpleView(false);
  QVERIFY(session.isHosting());
  session.setCoreStopGraceForTest(100);
  session.coreProcessStateChanged(ProcessState::Stopped);
  QTRY_VERIFY(!session.isHosting());
  QVERIFY(!hostingEnabled());
}

void LanSessionTests::simpleView_takesUpServerOnceStarted()
{
  hostedServerRuns();
  LanSession session;
  isolate(session);
  QSignalSpy starts(&session, &LanSession::startCoreRequested);
  QSignalSpy stops(&session, &LanSession::stopCoreRequested);

  // A core that starts by itself in the simple view, as with the start of the application
  session.setSimpleView(true);
  QVERIFY(!session.isHosting());
  session.coreProcessStateChanged(ProcessState::Starting);
  QVERIFY(!session.isHosting());
  session.coreProcessStateChanged(ProcessState::Started);

  QVERIFY(session.isHosting());
  QVERIFY(starts.isEmpty());
  QVERIFY(stops.isEmpty());
}

void LanSessionTests::simpleView_leavesManualServerAlone()
{
  // A server that was set up in the advanced view, with the security that is on by default: Host did not start it
  Settings::setValue(Settings::Core::CoreMode, CoreMode::Server);
  Settings::setValue(Settings::Security::TlsEnabled, true);
  Settings::setValue(Settings::Security::CheckPeers, true);
  drain(*s_sink);
  LanSession session;
  isolate(session);
  QSignalSpy hosting(&session, &LanSession::hostingChanged);
  QSignalSpy starts(&session, &LanSession::startCoreRequested);
  QSignalSpy stops(&session, &LanSession::stopCoreRequested);
  QSignalSpy errors(&session, &LanSession::error);
  QSignalSpy legacy(&session, &LanSession::legacySessionChanged);

  session.setSimpleView(true);
  session.coreProcessStateChanged(ProcessState::Starting);
  QVERIFY(legacy.isEmpty());
  session.coreProcessStateChanged(ProcessState::Started);

  // Nothing is hosted, listened for, announced or changed, and the view says why
  QVERIFY(!session.isHosting());
  QVERIFY(hosting.isEmpty());
  QCOMPARE(legacy.count(), 1);
  QVERIFY(legacy.last().at(0).toBool());
  QVERIFY(starts.isEmpty());
  QVERIFY(stops.isEmpty());
  QVERIFY(errors.isEmpty());
  QVERIFY(!hostingEnabled());
  QCOMPARE(session.pairingServerForTest()->port(), quint16(0));
  QCOMPARE(session.browsePort(), quint16(0));
  QTest::qWait(50);
  QVERIFY(!s_sink->hasPendingDatagrams());

  // A restart of the server is no news
  session.coreProcessStateChanged(ProcessState::Stopped);
  QCOMPARE(legacy.count(), 2);
  QVERIFY(!legacy.last().at(0).toBool());
  session.coreProcessStateChanged(ProcessState::Starting);
  session.coreProcessStateChanged(ProcessState::Started);
  QCOMPARE(legacy.count(), 3);

  // and the advanced view has no need of the notice
  session.setSimpleView(false);
  QCOMPARE(legacy.count(), 4);
  QVERIFY(!legacy.last().at(0).toBool());
  QVERIFY(!session.isHosting());
}

void LanSessionTests::simpleView_doesNotForceSecurityToTakeUp()
{
  struct Case
  {
    bool tls;
    bool checkPeers;
  };
  for (const auto &[tls, checkPeers] : {Case{true, false}, Case{false, true}, Case{false, false}}) {
    Settings::setValue(Settings::Core::CoreMode, CoreMode::Server);
    Settings::setValue(Settings::Lan::HostingEnabled, true);
    Settings::setValue(Settings::Security::TlsEnabled, tls);
    Settings::setValue(Settings::Security::CheckPeers, checkPeers);
    LanSession session;
    isolate(session);
    QSignalSpy starts(&session, &LanSession::startCoreRequested);
    QSignalSpy stops(&session, &LanSession::stopCoreRequested);
    QSignalSpy legacy(&session, &LanSession::legacySessionChanged);

    session.setSimpleView(true);
    session.coreProcessStateChanged(ProcessState::Started);

    // Without the security, the server is not the one Host started: it is left as it is, and Host is not remembered
    QVERIFY(!session.isHosting());
    QCOMPARE(legacy.count(), 1);
    QVERIFY(starts.isEmpty());
    QVERIFY(stops.isEmpty());
    QVERIFY(!hostingEnabled());
    QCOMPARE(Settings::value(Settings::Security::TlsEnabled).toBool(), tls);
    QCOMPARE(Settings::value(Settings::Security::CheckPeers).toBool(), checkPeers);
    QCOMPARE(coreMode(), CoreMode::Server);
  }
}

void LanSessionTests::simpleView_showsRunningClient()
{
  use(s_joiner);
  Settings::setValue(Settings::Core::CoreMode, CoreMode::Client);
  Settings::setValue(Settings::Client::RemoteHost, QStringLiteral("192.168.1.20:24811"));
  Settings::setValue(
      Settings::Lan::PairedHosts, QVariantList{QVariantMap{
                                      {QStringLiteral("id"), s_host.id},
                                      {QStringLiteral("name"), QStringLiteral("Studio")},
                                      {QStringLiteral("address"), QStringLiteral("192.168.1.20")},
                                      {QStringLiteral("fingerprint"), fingerprintText(s_host)}
                                  }}
  );
  LanSession session;
  isolate(session);
  QSignalSpy states(&session, &LanSession::joinStateChanged);
  QSignalSpy stops(&session, &LanSession::stopCoreRequested);
  QSignalSpy legacy(&session, &LanSession::legacySessionChanged);

  session.setSimpleView(true);
  QVERIFY(states.isEmpty());

  // A join saved the host as address and port, and the host is known by its address
  session.coreProcessStateChanged(ProcessState::Started);
  QCOMPARE(states.count(), 1);
  QCOMPARE(lastJoinState(states), JoinState::Joining);
  QCOMPARE(states.last().at(1).toString(), QStringLiteral("Studio"));
  QVERIFY(legacy.isEmpty());

  session.coreConnectionStateChanged(ConnectionState::Connected);
  QCOMPARE(lastJoinState(states), JoinState::Connected);
  QCOMPARE(states.last().at(1).toString(), QStringLiteral("Studio"));

  session.leave();
  QCOMPARE(lastJoinState(states), JoinState::Idle);
  QCOMPARE(stops.count(), 1);

  // A client set up by hand shows its address
  session.coreConnectionStateChanged(ConnectionState::Disconnected);
  Settings::setValue(Settings::Client::RemoteHost, QStringLiteral("10.0.0.7"));
  session.coreProcessStateChanged(ProcessState::Started);
  QCOMPARE(lastJoinState(states), JoinState::Joining);
  QCOMPARE(states.last().at(1).toString(), QStringLiteral("10.0.0.7"));
  QVERIFY(pairedHosts().size() == 1);
}

void LanSessionTests::simpleView_adoptedClientHasNoWatchdog()
{
  use(s_joiner);
  Settings::setValue(Settings::Core::CoreMode, CoreMode::Client);
  Settings::setValue(Settings::Client::RemoteHost, QStringLiteral("192.168.1.20:24811"));
  LanSession session;
  isolate(session);
  session.setJoinWatchdogForTest(100);
  QSignalSpy states(&session, &LanSession::joinStateChanged);
  QSignalSpy stops(&session, &LanSession::stopCoreRequested);
  QSignalSpy errors(&session, &LanSession::error);

  session.setSimpleView(true);
  session.coreProcessStateChanged(ProcessState::Started);
  QCOMPARE(lastJoinState(states), JoinState::Joining);

  // It was not started by a join: the user set it up, and it is only shown. Nor is it asked about certificates.
  QTest::qWait(500);
  QCOMPARE(lastJoinState(states), JoinState::Joining);
  QVERIFY(stops.isEmpty());
  QVERIFY(errors.isEmpty());
  QVERIFY(!session.rejectUntrustedPeer());
}

void LanSessionTests::screenQueue_holdsClientsWhileEditing()
{
  ClientScreenQueue queue;
  ScreenList layout;

  // Nothing is edited: the client is added at once
  QVERIFY(queue.submit(guest(1)));
  QVERIFY(queue.close(layout).isEmpty());

  // The Arrange screens dialog is open: they wait, once each, and however the name is written
  queue.open();
  QVERIFY(!queue.submit(guest(1)));
  QVERIFY(!queue.submit(guest(2)));
  auto again = guest(1);
  again.screenName = again.screenName.toUpper();
  QVERIFY(!queue.submit(again));

  // It closes: they are added, oldest first, and only once
  const auto queued = queue.close(layout);
  QCOMPARE(queued.size(), 2);
  QCOMPARE(queued.at(0).screenName, guest(1).screenName);
  QCOMPARE(queued.at(1).screenName, guest(2).screenName);
  QVERIFY(queue.close(layout).isEmpty());
  QVERIFY(queue.submit(guest(3)));
}

void LanSessionTests::screenQueue_nestsAndSkipsWhatTheLayoutHas()
{
  ClientScreenQueue queue;
  ScreenList layout;

  // A dialog on top of a dialog: the computers wait for both
  queue.open();
  queue.open();
  QVERIFY(!queue.submit(guest(1)));
  QVERIFY(!queue.submit(guest(2)));
  QVERIFY(!queue.submit(guest(3)));
  QVERIFY(queue.close(layout).isEmpty());
  QVERIFY(!queue.submit(guest(4)));

  // The layout that came out of the dialog has some of them, by name in any case
  layout.append(Screen(guest(2).screenName.toUpper()));
  layout.append(Screen(guest(4).screenName));
  const auto queued = queue.close(layout);
  QCOMPARE(queued.size(), 2);
  QCOMPARE(queued.at(0).screenName, guest(1).screenName);
  QCOMPARE(queued.at(1).screenName, guest(3).screenName);

  QVERIFY(ClientScreenQueue::inLayout(layout, guest(2).screenName));
  QVERIFY(!ClientScreenQueue::inLayout(layout, guest(1).screenName));
  QVERIFY(!ClientScreenQueue::inLayout(ScreenList(), guest(1).screenName));
}

QTEST_GUILESS_MAIN(LanSessionTests)
