/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "SwitchTwoTapTests.h"

#include "server/SwitchTwoTap.h"

#include <QThread>

using deskflow::server::SwitchTwoTap;

namespace {

const SwitchTwoTap::Area kScreen{0, 0, 1000, 800};
const int32_t kTapZone = 3;
// back from the left edge towards the middle, twice
const SwitchTwoTap::Motion kAwayFromLeft{10, 400, 5, 0, 5, 0};

SwitchTwoTap started(double delay = 60.0)
{
  SwitchTwoTap tap;
  tap.setDelay(delay);
  tap.start();
  return tap;
}

} // namespace

void SwitchTwoTapTests::disabledByDefault()
{
  SwitchTwoTap tap;
  QVERIFY(!tap.isEnabled());
  tap.setDelay(-1.0);
  QVERIFY(!tap.isEnabled());
  tap.setDelay(0.25);
  QVERIFY(tap.isEnabled());
}

void SwitchTwoTapTests::secondTap_afterMovingAwayFromEdge()
{
  auto tap = started();
  QVERIFY(tap.isStarted());
  QVERIFY(!tap.isSecondTap());

  tap.arm(Direction::Left, kScreen, kTapZone, kAwayFromLeft);

  QVERIFY(tap.isSecondTap());
}

void SwitchTwoTapTests::secondTap_notBeforeStart()
{
  SwitchTwoTap tap;
  tap.setDelay(60.0);
  tap.arm(Direction::Left, kScreen, kTapZone, kAwayFromLeft);

  QVERIFY(!tap.isStarted());
  QVERIFY(!tap.isSecondTap());
}

void SwitchTwoTapTests::arm_needsTwoMovesAwayFromEdge_data()
{
  QTest::addColumn<int>("edge");
  QTest::addColumn<int>("dx");
  QTest::addColumn<int>("dy");
  QTest::addColumn<int>("previousDx");
  QTest::addColumn<int>("previousDy");
  QTest::addColumn<bool>("armed");

  const auto left = static_cast<int>(Direction::Left);
  const auto right = static_cast<int>(Direction::Right);
  const auto top = static_cast<int>(Direction::Top);
  const auto bottom = static_cast<int>(Direction::Bottom);
  QTest::newRow("left, away twice") << left << 4 << 0 << 2 << 0 << true;
  QTest::newRow("left, one bogus move") << left << 4 << 0 << -2 << 0 << false;
  QTest::newRow("left, towards edge") << left << -4 << 0 << -2 << 0 << false;
  QTest::newRow("right, away twice") << right << -4 << 0 << -2 << 0 << true;
  QTest::newRow("right, towards edge") << right << 4 << 0 << 2 << 0 << false;
  QTest::newRow("top, away twice") << top << 0 << 4 << 0 << 2 << true;
  QTest::newRow("bottom, away twice") << bottom << 0 << -4 << 0 << -2 << true;
  QTest::newRow("bottom, sideways") << bottom << 4 << 0 << 4 << 0 << false;
}

void SwitchTwoTapTests::arm_needsTwoMovesAwayFromEdge()
{
  QFETCH(int, edge);
  QFETCH(int, dx);
  QFETCH(int, dy);
  QFETCH(int, previousDx);
  QFETCH(int, previousDy);
  QFETCH(bool, armed);

  auto tap = started();
  tap.arm(static_cast<Direction>(edge), kScreen, kTapZone, {500, 400, dx, dy, previousDx, previousDy});

  QCOMPARE(tap.isSecondTap(), armed);
}

void SwitchTwoTapTests::arm_notInsideTapZone()
{
  auto tap = started();
  auto motion = kAwayFromLeft;
  motion.x = kTapZone - 1;
  tap.arm(Direction::Left, kScreen, kTapZone, motion);
  QVERIFY(!tap.isSecondTap());

  motion.x = kTapZone;
  tap.arm(Direction::Left, kScreen, kTapZone, motion);
  QVERIFY(tap.isSecondTap());
}

void SwitchTwoTapTests::arm_tooSlow_stops()
{
  auto tap = started(0.001);
  QThread::msleep(20);

  tap.arm(Direction::Left, kScreen, kTapZone, kAwayFromLeft);

  QVERIFY(!tap.isStarted());
  QVERIFY(!tap.isSecondTap());
}

void SwitchTwoTapTests::setDelay_stops()
{
  auto tap = started();
  tap.arm(Direction::Left, kScreen, kTapZone, kAwayFromLeft);

  tap.setDelay(60.0);

  QVERIFY(!tap.isStarted());
  QVERIFY(!tap.isSecondTap());
}

QTEST_MAIN(SwitchTwoTapTests)
