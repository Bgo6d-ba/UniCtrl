/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "LanBeaconTests.h"

#include "gui/lan/LanBeacon.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QSignalSpy>
#include <QUdpSocket>

#include <map>
#include <memory>

using namespace deskflow::gui::lan;

namespace {

const auto kId = QStringLiteral("0b6f3f9e-6a3c-4a8e-9d51-3c7a2f1e8b20");

HostAnnouncement announcement(const QString &id = kId, const QString &name = QStringLiteral("Office PC"))
{
  return {id, name, kPairingPort, 24800};
}

// A valid announcement with one field replaced, or removed when the value is undefined
QByteArray withField(const QString &key, const QJsonValue &value)
{
  auto object = QJsonDocument::fromJson(announcement().toDatagram()).object();
  if (value.isUndefined()) {
    object.remove(key);
  } else {
    object.insert(key, value);
  }
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

// In order, from one socket, to a browser on localhost
void send(const QList<HostAnnouncement> &announcements, quint16 port)
{
  QUdpSocket socket;
  for (const auto &a : announcements) {
    socket.writeDatagram(a.toDatagram(), QHostAddress(QHostAddress::LocalHost), port);
  }
}

QStringList ids(const QList<DiscoveredHost> &hosts)
{
  QStringList ids;
  for (const auto &host : hosts) {
    ids.append(host.id);
  }
  return ids;
}

// Announcers on this computer that the browser takes for computers on the network: the address that a source is
// given is the address its announcements count as coming from, and 10.0.0.0/8 is the network of the browser
class Sources
{
public:
  explicit Sources(LanBrowser &browser)
  {
    setLocalNetworksForTest(QList<LocalNetwork>{{QHostAddress(QStringLiteral("10.0.0.1")), 8}});
    browser.setSenderForTest([this](const QHostAddress &sender, quint16 port) {
      return m_addresses.value(port, sender);
    });
  }

  // In order, from the source with this address
  void send(const QString &address, const QList<HostAnnouncement> &announcements, quint16 port)
  {
    auto &socket = m_sockets[address];
    if (!socket) {
      socket = std::make_unique<QUdpSocket>();
      socket->bind(QHostAddress(QHostAddress::LocalHost), 0);
      m_addresses.insert(socket->localPort(), QHostAddress(address));
    }
    for (const auto &a : announcements) {
      socket->writeDatagram(a.toDatagram(), QHostAddress(QHostAddress::LocalHost), port);
    }
  }

private:
  std::map<QString, std::unique_ptr<QUdpSocket>> m_sockets;
  QHash<quint16, QHostAddress> m_addresses;
};

// Not through QString::fromUcs4(), which takes a byte order mark for the start of the text and drops it
QString fromCodePoint(uint codePoint)
{
  if (QChar::requiresSurrogates(codePoint)) {
    return QString(QChar(QChar::highSurrogate(codePoint))) + QChar(QChar::lowSurrogate(codePoint));
  }
  return QString(QChar(static_cast<char16_t>(codePoint)));
}

// Whether an interface of this computer that is up has a subnet with the address, as its addresses say
bool isOnSubnetOfInterface(const QHostAddress &address)
{
  const auto interfaces = QNetworkInterface::allInterfaces();
  for (const auto &networkInterface : interfaces) {
    const auto flags = networkInterface.flags();
    if (!flags.testFlag(QNetworkInterface::IsUp) || flags.testFlag(QNetworkInterface::IsLoopBack)) {
      continue;
    }
    const auto entries = networkInterface.addressEntries();
    for (const auto &entry : entries) {
      if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol && entry.prefixLength() >= 1 &&
          address.isInSubnet(entry.ip(), entry.prefixLength())) {
        return true;
      }
    }
  }
  return false;
}

} // namespace

void LanBeaconTests::cleanup()
{
  setLocalNetworksForTest(std::nullopt);
}

void LanBeaconTests::randomCode_isSixDigits()
{
  for (int i = 0; i < 100; ++i) {
    const auto code = randomCode();
    QCOMPARE(code.size(), 6);
    QVERIFY2(std::ranges::all_of(code, [](QChar c) { return c >= u'0' && c <= u'9'; }), qPrintable(code));
  }
}

