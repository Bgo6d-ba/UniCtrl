/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2014 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ServerTests.h"

#include "../TestSettings.h"
#include "ServerFakes.h"
#include "common/Settings.h"
#include "common/StatusSink.h"
#include "deskflow/AppUtil.h"
#include "deskflow/IPlatformScreen.h"
#include "deskflow/OptionTypes.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/Screen.h"
#include "io/IStream.h"
#include "server/ClientProxy.h"
#include "server/PrimaryClient.h"
#include "server/Server.h"

#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

using deskflow::server::Config;
using enum Direction;

namespace {

class FakeStream : public deskflow::IStream
{
public:
  void close() override
  {
  }
  uint32_t read(void *, uint32_t) override
  {
    return 0;
  }
  void write(const void *buffer, uint32_t n) override
  {
    m_written.append(static_cast<const char *>(buffer), n);
  }
  void flush() override
  {
  }
  void shutdownInput() override
  {
  }
  void shutdownOutput() override
  {
  }
  void *getEventTarget() const override
  {
    return const_cast<FakeStream *>(this);
  }
  bool isReady() const override
  {
    return true;
  }
  uint32_t getSize() const override
  {
    return 0;
  }

  std::string m_written;
};

class FakeClient : public ClientProxy
{
public:
  explicit FakeClient(const std::string &name, bool *destroyed = nullptr)
      : ClientProxy(name, new FakeStream),
        m_destroyed(destroyed)
  {
  }
  ~FakeClient() override
  {
    if (m_destroyed != nullptr) {
      *m_destroyed = true;
    }
  }

  std::string written() const
  {
    return static_cast<FakeStream *>(getStream())->m_written;
  }

  bool getClipboard(ClipboardID, IClipboard *clipboard) const override
  {
    clipboard->open(0);
    clipboard->empty();
    clipboard->add(IClipboard::Format::Text, m_clipboardText);
    clipboard->close();
    return true;
  }
  void getShape(int32_t &x, int32_t &y, int32_t &w, int32_t &h) const override
  {
    x = 0;
    y = 0;
    w = 1000;
    h = 800;
  }
  void getCursorPos(int32_t &x, int32_t &y) const override
  {
    x = m_cursor.first;
    y = m_cursor.second;
  }
  void enter(int32_t x, int32_t y, uint32_t, KeyModifierMask, bool) override
  {
    m_enters.emplace_back(x, y);
  }
  bool leave() override
  {
    ++m_leaves;
    return true;
  }
  void setClipboard(ClipboardID, const IClipboard *clipboard) override
  {
    m_clipboards.push_back(clipboardText(clipboard));
  }
  void grabClipboard(ClipboardID id) override
  {
    m_grabs.push_back(id);
  }
  void setClipboardDirty(ClipboardID id, bool dirty) override
  {
    m_dirty.emplace_back(id, dirty);
  }
  void keyDown(KeyID id, KeyModifierMask, KeyButton, const std::string &) override
  {
    m_keyDowns.push_back(id);
  }
  void keyRepeat(KeyID id, KeyModifierMask, int32_t, KeyButton, const std::string &) override
  {
    m_keyRepeats.push_back(id);
  }
  void keyUp(KeyID id, KeyModifierMask, KeyButton) override
  {
    m_keyUps.push_back(id);
  }
  void mouseDown(ButtonID id) override
  {
    m_buttons.push_back(id);
  }
  void mouseUp(ButtonID id) override
  {
    m_buttonUps.push_back(id);
  }
  void mouseMove(int32_t x, int32_t y) override
  {
    m_moves.emplace_back(x, y);
  }
  void mouseRelativeMove(int32_t dx, int32_t dy) override
  {
    m_relativeMoves.emplace_back(dx, dy);
  }
  void mouseWheel(int32_t xDelta, int32_t yDelta) override
  {
    m_wheels.emplace_back(xDelta, yDelta);
  }
  void screensaver(bool activate) override
  {
    m_screensaver.push_back(activate);
  }
  void resetOptions() override
  {
    ++m_optionResets;
  }
  void setOptions(const OptionsList &) override
  {
  }
  void sendDragInfo(uint32_t, const char *, size_t) override
  {
  }
  void fileChunkSending(uint8_t, char *, size_t) override
  {
  }
  std::string getSecureInputApp() const override
  {
    return {};
  }
  void secureInputNotification(const std::string &) const override
  {
  }

