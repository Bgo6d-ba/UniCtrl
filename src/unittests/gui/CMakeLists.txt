# SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
# SPDX-License-Identifier: MIT

add_subdirectory(config)
add_subdirectory(core)
add_subdirectory(lan)

create_test(
  NAME LoggerTests
  DEPENDS gui
  SOURCE LoggerTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)

create_test(
  NAME TlsUtilityTests
  DEPENDS gui
  SOURCE TlsUtilityTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)

create_test(
  NAME IpcClientTests
  DEPENDS gui
  SOURCE IpcClientTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)

create_test(
  NAME HostJoinWidgetTests
  DEPENDS gui
  SOURCE HostJoinWidgetTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)
set_tests_properties(HostJoinWidgetTests PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
