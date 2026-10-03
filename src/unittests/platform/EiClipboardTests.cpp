/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "EiClipboardTests.h"

#include "platform/EiClipboard.h"

#include <atomic>
#include <string>
#include <thread>

using deskflow::EiClipboard;
using enum IClipboard::Format;

void EiClipboardTests::initTestCase()
{
  // a torn copy logs "not open" warnings; the assertions below are what matter
  m_log.setFilter(LogLevel::Level::Fatal);
}

void EiClipboardTests::copyTo_concurrentCopyFrom_neverTorn()
{
  EiClipboard cache(kClipboardClipboard);
  std::atomic<bool> done = false;

  // stands in for the portal callback writing the cache on the glib thread
  std::thread writer([&cache, &done] {
    for (int i = 0; i < 20000; ++i) {
      const auto value = std::string(64, static_cast<char>('a' + i % 26));
      Clipboard source;
      source.open(0);
      source.empty();
      source.add(Text, value);
      source.add(HTML, value);
      source.close();
      cache.copyFrom(&source);
    }
    done = true;
  });

  int torn = 0;
  while (!done) {
    Clipboard snapshot;
    cache.copyTo(&snapshot);
    snapshot.open(0);
    if (snapshot.has(Text) != snapshot.has(HTML) || snapshot.get(Text) != snapshot.get(HTML))
      ++torn;
    snapshot.close();
  }
  writer.join();

  QCOMPARE(torn, 0);
}

QTEST_MAIN(EiClipboardTests)