  Point m_cursor{500, 400};
  std::string m_clipboardText;
  std::vector<Point> m_enters;
  int m_leaves = 0;
  std::vector<std::string> m_clipboards;
  std::vector<ClipboardID> m_grabs;
  std::vector<std::pair<ClipboardID, bool>> m_dirty;
  std::vector<KeyID> m_keyDowns;
  std::vector<KeyID> m_keyRepeats;
  std::vector<KeyID> m_keyUps;
  std::vector<ButtonID> m_buttons;
  std::vector<ButtonID> m_buttonUps;
  std::vector<Point> m_moves;
  std::vector<Point> m_relativeMoves;
  std::vector<Point> m_wheels;
  std::vector<bool> m_screensaver;
  int m_optionResets = 0;

private:
  bool *m_destroyed;
};

// A real Screen and PrimaryClient over a fake platform screen, so the server
// runs its production code paths against recorded side effects.
struct Harness
{
  explicit Harness(
      const std::vector<std::string> &screens = {"left", "right", "up", "down"},
      deskflow::server::ServerOptions options = {}
  )
      : config(&events, std::move(options))
  {
    config.addScreen("server");
    for (const auto &name : screens) {
      config.addScreen(name);
    }
  }

  void link(const std::string &src, Direction dir, const std::string &dst)
  {
    QVERIFY(config.connect(src, dir, 0.0f, 1.0f, dst, 0.0f, 1.0f));
  }

  void linkAround()
  {
    link("server", Left, "left");
    link("left", Right, "server");
    link("server", Right, "right");
    link("server", Top, "up");
    link("server", Bottom, "down");
  }

  void start()
  {
    server = std::make_unique<Server>(config, &primary, &screen, &events);
    events.flush();
  }

  FakeClient *connect(const std::string &name, bool *destroyed = nullptr)
  {
    auto *client = new FakeClient(name, destroyed);
    server->adoptClient(client);
    return client;
  }

  bool movePrimary(int32_t x, int32_t y)
  {
    return events.send(EventTypes::PrimaryScreenMotionOnPrimary, platform, IPrimaryScreen::MotionInfo::alloc(x, y));
  }

  void moveSecondary(int32_t dx, int32_t dy)
  {
    events.send(EventTypes::PrimaryScreenMotionOnSecondary, platform, IPrimaryScreen::MotionInfo::alloc(dx, dy));
  }

  void sendToFilter(EventTypes type, EventData *data)
  {
    events.send(type, config.getInputFilter(), data);
  }

  int switches() const
  {
    return events.posted(EventTypes::ServerScreenSwitched);
  }

  FakeEventQueue events;
  Config config;
  FakePlatformScreen *platform = new FakePlatformScreen(&events);
  deskflow::Screen screen{platform, &events};
  PrimaryClient primary{"server", &screen};
  std::unique_ptr<Server> server;
};

void *targetOf(BaseClientProxy *client)
{
  return client;
}

const auto kMask = [](DirectionMask mask) { return static_cast<uint32_t>(mask); };

class TestAppUtil : public AppUtil
{
public:
  int run() override
  {
    return 0;
  }
  std::vector<std::string> getKeyboardLayoutList() override
  {
    return {"en"};
  }
  std::string getCurrentLanguageCode() override
  {
    return "en";
  }
};

class FakeStatusSink : public deskflow::IStatusSink
{
public:
  FakeStatusSink()
  {
    deskflow::setStatusSink(this);
  }
  ~FakeStatusSink() override
  {
    deskflow::setStatusSink(nullptr);
  }
  FakeStatusSink(const FakeStatusSink &) = delete;
  FakeStatusSink &operator=(const FakeStatusSink &) = delete;

  void sendStatus(const QString &command, const QString &args) override
  {
    m_messages.append(command + "=" + args);
  }

  QStringList take()
  {
    return std::exchange(m_messages, {});
  }

private:
  QStringList m_messages;
};

} // namespace

void ServerTests::initTestCase()
{
  QVERIFY(m_settingsDir.isValid());
  useTestSettings(m_settingsDir);
  qunsetenv("DESKFLOW_MOUSE_ADJUSTMENT");
  m_appUtil = std::make_unique<TestAppUtil>();
}

void ServerTests::SwitchToScreenInfo_alloc_screen()
{
  auto actual = new Server::SwitchToScreenInfo("test");
  QCOMPARE(actual->m_screen, "test");
  delete actual;
}

void ServerTests::KeyboardBroadcastInfo_alloc_stateAndSceens()
{
  auto info = new Server::KeyboardBroadcastInfo(Server::KeyboardBroadcastInfo::State::kOn, "test");
  QCOMPARE(info->m_state, Server::KeyboardBroadcastInfo::State::kOn);
  QCOMPARE(info->m_screens, "test");
  delete info;
}

