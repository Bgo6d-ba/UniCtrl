/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ClientProxyTests.h"

#include "ServerFakes.h"
#include "deskflow/AppUtil.h"
#include "deskflow/Clipboard.h"
#include "deskflow/PacketStreamFilter.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"
#include "deskflow/Screen.h"
#include "io/IStream.h"
#include "server/ClientProxy1_0.h"
#include "server/ClientProxy1_1.h"
#include "server/ClientProxy1_3.h"
#include "server/ClientProxy1_6.h"
#include "server/ClientProxy1_7.h"
#include "server/ClientProxy1_8.h"
#include "server/ClientProxyUnknown.h"
#include "server/Config.h"
#include "server/PrimaryClient.h"
#include "server/Server.h"

#include <memory>
#include <string>
#include <vector>

#include <QByteArray>
#include <QTest>

namespace {

class TestAppUtil : public AppUtil
{
public:
  int run() override
  {
    return 0;
  }

  std::vector<std::string> getKeyboardLayoutList() override
  {
    return {"en"};
  }

  std::string getCurrentLanguageCode() override
  {
    return "en";
  }
};

class CapturingStream : public deskflow::IStream
{
public:
  QByteArray take()
  {
    auto bytes = m_buffer;
    m_buffer.clear();
    return bytes;
  }

  void write(const void *buffer, uint32_t n) override
  {
    m_buffer.append(static_cast<const char *>(buffer), n);
  }

  void close() override
  {
  }

  uint32_t read(void *, uint32_t) override
  {
    return 0;
  }

  void flush() override
  {
  }

  void shutdownInput() override
  {
  }

  void shutdownOutput() override
  {
  }

  void *getEventTarget() const override
  {
    return const_cast<CapturingStream *>(this);
  }

  bool isReady() const override
  {
    return false;
  }

  uint32_t getSize() const override
  {
    return 0;
  }

private:
  QByteArray m_buffer;
};

// the 1.4 and later constructors assert the server pointer is non-null; sending never uses it
Server *const kUnusedServer = reinterpret_cast<Server *>(0x1);

std::unique_ptr<ClientProxy>
makeProxy(int minor, deskflow::IStream *stream, IEventQueue *events, Server *server = kUnusedServer)
{
  std::unique_ptr<ClientProxy> proxy;
  switch (minor) {
  case 0:
    proxy = std::make_unique<ClientProxy1_0>("client", stream, events);
    break;
  case 1:
    proxy = std::make_unique<ClientProxy1_1>("client", stream, events);
    break;
  case 3:
    proxy = std::make_unique<ClientProxy1_3>("client", stream, events);
    break;
  case 6:
    proxy = std::make_unique<ClientProxy1_6>("client", stream, server, events);
    break;
  case 7:
    proxy = std::make_unique<ClientProxy1_7>("client", stream, server, events);
    break;
  case 8:
    proxy = std::make_unique<ClientProxy1_8>("client", stream, server, events);
    break;
  default:
    break;
  }
  return proxy;
}

struct ProxyUnderTest
{
  MockEventQueue events;
  CapturingStream *stream = new CapturingStream;
  std::unique_ptr<ClientProxy> proxy;

  explicit ProxyUnderTest(int minor) : proxy(makeProxy(minor, stream, &events))
  {
    // drop the query-info and layout-sync messages the constructors send
    stream->take();
  }
};

// The client end of a connection: bytes the test feeds in, and what the proxy writes back
class WireStream : public deskflow::IStream
{
public:
  void feed(const std::string &bytes)
  {
    m_in += bytes;
  }

  std::string take()
  {
    return std::exchange(m_out, {});
  }

  void close() override
  {
  }

  uint32_t read(void *buffer, uint32_t n) override
  {
    const auto count = static_cast<uint32_t>(std::min<size_t>(n, m_in.size()));
    if (buffer != nullptr) {
      std::memcpy(buffer, m_in.data(), count);
    }
    m_in.erase(0, count);
    return count;
  }

  void write(const void *buffer, uint32_t n) override
  {
    m_out.append(static_cast<const char *>(buffer), n);
  }

  void flush() override
  {
  }

  void shutdownInput() override
  {
  }

  void shutdownOutput() override
  {
  }

  void *getEventTarget() const override
  {
    return const_cast<WireStream *>(this);
  }

  bool isReady() const override
  {
    return !m_in.empty();
  }

