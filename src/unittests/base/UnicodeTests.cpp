/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2014 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "UnicodeTests.h"

#include "base/Unicode.h"

#include <cstring>
#include <vector>

namespace {
std::vector<uint16_t> toUCS2(const std::string &utf8)
{
  const auto bytes = Unicode::UTF8ToUCS2(utf8, nullptr);
  std::vector<uint16_t> chars(bytes.size() / 2);
  std::memcpy(chars.data(), bytes.data(), bytes.size());
  return chars;
}
} // namespace

void UnicodeTests::initTestCase()
{
  m_log.setFilter(LogLevel::Level::Verbose);
}

void UnicodeTests::UTF16ToUTF8()
{
  bool errors;
  auto result = Unicode::UTF16ToUTF8(std::string("h\0e\0l\0l\0o\0", 10), &errors);

  QVERIFY(!errors);
  QCOMPARE(result.c_str(), "hello");
}

void UnicodeTests::byteOrderMark()
{
  bool errors = true;
  QCOMPARE(Unicode::UTF16ToUTF8(std::string("\xff\xfeh\0i\0", 6), &errors).c_str(), "hi");
  QVERIFY(!errors);
  QCOMPARE(Unicode::UTF16ToUTF8(std::string("\xfe\xff\0h\0i", 6), &errors).c_str(), "hi");
  QVERIFY(!errors);
  QCOMPARE(Unicode::UCS2ToUTF8(std::string("\xff\xfeh\0i\0", 6), &errors).c_str(), "hi");
  QCOMPARE(Unicode::UCS2ToUTF8(std::string("\xfe\xff\0h\0i", 6), &errors).c_str(), "hi");
  QCOMPARE(Unicode::UCS2ToUTF8(std::string(), &errors).c_str(), "");
}

void UnicodeTests::UTF8ToUCS2_malformedInput()
{
  using Chars = std::vector<uint16_t>;
  const uint16_t bad = 0xfffd;

  QCOMPARE(toUCS2("a\xc3\xa9\xe2\x82\xac"), (Chars{'a', 0xe9, 0x20ac}));
  QVERIFY(Unicode::isUTF8("\xc3\xa9"));

  // lone continuation byte and bytes that can never start a sequence
  QCOMPARE(toUCS2("\x80z"), (Chars{bad, 'z'}));
  QCOMPARE(toUCS2("\xfe\xffz"), (Chars{bad, bad, 'z'}));
  // a sequence cut short by a non-continuation byte consumes only its valid prefix
  QCOMPARE(toUCS2("\xe2\x82z"), (Chars{bad, 'z'}));
  QCOMPARE(toUCS2("\xf0\x90zzz"), (Chars{bad, 'z', 'z', 'z'}));
  // a sequence cut short by the end of input consumes the rest
  QCOMPARE(toUCS2("z\xe2\x82"), (Chars{'z', bad}));
  // overlong encodings, surrogates and non-characters
  QCOMPARE(toUCS2("\xc0\x80z"), (Chars{bad, 'z'}));
  QCOMPARE(toUCS2("\xe0\x80\xafz"), (Chars{bad, 'z'}));
  QCOMPARE(toUCS2("\xed\xa0\x80z"), (Chars{bad, 'z'}));
  QCOMPARE(toUCS2("\xef\xbf\xbez"), (Chars{bad, 'z'}));
  QVERIFY(!Unicode::isUTF8("\xc0\x80"));
}

QTEST_MAIN(UnicodeTests)
