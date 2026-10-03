# SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
# SPDX-License-Identifier: MIT

if(WIN32)
  set(extra_libs version)
endif()

create_test(
  NAME ClipboardTests
  DEPENDS domain
  LIBS arch base io ${extra_libs}
  SOURCE ClipboardTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/deskflow"
)

create_test(
  NAME ClipboardChunksTests
  DEPENDS domain
  LIBS arch base io ${extra_libs}
  SOURCE ClipboardChunksTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/deskflow"
)

create_test(
  NAME ProtocolUtilTests
  DEPENDS domain
  LIBS arch base io ${extra_libs}
  SOURCE ProtocolUtilTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/deskflow"
)

create_test(
  NAME IKeyStateTests
  DEPENDS domain
  LIBS arch base ${extra_libs}
  SOURCE IKeyStateTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/deskflow"
)

create_test(
  NAME KeyMapTests
  DEPENDS domain
  LIBS arch base ${extra_libs}
  SOURCE KeyMapTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/deskflow"
)

create_test(
  NAME KeyStateTests
  DEPENDS domain
  LIBS arch base ${extra_libs}
  SOURCE KeyStateTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/deskflow"
)

create_test(
  NAME IpcServerTests
  DEPENDS app
  LIBS arch base ${extra_libs}
  SOURCE IpcServerTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/deskflow"
)

create_test(
  NAME ScreenCommandTests
  DEPENDS app
  LIBS arch base ${extra_libs}
  SOURCE ScreenCommandTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/deskflow"
)

create_test(
  NAME PlatformScreenTests
  DEPENDS domain
  LIBS arch base ${extra_libs}
  SOURCE PlatformScreenTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/deskflow"
)

create_test(
  NAME KeyboardLayoutManagerTests
  DEPENDS domain
  LIBS arch base ${extra_libs}
  SOURCE KeyboardLayoutManagerTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/deskflow"
)

create_test(
  NAME DaemonPolicyTests
  DEPENDS app
  LIBS arch base ${extra_libs}
  SOURCE DaemonPolicyTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/deskflow"
)