void ServerTests::edgeCrossing_switchesToLinkedScreen_data()
{
  QTest::addColumn<QString>("screen");
  QTest::addColumn<int>("x");
  QTest::addColumn<int>("y");
  QTest::addColumn<int>("enterX");
  QTest::addColumn<int>("enterY");

  // the position along the edge is mapped proportionally onto the 1000x800 client
  QTest::newRow("left") << "left" << 0 << 540 << 999 << 400;
  QTest::newRow("right") << "right" << 1919 << 540 << 0 << 400;
  QTest::newRow("top") << "up" << 960 << 0 << 500 << 799;
  QTest::newRow("bottom") << "down" << 960 << 1079 << 500 << 0;
  QTest::newRow("proportional") << "left" << 0 << 270 << 999 << 200;
}

void ServerTests::edgeCrossing_switchesToLinkedScreen()
{
  QFETCH(QString, screen);
  QFETCH(int, x);
  QFETCH(int, y);
  QFETCH(int, enterX);
  QFETCH(int, enterY);

  Harness h;
  h.linkAround();
  h.start();
  std::map<std::string, FakeClient *> clients;
  for (const auto *name : {"left", "right", "up", "down"}) {
    clients[name] = h.connect(name);
  }

  h.movePrimary(x, y);

  for (const auto &[name, client] : clients) {
    if (name == screen.toStdString()) {
      QCOMPARE(client->m_enters, (std::vector<Point>{{enterX, enterY}}));
    } else {
      QVERIFY(client->m_enters.empty());
    }
  }
  QCOMPARE(h.switches(), 1);
}

void ServerTests::edgeCrossing_noNeighbour_staysOnPrimary()
{
  Harness h;
  h.link("server", Left, "left");
  h.start();
  auto *left = h.connect("left");
  auto *right = h.connect("right");

  h.movePrimary(1919, 540);
  h.movePrimary(960, 0);

  QVERIFY(right->m_enters.empty());
  QCOMPARE(h.switches(), 0);
  QCOMPARE(h.platform->m_activeSides, kMask(DirectionMask::LeftMask));

  h.movePrimary(0, 540);
  QCOMPARE(left->m_enters.size(), 1);
}

void ServerTests::secondaryMotion_returnsToPrimaryOutsideJumpZone()
{
  Harness h;
  h.linkAround();
  h.start();
  auto *left = h.connect("left");
  h.movePrimary(0, 540);
  QCOMPARE(left->m_enters, (std::vector<Point>{{999, 400}}));

  h.platform->m_warps.clear();
  h.moveSecondary(1, 0);

  // entering at x=0 would sit in the jump zone of the linked left edge
  QCOMPARE(h.platform->m_warps, (std::vector<Point>{{1, 540}}));
  QCOMPARE(h.switches(), 2);
}

void ServerTests::secondaryMotion_clampsAtEdgeWithoutNeighbour()
{
  Harness h;
  h.linkAround();
  h.start();
  auto *left = h.connect("left");
  h.movePrimary(0, 540);

  h.moveSecondary(-20, -500);

  QCOMPARE(left->m_moves, (std::vector<Point>{{979, 0}}));
  QCOMPARE(h.switches(), 1);
}

void ServerTests::secondaryMotion_largeMoveCrossesSeveralScreens()
{
  Harness h({"l1", "l2", "l3"});
  h.link("server", Left, "l1");
  h.link("l1", Left, "l2");
  h.link("l2", Left, "l3");
  h.start();
  auto *l1 = h.connect("l1");
  auto *l2 = h.connect("l2");
  auto *l3 = h.connect("l3");
  h.movePrimary(0, 540);
  QCOMPARE(l1->m_enters.size(), 1);

  h.moveSecondary(-2500, 0);

  QVERIFY(l2->m_enters.empty());
  QCOMPARE(l3->m_enters, (std::vector<Point>{{499, 400}}));
}

void ServerTests::neighbour_skipsOfflineScreen()
{
  Harness h({"offline", "far"});
  h.link("server", Left, "offline");
  h.link("offline", Left, "far");
  h.start();
  auto *far = h.connect("far");

  h.movePrimary(0, 540);

  QCOMPARE(far->m_enters, (std::vector<Point>{{999, 400}}));
}

void ServerTests::neighbour_offlineLinkCycle_terminates()
{
  Harness h({"a", "b", "c"});
  h.link("server", Left, "a");
  h.link("a", Left, "b");
  h.link("b", Left, "a");
  h.start();
  auto *c = h.connect("c");

  h.movePrimary(0, 540);

  QCOMPARE(h.switches(), 0);
  QVERIFY(c->m_enters.empty());
}