void LanBeaconTests::displayName_validation()
{
  QVERIFY(isValidDisplayName(QStringLiteral("Office PC")));
  QVERIFY(isValidDisplayName(QString(kMaxDisplayNameLength, u'x')));
  QVERIFY(isValidDisplayName(QStringLiteral("Бат \U0001F4BB")));

  QVERIFY(!isValidDisplayName(QString()));
  QVERIFY(!isValidDisplayName(QStringLiteral("   ")));
  QVERIFY(!isValidDisplayName(QString(kMaxDisplayNameLength + 1, u'x')));
  QVERIFY(!isValidDisplayName(QStringLiteral("a\tb")));
  QVERIFY(!isValidDisplayName(QStringLiteral("a\u007fb")));
  QVERIFY(!isValidDisplayName(QStringLiteral("a b")));
  QVERIFY(!isValidDisplayName(QString(QChar(0xd800))));
}

void LanBeaconTests::displayName_formatCharacters_data()
{
  QTest::addColumn<uint>("codePoint");

  QTest::newRow("left-to-right embedding") << 0x202Au;
  QTest::newRow("right-to-left embedding") << 0x202Bu;
  QTest::newRow("pop directional formatting") << 0x202Cu;
  QTest::newRow("left-to-right override") << 0x202Du;
  QTest::newRow("right-to-left override") << 0x202Eu;
  QTest::newRow("left-to-right isolate") << 0x2066u;
  QTest::newRow("right-to-left isolate") << 0x2067u;
  QTest::newRow("first strong isolate") << 0x2068u;
  QTest::newRow("pop directional isolate") << 0x2069u;
  QTest::newRow("left-to-right mark") << 0x200Eu;
  QTest::newRow("right-to-left mark") << 0x200Fu;
  QTest::newRow("arabic letter mark") << 0x061Cu;
  QTest::newRow("zero width space") << 0x200Bu;
  QTest::newRow("word joiner") << 0x2060u;
  QTest::newRow("soft hyphen") << 0x00ADu;
  QTest::newRow("byte order mark") << 0xFEFFu;
  QTest::newRow("language tag, a surrogate pair") << 0xE0001u;
  QTest::newRow("tag latin small letter g, a surrogate pair") << 0xE0067u;
}

void LanBeaconTests::displayName_formatCharacters()
{
  QFETCH(uint, codePoint);

  const auto format = fromCodePoint(codePoint);
  QVERIFY(!isValidDisplayName(QStringLiteral("abc") + format + QStringLiteral("txt.exe")));
  QVERIFY(!isValidDisplayName(format + QStringLiteral("Office PC")));
  QVERIFY(!isValidDisplayName(QStringLiteral("Office PC") + format));
  QVERIFY(!isValidDisplayName(format));
  QVERIFY(!isValidDisplayName(format + format));
  // also where a beacon carries it, which is trimmed first
  QVERIFY(!HostAnnouncement::parse(
      announcement(kId, QStringLiteral(" Office") + format + QStringLiteral("PC ")).toDatagram()
  ));
}

void LanBeaconTests::displayName_invisibleOnly_data()
{
  QTest::addColumn<QString>("name");

  QTest::newRow("no-break space") << QStringLiteral("\u00a0");
  QTest::newRow("em space and ideographic space") << QStringLiteral("\u2003\u3000");
  QTest::newRow("zero width joiner alone") << QStringLiteral("\u200d");
  QTest::newRow("joiners and spaces") << QStringLiteral(" \u200d \u200d");
  QTest::newRow("hangul filler") << QStringLiteral("\u3164");
  QTest::newRow("halfwidth hangul filler") << QStringLiteral("\uffa0");
  QTest::newRow("hangul choseong filler") << QStringLiteral("\u115f\u1160");
  QTest::newRow("braille blank") << QStringLiteral("\u2800");
  QTest::newRow("variation selector") << QStringLiteral("\ufe0f");
  QTest::newRow("combining mark") << QStringLiteral("\u0301");
  QTest::newRow("combining grapheme joiner") << QStringLiteral("\u034f");
}

