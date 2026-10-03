/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/SwitchTwoTap.h"

#include "base/Log.h"

namespace deskflow::server {

void SwitchTwoTap::setDelay(double seconds)
{
  m_delay = seconds < 0.0 ? 0.0 : seconds;
  stop();
}

bool SwitchTwoTap::isEnabled() const
{
  return m_delay > 0.0;
}

void SwitchTwoTap::start()
{
  m_engaged = true;
  m_armed = false;
  m_timer.reset();
  LOG_VERBOSE("waiting for second tap");
}

void SwitchTwoTap::stop()
{
  m_engaged = false;
  m_armed = false;
}

bool SwitchTwoTap::isStarted() const
{
  return m_engaged;
}

void SwitchTwoTap::arm(Direction edge, const Area &screen, int32_t tapZone, const Motion &motion)
{
  if (!m_engaged) {
    return;
  }
  if (m_timer.getTime() > m_delay) {
    // second tap took too long
    stop();
    return;
  }
  if (m_armed) {
    return;
  }

  const auto &[x, y, dx, dy, previousDx, previousDy] = motion;
  if (x < screen.x + tapZone || x >= screen.x + screen.w - tapZone || y < screen.y + tapZone ||
      y >= screen.y + screen.h - tapZone) {
    return;
  }

  // win32 can generate bogus mouse events that appear to move in the opposite
  // direction to the real motion, so require two moves away from the edge
  switch (edge) {
    using enum Direction;
  case Left:
    m_armed = dx > 0 && previousDx > 0;
    break;
  case Right:
    m_armed = dx < 0 && previousDx < 0;
    break;
  case Top:
    m_armed = dy > 0 && previousDy > 0;
    break;
  case Bottom:
    m_armed = dy < 0 && previousDy < 0;
    break;
  default:
    break;
  }
}

bool SwitchTwoTap::isSecondTap() const
{
  return m_armed && m_timer.getTime() <= m_delay;
}

} // namespace deskflow::server
