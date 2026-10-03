/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

// Fakes for running a real Server, Screen and PrimaryClient in tests and fuzzers

#include "../deskflow/MockEventQueue.h"
#include "deskflow/IClipboard.h"
#include "deskflow/IPlatformScreen.h"

#include <algorithm>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

using Point = std::pair<int32_t, int32_t>;

inline std::string clipboardText(const IClipboard *clipboard)
{
  if (clipboard == nullptr || !clipboard->open(0)) {
    return {};
  }
  auto text = clipboard->has(IClipboard::Format::Text) ? clipboard->get(IClipboard::Format::Text) : std::string();
  clipboard->close();
  return text;
}

// Keeps handlers so tests can deliver events the way the real queue would.
class FakeEventQueue : public MockEventQueue
{
public:
  struct TimerInfo
  {
    EventQueueTimer *m_timer;
    double m_duration;
    void *m_target;
  };

  ~FakeEventQueue() override
  {
    for (const auto &event : m_posted) {
      Event::deleteData(event);
    }
  }

  void addHandler(EventTypes type, void *target, const EventHandler &handler) override
  {
    m_handlers[{type, target}] = handler;
  }

  void removeHandler(EventTypes type, void *target) override
  {
    m_handlers.erase({type, target});
  }

  void removeHandlers(void *target) override
  {
    std::erase_if(m_handlers, [target](const auto &entry) { return entry.first.second == target; });
  }

  bool dispatchEvent(const Event &event) override
  {
    // like EventQueue, fall back to the target's catch-all handler
    auto it = m_handlers.find({event.getType(), event.getTarget()});
    if (it == m_handlers.end()) {
      it = m_handlers.find({EventTypes::Unknown, event.getTarget()});
    }
    if (it == m_handlers.end()) {
      return false;
    }
    // copy: the handler may remove itself
    const auto handler = it->second;
    handler(event);
    return true;
  }

  void addEvent(Event &&event) override
  {
    m_posted.push_back(std::move(event));
  }

  EventQueueTimer *newOneShotTimer(double duration, void *target) override
  {
    // Server treats timers as opaque handles, so any unique address will do.
    auto *timer = reinterpret_cast<EventQueueTimer *>(&m_timerStorage.emplace_back());
    m_timers.push_back({timer, duration, target});
    m_liveTimers.insert(timer);
    return timer;
  }

  void deleteTimer(EventQueueTimer *timer) override
  {
    m_liveTimers.erase(timer);
  }

  template <typename T = EventData>
  bool send(EventTypes type, void *target, T *data = nullptr, Event::Flags flags = Event::EventFlags::NoFlags)
  {
    Event event(type, target, data, flags);
    const bool handled = dispatchEvent(event);
    Event::deleteData(event);
    return handled;
  }

  // deliver everything posted so far, like one pass of the event loop
  void flush()
  {
    auto posted = std::move(m_posted);
    m_posted.clear();
    for (const auto &event : posted) {
      dispatchEvent(event);
      Event::deleteData(event);
    }
  }

  int posted(EventTypes type) const
  {
    return static_cast<int>(std::ranges::count_if(m_posted, [type](const Event &e) { return e.getType() == type; }));
  }

  bool hasHandler(EventTypes type, void *target) const
  {
    return m_handlers.contains({type, target});
  }

  std::map<std::pair<EventTypes, void *>, EventHandler> m_handlers;
  std::vector<Event> m_posted;
  std::deque<char> m_timerStorage;
  std::vector<TimerInfo> m_timers;
  std::set<EventQueueTimer *> m_liveTimers;
};

class FakePlatformScreen : public IPlatformScreen
{
public:
  explicit FakePlatformScreen(IEventQueue *events) : IPlatformScreen(events)
  {
  }

  void *getEventTarget() const override
  {
    return const_cast<FakePlatformScreen *>(this);
  }
  bool getClipboard(ClipboardID, IClipboard *) const override
  {
    return false;
  }
  void getShape(int32_t &x, int32_t &y, int32_t &w, int32_t &h) const override
  {
    x = 0;
    y = 0;
    w = 1920;
    h = 1080;
  }
  void getCursorPos(int32_t &x, int32_t &y) const override
  {
    getCursorCenter(x, y);
  }
  void reconfigure(uint32_t activeSides) override
  {
    m_activeSides = activeSides;
  }
  uint32_t activeSides() override
  {
    return m_activeSides;
  }
  void warpCursor(int32_t x, int32_t y) override
  {
    m_warps.emplace_back(x, y);
  }
  uint32_t registerHotKey(KeyID, KeyModifierMask) override
  {
    return ++m_hotKeys;
  }
  void unregisterHotKey(uint32_t) override
  {
  }
  void fakeInputBegin() override
  {
  }
  void fakeInputEnd() override
  {
  }
  int32_t getJumpZoneSize() const override
  {
    return 1;
  }
  bool isAnyMouseButtonDown(uint32_t &) const override
  {
    return m_buttonDown;
  }
  void getCursorCenter(int32_t &x, int32_t &y) const override
  {
    x = 960;
    y = 540;
  }
  void fakeMouseButton(ButtonID, bool) override
  {
  }
  void fakeMouseMove(int32_t, int32_t) override
  {
  }
  void fakeMouseRelativeMove(int32_t, int32_t) const override
  {
  }
  void fakeMouseWheel(ScrollDelta) const override
  {
  }
  void updateKeyMap() override
  {
  }
  void updateKeyState() override
  {
  }
  void setHalfDuplexMask(KeyModifierMask) override
  {
  }
  void fakeKeyDown(KeyID, KeyModifierMask, KeyButton, const std::string &) override
  {
  }
  bool fakeKeyRepeat(KeyID, KeyModifierMask, int32_t, KeyButton, const std::string &) override
  {
    return false;
  }
  bool fakeKeyUp(KeyButton) override
  {
    return false;
  }
  void fakeAllKeysUp() override
  {
  }
  bool fakeCtrlAltDel() override
  {
    return false;
  }
  bool isKeyDown(KeyButton) const override
  {
    return false;
  }
  KeyModifierMask getActiveModifiers() const override
  {
    return 0;
  }
  KeyModifierMask pollActiveModifiers() const override
  {
    return m_toggleMask;
  }
  int32_t pollActiveGroup() const override
  {
    return 0;
  }
  void pollPressedKeys(KeyButtonSet &) const override
  {
  }
  void enable() override
  {
  }
  void disable() override
  {
  }
  void enter() override
  {
  }
  bool canLeave() override
  {
    return true;
  }
  void leave() override
  {
  }
  bool setClipboard(ClipboardID, const IClipboard *clipboard) override
  {
    m_clipboards.push_back(clipboardText(clipboard));
    return true;
  }
  void checkClipboards() override
  {
  }
  void openScreensaver(bool) override
  {
  }
  void closeScreensaver() override
  {
  }
  void screensaver(bool) override
  {
  }
  void resetOptions() override
  {
  }
  void setOptions(const OptionsList &) override
  {
  }
  void setSequenceNumber(uint32_t) override
  {
  }
  std::string getSecureInputApp() const override
  {
    return {};
  }
  bool isPrimary() const override
  {
    return true;
  }

  uint32_t m_activeSides = 0;
  uint32_t m_hotKeys = 0;
  KeyModifierMask m_toggleMask = 0;
  bool m_buttonDown = false;
  std::vector<Point> m_warps;
  std::vector<std::string> m_clipboards;

protected:
  void handleSystemEvent(const Event &) override
  {
  }
};