void LanBeaconTests::displayName_invisibleOnly()
{
  QFETCH(QString, name);

  QVERIFY(!isValidDisplayName(name));
  QVERIFY(!isValidDisplayName(name + name));
  QVERIFY(!HostAnnouncement::parse(announcement(kId, name).toDatagram()));
}

void LanBeaconTests::displayName_acceptsWhatIsVisible_data()
{
  QTest::addColumn<QString>("name");

  QTest::newRow("man technologist, an emoji joined with ZWJ") << QStringLiteral("\U0001F468\u200d\U0001F4BB");
  QTest::newRow("family, joined twice") << QStringLiteral("\U0001F468\u200d\U0001F469\u200d\U0001F467 PC");
  QTest::newRow("heart with a variation selector") << QStringLiteral("\u2764\ufe0f");
  QTest::newRow("letter and combining mark") << QStringLiteral("e\u0301");
  QTest::newRow("cyrillic") << QStringLiteral("Батын компьютер");
  QTest::newRow("cjk") << QStringLiteral("办公室电脑");
  QTest::newRow("only punctuation") << QStringLiteral("...");
  QTest::newRow("joiner beside letters") << QStringLiteral("a\u200db");
  QTest::newRow("persian, written with a zero width non-joiner")
      << QStringLiteral("\u0645\u06cc\u200c\u062e\u0648\u0627\u0647\u0645");
  QTest::newRow("traditional mongolian, with the vowel separator") << QStringLiteral("\u1820\u180e\u1821");
  QTest::newRow("emoji from the supplementary planes") << QStringLiteral("\U0001F4BB\U0001F5A5");
}

void LanBeaconTests::displayName_acceptsWhatIsVisible()
{
  QFETCH(QString, name);

  QVERIFY(isValidDisplayName(name));
  const auto parsed = HostAnnouncement::parse(announcement(kId, name).toDatagram());
  QVERIFY(parsed);
  QCOMPARE(parsed->name, name);
}

void LanBeaconTests::fingerprint_validation()
{
  QVERIFY(isValidFingerprint(QString(kFingerprintLength, u'a')));
  QVERIFY(isValidFingerprint(QStringLiteral("0123456789abcdef").repeated(4)));

  QVERIFY(!isValidFingerprint(QString()));
  QVERIFY(!isValidFingerprint(QString(kFingerprintLength - 1, u'a')));
  QVERIFY(!isValidFingerprint(QString(kFingerprintLength + 1, u'a')));
  QVERIFY(!isValidFingerprint(QString(kFingerprintLength, u'A')));
  QVERIFY(!isValidFingerprint(QString(kFingerprintLength, u'g')));
  QVERIFY(!isValidFingerprint(QString(kFingerprintLength - 1, u'a') + QChar(u' ')));
  QVERIFY(!isValidFingerprint(QString(kFingerprintLength - 1, u'a') + QChar(0x0661)));
}

