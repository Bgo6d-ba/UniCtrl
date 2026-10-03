/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/ProtocolUtil.h"
#include "base/Log.h"
#include "deskflow/DeskflowException.h"
#include "deskflow/ProtocolTypes.h"
#include "io/IStream.h"

#include <array>

using deskflow::protocol::FormatArg;
using deskflow::protocol::FormatSpec;

namespace {

void appendInt(std::vector<uint8_t> &buffer, uint32_t value, size_t size)
{
  for (auto shift = static_cast<int>(size - 1) * 8; shift >= 0; shift -= 8) {
    buffer.push_back(static_cast<uint8_t>((value >> shift) & 0xffU));
  }
}

template <typename T> void appendVector(std::vector<uint8_t> &buffer, const std::vector<T> &values)
{
  appendInt(buffer, static_cast<uint32_t>(values.size()), sizeof(uint32_t));
  for (const auto value : values) {
    appendInt(buffer, value, sizeof(T));
  }
}

} // namespace

//
// ProtocolUtil
//

void ProtocolUtil::write(deskflow::IStream *stream, const char *fmt, const std::vector<uint8_t> &buffer)
{
  assert(stream != nullptr);
  LOG_VERBOSE("writef(%s)", fmt);

  if (buffer.empty()) {
    return;
  }

  const auto size = static_cast<uint32_t>(buffer.size());
  try {
    stream->write(buffer.data(), size);
    LOG_VERBOSE("wrote %d bytes", size);
  } catch (const BaseException &exception) {
    LOG_VERBOSE("exception <%s> during wrote %d bytes into stream", exception.what(), size);
    throw;
  }
}

void ProtocolUtil::writeInt(std::vector<uint8_t> &buffer, FormatArg arg, uint32_t value)
{
  using enum FormatArg;
  appendInt(buffer, value, arg == Int1 ? 1 : arg == Int2 ? 2 : 4);
}

void ProtocolUtil::writeValue(std::vector<uint8_t> &buffer, const FormatSpec &spec, const std::string &value)
{
  if (spec.length == 0) {
    appendInt(buffer, static_cast<uint32_t>(value.size()), sizeof(uint32_t));
  } else if (value.size() != spec.length) {
    throw std::invalid_argument("fixed length protocol string has the wrong length");
  }
  buffer.insert(buffer.end(), value.begin(), value.end());
}

void ProtocolUtil::writeValue(std::vector<uint8_t> &buffer, const FormatSpec &, const std::vector<uint8_t> &value)
{
  appendVector(buffer, value);
}

void ProtocolUtil::writeValue(std::vector<uint8_t> &buffer, const FormatSpec &, const std::vector<uint16_t> &value)
{
  appendVector(buffer, value);
}

void ProtocolUtil::writeValue(std::vector<uint8_t> &buffer, const FormatSpec &, const std::vector<uint32_t> &value)
{
  appendVector(buffer, value);
}

void ProtocolUtil::read(deskflow::IStream *stream, void *vbuffer, uint32_t count)
{
  assert(stream != nullptr);
  assert(vbuffer != nullptr || count == 0);

  auto *buffer = static_cast<uint8_t *>(vbuffer);
  while (count > 0) {
    // read more
    uint32_t n = stream->read(buffer, count);

    // bail if stream has hungup
    if (n == 0) {
      LOG_VERBOSE("unexpected disconnect in readf(), %d bytes left", count);
      throw IOEndOfStreamException();
    }

    // prepare for next read
    buffer += n;
    count -= n;
  }
}

void ProtocolUtil::readLiteral(deskflow::IStream *stream, std::string_view literal)
{
  for (const char expected : literal) {
    char actual = 0;
    read(stream, &actual, 1);
    if (actual != expected) {
      LOG_VERBOSE("readf: format mismatch: %c vs %c", expected, actual);
      throw XIOReadMismatch();
    }
  }
}

uint32_t ProtocolUtil::readInt(deskflow::IStream *stream, size_t size)
{
  std::array<uint8_t, 4> buffer = {};
  read(stream, buffer.data(), static_cast<uint32_t>(size));

  uint32_t result = 0;
  for (size_t i = 0; i < size; ++i) {
    result = (result << 8) | buffer[i];
  }
  LOG_VERBOSE("readf: read %d byte integer: %d (0x%x)", static_cast<int>(size), result, result);
  return result;
}

uint32_t ProtocolUtil::readVectorSize(deskflow::IStream *stream)
{
  auto size = readInt(stream, sizeof(uint32_t));

  if (size > PROTOCOL_MAX_LIST_LENGTH) {
    LOG_ERR("readVectorSize: vector length exceeds maximum allowed size: %u", size);
    throw BadClientException("Too long message received");
  }

  return size;
}

void ProtocolUtil::readValue(deskflow::IStream *stream, const FormatSpec &spec, std::string &value)
{
  // a \%s string carries its length, a \%Ns string is always N bytes
  const auto length = spec.length == 0 ? readInt(stream, sizeof(uint32_t)) : spec.length;

  if (length > PROTOCOL_MAX_STRING_LENGTH) {
    LOG_ERR("read: string length exceeds maximum allowed size: %u", length);
    throw BadClientException("Too long message received");
  }

  std::string data(length, '\0');
  read(stream, data.data(), length);
  LOG_VERBOSE("readf: read %d byte string", length);
  value = std::move(data);
}

void ProtocolUtil::readValue(deskflow::IStream *stream, const FormatSpec &, std::vector<uint8_t> &value)
{
  for (auto size = readVectorSize(stream); size > 0; --size) {
    value.push_back(static_cast<uint8_t>(readInt(stream, sizeof(uint8_t))));
  }
}

void ProtocolUtil::readValue(deskflow::IStream *stream, const FormatSpec &, std::vector<uint16_t> &value)
{
  for (auto size = readVectorSize(stream); size > 0; --size) {
    value.push_back(static_cast<uint16_t>(readInt(stream, sizeof(uint16_t))));
  }
}

void ProtocolUtil::readValue(deskflow::IStream *stream, const FormatSpec &, std::vector<uint32_t> &value)
{
  for (auto size = readVectorSize(stream); size > 0; --size) {
    value.push_back(readInt(stream, sizeof(uint32_t)));
  }
}

//
// XIOReadMismatch
//

QString XIOReadMismatch::getWhat() const throw()
{
  return format("XIOReadMismatch", "ProtocolUtil::readf() mismatch");
}
