/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "io/IOException.h"

#include <concepts>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace deskflow {
class IStream;
}

namespace deskflow::protocol {

//! What a format specifier transfers; \c End marks the end of the format
enum class FormatArg : uint8_t
{
  End,
  Int1,
  Int2,
  Int4,
  Vec1,
  Vec2,
  Vec4,
  String,
  Invalid
};

struct FormatSpec
{
  std::string_view literal; //!< regular characters before the specifier
  FormatArg arg;
  uint32_t length; //!< byte count of a fixed \%Ns string, 0 for a length-prefixed \%s
};

//! Returns the literal run and specifier starting at \p pos, and advances \p pos past them
constexpr FormatSpec nextSpec(std::string_view fmt, size_t &pos)
{
  const auto start = pos;
  pos = fmt.find('%', start);
  if (pos == std::string_view::npos) {
    pos = fmt.size();
    return {fmt.substr(start), FormatArg::End, 0};
  }

  FormatSpec spec{fmt.substr(start, pos - start), FormatArg::Invalid, 0};
  ++pos;
  uint32_t length = 0;
  while (pos < fmt.size() && fmt[pos] >= '0' && fmt[pos] <= '9') {
    length = length * 10 + static_cast<uint32_t>(fmt[pos++] - '0');
  }
  const char type = pos < fmt.size() ? fmt[pos++] : '\0';

  using enum FormatArg;
  if (type == 's') {
    spec.arg = String;
    spec.length = length;
  } else if (type == 'i' || type == 'I') {
    const bool vector = type == 'I';
    if (length == 1) {
      spec.arg = vector ? Vec1 : Int1;
    } else if (length == 2) {
      spec.arg = vector ? Vec2 : Int2;
    } else if (length == 4) {
      spec.arg = vector ? Vec4 : Int4;
    }
  }
  return spec;
}

//! The specifier a value of type \p T is read with
template <typename T> constexpr FormatArg formatArgOf()
{
  using enum FormatArg;
  if constexpr (std::integral<T> && !std::same_as<T, bool>) {
    return sizeof(T) == 1 ? Int1 : sizeof(T) == 2 ? Int2 : sizeof(T) == 4 ? Int4 : Invalid;
  } else if constexpr (std::same_as<T, std::string>) {
    return String;
  } else if constexpr (std::same_as<T, std::vector<uint8_t>>) {
    return Vec1;
  } else if constexpr (std::same_as<T, std::vector<uint16_t>>) {
    return Vec2;
  } else if constexpr (std::same_as<T, std::vector<uint32_t>>) {
    return Vec4;
  } else {
    return Invalid;
  }
}

//! readf() stores into exactly the width on the wire, so it takes a pointer to that type.
//! writef() truncates any integer of up to 4 bytes to the specifier's width, as the wire always has.
template <bool IsRead, typename T> constexpr bool argMatches(FormatArg arg)
{
  using enum FormatArg;
  if constexpr (IsRead) {
    if constexpr (std::is_pointer_v<T> && !std::is_const_v<std::remove_pointer_t<T>>) {
      return arg != Invalid && arg == formatArgOf<std::remove_pointer_t<T>>();
    } else {
      return false;
    }
  } else if constexpr (std::integral<T> && !std::same_as<T, bool>) {
    return sizeof(T) <= 4 && (arg == Int1 || arg == Int2 || arg == Int4);
  } else {
    return arg != Invalid && arg == formatArgOf<T>();
  }
}

template <bool IsRead, typename... Args> constexpr bool formatMatches(std::string_view fmt)
{
  size_t pos = 0;
  return (argMatches<IsRead, Args>(nextSpec(fmt, pos).arg) && ...) && nextSpec(fmt, pos).arg == FormatArg::End;
}

// Not constexpr, so reaching it while checking a format is the compile error.
inline void formatDoesNotMatchArguments()
{
}

//! A format string checked against its argument types at compile time, like std::format_string
template <bool IsRead, typename... Args> class Format
{
public:
  consteval Format(const char *fmt) : m_fmt(fmt)
  {
    if (!formatMatches<IsRead, Args...>(m_fmt)) {
      formatDoesNotMatchArguments();
    }
  }

  //! For a format only known at run time; throws std::invalid_argument when it does not match
  static Format checked(const char *fmt)
  {
    if (fmt == nullptr || !formatMatches<IsRead, Args...>(fmt)) {
      throw std::invalid_argument("protocol format does not match its arguments");
    }
    return Format(fmt, 0);
  }

  const char *c_str() const
  {
    return m_fmt;
  }

private:
  Format(const char *fmt, int) : m_fmt(fmt)
  {
  }

  const char *m_fmt;
};

} // namespace deskflow::protocol

//! Deskflow protocol utilities
/*!
This class provides various functions for implementing the deskflow
protocol.
*/
class ProtocolUtil
{
public:
  template <typename... Args> using WriteFormat = deskflow::protocol::Format<false, std::type_identity_t<Args>...>;
  template <typename... Args> using ReadFormat = deskflow::protocol::Format<true, std::type_identity_t<Args>...>;