void LanBeaconTests::localPeer_subnets()
{
  setLocalNetworksForTest(
      QList<LocalNetwork>{
          {QHostAddress(QStringLiteral("192.168.1.5")), 24},
          {QHostAddress(QStringLiteral("10.20.30.40")), 16},
          {QHostAddress(QStringLiteral("172.16.0.9")), 32},
          {QHostAddress(QStringLiteral("192.0.2.1")), 0},
          {QHostAddress(QStringLiteral("198.51.100.1")), -1}
      }
  );
  const auto isPeer = [](const QString &address) { return isLocalPeer(QHostAddress(address)); };

  QVERIFY(isPeer(QStringLiteral("192.168.1.77")));
  QVERIFY(isPeer(QStringLiteral("192.168.1.0")));
  QVERIFY(isPeer(QStringLiteral("192.168.1.255")));
  QVERIFY(!isPeer(QStringLiteral("192.168.2.5")));
  QVERIFY(!isPeer(QStringLiteral("192.168.0.255")));

  QVERIFY(isPeer(QStringLiteral("10.20.255.1")));
  QVERIFY(!isPeer(QStringLiteral("10.21.0.1")));
  QVERIFY(!isPeer(QStringLiteral("10.19.255.255")));

  // a subnet of one address has no neighbours
  QVERIFY(isPeer(QStringLiteral("172.16.0.9")));
  QVERIFY(!isPeer(QStringLiteral("172.16.0.10")));

  // a prefix of 0 would let everything in, and -1 is a netmask that Qt could not make sense of
  QVERIFY(!isPeer(QStringLiteral("192.0.2.77")));
  QVERIFY(!isPeer(QStringLiteral("8.8.8.8")));
  QVERIFY(!isPeer(QStringLiteral("198.51.100.2")));

  QVERIFY(isPeer(QStringLiteral("::ffff:192.168.1.77")));
  QVERIFY(!isPeer(QStringLiteral("::ffff:192.168.2.77")));
  QVERIFY(!isPeer(QStringLiteral("2001:db8::1")));
  QVERIFY(!isPeer(QStringLiteral("fe80::1")));
  QVERIFY(!isLocalPeer(QHostAddress()));
}

void LanBeaconTests::localPeer_loopbackAlways()
{
  setLocalNetworksForTest(QList<LocalNetwork>{});
  QVERIFY(isLocalPeer(QHostAddress(QHostAddress::LocalHost)));
  QVERIFY(isLocalPeer(QHostAddress(QStringLiteral("127.8.9.10"))));
  QVERIFY(!isLocalPeer(QHostAddress(QStringLiteral("192.168.1.77"))));
}

// Without a hook the interfaces of this computer decide
void LanBeaconTests::localPeer_followsInterfaces()
{
  QVERIFY(isLocalPeer(QHostAddress(QHostAddress::LocalHost)));

  QList<QHostAddress> probes;
  for (const auto &address :
       {QStringLiteral("203.0.113.77"), QStringLiteral("8.8.8.8"), QStringLiteral("10.255.255.254"),
        QStringLiteral("172.16.5.4"), QStringLiteral("192.168.0.1"), QStringLiteral("192.168.1.1"),
        QStringLiteral("169.254.1.1"), QStringLiteral("100.64.0.9")}) {
    probes.append(QHostAddress(address));
  }
  // the addresses of this computer, and the ones next to them
  const auto interfaces = QNetworkInterface::allInterfaces();
  for (const auto &networkInterface : interfaces) {
    const auto entries = networkInterface.addressEntries();
    for (const auto &entry : entries) {
      bool isIPv4 = false;
      const auto ip = entry.ip().toIPv4Address(&isIPv4);
      if (isIPv4) {
        probes << QHostAddress(ip) << QHostAddress(ip + 1) << QHostAddress(ip - 1) << QHostAddress(ip ^ 0x100)
               << QHostAddress(ip ^ 0x10000);
      }
    }
  }
  for (const auto &probe : std::as_const(probes)) {
    if (probe.isLoopback()) {
      continue;
    }
    QVERIFY2(isLocalPeer(probe) == isOnSubnetOfInterface(probe), qPrintable(probe.toString()));
  }
}

void LanBeaconTests::announcement_roundTrip()
{
  const auto datagram = announcement().toDatagram();
  QVERIFY(datagram.size() <= kMaxBeaconSize);
  QVERIFY(HostAnnouncement::parse(datagram) == announcement());

  const auto padded = datagram + QByteArray(kMaxBeaconSize - datagram.size(), ' ');
  QVERIFY(HostAnnouncement::parse(padded) == announcement());
  QVERIFY(!HostAnnouncement::parse(padded + ' '));
}

void LanBeaconTests::parse_trimsName()
{
  const auto parsed = HostAnnouncement::parse(announcement(kId, QStringLiteral("  Office PC \t")).toDatagram());
  QVERIFY(parsed);
  QCOMPARE(parsed->name, QStringLiteral("Office PC"));
}

