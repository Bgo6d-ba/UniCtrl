/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "common/NetworkProtocol.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace deskflow::server {

//! The user's settings the server needs, filled by the app layer so the server library never reads Settings
struct ServerOptions
{
  struct Screen
  {
    std::string name;
    std::vector<std::string> aliases;
    bool halfDuplexCapsLock = false;
    bool halfDuplexNumLock = false;
    bool halfDuplexScrollLock = false;
    bool xtestXineramaUnaware = false;
    bool weakX11Focus = false;
    int switchCornerSize = 0;
    bool switchCornerTopLeft = false;
    bool switchCornerTopRight = false;
    bool switchCornerBottomLeft = false;
    bool switchCornerBottomRight = false;
    // modifier remapping by name; empty keeps the modifier itself
    std::string modifierAlt;
    std::string modifierAltGr;
    std::string modifierCtrl;
    std::string modifierMeta;
    std::string modifierShift;
    std::string modifierSuper;
  };

  //! Screens the user configured; a configuration read starts from these
  std::vector<Screen> screens;

  std::optional<int> heartbeatMs;
  std::optional<int> switchDelayMs;
  std::optional<int> switchDoubleTapMs;
  bool defaultLockToScreenState = false;
  bool disableLockToScreen = false;
  bool relativeMouseMoves = false;
  bool win32KeepForeground = true;
  bool clipboardSharing = true;
  uint32_t clipboardSizeMiB = 3;

  //! Address to listen on; empty listens on every interface
  std::string listenInterface;
  int port = 24800;
  NetworkProtocol protocol = NetworkProtocol::Barrier;
};

} // namespace deskflow::server
