/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

// Feeds raw wire bytes to PacketStreamFilter in chunks (the first byte picks the chunk size) and checks
// it delivers exactly the packets a straightforward length-prefix parser finds.

#include "FuzzSupport.h"

#include "deskflow/PacketStreamFilter.h"
#include "deskflow/ProtocolTypes.h"

using deskflow::fuzz::check;

namespace {

// Complete non-empty packets up to the first oversized length, and whether one was found
std::pair<std::vector<std::string>, bool> expectedPackets(std::string_view wire)
{
  std::vector<std::string> packets;
  while (wire.size() >= 4) {
    const auto *p = reinterpret_cast<const uint8_t *>(wire.data());
    const uint32_t length = (uint32_t{p[0]} << 24) | (uint32_t{p[1]} << 16) | (uint32_t{p[2]} << 8) | p[3];
    if (length > PROTOCOL_MAX_MESSAGE_LENGTH) {
      return {packets, true};
    }
    if (wire.size() - 4 < length) {
      break;
    }
    if (length != 0) {
      packets.emplace_back(wire.substr(4, length));
    }
    wire.remove_prefix(4 + length);
  }
  return {packets, false};
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  deskflow::fuzz::initArchAndLog();
  if (size == 0) {
    return 0;
  }
  const size_t chunkSize = 1 + data[0];
  const std::string_view wire(reinterpret_cast<const char *>(data) + 1, size - 1);

  deskflow::fuzz::EventRecorder events;
  auto *source = new deskflow::fuzz::MemoryStream;
  PacketStreamFilter filter(&events, source);

  std::vector<std::string> packets;
  for (size_t offset = 0; offset < wire.size(); offset += chunkSize) {
    source->append(wire.substr(offset, chunkSize));
    events.dispatch(EventTypes::StreamInputReady, source->getEventTarget());
    while (filter.isReady()) {
      std::string packet(filter.getSize(), '\0');
      check(filter.read(packet.data(), static_cast<uint32_t>(packet.size())) == packet.size(), "short packet read");
      packets.push_back(std::move(packet));
    }
  }

  const auto [expected, oversized] = expectedPackets(wire);
  const bool formatError =
      std::ranges::find(events.m_added, EventTypes::StreamInputFormatError) != events.m_added.end();
  check(formatError == oversized, "format error does not match an oversized length");
  check(packets == expected, "delivered packets differ from the wire");
  return 0;
}