  uint32_t getSize() const override
  {
    return static_cast<uint32_t>(m_in.size());
  }

private:
  std::string m_in;
  std::string m_out;
};

std::string packet(const std::string &body)
{
  const auto n = static_cast<uint32_t>(body.size());
  return std::string{char(n >> 24), char(n >> 16), char(n >> 8), char(n)} + body;
}

template <typename... Args> std::string encode(ProtocolUtil::WriteFormat<Args...> fmt, const Args &...args)
{
  WireStream stream;
  ProtocolUtil::writef(&stream, fmt, args...);
  return stream.take();
}

// A proxy for client "client" on a real server, reading framed messages as it would from a socket
struct ServerSide
{
  explicit ServerSide(int minor = 8, deskflow::server::ServerOptions options = {}) : config(&events, std::move(options))
  {
    config.addScreen("server");
    config.addScreen("client");
    server = std::make_unique<Server>(config, &primary, &screen, &events);
    events.flush();
    proxy = makeProxy(minor, new PacketStreamFilter(&events, wire), &events, server.get());
    wire->take();
  }

  //! Delivers raw bytes, as the socket does when they arrive
  void receive(const std::string &bytes)
  {
    wire->feed(bytes);
    events.send(EventTypes::StreamInputReady, wire);
  }

  template <typename... Args> void receive(ProtocolUtil::WriteFormat<Args...> fmt, const Args &...args)
  {
    receive(packet(encode(fmt, args...)));
  }

  void sendInfo(int16_t width = 1000)
  {
    receive(kMsgDInfo, int16_t{0}, int16_t{0}, width, int16_t{800}, int16_t{0}, int16_t{500}, int16_t{400});
  }

  void sendClipboard(ClipboardID id, uint32_t seq, const std::string &data)
  {
    receive(kMsgDClipboard, id, seq, uint8_t(ChunkType::DataStart), std::to_string(data.size()));
    receive(kMsgDClipboard, id, seq, uint8_t(ChunkType::DataChunk), data);
    receive(kMsgDClipboard, id, seq, uint8_t(ChunkType::DataEnd), std::string());
  }

  std::vector<const Event *> posted(EventTypes type) const
  {
    std::vector<const Event *> events_;
    for (const auto &event : events.m_posted) {
      if (event.getType() == type) {
        events_.push_back(&event);
      }
    }
    return events_;
  }

  FakeEventQueue events;
  deskflow::server::Config config;
  FakePlatformScreen *platform = new FakePlatformScreen(&events);
  deskflow::Screen screen{platform, &events};
  PrimaryClient primary{"server", &screen};
  std::unique_ptr<Server> server;
  WireStream *wire = new WireStream;
  std::unique_ptr<ClientProxy> proxy;
};

std::string marshalledText(const std::string &text)
{
  Clipboard clipboard;
  clipboard.open(0);
  clipboard.add(IClipboard::Format::Text, text);
  clipboard.close();
  return clipboard.marshall();
}

const KeyID kKey = 0x61;
const KeyModifierMask kMask = 0;
const KeyButton kButton = 0x1e;
const int32_t kCount = 3;
const std::string kLang = "en";

} // namespace

void ClientProxyTests::initTestCase()
{
  // the 1.8 constructor reads the keyboard layouts through AppUtil::instance()
  static TestAppUtil appUtil;
}

// These formats are frozen because shipped third-party clients parse them byte
// for byte: Synergy 1.4 through 1.14.1 negotiate 1.4 through 1.7, Barrier and
// Input Leap negotiate 1.6, and Synergy 1.14.2 onwards and Deskflow negotiate 1.8.
void ClientProxyTests::keyDown_data()
{
  QTest::addColumn<int>("minor");
  QTest::addColumn<QByteArray>("expected");

  QTest::newRow("1.0") << 0 << "DKDN" + QByteArray::fromHex("0061 0000");
  QTest::newRow("1.1") << 1 << "DKDN" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.6") << 6 << "DKDN" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.7") << 7 << "DKDN" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.8") << 8 << "DKDL" + QByteArray::fromHex("0061 0000 001e 00000002") + "en";
}

void ClientProxyTests::keyDown()
{
  QFETCH(int, minor);
  QFETCH(QByteArray, expected);

  ProxyUnderTest test(minor);
  test.proxy->keyDown(kKey, kMask, kButton, kLang);
  QCOMPARE(test.stream->take(), expected);
}

