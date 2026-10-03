/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ProtocolUtilTests.h"

#include "MockEventQueue.h"
#include "deskflow/DeskflowException.h"
#include "deskflow/PacketStreamFilter.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"
#include "io/IStream.h"

#include <QTest>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {

class MemoryStream : public deskflow::IStream
{
public:
  explicit MemoryStream(std::string bytes) : m_bytes(std::move(bytes))
  {
  }

  void close() override
  {
  }

  uint32_t read(void *buffer, uint32_t n) override
  {
    const auto count = static_cast<uint32_t>(std::min<size_t>(n, m_bytes.size()));
    if (buffer != nullptr) {
      std::memcpy(buffer, m_bytes.data(), count);
    }
    m_bytes.erase(0, count);
    return count;
  }

  void write(const void *buffer, uint32_t n) override
  {
    m_bytes.append(static_cast<const char *>(buffer), n);
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
    return const_cast<MemoryStream *>(this);
  }

  bool isReady() const override
  {
    return !m_bytes.empty();
  }

  uint32_t getSize() const override
  {
    return static_cast<uint32_t>(m_bytes.size());
  }

private:
  std::string m_bytes;
};

std::string bigEndian32(uint32_t value)
{
  return {
      static_cast<char>((value >> 24) & 0xff), static_cast<char>((value >> 16) & 0xff),
      static_cast<char>((value >> 8) & 0xff), static_cast<char>(value & 0xff)
  };
}

// Writes the values with Fmt, reads them back with Fmt, and expects the same values and nothing left over.
template <const char *const &Fmt, typename... T> bool roundTrips(const T &...sent)
{
  MemoryStream stream({});
  ProtocolUtil::writef(&stream, Fmt, sent...);

  std::tuple<T...> received;
  const bool read =
      std::apply([&stream](T &...values) { return ProtocolUtil::readf(&stream, Fmt, &values...); }, received);
  return read && received == std::tuple<T...>(sent...) && stream.getSize() == 0;
}

// writef() and readf() run formatMatches() in the consteval constructor of their format argument, so each of the
// mismatched calls below is a compile error at the call site. Checking the validator directly documents that.
using deskflow::protocol::formatMatches;
constexpr bool kWrite = false;
constexpr bool kRead = true;

static_assert(formatMatches<kWrite, int32_t, int16_t, uint32_t, uint16_t>(kMsgCEnter));
static_assert(formatMatches<kRead, int16_t *, int16_t *, uint32_t *, uint16_t *>(kMsgCEnter + 4));
static_assert(formatMatches<kWrite, std::string, int16_t, int16_t>(kMsgHello));
static_assert(formatMatches<kRead, std::vector<uint32_t> *>(kMsgDSetOptions));

// too few, too many, or the wrong kind of argument
static_assert(!formatMatches<kWrite, int16_t>(kMsgDMouseMove));
static_assert(!formatMatches<kWrite, int16_t, int16_t, int16_t>(kMsgDMouseMove));
static_assert(!formatMatches<kWrite, std::string, int16_t>(kMsgDMouseMove));
static_assert(!formatMatches<kWrite, int16_t, int16_t>(kMsgDSecureInputNotification));
static_assert(!formatMatches<kWrite, const char *>(kMsgDSecureInputNotification));
static_assert(!formatMatches<kWrite, bool>(kMsgDMouseDown));
static_assert(!formatMatches<kWrite, int64_t>(kMsgDMouseDown));
static_assert(!formatMatches<kWrite, std::vector<uint16_t>>(kMsgDSetOptions));

// reads must point at a mutable value exactly as wide as the specifier
static_assert(!formatMatches<kRead, int32_t *, int32_t *>(kMsgDMouseMove + 4));
static_assert(!formatMatches<kRead, uint8_t *, uint8_t *>(kMsgDMouseMove + 4));
static_assert(!formatMatches<kRead, int16_t, int16_t>(kMsgDMouseMove + 4));
static_assert(!formatMatches<kRead, const int16_t *, const int16_t *>(kMsgDMouseMove + 4));
static_assert(!formatMatches<kRead, std::nullptr_t>(kMsgDMouseDown + 4));

// malformed specifiers never match
static_assert(!formatMatches<kWrite, int32_t>("%3i"));
static_assert(!formatMatches<kWrite, int32_t>("%i"));
static_assert(!formatMatches<kWrite>("%%"));
static_assert(!formatMatches<kWrite>("CBYE%"));
static_assert(!formatMatches<kWrite, std::vector<uint8_t>>("%8I"));

} // namespace