void ServerTests::lockCursor_eventBlocksAndReleasesSwitching()
{
  using Info = Server::LockCursorToScreenInfo;
  Harness h;
  h.linkAround();
  h.start();
  auto *left = h.connect("left");
  QVERIFY(h.platform->m_activeSides & kMask(DirectionMask::LeftMask));

  h.sendToFilter(EventTypes::ServerLockCursorToScreen, new Info(Info::kOn));
  QCOMPARE(h.platform->m_activeSides, 0u);
  h.movePrimary(0, 540);
  QVERIFY(left->m_enters.empty());

  h.sendToFilter(EventTypes::ServerLockCursorToScreen, new Info(Info::kToggle));
  QVERIFY(h.platform->m_activeSides & kMask(DirectionMask::LeftMask));
  h.movePrimary(0, 540);
  QCOMPARE(left->m_enters.size(), 1);
}

void ServerTests::lockCursor_scrollLockAtStartup()
{
  Harness h;
  h.linkAround();
  h.platform->m_toggleMask = KeyModifierScrollLock;
  h.start();
  auto *left = h.connect("left");

  h.movePrimary(0, 540);

  QVERIFY(left->m_enters.empty());
}

void ServerTests::lockCursor_disabledByOption()
{
  Harness h;
  h.linkAround();
  h.config.addOption("", kOptionDisableLockToScreen, 1);
  h.platform->m_toggleMask = KeyModifierScrollLock;
  h.start();
  auto *left = h.connect("left");

  h.movePrimary(0, 540);

  QCOMPARE(left->m_enters.size(), 1);
}

void ServerTests::lockCursor_mouseButtonHeld()
{
  Harness h;
  h.linkAround();
  h.start();
  auto *left = h.connect("left");
  h.platform->m_buttonDown = true;

  h.movePrimary(0, 540);
  QVERIFY(left->m_enters.empty());

  h.platform->m_buttonDown = false;
  h.movePrimary(0, 540);
  QCOMPARE(left->m_enters.size(), 1);
}

void ServerTests::switchDelay_switchesOnTimer()
{
  Harness h;
  h.linkAround();
  h.config.addOption("", kOptionScreenSwitchDelay, 250);
  h.start();
  auto *left = h.connect("left");

  h.movePrimary(0, 540);
  QVERIFY(left->m_enters.empty());
  QCOMPARE(h.events.m_timers.size(), 1);
  const auto timer = h.events.m_timers.back();
  QCOMPARE(timer.m_duration, 0.25);
  QCOMPARE(timer.m_target, static_cast<void *>(h.server.get()));

  h.events.send(EventTypes::Timer, h.server.get());

  QCOMPARE(left->m_enters, (std::vector<Point>{{999, 400}}));
  QVERIFY(!h.events.m_liveTimers.contains(timer.m_timer));
}

void ServerTests::switchDelay_cancelledWhenLeavingEdge()
{
  Harness h;
  h.linkAround();
  h.config.addOption("", kOptionScreenSwitchDelay, 250);
  h.start();
  h.connect("left");

  h.movePrimary(0, 540);
  QCOMPARE(h.events.m_liveTimers.size(), 1);

  h.movePrimary(500, 540);
  QVERIFY(h.events.m_liveTimers.empty());
}

void ServerTests::switchDelay_onSecondary()
{
  Harness h;
  h.linkAround();
  h.config.addOption("", kOptionScreenSwitchDelay, 250);
  h.start();
  auto *left = h.connect("left");
  h.movePrimary(0, 540);
  h.events.send(EventTypes::Timer, h.server.get());
  QCOMPARE(left->m_enters.size(), 1);

  // pushing against the edge waits without moving the cursor
  h.moveSecondary(5, 0);
  QCOMPARE(h.events.m_liveTimers.size(), 1);
  QVERIFY(left->m_moves.empty());

  h.moveSecondary(-10, 0);
  QVERIFY(h.events.m_liveTimers.empty());
  QCOMPARE(left->m_moves, (std::vector<Point>{{989, 400}}));

  h.moveSecondary(20, 0);
  h.platform->m_warps.clear();
  h.events.send(EventTypes::Timer, h.server.get());
  QCOMPARE(h.platform->m_warps, (std::vector<Point>{{9, 540}}));
}

void ServerTests::switchDelay_onSecondaryBottomEdge_keepsWaitingAlongEdge()
{
  Harness h;
  h.linkAround();
  h.link("up", Bottom, "server");
  h.config.addOption("", kOptionScreenSwitchDelay, 250);
  h.start();
  auto *up = h.connect("up");
  h.movePrimary(960, 0);
  h.events.send(EventTypes::Timer, h.server.get());
  QCOMPARE(up->m_enters, (std::vector<Point>{{500, 799}}));

  h.moveSecondary(0, 5);
  QCOMPARE(h.events.m_liveTimers.size(), 1);

  h.moveSecondary(3, 0);
  QCOMPARE(h.events.m_liveTimers.size(), 1);

  h.moveSecondary(0, -10);
  QVERIFY(h.events.m_liveTimers.empty());
}

