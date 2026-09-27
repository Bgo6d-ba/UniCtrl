/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "SettingsTests.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QThread>
#include <QUuid>

#include <atomic>

void SettingsTests::initTestCase()
{
  QVERIFY(m_dir.isValid());
  // Settings reads these when first constructed; keep it off the user's real files
  qputenv("XDG_CONFIG_HOME", m_dir.path().toLocal8Bit());
  qputenv("XDG_STATE_HOME", m_dir.path().toLocal8Bit());
}

void SettingsTests::setSettingsFile()
{
  Settings::setSettingsFile(m_settingsFile);
}

void SettingsTests::setStateFile()
{
  Settings::setStateFile(m_stateFile);
}

void SettingsTests::settingsFile()
{
  QVERIFY(Settings::settingsFile().endsWith(m_settingsFile));
}

void SettingsTests::settingsPath()
{
  QVERIFY(Settings::settingsPath().endsWith(m_settingsPath));
}

void SettingsTests::tlsDir()
{
  QVERIFY(Settings::tlsDir().endsWith(m_expectedTlsDir));
}

void SettingsTests::tlsTrustedServersDb()
{
  QVERIFY(Settings::tlsTrustedServersDb().endsWith(m_expectedTlsServerDB));
}

void SettingsTests::tlsTrustedClientsDb()
{
  QVERIFY(Settings::tlsTrustedClientsDb().endsWith(m_expectedTlsClientDB));
}

void SettingsTests::checkValidSettings()
{
  QSignalSpy spy(Settings::instance(), &Settings::settingsChanged);
  QVERIFY(spy.isValid());

  const auto keysToCheck = QRegularExpression(QLatin1String("[^%1]").arg(Settings::Core::ComputerName));
  const auto validKeys = Settings::validKeys().filter(keysToCheck);
  for (const auto &setting : validKeys) {
    const auto value = Settings::value(setting).toString();
    QCOMPARE(Settings::defaultValue(setting).toString(), value);

    Settings::setValue(setting, "NEW_VALUE");
    QCOMPARE(spy.count(), 1);
    QCOMPARE(qvariant_cast<QString>(spy.first().at(0)), setting);
    QCOMPARE(Settings::value(setting).toString(), "NEW_VALUE");

    Settings::setValue(setting);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(Settings::value(setting).toString(), value);

    // Reset the spy for the next loop
    spy.clear();
    QCOMPARE(spy.count(), 0);
  }
}

void SettingsTests::checkCleanScreenName()
{
  const auto input = QStringLiteral("--!_ _-S@c#r$e%e^&*(n)= +Name\n[1]2|3?4--5>6<,7`~/8*90\\.lan--..    ..");
  const auto expected = QStringLiteral("Screen_Name_1234--567890.lan");

  Settings::setValue(Settings::Core::ComputerName, input);

  QCOMPARE(Settings::value(Settings::Core::ComputerName).toString(), expected);
}

void SettingsTests::checkCleanScreenName_LongName()
{
  QString input;
  input.fill('f', 300);
  input.prepend('.');

  QString expected;
  expected.fill('f', 255);

  Settings::setValue(Settings::Core::ComputerName, input);

  QCOMPARE(Settings::value(Settings::Core::ComputerName).toString(), expected);
}

void SettingsTests::checkDisplayName()
{
  // Falls back to the computer name, which may be longer than a display name
  QCOMPARE(Settings::displayName(), QString(Settings::MaxDisplayNameLength, 'f'));

  Settings::setValue(Settings::Core::DisplayName, QStringLiteral("  Bat\tmunkh's\n\x01 Mac  "));
  QCOMPARE(Settings::value(Settings::Core::DisplayName).toString(), QStringLiteral("Bat munkh's Mac"));
  QCOMPARE(Settings::displayName(), QStringLiteral("Bat munkh's Mac"));

  // Never cuts a character in half
  const QString name(Settings::MaxDisplayNameLength - 1, 'x');
  Settings::setValue(Settings::Core::DisplayName, name + QStringLiteral("\U0001F600"));
  QCOMPARE(Settings::displayName(), name);

  Settings::setValue(Settings::Core::ComputerName, QStringLiteral("office-pc"));
  Settings::setValue(Settings::Core::DisplayName, QStringLiteral(" \t "));
  QCOMPARE(Settings::value(Settings::Core::DisplayName).toString(), QStringLiteral("office-pc"));
  QCOMPARE(Settings::displayName(), QStringLiteral("office-pc"));
}

void SettingsTests::checkHostId()
{
  const auto id = Settings::hostId();
  QVERIFY(!QUuid::fromString(id).isNull());
  QVERIFY(!id.startsWith('{'));
  QCOMPARE(Settings::hostId(), id);
  QCOMPARE(QSettings(m_settingsFile, QSettings::IniFormat).value(Settings::Lan::HostId).toString(), id);

  Settings::setValue(Settings::Lan::HostId, QStringLiteral("{%1}").arg(id));
  QCOMPARE(Settings::hostId(), id);

  Settings::setValue(Settings::Lan::HostId, QStringLiteral("not-an-id"));
  QVERIFY(!QUuid::fromString(Settings::hostId()).isNull());
}