void ProtocolUtilTests::readf_string_roundTrip()
{
  MemoryStream stream({});
  const std::string sent = "hello";
  ProtocolUtil::writef(&stream, "SECN%s", sent);

  std::string received;
  QVERIFY(ProtocolUtil::readf(&stream, "SECN%s", &received));
  QCOMPARE(received, sent);
}

void ProtocolUtilTests::readf_string_maxLength_accepted()
{
  MemoryStream stream(bigEndian32(PROTOCOL_MAX_STRING_LENGTH) + std::string(PROTOCOL_MAX_STRING_LENGTH, 'x'));

  std::string received;
  QVERIFY(ProtocolUtil::readf(&stream, "%s", &received));
  QCOMPARE(received.size(), static_cast<size_t>(PROTOCOL_MAX_STRING_LENGTH));
}

void ProtocolUtilTests::readf_string_oversizedLength_throws()
{
  // only the length prefix is sent, so this must be rejected before any payload is buffered
  MemoryStream stream(bigEndian32(PROTOCOL_MAX_STRING_LENGTH + 1));

  std::string received;
  QVERIFY_THROWS_EXCEPTION(BadClientException, (void)ProtocolUtil::readf(&stream, "%s", &received));
  QVERIFY(received.empty());
}

void ProtocolUtilTests::readf_truncatedInput_returnsFalse()
{
  MemoryStream stream(std::string("\x00\x01\x00", 3));

  int16_t x = 0;
  int16_t y = 0;
  QVERIFY(!ProtocolUtil::readf(&stream, "%2i%2i", &x, &y));
}

void ProtocolUtilTests::readf_literalMismatch_returnsFalse()
{
  MemoryStream stream("DKDN");

  QVERIFY(!ProtocolUtil::readf(&stream, "DKUP"));
}

void ProtocolUtilTests::roundTrip_everyMessage()
{
  const std::string text = "text";

  QVERIFY(roundTrips<kMsgHello>(std::string("Barrier"), int16_t{1}, int16_t{8}));
  QVERIFY(roundTrips<kMsgHelloBack>(std::string("Synergy"), int16_t{1}, int16_t{6}, text));
  QVERIFY(roundTrips<kMsgCNoop>());
  QVERIFY(roundTrips<kMsgCClose>());
  QVERIFY(roundTrips<kMsgCEnter>(int16_t{-10}, int16_t{20}, uint32_t{0xdeadbeef}, uint16_t{0x2002}));
  QVERIFY(roundTrips<kMsgCLeave>());
  QVERIFY(roundTrips<kMsgCClipboard>(uint8_t{1}, uint32_t{7}));
  QVERIFY(roundTrips<kMsgCScreenSaver>(uint8_t{1}));
  QVERIFY(roundTrips<kMsgCResetOptions>());
  QVERIFY(roundTrips<kMsgCInfoAck>());
  QVERIFY(roundTrips<kMsgCKeepAlive>());
  QVERIFY(roundTrips<kMsgDKeyDown>(uint16_t{0x61}, uint16_t{0x2}, uint16_t{0x1e}, text));
  QVERIFY(roundTrips<kMsgDKeyDown1_1>(uint16_t{0x61}, uint16_t{0x2}, uint16_t{0x1e}));
  QVERIFY(roundTrips<kMsgDKeyDown1_0>(uint16_t{0x61}, uint16_t{0x2}));
  QVERIFY(roundTrips<kMsgDKeyRepeat>(uint16_t{0x61}, uint16_t{0x2}, int16_t{3}, uint16_t{0x1e}, text));
  QVERIFY(roundTrips<kMsgDKeyRepeat1_1>(uint16_t{0x61}, uint16_t{0x2}, int16_t{3}, uint16_t{0x1e}));
  QVERIFY(roundTrips<kMsgDKeyRepeat1_0>(uint16_t{0x61}, uint16_t{0x2}, int16_t{3}));
  QVERIFY(roundTrips<kMsgDKeyUp>(uint16_t{0x61}, uint16_t{0x2}, uint16_t{0x1e}));
  QVERIFY(roundTrips<kMsgDKeyUp1_0>(uint16_t{0x61}, uint16_t{0x2}));
  QVERIFY(roundTrips<kMsgDMouseDown>(uint8_t{3}));
  QVERIFY(roundTrips<kMsgDMouseUp>(uint8_t{3}));
  QVERIFY(roundTrips<kMsgDMouseMove>(int16_t{-1}, int16_t{32767}));
  QVERIFY(roundTrips<kMsgDMouseRelMove>(int16_t{-32768}, int16_t{5}));
  QVERIFY(roundTrips<kMsgDMouseWheel>(int16_t{-120}, int16_t{120}));
  QVERIFY(roundTrips<kMsgDMouseWheel1_0>(int16_t{-120}));
  QVERIFY(roundTrips<kMsgDClipboard>(uint8_t{0}, uint32_t{9}, uint8_t{2}, text));
  QVERIFY(
      roundTrips<kMsgDInfo>(
          int16_t{-1920}, int16_t{0}, int16_t{1920}, int16_t{1080}, int16_t{0}, int16_t{-960}, int16_t{540}
      )
  );
  QVERIFY(roundTrips<kMsgDSetOptions>(std::vector<uint32_t>{1, 0xffffffff, 0}));
  QVERIFY(roundTrips<kMsgDFileTransfer>(uint8_t{2}, text));
  QVERIFY(roundTrips<kMsgDDragInfo>(uint16_t{1}, text));
  QVERIFY(roundTrips<kMsgDSecureInputNotification>(text));
  QVERIFY(roundTrips<kMsgDLanguageSynchronisation>(std::string("enru")));
  QVERIFY(roundTrips<kMsgQInfo>());
  QVERIFY(roundTrips<kMsgEIncompatible>(int16_t{1}, int16_t{8}));
  QVERIFY(roundTrips<kMsgEBusy>());
  QVERIFY(roundTrips<kMsgEUnknown>());
  QVERIFY(roundTrips<kMsgEBad>());
}