void ServerTests::twoTap_secondTapSwitches()
{
  Harness h;
  h.linkAround();
  h.config.addOption("", kOptionScreenSwitchTwoTap, 5000);
  h.start();
  auto *left = h.connect("left");

  h.movePrimary(500, 540);
  h.movePrimary(0, 540);
  QVERIFY(left->m_enters.empty());

  // two moves away from the edge, outside the tap zone, arm the second tap
  h.movePrimary(5, 540);
  h.movePrimary(10, 540);
  h.movePrimary(0, 540);

  QCOMPARE(left->m_enters.size(), 1);
}

void ServerTests::twoTap_withoutLeavingTapZone_doesNotSwitch()
{
  Harness h;
  h.linkAround();
  h.config.addOption("", kOptionScreenSwitchTwoTap, 5000);
  h.start();
  auto *left = h.connect("left");

  h.movePrimary(500, 540);
  h.movePrimary(0, 540);
  h.movePrimary(1, 540);
  h.movePrimary(2, 540);
  h.movePrimary(0, 540);

  QVERIFY(left->m_enters.empty());
}

void ServerTests::corner_blocksSwitch()
{
  deskflow::server::ServerOptions options;
  auto &server = options.screens.emplace_back();
  server.name = "server";
  server.switchCornerSize = 50;
  server.switchCornerTopLeft = true;
  Harness h({"left", "right", "up", "down"}, options);
  h.linkAround();
  h.start();
  auto *left = h.connect("left");

  h.movePrimary(0, 10);
  QVERIFY(left->m_enters.empty());

  h.movePrimary(0, 500);
  QCOMPARE(left->m_enters.size(), 1);
}

void ServerTests::clipboardGrab_notifiesOtherScreens()
{
  Harness h;
  h.start();
  auto *left = h.connect("left");
  auto *right = h.connect("right");
  h.platform->m_clipboards.clear();

  IScreen::ClipboardInfo info{kClipboardClipboard, 1};
  h.events.send(EventTypes::ClipboardGrabbed, left->getEventTarget(), &info, Event::EventFlags::DontFreeData);

  QCOMPARE(right->m_grabs, std::vector<ClipboardID>{kClipboardClipboard});
  QVERIFY(left->m_grabs.empty());
  QCOMPARE(left->m_dirty, (std::vector<std::pair<ClipboardID, bool>>{{kClipboardClipboard, false}}));
  // the primary screen takes ownership by clearing its clipboard
  QCOMPARE(h.platform->m_clipboards, std::vector<std::string>{""});
}

void ServerTests::clipboardChange_fromNonOwner_isIgnored()
{
  Harness h;
  h.start();
  auto *left = h.connect("left");
  auto *right = h.connect("right");
  IScreen::ClipboardInfo grab{kClipboardClipboard, 1};
  h.events.send(EventTypes::ClipboardGrabbed, left->getEventTarget(), &grab, Event::EventFlags::DontFreeData);
  h.platform->m_clipboards.clear();

  // a remote client sends clipboard data without having grabbed the clipboard
  right->m_clipboardText = "not yours";
  IScreen::ClipboardInfo change{kClipboardClipboard, 2};
  h.events.send(EventTypes::ClipboardChanged, right->getEventTarget(), &change, Event::EventFlags::DontFreeData);

  QVERIFY(h.platform->m_clipboards.empty());
  QVERIFY(left->m_clipboards.empty());
}

void ServerTests::clipboardGrab_ignoresStaleSequence()
{
  Harness h;
  h.start();
  auto *left = h.connect("left");
  auto *right = h.connect("right");
  IScreen::ClipboardInfo first{kClipboardClipboard, 5};
  h.events.send(EventTypes::ClipboardGrabbed, left->getEventTarget(), &first, Event::EventFlags::DontFreeData);

  IScreen::ClipboardInfo stale{kClipboardClipboard, 4};
  h.events.send(EventTypes::ClipboardGrabbed, right->getEventTarget(), &stale, Event::EventFlags::DontFreeData);

  QVERIFY(left->m_grabs.empty());
}

void ServerTests::clipboardGrab_ignoredWhenSharingDisabled()
{
  Harness h;
  h.config.addOption("", kOptionClipboardSharing, 0);
  h.start();
  auto *left = h.connect("left");
  auto *right = h.connect("right");

  IScreen::ClipboardInfo info{kClipboardClipboard, 1};
  h.events.send(EventTypes::ClipboardGrabbed, left->getEventTarget(), &info, Event::EventFlags::DontFreeData);

  QVERIFY(right->m_grabs.empty());
}

