/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ServerAppTests.h"

#include "../TestSettings.h"
#include "ServerFakes.h"
#include "common/ExitCodes.h"
#include "deskflow/Screen.h"
#include "deskflow/ScreenException.h"
#include "deskflow/ServerApp.h"
#include "net/SocketMultiplexer.h"

#include <QFile>

#include <limits>
#include <vector>

namespace {

// A server whose screen is unavailable for the first few tries, as when the display is not back yet after a resume
class TestServerApp : public ServerApp
{
public:
  using ServerApp::ServerApp;

  deskflow::Screen *createScreen() override
  {
    if (m_screenUnavailableFor > 0) {
      --m_screenUnavailableFor;
      throw ScreenUnavailableException();
    }
    return new deskflow::Screen(new FakePlatformScreen(getEvents()), getEvents());
  }

  int m_screenUnavailableFor = 0;
};

std::vector<int> quitCodes(const FakeEventQueue &events)
{
  std::vector<int> codes;
  for (const auto &event : events.m_posted) {
    if (event.getType() == EventTypes::Quit) {
      const auto *exit = dynamic_cast<const ExitEventData *>(event.getDataObject());
      codes.push_back(exit != nullptr ? exit->exitCode() : s_exitSuccess);
    }
  }
  return codes;
}

// fires pending timers, like the event loop would, until none are left
void runTimers(FakeEventQueue &events)
{
  for (int i = 0; i < 100 && !events.m_liveTimers.empty(); ++i) {
    events.send(EventTypes::Timer, *events.m_liveTimers.begin());
  }
}

} // namespace

void ServerAppTests::initTestCase()
{
  QVERIFY(m_settingsDir.isValid());
  useTestSettings(m_settingsDir);
  m_arch.init();

  // a server that can listen on a free local port
  Settings::setValue(Settings::Core::ComputerName, QStringLiteral("server"));
  Settings::setValue(Settings::Core::Interface, QStringLiteral("127.0.0.1"));
  Settings::setValue(Settings::Core::Port, 0);
  Settings::setValue(QStringLiteral("screen_0/name"), QStringLiteral("server"));
  m_configPath = m_settingsDir.filePath(QStringLiteral("server.conf"));
  QFile configFile(m_configPath);
  QVERIFY(configFile.open(QIODevice::WriteOnly));
  configFile.write("section: options\nend\n");
}

void ServerAppTests::resume_failedRestart_retriesThenQuitsWithError()
{
  FakeEventQueue events;
  TestServerApp app(&events);
  app.m_screenUnavailableFor = std::numeric_limits<int>::max();

  app.runInner([&app, &events] {
    app.handleSuspend();
    app.handleResume();
    runTimers(events);
    return s_exitSuccess;
  });

  QCOMPARE(events.m_timers.size(), 5);
  QCOMPARE(quitCodes(events), std::vector<int>{s_exitFailed});
}

void ServerAppTests::resume_screenBackOnRetry_startsServer()
{
  FakeEventQueue events;
  TestServerApp app(&events);
  app.m_screenUnavailableFor = 2;
  bool started = false;

  app.runInner([this, &app, &events, &started] {
    app.setSocketMultiplexer(std::make_unique<SocketMultiplexer>());
    app.loadConfig(m_configPath);
    app.handleSuspend();
    app.handleResume();
    runTimers(events);
    // already started, so this does no work
    started = app.startServer() && events.m_timers.size() == 2;
    app.cleanupServer();
    return s_exitSuccess;
  });

  QVERIFY(started);
  QVERIFY(quitCodes(events).empty());
}

void ServerAppTests::destroy_whileStarted_configOutlivesServer()
{
  FakeEventQueue events;
  bool started = false;
  {
    TestServerApp app(&events);
    app.runInner([this, &app, &started] {
      app.setSocketMultiplexer(std::make_unique<SocketMultiplexer>());
      started = app.loadConfig(m_configPath) && app.initServer() && app.startServer();
      return s_exitSuccess;
    });
    // no cleanupServer(), as when an exception escapes the event loop; ASan flags a config freed before the server
  }
  QVERIFY(started);
}

QTEST_MAIN(ServerAppTests)
