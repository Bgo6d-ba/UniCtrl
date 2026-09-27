/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ScreenCommandTests.h"

#include "../TestSettings.h"

#include "common/Settings.h"
#include "deskflow/App.h"

#include <QFile>

void ScreenCommandTests::initTestCase()
{
#ifdef Q_OS_WIN
  QSKIP("the marker command relies on a POSIX touch");
#endif
  QVERIFY(m_dir.isValid());
  useTestSettings(m_dir);
}

QString ScreenCommandTests::touchCommand(const QString &marker) const
{
  return QStringLiteral("touch \"%1\"").arg(m_dir.filePath(marker));
}

void ScreenCommandTests::disabledCommand_doesNotRun()
{
  Settings::setValue(Settings::Core::EnableEnterCommand, false);
  Settings::setValue(Settings::Core::ComputerEnterCommand, touchCommand(QStringLiteral("disabled")));

  App::runScreenCommand(true);

  QTest::qWait(500);
  QVERIFY(!QFile::exists(m_dir.filePath(QStringLiteral("disabled"))));
}

void ScreenCommandTests::enterCommand_runsOnEnterOnly()
{
  Settings::setValue(Settings::Core::EnableEnterCommand, true);
  Settings::setValue(Settings::Core::ComputerEnterCommand, touchCommand(QStringLiteral("entered")));

  App::runScreenCommand(false);
  QTest::qWait(500);
  QVERIFY(!QFile::exists(m_dir.filePath(QStringLiteral("entered"))));

  App::runScreenCommand(true);
  QTRY_VERIFY(QFile::exists(m_dir.filePath(QStringLiteral("entered"))));
}

void ScreenCommandTests::exitCommand_runsOnExitOnly()
{
  Settings::setValue(Settings::Core::EnableEnterCommand, false);
  Settings::setValue(Settings::Core::EnableExitCommand, true);
  Settings::setValue(Settings::Core::ComputerExitCommand, touchCommand(QStringLiteral("left")));

  App::runScreenCommand(true);
  QTest::qWait(500);
  QVERIFY(!QFile::exists(m_dir.filePath(QStringLiteral("left"))));

  App::runScreenCommand(false);
  QTRY_VERIFY(QFile::exists(m_dir.filePath(QStringLiteral("left"))));
}

QTEST_MAIN(ScreenCommandTests)