void ServerTests::clipboardChanged_propagatesToActiveScreen()
{
  Harness h;
  h.linkAround();
  h.start();
  auto *left = h.connect("left");
  auto *right = h.connect("right");
  IScreen::ClipboardInfo info{kClipboardClipboard, 1};
  h.events.send(EventTypes::ClipboardGrabbed, left->getEventTarget(), &info, Event::EventFlags::DontFreeData);
  right->m_dirty.clear();
  h.platform->m_clipboards.clear();

  left->m_clipboardText = "hello";
  h.events.send(EventTypes::ClipboardChanged, left->getEventTarget(), &info, Event::EventFlags::DontFreeData);

  QCOMPARE(right->m_dirty, (std::vector<std::pair<ClipboardID, bool>>{{kClipboardClipboard, true}}));
  QCOMPARE(h.platform->m_clipboards, std::vector<std::string>{"hello"});

  h.movePrimary(1919, 540);
  QCOMPARE(right->m_clipboards.at(kClipboardClipboard), "hello");
}

void ServerTests::adoptClient_sendsOptionsAndNotifies()
{
  Harness h;
  h.start();

  auto *left = h.connect("left");

  std::vector<std::string> clients;
  h.server->getClients(clients);
  QCOMPARE(clients, (std::vector<std::string>{"left", "server"}));
  QCOMPARE(left->m_optionResets, 1);
  QCOMPARE(h.events.posted(EventTypes::ServerConnected), 1);
}

void ServerTests::adoptClient_duplicateName_closedBusy()
{
  Harness h;
  h.start();
  auto *first = h.connect("left");

  auto *second = h.connect("left");

  QCOMPARE(second->written(), std::string(kMsgEBusy));
  QVERIFY(first->written().empty());
  QCOMPARE(h.events.m_liveTimers.size(), 1);
}

void ServerTests::adoptClient_unknownName_closedAndDeletedOnDisconnect()
{
  Harness h;
  h.start();
  bool destroyed = false;
  auto *stranger = h.connect("stranger", &destroyed);
  QCOMPARE(stranger->written(), std::string(kMsgEUnknown));
  QCOMPARE(h.events.m_liveTimers.size(), 1);
  const auto timer = h.events.m_timers.back().m_timer;

  h.events.send(EventTypes::ClientProxyDisconnected, targetOf(stranger));

  QVERIFY(destroyed);
  QVERIFY(h.events.m_liveTimers.empty());
  QVERIFY(!h.events.hasHandler(EventTypes::Timer, timer));
  QVERIFY(!h.events.hasHandler(EventTypes::ClientProxyDisconnected, targetOf(stranger)));
}

void ServerTests::adoptClient_nonRemoteUnknownClient_notClosed()
{
  Harness h;
  h.start();
  auto *platform = new FakePlatformScreen(&h.events);
  deskflow::Screen screen(platform, &h.events);
  PrimaryClient local("stranger", &screen);

  // only a ClientProxy can be told to disconnect
  h.server->adoptClient(&local);

  QVERIFY(h.events.m_timers.empty());
  QVERIFY(!h.events.hasHandler(EventTypes::ClientProxyDisconnected, targetOf(&local)));
}

void ServerTests::clientDisconnect_whileActive_returnsToPrimary()
{
  Harness h;
  h.linkAround();
  h.start();
  bool destroyed = false;
  auto *left = h.connect("left", &destroyed);
  h.movePrimary(0, 540);
  h.platform->m_warps.clear();
  const void *leftTarget = left->getEventTarget();

  h.events.send(EventTypes::ClientProxyDisconnected, targetOf(left));

  QVERIFY(destroyed);
  QCOMPARE(h.platform->m_warps, (std::vector<Point>{{960, 540}}));
  QCOMPARE(h.events.posted(EventTypes::ServerDisconnected), 1);
  QVERIFY(!h.events.hasHandler(EventTypes::ScreenShapeChanged, const_cast<void *>(leftTarget)));
  std::vector<std::string> clients;
  h.server->getClients(clients);
  QCOMPARE(clients, std::vector<std::string>{"server"});
}

