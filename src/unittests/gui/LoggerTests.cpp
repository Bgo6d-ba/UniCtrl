/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2024 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "LoggerTests.h"

#include "../TestSettings.h"
#include "common/Settings.h"

#include "gui/Logger.h"

#include <QSignalSpy>

using namespace deskflow::gui;

void LoggerTests::initTestCase()
{
  QVERIFY(m_dir.isValid());
  useTestSettings(m_dir);
}

void LoggerTests::newLine()
{
  QSignalSpy spy(Logger::instance(), &Logger::newLine);
  QVERIFY(spy.isValid());

  Settings::setValue(Settings::Log::GuiDebug, true);
  Logger::instance()->handleMessage(QtDebugMsg, "stub", "test");

  QCOMPARE(spy.count(), 1);
  QVERIFY(qvariant_cast<QString>(spy.takeFirst().at(0)).contains("test"));
  Settings::setValue(Settings::Log::GuiDebug, false);
}

void LoggerTests::noNewLine()
{
  bool newLineEmitted = false;

  QSignalSpy spy(Logger::instance(), &Logger::newLine);
  QVERIFY(spy.isValid());

  Settings::setValue(Settings::Log::GuiDebug, false);
  Logger::instance()->handleMessage(QtDebugMsg, "stub", "test");
  QCOMPARE(spy.count(), 0);
  QVERIFY(!newLineEmitted);
}

QTEST_MAIN(LoggerTests)