void ProtocolUtilTests::writef_hello_fixedLengthName()
{
  // the protocol name is exactly 7 bytes with no length prefix, and the name after it is prefixed
  MemoryStream stream({});
  ProtocolUtil::writef(&stream, kMsgHelloBack, std::string("Barrier"), 1, 8, std::string("workstation"));

  std::string bytes(stream.getSize(), '\0');
  QCOMPARE(stream.read(bytes.data(), stream.getSize()), static_cast<uint32_t>(bytes.size()));
  QCOMPARE(bytes, std::string("Barrier\x00\x01\x00\x08\x00\x00\x00\x0Bworkstation", 26));
}

void ProtocolUtilTests::writef_fixedLengthStringWrongLength_throws()
{
  MemoryStream stream({});

  QVERIFY_THROWS_EXCEPTION(
      std::invalid_argument, ProtocolUtil::writef(&stream, kMsgHello, std::string("Deskflow"), 1, 8)
  );
  QCOMPARE(stream.getSize(), 0u);
}

void ProtocolUtilTests::writef_checkedFormat()
{
  MemoryStream stream({});

  ProtocolUtil::writef(&stream, ProtocolUtil::WriteFormat<>::checked(kMsgCClose));
  QVERIFY(ProtocolUtil::readf(&stream, kMsgCClose));

  QVERIFY_THROWS_EXCEPTION(std::invalid_argument, ProtocolUtil::WriteFormat<>::checked(kMsgDMouseDown));
  QVERIFY_THROWS_EXCEPTION(std::invalid_argument, ProtocolUtil::WriteFormat<>::checked(nullptr));
}

void ProtocolUtilTests::packetStreamFilter_emptyPacket_nextPacketReadable()
{
  // keeps the handler PacketStreamFilter installs on its source, so the test can deliver input
  class HandlerQueue : public MockEventQueue
  {
  public:
    void addHandler(EventTypes, void *, const EventHandler &handler) override
    {
      m_handler = handler;
    }
    EventHandler m_handler;
  };

  HandlerQueue events;
  auto *source = new MemoryStream(bigEndian32(0) + bigEndian32(4) + "CNOP");
  PacketStreamFilter filter(&events, source);
  events.m_handler(Event(EventTypes::StreamInputReady, source->getEventTarget()));

  QVERIFY(filter.isReady());
  std::string packet(4, '\0');
  QCOMPARE(filter.read(packet.data(), 4), 4u);
  QCOMPARE(packet, "CNOP");
}

QTEST_MAIN(ProtocolUtilTests)