void SettingsTests::checkHostingEnabled()
{
  // Nothing is hosted until the user hosts, and a fresh settings file has no say in it
  QVERIFY(Settings::validKeys().contains(Settings::Lan::HostingEnabled));
  QVERIFY(!Settings::value(Settings::Lan::HostingEnabled).toBool());
  QVERIFY(!Settings::defaultValue(Settings::Lan::HostingEnabled).toBool());
  QVERIFY(Settings::defaultValue(Settings::Lan::HostingEnabled).isValid());

  Settings::setValue(Settings::Lan::HostingEnabled, true);
  QVERIFY(Settings::value(Settings::Lan::HostingEnabled).toBool());
  QCOMPARE(
      QSettings(m_settingsFile, QSettings::IniFormat).value(Settings::Lan::HostingEnabled).toString(),
      QStringLiteral("true")
  );

  // Cleared, it is not left in the file
  Settings::setValue(Settings::Lan::HostingEnabled);
  QVERIFY(!Settings::value(Settings::Lan::HostingEnabled).toBool());
  QVERIFY(!QSettings(m_settingsFile, QSettings::IniFormat).contains(Settings::Lan::HostingEnabled));
}

void SettingsTests::checkLanSettingsKept()
{
  const QVariantList hosts = {
      QVariantMap{{"id", "a"}, {"name", "Office, PC"}, {"fingerprint", QString(64, 'a')}},
      QVariantMap{{"id", "b"}, {"name", "Laptop"}, {"fingerprint", QString(64, 'b')}},
  };
  const QVariantList clients = {
      QVariantMap{{"screen", "office-pc"}, {"name", "Office, PC"}},
      QVariantMap{{"screen", "laptop"}, {"name", "Laptop"}},
  };
  const auto otherFile = QStringLiteral("%1/Reload.conf").arg(m_settingsPathTemp);

  for (const auto &paired : {hosts.mid(0, 1), hosts}) {
    Settings::setValue(Settings::Lan::PairedHosts, paired);
    Settings::setValue(Settings::Lan::ClientNames, clients);
    Settings::setValue(Settings::Lan::Password, QStringLiteral("pass, word"));
    // Text, as a value is when it is read from a file: a bool that was set in this process is not one to keep
    Settings::setValue(Settings::Lan::HostingEnabled, QStringLiteral("true"));

    // The server config dialog removes unknown groups, loading a file removes unknown keys
    Settings::removeUnknownScreens({});
    Settings::setSettingsFile(otherFile);
    Settings::setSettingsFile(m_settingsFile);

    QCOMPARE(Settings::value(Settings::Lan::PairedHosts).toList(), paired);
    QCOMPARE(Settings::value(Settings::Lan::ClientNames).toList(), clients);
    QCOMPARE(Settings::value(Settings::Lan::Password).toString(), QStringLiteral("pass, word"));
    QVERIFY(Settings::value(Settings::Lan::HostingEnabled).toBool());
  }
  Settings::setValue(Settings::Lan::HostingEnabled);
}

void SettingsTests::checkClearState()
{
  Settings::setValue(Settings::Gui::WindowGeometry, QRect(1, 2, 3, 4));
  QVERIFY(Settings::value(Settings::Gui::WindowGeometry).toRect().isValid());

  Settings::clearState();

  QVERIFY(!Settings::value(Settings::Gui::WindowGeometry).toRect().isValid());
}

void SettingsTests::concurrentAccess()
{
  const auto otherFile = QStringLiteral("%1/Other.conf").arg(m_settingsPathTemp);
  std::atomic_bool stop = false;

  QList<QThread *> threads;
  for (const auto &key : {Settings::Core::ComputerEnterCommand, Settings::Core::ComputerExitCommand}) {
    threads.append(QThread::create([&stop, key] {
      for (int i = 0; !stop; ++i) {
        Settings::setValue(key, QString::number(i));
        std::ignore = Settings::value(key);
        std::ignore = Settings::tlsDir();
      }
    }));
    threads.last()->start();
  }

  // Swapping the file replaces the QSettings instance the workers are using.
  for (int i = 0; i < 200; ++i) {
    Settings::setSettingsFile(i % 2 ? otherFile : m_settingsFile);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }

  stop = true;
  for (auto *thread : std::as_const(threads)) {
    QVERIFY(thread->wait(10000));
    delete thread;
  }

  Settings::setSettingsFile(m_settingsFile);
  QVERIFY(Settings::settingsFile().endsWith(m_settingsFile));
}

QTEST_MAIN(SettingsTests)