void LanBeaconTests::parse_rejects_data()
{
  QTest::addColumn<QByteArray>("datagram");

  QTest::newRow("empty") << QByteArray();
  QTest::newRow("not json") << QByteArray("deskflow-plus");
  QTest::newRow("array") << QByteArray("[1,2]");
  QTest::newRow("other service") << withField("svc", "synergy");
  QTest::newRow("version 2") << withField("v", 2);
  QTest::newRow("version as string") << withField("v", "1");
  QTest::newRow("no id") << withField("id", QJsonValue::Undefined);
  QTest::newRow("id with space") << withField("id", "a b");
  QTest::newRow("id with slash") << withField("id", "a/b");
  QTest::newRow("id too long") << withField("id", QString(65, u'a'));
  QTest::newRow("no name") << withField("name", QJsonValue::Undefined);
  QTest::newRow("blank name") << withField("name", "   ");
  QTest::newRow("name too long") << withField("name", QString(kMaxDisplayNameLength + 1, u'n'));
  QTest::newRow("name with line break") << withField("name", "Office\nPC");
  QTest::newRow("name with right-to-left override") << withField("name", QStringLiteral("abc\u202etxt.exe"));
  QTest::newRow("name with zero width space") << withField("name", QStringLiteral("Office\u200bPC"));
  QTest::newRow("name of zero width spaces") << withField("name", QStringLiteral("\u200b\u200b"));
  QTest::newRow("name of a hangul filler") << withField("name", QStringLiteral("\u3164"));
  QTest::newRow("name not a string") << withField("name", 42);
  QTest::newRow("pair port 0") << withField("pair", 0);
  QTest::newRow("pair port too big") << withField("pair", 65536);
  QTest::newRow("core port fraction") << withField("core", 24800.5);
  QTest::newRow("core port as string") << withField("core", "24800");
  QTest::newRow("no core port") << withField("core", QJsonValue::Undefined);
}

void LanBeaconTests::parse_rejects()
{
  QFETCH(QByteArray, datagram);
  QVERIFY(!HostAnnouncement::parse(datagram));
}

void LanBeaconTests::browser_receivesAnnouncer()
{
  LanBrowser browser;
  QVERIFY(browser.start(0));
  QSignalSpy changed(&browser, &LanBrowser::hostsChanged);
  LanAnnouncer announcer;
  announcer.setTargetsForTest({QHostAddress(QHostAddress::LocalHost)}, browser.port());
  announcer.setAnnouncement(announcement());
  announcer.start();

  QTRY_COMPARE(browser.hosts().size(), 1);
  const auto host = browser.hosts().first();
  QVERIFY(static_cast<const HostAnnouncement &>(host) == announcement());
  QCOMPARE(host.address, QHostAddress(QHostAddress::LocalHost));
  QCOMPARE(changed.count(), 1);

  // a changed announcement goes out right away, and is told a little later than that when the last change is recent
  announcer.setAnnouncement(announcement(kId, QStringLiteral("Renamed")));
  QTRY_COMPARE(browser.hosts().first().name, QStringLiteral("Renamed"));
  QCOMPARE(browser.hosts().size(), 1);
  QTRY_COMPARE(changed.count(), 2);

  // and the host is forgotten at once
  browser.stop();
  QVERIFY(browser.hosts().isEmpty());
  QCOMPARE(changed.count(), 3);
}

void LanBeaconTests::browser_ignoresOwnId()
{
  LanBrowser browser;
  QVERIFY(browser.start(0));
  browser.setOwnId(QStringLiteral("mine"));

  send({announcement(QStringLiteral("mine")), announcement(QStringLiteral("other"))}, browser.port());
  QTRY_COMPARE(ids(browser.hosts()), QStringList{QStringLiteral("other")});

  QSignalSpy changed(&browser, &LanBrowser::hostsChanged);
  browser.setOwnId(QStringLiteral("other"));
  QVERIFY(browser.hosts().isEmpty());
  QTRY_COMPARE(changed.count(), 1);
}

void LanBeaconTests::browser_sortsByName()
{
  LanBrowser browser;
  QVERIFY(browser.start(0));

  send(
      {announcement(QStringLiteral("b"), QStringLiteral("beta")),
       announcement(QStringLiteral("c"), QStringLiteral("alpha")),
       announcement(QStringLiteral("a"), QStringLiteral("Alpha"))},
      browser.port()
  );
  QTRY_COMPARE(ids(browser.hosts()), (QStringList{QStringLiteral("a"), QStringLiteral("c"), QStringLiteral("b")}));
}