void ClientProxyTests::keyRepeat_data()
{
  QTest::addColumn<int>("minor");
  QTest::addColumn<QByteArray>("expected");

  QTest::newRow("1.0") << 0 << "DKRP" + QByteArray::fromHex("0061 0000 0003");
  QTest::newRow("1.1") << 1 << "DKRP" + QByteArray::fromHex("0061 0000 0003 001e");
  QTest::newRow("1.6") << 6 << "DKRP" + QByteArray::fromHex("0061 0000 0003 001e");
  QTest::newRow("1.7") << 7 << "DKRP" + QByteArray::fromHex("0061 0000 0003 001e");
  QTest::newRow("1.8") << 8 << "DKRP" + QByteArray::fromHex("0061 0000 0003 001e 00000002") + "en";
}

void ClientProxyTests::keyRepeat()
{
  QFETCH(int, minor);
  QFETCH(QByteArray, expected);

  ProxyUnderTest test(minor);
  test.proxy->keyRepeat(kKey, kMask, kCount, kButton, kLang);
  QCOMPARE(test.stream->take(), expected);
}

void ClientProxyTests::keyUp_data()
{
  QTest::addColumn<int>("minor");
  QTest::addColumn<QByteArray>("expected");

  QTest::newRow("1.0") << 0 << "DKUP" + QByteArray::fromHex("0061 0000");
  QTest::newRow("1.1") << 1 << "DKUP" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.6") << 6 << "DKUP" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.7") << 7 << "DKUP" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.8") << 8 << "DKUP" + QByteArray::fromHex("0061 0000 001e");
}

void ClientProxyTests::keyUp()
{
  QFETCH(int, minor);
  QFETCH(QByteArray, expected);

  ProxyUnderTest test(minor);
  test.proxy->keyUp(kKey, kMask, kButton);
  QCOMPARE(test.stream->take(), expected);
}

void ClientProxyTests::isValidName_data()
{
  QTest::addColumn<QByteArray>("name");
  QTest::addColumn<bool>("valid");

  QTest::newRow("plain") << QByteArray("laptop") << true;
  QTest::newRow("utf8") << QByteArray("\xd0\xbd\xd0\xbe\xd1\x83\xd1\x82") << true;
  QTest::newRow("empty") << QByteArray() << false;
  QTest::newRow("newline") << QByteArray("laptop\nfake") << false;
  QTest::newRow("carriage return") << QByteArray("laptop\r") << false;
  QTest::newRow("nul") << QByteArray("lap\0top", 7) << false;
  QTest::newRow("delete") << QByteArray("laptop\x7f") << false;
  // the gui ipc joins connected client names with commas
  QTest::newRow("comma") << QByteArray("laptop,fake") << false;
}

void ClientProxyTests::isValidName()
{
  QFETCH(QByteArray, name);
  QFETCH(bool, valid);

  QCOMPARE(ClientProxyUnknown::isValidName(name.toStdString()), valid);
}

void ClientProxyTests::handshakeInfo_readiesClient()
{
  ServerSide test;
  test.sendInfo();

  QCOMPARE(test.posted(EventTypes::ClientProxyReady).size(), 1);
  QCOMPARE(test.wire->take(), packet(kMsgCInfoAck));
  int32_t x;
  int32_t y;
  int32_t w;
  int32_t h;
  test.proxy->getShape(x, y, w, h);
  QCOMPARE((std::vector<int32_t>{x, y, w, h}), (std::vector<int32_t>{0, 0, 1000, 800}));
}

void ClientProxyTests::handshakeInfo_retriesAfterInvalidShape()
{
  ServerSide test;
  test.sendInfo(0);
  QVERIFY(test.posted(EventTypes::ClientProxyReady).empty());

  test.sendInfo();

  QCOMPARE(test.posted(EventTypes::ClientProxyReady).size(), 1);
  QVERIFY(test.posted(EventTypes::ScreenShapeChanged).empty());
}

void ClientProxyTests::info_afterHandshake_changesShape()
{
  ServerSide test;
  test.sendInfo();
  test.sendInfo(1200);

  QCOMPARE(test.posted(EventTypes::ScreenShapeChanged).size(), 1);
  int32_t x;
  int32_t y;
  int32_t w;
  int32_t h;
  test.proxy->getShape(x, y, w, h);
  QCOMPARE(w, 1200);
}

