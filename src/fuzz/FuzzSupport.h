/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "arch/Arch.h"
#include "base/IEventQueue.h"
#include "base/Log.h"
#include "io/IStream.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

namespace deskflow::fuzz {

//! Creates the arch layer and a log that only prints fatal messages, once per process
inline void initArchAndLog()
{
  static const bool done = [] {
    static Arch arch;
    arch.init();
    static Log log;
    log.setFilter(LogLevel::Level::Fatal);
    return true;
  }();
  (void)done;
}

//! Stops the run on a broken invariant, which libFuzzer and the standalone driver report as a crash
[[noreturn]] inline void fail(const char *what)
{
  std::fprintf(stderr, "fuzz invariant failed: %s\n", what);
  std::abort();
}

inline void check(bool condition, const char *what)
{
  if (!condition) {
    fail(what);
  }
}

//! A stream over bytes in memory; reads may be capped to exercise short reads
class MemoryStream : public IStream
{
public:
  explicit MemoryStream(std::string_view bytes = {}) : m_bytes(bytes)
  {
  }

  void append(std::string_view bytes)
  {
    m_bytes.append(bytes);
  }

  const std::string &bytes() const
  {
    return m_bytes;
  }

  void close() override
  {
  }

  uint32_t read(void *buffer, uint32_t n) override
  {
    const auto count = static_cast<uint32_t>(std::min<size_t>(n, m_bytes.size()));
    if (buffer != nullptr) {
      std::memcpy(buffer, m_bytes.data(), count);
    }
    m_bytes.erase(0, count);
    return count;
  }

  void write(const void *buffer, uint32_t n) override
  {
    m_bytes.append(static_cast<const char *>(buffer), n);
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
    return const_cast<MemoryStream *>(this);
  }

  bool isReady() const override
  {
    return !m_bytes.empty();
  }

  uint32_t getSize() const override
  {
    return static_cast<uint32_t>(m_bytes.size());
  }

private:
  std::string m_bytes;
};

//! Keeps handlers so the fuzzer can deliver events itself, and records the types of added events
class EventRecorder : public IEventQueue
{
public:
  void dispatch(EventTypes type, void *target)
  {
    const Event event(type, target);
    for (const auto key : {type, EventTypes::Unknown}) {
      if (const auto it = m_handlers.find({key, target}); it != m_handlers.end()) {
        it->second(event);
        return;
      }
    }
  }

  std::vector<EventTypes> m_added;

  int loop() override
  {
    return 0;
  }
  void adoptBuffer(IEventQueueBuffer *) override
  {
  }
  bool getEvent(Event &, double) override
  {
    return false;
  }
  bool dispatchEvent(const Event &) override
  {
    return true;
  }
  void addEvent(Event &&event) override
  {
    m_added.push_back(event.getType());
    Event::deleteData(event);
  }
  EventQueueTimer *newTimer(double, void *) override
  {
    return nullptr;
  }
  EventQueueTimer *newOneShotTimer(double, void *) override
  {
    return nullptr;
  }
  void deleteTimer(EventQueueTimer *) override
  {
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
  void waitForReady() const override
  {
  }
  void *getSystemTarget() override
  {
    return this;
  }

private:
  std::map<std::pair<EventTypes, void *>, EventHandler> m_handlers;
};

} // namespace deskflow::fuzz