void LanBeaconTests::browser_expiresHosts()
{
  LanBrowser browser;
  browser.setExpiryForTest(200);
  QVERIFY(browser.start(0));
  QList<qsizetype> sizes;
  connect(&browser, &LanBrowser::hostsChanged, this, [&browser, &sizes] { sizes.append(browser.hosts().size()); });

  send({announcement()}, browser.port());
  QTRY_COMPARE(sizes, (QList<qsizetype>{1, 0}));
}

void LanBeaconTests::browser_capsHosts()
{
  LanBrowser browser;
  QVERIFY(browser.start(0));
  Sources sources(browser);

  // each of 17 addresses announces the most hosts it may, which is one address more than there is room for
  static_assert(kMaxHosts % kMaxHostsPerAddress == 0);
  for (int source = 0; source <= kMaxHosts / kMaxHostsPerAddress; ++source) {
    QList<HostAnnouncement> hosts;
    for (int i = 0; i < kMaxHostsPerAddress; ++i) {
      hosts.append(announcement(QStringLiteral("host-%1-%2").arg(source).arg(i)));
    }
    sources.send(QStringLiteral("10.0.0.%1").arg(source + 1), hosts, browser.port());
  }
  // updates to known hosts still count, so this marks the end of the flood
  sources.send(
      QStringLiteral("10.0.0.1"), {announcement(QStringLiteral("host-0-0"), QStringLiteral("Last"))}, browser.port()
  );

  const auto sawLast = [&browser] {
    const auto hosts = browser.hosts();
    return std::ranges::any_of(hosts, [](const DiscoveredHost &host) { return host.name == u"Last"; });
  };
  QTRY_VERIFY(sawLast());
  QCOMPARE(browser.hosts().size(), kMaxHosts);
  QVERIFY(!ids(browser.hosts()).contains(QStringLiteral("host-%1-0").arg(kMaxHosts / kMaxHostsPerAddress)));
}

void LanBeaconTests::browser_capsHostsPerAddress()
{
  LanBrowser browser;
  QVERIFY(browser.start(0));
  Sources sources(browser);
  const auto flooder = QStringLiteral("10.0.0.1");
  const auto other = QStringLiteral("10.0.0.2");

  // 2000 hosts from one address, in batches that the browser gets to read in between
  for (int batch = 0; batch < 20; ++batch) {
    QList<HostAnnouncement> flood;
    for (int i = 0; i < 100; ++i) {
      flood.append(announcement(QStringLiteral("flood-%1").arg(batch * 100 + i)));
    }
    sources.send(flooder, flood, browser.port());
    QTest::qWait(1);
  }
  sources.send(other, {announcement(QStringLiteral("real"))}, browser.port());

  QTRY_VERIFY(ids(browser.hosts()).contains(QStringLiteral("real")));
  const auto hosts = ids(browser.hosts());
  QCOMPARE(hosts.size(), kMaxHostsPerAddress + 1);
  // the ones that were heard first are kept
  for (int i = 0; i < kMaxHostsPerAddress; ++i) {
    QVERIFY(hosts.contains(QStringLiteral("flood-%1").arg(i)));
  }

  // a host doesn't move to an address that has no room for it
  sources.send(flooder, {announcement(QStringLiteral("real"), QStringLiteral("Moved"))}, browser.port());
  sources.send(other, {announcement(QStringLiteral("marker"))}, browser.port());
  QTRY_VERIFY(ids(browser.hosts()).contains(QStringLiteral("marker")));
  const auto known = browser.hosts();
  const auto real = std::ranges::find(known, QStringLiteral("real"), &DiscoveredHost::id);
  QVERIFY(real != known.cend());
  QCOMPARE(real->name, QStringLiteral("Office PC"));
  QCOMPARE(real->address, QHostAddress(other));
  QCOMPARE(known.size(), kMaxHostsPerAddress + 2);
}

