/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/Log.h"

#include <QTest>

class SwitchTwoTapTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void disabledByDefault();
  void secondTap_afterMovingAwayFromEdge();
  void secondTap_notBeforeStart();
  void arm_needsTwoMovesAwayFromEdge_data();
  void arm_needsTwoMovesAwayFromEdge();
  void arm_notInsideTapZone();
  void arm_tooSlow_stops();
  void setDelay_stops();

private:
  Log m_log;
};
