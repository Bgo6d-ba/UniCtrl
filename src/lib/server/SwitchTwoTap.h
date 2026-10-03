/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/DirectionTypes.h"
#include "base/Stopwatch.h"

#include <cstdint>

namespace deskflow::server {

//! Double-tap screen switching
/*!
The first tap on an edge starts it, moving back out of the tap zone arms it, and a second
tap on the same edge within the delay lets the switch happen.
*/
class SwitchTwoTap
{
public:
  //! The active screen's shape
  struct Area
  {
    int32_t x = 0;
    int32_t y = 0;
    int32_t w = 0;
    int32_t h = 0;
  };

  //! A cursor position with the last two moves that led to it
  struct Motion
  {
    int32_t x = 0;
    int32_t y = 0;
    int32_t dx = 0;
    int32_t dy = 0;
    int32_t previousDx = 0;
    int32_t previousDy = 0;
  };

  //! Sets the delay in seconds allowed between the taps; zero or less disables two-tap
  void setDelay(double seconds);
  bool isEnabled() const;

  //! A first tap
  void start();
  void stop();
  bool isStarted() const;

  //! Arms once the cursor is back out of the tap zone and moving away from \p edge, or stops when too slow
  void arm(Direction edge, const Area &screen, int32_t tapZone, const Motion &motion);

  //! True if a tap now is the second tap
  bool isSecondTap() const;

private:
  double m_delay = 0.0;
  bool m_engaged = false;
  bool m_armed = false;
  Stopwatch m_timer;
};

} // namespace deskflow::server
