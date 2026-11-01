/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "OSXDockSwipeTests.h"

#include "platform/OSXDockSwipe.h"

void OSXDockSwipeTests::finishedSwipe_mapsToControlArrow_data()
{
  QTest::addColumn<qint64>("motion");
  QTest::addColumn<double>("progress");
  QTest::addColumn<int>("key");

  // signs as recorded from a MacBook trackpad
  QTest::newRow("up: mission control") << qint64{kSwipeMotionVertical} << -0.375 << int{kVK_UpArrow};
  QTest::newRow("down: app expose") << qint64{kSwipeMotionVertical} << 0.585 << int{kVK_DownArrow};
  QTest::newRow("left") << qint64{kSwipeMotionHorizontal} << -0.307 << int{kVK_LeftArrow};
  QTest::newRow("right") << qint64{kSwipeMotionHorizontal} << 0.614 << int{kVK_RightArrow};
}

void OSXDockSwipeTests::finishedSwipe_mapsToControlArrow()
{
  QFETCH(qint64, motion);
  QFETCH(double, progress);
  QFETCH(int, key);

  const auto shortcut = dockSwipeShortcut(kHIDDockSwipe, kGesturePhaseEnded, motion, progress);
  QVERIFY(shortcut.has_value());
  QCOMPARE(int{*shortcut}, key);
}

void OSXDockSwipeTests::ignoresOtherEvents_data()
{
  QTest::addColumn<qint64>("hidType");
  QTest::addColumn<qint64>("phase");
  QTest::addColumn<qint64>("motion");
  QTest::addColumn<double>("progress");

  QTest::newRow("swipe still moving") << qint64{kHIDDockSwipe} << qint64{2} << qint64{kSwipeMotionVertical} << -0.4;
  QTest::newRow("swipe too short") << qint64{kHIDDockSwipe} << qint64{kGesturePhaseEnded}
                                   << qint64{kSwipeMotionVertical} << -0.05;
  QTest::newRow("not a dock swipe") << qint64{6} << qint64{kGesturePhaseEnded} << qint64{kSwipeMotionVertical}
                                    << -0.4;
  QTest::newRow("other motion") << qint64{kHIDDockSwipe} << qint64{kGesturePhaseEnded} << qint64{5} << -0.4;
}

void OSXDockSwipeTests::ignoresOtherEvents()
{
  QFETCH(qint64, hidType);
  QFETCH(qint64, phase);
  QFETCH(qint64, motion);
  QFETCH(double, progress);

  QVERIFY(!dockSwipeShortcut(hidType, phase, motion, progress).has_value());
}

QTEST_MAIN(OSXDockSwipeTests)