void ServerTests::closeTimeout_deletesClientAndTimer()
{
  Harness h;
  h.start();
  bool destroyed = false;
  auto *left = h.connect("left", &destroyed);
  Config reduced(&h.events);
  reduced.addScreen("server");

  QVERIFY(h.server->setConfig(reduced));
  QCOMPARE(left->written(), std::string(kMsgCClose));
  QCOMPARE(h.events.m_timers.size(), 1);
  const auto timer = h.events.m_timers.back();
  QCOMPARE(timer.m_duration, 5.0);

  h.events.send(EventTypes::Timer, timer.m_timer);

  QVERIFY(destroyed);
  QVERIFY(!h.events.hasHandler(EventTypes::Timer, timer.m_timer));
  QVERIFY(h.events.m_liveTimers.empty());
}

void ServerTests::teardown_removesAllHandlersAndTimers()
{
  Harness h;
  h.linkAround();
  h.config.addOption("", kOptionScreenSwitchDelay, 250);
  h.start();
  bool leftDestroyed = false;
  bool rightDestroyed = false;
  h.connect("left", &leftDestroyed);
  h.connect("right", &rightDestroyed);
  h.connect("stranger");
  h.movePrimary(0, 540);
  QCOMPARE(h.events.m_liveTimers.size(), 2);

  h.server.reset();

  QVERIFY(leftDestroyed);
  QVERIFY(rightDestroyed);
  QVERIFY(h.events.m_liveTimers.empty());
  for (const auto &[key, handler] : h.events.m_handlers) {
    qWarning() << "leftover handler for event type" << static_cast<int>(key.first);
  }
  QVERIFY(h.events.m_handlers.empty());
}

void ServerTests::switchToScreenEvent_jumpsToScreen()
{
  Harness h;
  h.start();
  h.connect("left");
  auto *right = h.connect("right");

  h.sendToFilter(EventTypes::ServerSwitchToScreen, new Server::SwitchToScreenInfo("nobody"));
  QCOMPARE(h.switches(), 0);

  // enters where the cursor was when the client connected
  h.sendToFilter(EventTypes::ServerSwitchToScreen, new Server::SwitchToScreenInfo("right"));
  QCOMPARE(right->m_enters, (std::vector<Point>{{500, 400}}));
}

void ServerTests::switchInDirectionEvent_jumpsToNeighbour()
{
  Harness h;
  h.linkAround();
  h.start();
  auto *right = h.connect("right");

  h.sendToFilter(EventTypes::ServerSwitchInDirection, new Server::SwitchInDirectionInfo(Right));

  QCOMPARE(right->m_enters, (std::vector<Point>{{500, 400}}));
}

void ServerTests::toggleScreenEvent_cyclesScreens()
{
  Harness h;
  h.start();
  auto *left = h.connect("left");
  auto *right = h.connect("right");

  h.sendToFilter(EventTypes::ServerToggleScreen, nullptr);
  QCOMPARE(left->m_enters.size(), 1);
  h.sendToFilter(EventTypes::ServerToggleScreen, nullptr);
  QCOMPARE(right->m_enters.size(), 1);
  h.sendToFilter(EventTypes::ServerToggleScreen, nullptr);
  QCOMPARE(h.switches(), 3);
}

void ServerTests::keyboardBroadcast_sendsKeysToAllScreens()
{
  using Info = Server::KeyboardBroadcastInfo;
  Harness h;
  h.start();
  auto *left = h.connect("left");
  auto *right = h.connect("right");

  h.sendToFilter(EventTypes::ServerKeyboardBroadcast, new Info(Info::kOn));
  h.sendToFilter(EventTypes::KeyStateKeyDown, IKeyState::KeyInfo::alloc('a', 0, 30, 1));
  h.sendToFilter(EventTypes::KeyStateKeyUp, IKeyState::KeyInfo::alloc('a', 0, 30, 1));

  QCOMPARE(left->m_keyDowns, std::vector<KeyID>{'a'});
  QCOMPARE(right->m_keyDowns, std::vector<KeyID>{'a'});
  QCOMPARE(left->m_keyUps, std::vector<KeyID>{'a'});
  QCOMPARE(right->m_keyUps, std::vector<KeyID>{'a'});

  h.sendToFilter(EventTypes::ServerKeyboardBroadcast, new Info(Info::kToggle));
  h.sendToFilter(EventTypes::KeyStateKeyUp, IKeyState::KeyInfo::alloc('b', 0, 48, 1));
  QCOMPARE(left->m_keyUps.size(), 1);
}

void ServerTests::keyboardBroadcast_limitedToScreens()
{
  using Info = Server::KeyboardBroadcastInfo;
  Harness h;
  h.start();
  auto *left = h.connect("left");
  auto *right = h.connect("right");

  h.sendToFilter(EventTypes::ServerKeyboardBroadcast, new Info(Info::kOn, ":left:"));
  h.sendToFilter(EventTypes::KeyStateKeyUp, IKeyState::KeyInfo::alloc('a', 0, 30, 1));

  QCOMPARE(left->m_keyUps, std::vector<KeyID>{'a'});
  QVERIFY(right->m_keyUps.empty());
}

