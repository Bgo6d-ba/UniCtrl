# SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
# SPDX-FileCopyrightText: (C) 2024 - 2026 Chris Rizzitello <sithlord48@gmail.com>
# SPDX-FileCopyrightText: (C) 2012 - 2025 Symless Ltd
# SPDX-FileCopyrightText: (C) 2009 - 2012 Nick Bolton
# SPDX-License-Identifier: MIT

# This directory builds two targets, see docs/dev/architecture.md:
#   domain - protocol, key maps, clipboard, Screen; used by server, client and platform
#   app    - process wiring (App, ServerApp, ClientApp, IPC servers); uses server, client and platform

add_library(domain STATIC
  AppUtil.cpp
  AppUtil.h
  Chunk.cpp
  Chunk.h
  ClipboardTypes.h
  Clipboard.cpp
  Clipboard.h
  ClipboardChunk.cpp
  ClipboardChunk.h
  DeskflowException.cpp
  DeskflowException.h
  DisplayInvalidException.h
  FileDragTypes.h
  IApp.h
  IClient.h
  IClipboard.cpp
  IClipboard.h
  IKeyState.cpp
  IKeyState.h
  IPlatformScreen.cpp
  IPlatformScreen.h
  IPrimaryScreen.cpp
  IPrimaryScreen.h
  IScreen.h
  IScreenSaver.h
  ISecondaryScreen.h
  KeyTypes.cpp
  KeyTypes.h
  KeyMap.cpp
  KeyMap.h
  KeyState.cpp
  KeyState.h
  MouseTypes.h
  OptionTypes.h
  PacketStreamFilter.cpp
  PacketStreamFilter.h
  PlatformScreen.cpp
  PlatformScreen.h
  ProtocolTypes.h
  ProtocolUtil.cpp
  ProtocolUtil.h
  Screen.cpp
  Screen.h
  ScreenException.cpp
  ScreenException.h
  StreamChunker.cpp
  StreamChunker.h
  KeyboardLayoutManager.cpp
  KeyboardLayoutManager.h
)

target_link_libraries(domain PUBLIC io)

if(WIN32)
  set(APP_PLATFORM_CODE
    win32/AppUtilWindows.cpp
    win32/AppUtilWindows.h
  )
elseif(UNIX)
  set(APP_PLATFORM_CODE
    unix/AppUtilUnix.cpp
    unix/AppUtilUnix.h
  )
endif()

add_library(app STATIC ${APP_PLATFORM_CODE}
  App.cpp
  App.h
  ClientApp.cpp
  ClientApp.h
  ServerApp.cpp
  ServerApp.h
  ipc/IpcServer.cpp
  ipc/IpcServer.h
  ipc/DaemonIpcServer.cpp
  ipc/DaemonIpcServer.h
  ipc/CoreIpcServer.cpp
  ipc/CoreIpcServer.h
  ipc/DaemonPolicy.cpp
  ipc/DaemonPolicy.h
)

target_link_libraries(app PUBLIC server client platform Qt6::Network)