  //! Write formatted data
  /*!
  Write formatted binary data to a stream as one write.  \c fmt consists of
  regular characters and format specifiers.  Format specifiers
  begin with \%.  All characters not part of a format specifier
  are regular and are transmitted unchanged.  The arguments are checked
  against \c fmt at compile time.

  Format specifiers are:
  - \%1i  -- integer, as a 1 byte integer
  - \%2i  -- integer, as a 2 byte integer in NBO
  - \%4i  -- integer, as a 4 byte integer in NBO
  - \%1I  -- std::vector<uint8_t>, as a 4 byte count then 1 byte integers
  - \%2I  -- std::vector<uint16_t>, as a 4 byte count then 2 byte integers in NBO
  - \%4I  -- std::vector<uint32_t>, as a 4 byte count then 4 byte integers in NBO
  - \%s   -- std::string, as a 4 byte length then its bytes
  - \%Ns  -- std::string of exactly N bytes, as its bytes; throws std::invalid_argument otherwise
  */
  template <typename... Args>
  static void writef(deskflow::IStream *stream, WriteFormat<Args...> fmt, const Args &...args)
  {
    const std::string_view format = fmt.c_str();
    std::vector<uint8_t> buffer;
    size_t pos = 0;
    (writeArg(buffer, format, pos, args), ...);
    const auto tail = deskflow::protocol::nextSpec(format, pos).literal;
    buffer.insert(buffer.end(), tail.begin(), tail.end());
    write(stream, fmt.c_str(), buffer);
  }

  //! Read formatted data
  /*!
  Read formatted binary data from a stream.  This performs the
  reverse operation of writef(), taking a pointer to each value.
  Integers must be exactly as wide as their specifier.  Vectors are
  appended to.  Returns true if the entire format was successfully
  parsed, false otherwise.  Lengths over the protocol limits throw
  BadClientException.
  */
  template <typename... Args>
  [[nodiscard]] static bool readf(deskflow::IStream *stream, ReadFormat<Args...> fmt, Args... args)
  {
    if (stream == nullptr) {
      return false;
    }
    try {
      const std::string_view format = fmt.c_str();
      size_t pos = 0;
      (readArg(stream, format, pos, args), ...);
      readLiteral(stream, deskflow::protocol::nextSpec(format, pos).literal);
      return true;
    } catch (IOException &) {
      return false;
    } catch (const std::bad_alloc &) {
      return false;
    }
  }

private:
  template <typename T>
  static void writeArg(std::vector<uint8_t> &buffer, std::string_view fmt, size_t &pos, const T &value)
  {
    const auto spec = deskflow::protocol::nextSpec(fmt, pos);
    buffer.insert(buffer.end(), spec.literal.begin(), spec.literal.end());
    if constexpr (std::integral<T>) {
      writeInt(buffer, spec.arg, static_cast<uint32_t>(value));
    } else {
      writeValue(buffer, spec, value);
    }
  }

  template <typename T> static void readArg(deskflow::IStream *stream, std::string_view fmt, size_t &pos, T *value)
  {
    const auto spec = deskflow::protocol::nextSpec(fmt, pos);
    readLiteral(stream, spec.literal);
    if constexpr (std::integral<T>) {
      *value = static_cast<T>(readInt(stream, sizeof(T)));
    } else {
      readValue(stream, spec, *value);
    }
  }

  static void write(deskflow::IStream *stream, const char *fmt, const std::vector<uint8_t> &buffer);
  static void writeInt(std::vector<uint8_t> &buffer, deskflow::protocol::FormatArg arg, uint32_t value);
  static void writeValue(std::vector<uint8_t> &, const deskflow::protocol::FormatSpec &, const std::string &);
  static void writeValue(std::vector<uint8_t> &, const deskflow::protocol::FormatSpec &, const std::vector<uint8_t> &);
  static void writeValue(std::vector<uint8_t> &, const deskflow::protocol::FormatSpec &, const std::vector<uint16_t> &);
  static void writeValue(std::vector<uint8_t> &, const deskflow::protocol::FormatSpec &, const std::vector<uint32_t> &);

  static void read(deskflow::IStream *, void *, uint32_t);
  static void readLiteral(deskflow::IStream *, std::string_view literal);
  static uint32_t readInt(deskflow::IStream *, size_t size);
  static uint32_t readVectorSize(deskflow::IStream *stream);
  static void readValue(deskflow::IStream *, const deskflow::protocol::FormatSpec &, std::string &);
  static void readValue(deskflow::IStream *, const deskflow::protocol::FormatSpec &, std::vector<uint8_t> &);
  static void readValue(deskflow::IStream *, const deskflow::protocol::FormatSpec &, std::vector<uint16_t> &);
  static void readValue(deskflow::IStream *, const deskflow::protocol::FormatSpec &, std::vector<uint32_t> &);
};

//! Mismatched read exception
/*!
Thrown by ProtocolUtil::readf() when the data being read does not
match the format.
*/
class XIOReadMismatch : public IOException
{
public:
  // BaseException overrides
  QString getWhat() const throw() override;
};