void ServerTests::keys_relayedToActiveScreen()
{
  Harness h;
  h.linkAround();
  h.start();
  auto *left = h.connect("left");
  auto *right = h.connect("right");
  h.movePrimary(0, 540);

  h.sendToFilter(EventTypes::KeyStateKeyDown, IKeyState::KeyInfo::alloc('x', 0, 7, 1));
  h.sendToFilter(EventTypes::KeyStateKeyRepeat, IKeyState::KeyInfo::alloc('x', 0, 7, 2));
  h.sendToFilter(EventTypes::KeyStateKeyUp, IKeyState::KeyInfo::alloc('x', 0, 7, 1));

  QCOMPARE(left->m_keyDowns, std::vector<KeyID>{'x'});
  QCOMPARE(left->m_keyRepeats, std::vector<KeyID>{'x'});
  QCOMPARE(left->m_keyUps, std::vector<KeyID>{'x'});
  QVERIFY(right->m_keyDowns.empty());
}

void ServerTests::mouseEvents_relayedToActiveScreen()
{
  Harness h;
  h.linkAround();
  h.start();
  auto *left = h.connect("left");
  auto *right = h.connect("right");
  h.movePrimary(0, 540);

  h.events.send(
      EventTypes::PrimaryScreenButtonDown, h.config.getInputFilter(), IPrimaryScreen::ButtonInfo::alloc(1, 0)
  );
  h.events.send(EventTypes::PrimaryScreenButtonUp, h.config.getInputFilter(), IPrimaryScreen::ButtonInfo::alloc(1, 0));
  h.events.send(EventTypes::PrimaryScreenWheel, h.platform, IPrimaryScreen::WheelInfo::alloc(0, 120));

  QCOMPARE(left->m_buttons, std::vector<ButtonID>{1});
  QCOMPARE(left->m_buttonUps, std::vector<ButtonID>{1});
  QCOMPARE(left->m_wheels, (std::vector<Point>{{0, 120}}));
  QVERIFY(right->m_buttons.empty());
}

void ServerTests::screensaver_returnsToPreviousScreen()
{
  Harness h;
  h.linkAround();
  h.start();
  auto *left = h.connect("left");
  auto *right = h.connect("right");
  h.movePrimary(0, 540);

  h.events.send(EventTypes::PrimaryScreenSaverActivated, h.platform);
  QCOMPARE(h.switches(), 2);
  QCOMPARE(right->m_screensaver, std::vector<bool>{true});

  h.events.send(EventTypes::PrimaryScreenSaverDeactivated, h.platform);
  QCOMPARE(left->m_enters, (std::vector<Point>{{999, 400}, {999, 400}}));
  QCOMPARE(right->m_screensaver, (std::vector<bool>{true, false}));
}

void ServerTests::relativeMoves_whenLocked()
{
  using Info = Server::LockCursorToScreenInfo;
  Harness h;
  h.linkAround();
  h.config.addOption("", kOptionRelativeMouseMoves, 1);
  h.start();
  auto *left = h.connect("left");
  h.movePrimary(0, 540);
  h.sendToFilter(EventTypes::ServerLockCursorToScreen, new Info(Info::kOn));

  h.moveSecondary(3, 4);
  QCOMPARE(left->m_relativeMoves, (std::vector<Point>{{3, 4}}));

  h.sendToFilter(EventTypes::ServerLockCursorToScreen, new Info(Info::kOff));
  QCOMPARE(left->m_moves, (std::vector<Point>{{500, 400}}));
}

void ServerTests::statusSink_reportsConnectionsAndRejections()
{
  FakeStatusSink sink;
  Harness h({"left", "right"});
  h.start();

  auto *left = h.connect("left");
  QCOMPARE(sink.take(), QStringList({"connectionState=Connected", "connectedClients=left"}));

  auto *right = h.connect("right");
  QCOMPARE(sink.take(), QStringList({"connectionState=Connected", "connectedClients=left,right"}));

  h.connect("stranger");
  QCOMPARE(sink.take(), QStringList({"unrecognisedClient=stranger"}));

  h.events.send(EventTypes::ClientProxyDisconnected, targetOf(left));
  QCOMPARE(sink.take(), QStringList({"connectionState=Connected", "connectedClients=right"}));

  h.events.send(EventTypes::ClientProxyDisconnected, targetOf(right));
  QCOMPARE(sink.take(), QStringList({"connectionState=Listening", "connectedClients="}));
}

QTEST_MAIN(ServerTests)