void ClientProxyTests::grabClipboard_postsGrab()
{
  ServerSide test;
  test.sendInfo();
  test.receive(kMsgCClipboard, ClipboardID{kClipboardEnd}, uint32_t{3});
  test.receive(kMsgCClipboard, ClipboardID{kClipboardSelection}, uint32_t{7});

  const auto grabs = test.posted(EventTypes::ClipboardGrabbed);
  QCOMPARE(grabs.size(), 1);
  const auto *info = static_cast<const IScreen::ClipboardInfo *>(grabs[0]->getData());
  QCOMPARE(info->m_id, kClipboardSelection);
  QCOMPARE(info->m_sequenceNumber, 7u);
  QVERIFY(test.posted(EventTypes::ClientProxyDisconnected).empty());
}

void ClientProxyTests::unknownMessage_isSkipped()
{
  ServerSide test;
  test.sendInfo();
  test.receive(packet("XXXX-trailing-bytes"));
  test.receive(kMsgCClipboard, ClipboardID{kClipboardClipboard}, uint32_t{1});

  QCOMPARE(test.posted(EventTypes::ClipboardGrabbed).size(), 1);
  QVERIFY(test.posted(EventTypes::ClientProxyDisconnected).empty());
}

void ClientProxyTests::truncatedCode_disconnects()
{
  ServerSide test;
  test.sendInfo();
  test.receive(packet("CC"));

  QCOMPARE(test.posted(EventTypes::ClientProxyDisconnected).size(), 1);
}

void ClientProxyTests::oversizedString_disconnects()
{
  ServerSide test;
  test.sendInfo();
  // a clipboard chunk whose string length is beyond the protocol limit
  test.receive(packet(std::string("DCLP\x00\x00\x00\x00\x01\x02\x7f\xff\xff\xff", 14)));

  QCOMPARE(test.posted(EventTypes::ClientProxyDisconnected).size(), 1);
  QVERIFY(test.posted(EventTypes::ClipboardChanged).empty());
}

void ClientProxyTests::clipboardTransfer_postsChange_data()
{
  QTest::addColumn<int>("minor");
  QTest::newRow("1.6") << 6;
  QTest::newRow("1.8") << 8;
}

void ClientProxyTests::clipboardTransfer_postsChange()
{
  QFETCH(int, minor);
  ServerSide test(minor);
  test.sendInfo();
  test.sendClipboard(kClipboardClipboard, 4, marshalledText("hello"));

  const auto changes = test.posted(EventTypes::ClipboardChanged);
  QCOMPARE(changes.size(), 1);
  QCOMPARE(static_cast<const IScreen::ClipboardInfo *>(changes[0]->getData())->m_sequenceNumber, 4u);
  Clipboard clipboard;
  QVERIFY(test.proxy->getClipboard(kClipboardClipboard, &clipboard));
  QCOMPARE(clipboardText(&clipboard), "hello");
}

void ClientProxyTests::clipboardTransfer_overLimit_isDropped()
{
  deskflow::server::ServerOptions options;
  options.clipboardSizeMiB = 1;
  ServerSide test(8, options);
  test.sendInfo();
  // declares more than the 1 MiB limit, then streams on regardless
  const auto id = ClipboardID{kClipboardClipboard};
  test.receive(kMsgDClipboard, id, uint32_t{1}, uint8_t(ChunkType::DataStart), std::string("2000000"));
  test.receive(kMsgDClipboard, id, uint32_t{1}, uint8_t(ChunkType::DataChunk), std::string("x"));
  test.receive(kMsgDClipboard, id, uint32_t{1}, uint8_t(ChunkType::DataEnd), std::string());
  QVERIFY(test.posted(EventTypes::ClipboardChanged).empty());

  // the next transfer within the limit still arrives
  test.sendClipboard(kClipboardClipboard, 2, marshalledText("small"));
  QCOMPARE(test.posted(EventTypes::ClipboardChanged).size(), 1);
  QVERIFY(test.posted(EventTypes::ClientProxyDisconnected).empty());
}

void ClientProxyTests::clientClipboard_reachesServerDesktop()
{
  ServerSide test;
  test.sendInfo();
  test.server->adoptClient(test.proxy.release());
  test.events.flush();
  test.platform->m_clipboards.clear();

  test.receive(kMsgCClipboard, ClipboardID{kClipboardClipboard}, uint32_t{1});
  test.events.flush();
  test.sendClipboard(kClipboardClipboard, 1, marshalledText("from the client"));
  test.events.flush();

  QVERIFY(!test.platform->m_clipboards.empty());
  QCOMPARE(test.platform->m_clipboards.back(), "from the client");
}

QTEST_MAIN(ClientProxyTests)
