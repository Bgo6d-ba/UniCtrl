/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <Carbon/Carbon.h>

#include <cmath>
#include <cstdint>
#include <optional>

// Undocumented fields of the events the Dock turns into Mission Control and space changes,
// as seen from three-finger trackpad swipes on macOS 26.
inline const CGEventType kDockControlEventType = static_cast<CGEventType>(30);
inline const CGEventField kGestureHIDTypeField = static_cast<CGEventField>(110);
inline const CGEventField kGestureSwipeMotionField = static_cast<CGEventField>(123);
inline const CGEventField kGestureSwipeProgressField = static_cast<CGEventField>(124);
inline const CGEventField kGesturePhaseField = static_cast<CGEventField>(132);
inline constexpr int64_t kHIDDockSwipe = 23;
inline constexpr int64_t kSwipeMotionHorizontal = 1;
inline constexpr int64_t kSwipeMotionVertical = 2;
inline constexpr int64_t kGesturePhaseEnded = 4;
// smaller swipes are ones macOS would cancel too
inline constexpr double kSwipeMinProgress = 0.1;

// The arrow key that, held with Control, does on another Mac what a finished swipe does here:
// up is Mission Control, down is App Exposé, left and right change space.
inline std::optional<CGKeyCode> dockSwipeShortcut(int64_t hidType, int64_t phase, int64_t motion, double progress)
{
  if (hidType != kHIDDockSwipe || phase != kGesturePhaseEnded || std::fabs(progress) < kSwipeMinProgress)
    return std::nullopt;
  if (motion == kSwipeMotionVertical)
    return progress < 0 ? kVK_UpArrow : kVK_DownArrow;
  if (motion == kSwipeMotionHorizontal)
    return progress < 0 ? kVK_LeftArrow : kVK_RightArrow;
  return std::nullopt;
}
