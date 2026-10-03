/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2024 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "LogTests.h"
#include <clocale>
#include <iostream>
#include <sstream>

#define LEVEL_PRINT LogLevel::Level::Print,
#define LEVEL_ERR LogLevel::Level::Error,
#define LEVEL_INFO LogLevel::Level::Info,

QString sanitizeBuffer(const std::stringstream &in)
{
  static QRegularExpression timestampRegex("\\[\\S+\\] ");
  QString rtn = QString::fromStdString(in.str()).simplified();
  rtn.remove(timestampRegex);
  return rtn;
}

void LogTests::initTestCase()
{
  std::setlocale(LC_NUMERIC, "C"); // NOLINT(concurrency-mt-unsafe,cert-err33-c): single-threaded test setup
  m_log.setFilter(LogLevel::Level::Debug);
}

void LogTests::printWithErrorValidOutput()
{
  std::stringstream buffer;
  std::streambuf *old = std::cerr.rdbuf(buffer.rdbuf());

  m_log.print(nullptr, 0, LEVEL_ERR "test message");

  auto string = sanitizeBuffer(buffer);
  std::cerr.rdbuf(old);

  QCOMPARE(string, "ERROR: test message");
}

void LogTests::printTestPrintLevel()
{
  std::stringstream buffer;
  std::streambuf *old = std::cout.rdbuf(buffer.rdbuf());

  m_log.print(nullptr, 0, LEVEL_PRINT "test message");

  auto string = sanitizeBuffer(buffer);
  std::cout.rdbuf(old);

  QCOMPARE(string, "test message");
}

void LogTests::printTestWithArgs()
{
  std::stringstream buffer;
  std::streambuf *old = std::cout.rdbuf(buffer.rdbuf());

  m_log.print(nullptr, 0, LEVEL_INFO "test %s", "IamARG");

  auto string = sanitizeBuffer(buffer);
  std::cout.rdbuf(old);

  QCOMPARE(string, "INFO: test IamARG");
}

void LogTests::printTestLogString()
{
  std::stringstream buffer;
  std::streambuf *old = std::cout.rdbuf(buffer.rdbuf());

  auto longString = QString(10000, 'a');
  m_log.print(nullptr, 0, LEVEL_INFO "%s", qPrintable(longString));

  auto string = sanitizeBuffer(buffer);
  std::cout.rdbuf(old);

  QCOMPARE(string, QString("INFO: %1").arg(longString));
}

void LogTests::printLevelToHigh()
{
  std::stringstream buffer;
  std::streambuf *old = std::cout.rdbuf(buffer.rdbuf());

  m_log.print(CLOG_VERBOSE "test message");

  auto string = sanitizeBuffer(buffer);
  std::cout.rdbuf(old);

  QCOMPARE(string, QString{});
}

void LogTests::printInfoWithFileAndLine()
{
  std::stringstream buffer;
  std::streambuf *old = std::cout.rdbuf(buffer.rdbuf());

  m_log.print("test file", 123, LEVEL_INFO "test message");

  auto string = sanitizeBuffer(buffer);
  std::cout.rdbuf(old);

  QCOMPARE(string, "INFO: test message test file:123");
}

void LogTests::printErrWithFileAndLine()
{
  std::stringstream buffer;
  std::streambuf *old = std::cerr.rdbuf(buffer.rdbuf());

  m_log.print("test file", 123, LEVEL_ERR "test message");

  auto string = sanitizeBuffer(buffer);
  std::cerr.rdbuf(old);

  QCOMPARE(string, "ERROR: test message test file:123");
}

void LogTests::printMessageOfExactlyTheBufferSize()
{
  std::stringstream buffer;
  std::streambuf *old = std::cout.rdbuf(buffer.rdbuf());

  // the first attempt formats into 1024 bytes, which cannot also hold the terminator
  const auto exact = std::string(1024, 'b');
  m_log.print(nullptr, 0, LEVEL_PRINT "%s", exact.c_str());

  std::cout.rdbuf(old);
  QCOMPARE(sanitizeBuffer(buffer), QString::fromStdString(exact));
}

void LogTests::printEncodingErrorLogsTheFormat()
{
  std::stringstream buffer;
  std::streambuf *old = std::cout.rdbuf(buffer.rdbuf());

  // the C locale cannot convert a CJK wide character, so vsnprintf returns -1
  const std::string previous = std::setlocale(LC_CTYPE, nullptr); // NOLINT(concurrency-mt-unsafe)
  std::setlocale(LC_CTYPE, "C");                                  // NOLINT(concurrency-mt-unsafe,cert-err33-c)
  m_log.print(nullptr, 0, LEVEL_PRINT "name: %ls", L"\u4e2d");
  std::setlocale(LC_CTYPE, previous.c_str()); // NOLINT(concurrency-mt-unsafe,cert-err33-c)

  std::cout.rdbuf(old);
  QCOMPARE(sanitizeBuffer(buffer), "name: %ls");
}

QTEST_MAIN(LogTests)