void LanBeaconTests::browser_dropsOtherSubnets()
{
  LanBrowser browser;
  QVERIFY(browser.start(0));
  Sources sources(browser);

  // 10.0.0.0/8 is the network of the browser, and 192.168.1.9 is not on it
  sources.send(QStringLiteral("192.168.1.9"), {announcement(QStringLiteral("far"))}, browser.port());
  sources.send(QStringLiteral("10.9.9.9"), {announcement(QStringLiteral("near"))}, browser.port());
  sources.send(QStringLiteral("192.168.1.9"), {announcement(QStringLiteral("far-again"))}, browser.port());
  sources.send(QStringLiteral("10.9.9.10"), {announcement(QStringLiteral("marker"))}, browser.port());

  QTRY_VERIFY(ids(browser.hosts()).contains(QStringLiteral("marker")));
  QCOMPARE(ids(browser.hosts()), (QStringList{QStringLiteral("marker"), QStringLiteral("near")}));

  // this computer's own loopback is always let in
  setLocalNetworksForTest(QList<LocalNetwork>{});
  send({announcement(QStringLiteral("loopback"))}, browser.port());
  QTRY_VERIFY(ids(browser.hosts()).contains(QStringLiteral("loopback")));
}

void LanBeaconTests::browser_coalescesChanges()
{
  constexpr int intervalMs = 100;
  LanBrowser browser;
  browser.setChangeIntervalForTest(intervalMs);
  QVERIFY(browser.start(0));

  QElapsedTimer clock;
  clock.start();
  QList<qint64> times;
  QStringList names;
  connect(&browser, &LanBrowser::hostsChanged, this, [&browser, &clock, &times, &names] {
    times.append(clock.elapsed());
    names.append(browser.hosts().first().name);
  });

  // 2000 datagrams, each of which changes the host: half of them in a tight loop, and half at a pace that lets the
  // browser read them a few at a time. The kernel may drop some of the first ones
  QUdpSocket socket;
  const auto change = [&socket, &browser](int i) {
    socket.writeDatagram(
        announcement(kId, QStringLiteral("Name %1").arg(i)).toDatagram(), QHostAddress(QHostAddress::LocalHost),
        browser.port()
    );
  };
  for (int i = 0; i < 1000; ++i) {
    change(i);
  }
  for (int i = 1000; i < 2000; ++i) {
    change(i);
    if (i % 5 == 0) {
      QCoreApplication::processEvents();
    }
  }
  QTest::qWait(10 * intervalMs);

  // the first change is told at once, the ones that follow in one signal, and never more often than the interval
  QVERIFY(!times.isEmpty());
  QVERIFY2(times.size() <= 10, qPrintable(QStringLiteral("%1 signals").arg(times.size())));
  for (qsizetype i = 1; i < times.size(); ++i) {
    QVERIFY2(times.at(i) - times.at(i - 1) >= intervalMs * 8 / 10, qPrintable(QStringLiteral("gap %1").arg(i)));
  }
  // and the last signal is about the state that the flood left
  QCOMPARE(names.last(), browser.hosts().first().name);
}

void LanBeaconTests::browser_ignoresNoise()
{
  LanBrowser browser;
  QVERIFY(browser.start(0));
  QSignalSpy changed(&browser, &LanBrowser::hostsChanged);

  // what can't be an announcement changes nothing, and neither does a host that repeats itself
  QUdpSocket socket;
  const auto valid = announcement().toDatagram();
  const QHostAddress loopback(QHostAddress::LocalHost);
  for (int i = 0; i < 1000; ++i) {
    socket.writeDatagram(QByteArray("not an announcement"), loopback, browser.port());
    socket.writeDatagram(valid, loopback, browser.port());
    socket.writeDatagram(QByteArray(kMaxBeaconSize + 1, 'x'), loopback, browser.port());
  }
  QTRY_COMPARE(browser.hosts().size(), 1);
  QTest::qWait(300);
  QCOMPARE(changed.count(), 1);
}

QTEST_GUILESS_MAIN(LanBeaconTests)
