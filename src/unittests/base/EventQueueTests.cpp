/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "EventQueueTests.h"

#include "base/EventQueue.h"

#include <QTest>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

namespace {

class CountingData : public EventData
{
public:
  explicit CountingData(int &destroyed) : m_destroyed(destroyed)
  {
  }
  ~CountingData() override
  {
    ++m_destroyed;
  }

private:
  int &m_destroyed;
};

bool waitUntil(const std::atomic<bool> &flag)
{
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (!flag && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return flag;
}

} // namespace

void EventQueueTests::initTestCase()
{
  m_arch.init();
}

void EventQueueTests::dispatchEvent_noHandler_returnsFalse()
{
  EventQueue events;

  QVERIFY(!events.dispatchEvent(Event(EventTypes::ClientDisconnected, this)));
}

void EventQueueTests::dispatchEvent_noTypeHandler_dispatchesUnknownHandler()
{
  EventQueue events;
  bool fallbackCalled = false;
  events.addHandler(EventTypes::Unknown, this, [&fallbackCalled](const Event &) { fallbackCalled = true; });

  QVERIFY(events.dispatchEvent(Event(EventTypes::ClientDisconnected, this)));
  QVERIFY(fallbackCalled);
}

void EventQueueTests::dispatchEvent_handlerRemovesItself_keepsHandlerAliveUntilReturn()
{
  EventQueue events;
  auto handlerLifetime = std::make_shared<int>(1);
  std::weak_ptr<int> handlerLifetimeObserver = handlerLifetime;
  bool handlerAliveAfterRemoval = false;

  events.addHandler(
      EventTypes::ClientDisconnected, this,
      [this, &events, &handlerLifetimeObserver, &handlerAliveAfterRemoval, handlerLifetime](const Event &) {
        events.removeHandler(EventTypes::ClientDisconnected, this);
        handlerAliveAfterRemoval = handlerLifetime != nullptr && !handlerLifetimeObserver.expired();
      }
  );
  handlerLifetime.reset();

  QVERIFY(events.dispatchEvent(Event(EventTypes::ClientDisconnected, this)));
  QVERIFY(handlerAliveAfterRemoval);
  QVERIFY(handlerLifetimeObserver.expired());
}

void EventQueueTests::addEvent_dataObjectWithFlags_deletedOnceAfterDispatch()
{
  EventQueue events;
  int destroyed = 0;
  bool sawData = false;
  events.addHandler(EventTypes::ClientDisconnected, this, [&sawData](const Event &event) {
    sawData = event.getDataObject() != nullptr && event.getData() == nullptr;
  });

  events.addEvent(
      Event(EventTypes::ClientDisconnected, this, new CountingData(destroyed), Event::EventFlags::DeliverImmediately)
  );

  QVERIFY(sawData);
  QCOMPARE(destroyed, 1);
}

void EventQueueTests::deleteData_dontFreeData_keepsDataObject()
{
  int destroyed = 0;
  CountingData data(destroyed);

  Event::deleteData(Event(EventTypes::ClientDisconnected, this, &data, Event::EventFlags::DontFreeData));

  QCOMPARE(destroyed, 0);
}

void EventQueueTests::loop_addEventFromOtherThreadsWhileStarting_deliversAll()
{
  constexpr int threadCount = 4;
  constexpr int eventsPerThread = 500;

  for (int round = 0; round < 20; ++round) {
    EventQueue events;
    std::atomic<int> received = 0;
    events.addHandler(EventTypes::ClientDisconnected, this, [&received](const Event &) { ++received; });

    std::atomic<bool> go = false;
    std::vector<std::thread> producers;
    producers.reserve(threadCount);
    for (int i = 0; i < threadCount; ++i) {
      producers.emplace_back([this, &events, &go] {
        while (!go) {
          std::this_thread::yield();
        }
        for (int j = 0; j < eventsPerThread; ++j) {
          events.addEvent(Event(EventTypes::ClientDisconnected, this));
        }
      });
    }

    std::atomic<bool> finished = false;
    std::thread consumer([&events, &finished] {
      events.loop();
      finished = true;
    });
    go = true;
    for (auto &producer : producers) {
      producer.join();
    }
    events.addEvent(Event(EventTypes::Quit));

    const bool quit = waitUntil(finished);
    if (!quit) {
      events.addEvent(Event(EventTypes::Quit));
    }
    consumer.join();
    QVERIFY(quit);
    QCOMPARE(received.load(), threadCount * eventsPerThread);
  }
}

void EventQueueTests::waitForReady_afterLoopStarted_returns()
{
  EventQueue events;
  std::atomic<bool> handled = false;
  events.addHandler(EventTypes::ClientDisconnected, this, [&handled](const Event &) { handled = true; });
  events.addEvent(Event(EventTypes::ClientDisconnected, this));

  std::thread consumer([&events] { events.loop(); });
  QVERIFY(waitUntil(handled));

  bool threw = false;
  try {
    events.waitForReady();
  } catch (const std::runtime_error &) {
    threw = true;
  }

  events.addEvent(Event(EventTypes::Quit));
  consumer.join();
  QVERIFY(!threw);
}

void EventQueueTests::newOneShotTimer_fromOtherThread_fires()
{
  constexpr int timerCount = 2000;
  EventQueue events;
  int target = 0;
  std::atomic<int> fired = 0;
  events.addHandler(EventTypes::Timer, &target, [&events, &fired](const Event &event) {
    events.deleteTimer(static_cast<IEventQueue::TimerEvent *>(event.getData())->m_timer);
    ++fired;
  });
  // keeps the event thread working its timer queue while another thread adds to it
  auto *tick = events.newTimer(0.001, nullptr);

  std::thread consumer([&events] { events.loop(); });
  std::thread producer([&events, &target] {
    for (int i = 0; i < timerCount; ++i) {
      events.newOneShotTimer(0.001, &target);
    }
  });
  producer.join();

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (fired < timerCount && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  events.addEvent(Event(EventTypes::Quit));
  consumer.join();
  events.deleteTimer(tick);
  QCOMPARE(fired.load(), timerCount);
}

QTEST_MAIN(EventQueueTests)
