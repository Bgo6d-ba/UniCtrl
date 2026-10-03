/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2014 - 2021 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "StringTests.h"

#include "base/String.h"

void StringTests::formatWithArgs()
{
  const char *format = "%%%{1}=%{2}";
  const char *arg1 = "answer";
  const char *arg2 = "42";

  std::string result = deskflow::string::format(format, arg1, arg2);

  QCOMPARE(result, "%answer=42");
}

void StringTests::formatedString()
{
  const char *format = "%s=%d";
  const char *arg1 = "answer";
  int arg2 = 42;

  std::string result = deskflow::string::sprintf(format, arg1, arg2);

  QCOMPARE("answer=42", result);
}

void StringTests::sprintf_resultFillsStackBuffer_notTruncated()
{
  for (const std::size_t size : {1023, 1024, 1025, 5000}) {
    const std::string expected(size, 'x');

    QCOMPARE(deskflow::string::sprintf("%s", expected.c_str()), expected);
  }
}

QTEST_MAIN(StringTests)
