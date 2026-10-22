# SPDX-FileCopyrightText: (C) 2025 - 2026 Deskflow Developers
# SPDX-License-Identifier: MIT

create_test(
  NAME EiClipboardTests
  DEPENDS platform
  LIBS base arch
  SOURCE EiClipboardTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/platform"
)

if (WIN32)
  create_test(
    NAME MSWindowsClipboardTests
    DEPENDS platform
    LIBS base arch
    SOURCE MSWindowsClipboardTests.cpp
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/platform"
  )
elseif(APPLE)
  create_test(
    NAME OSXClipboardTests
    DEPENDS platform
    LIBS base arch
    SOURCE OSXClipboardTests.cpp
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/platform"
  )
  create_test(
    NAME OSXKeyStateTests
    DEPENDS platform
    LIBS base arch
    SOURCE OSXKeyStateTests.cpp
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/platform"
  )
  if(ENABLE_INTERACTIVE_TESTS)
    set_tests_properties(OSXKeyStateTests PROPERTIES
      LABELS interactive
      ENVIRONMENT DESKFLOW_INTERACTIVE_TESTS=1
      RUN_SERIAL TRUE
    )
  endif()
elseif(UNIX)
  if(LIBEI_FOUND)
    create_test(
      NAME EiHotKeyModifierMatchingTests
      DEPENDS platform
      LIBS base arch
      SOURCE EiHotKeyModifierMatchingTests.cpp
      WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/platform"
    )

    create_test(
      NAME EiKeyStateTests
      DEPENDS platform
      LIBS base arch
      SOURCE EiKeyStateTests.cpp
      WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/platform"
    )
    create_test(
      NAME EiScreenTests
      DEPENDS platform
      LIBS base arch
      SOURCE EiScreenTests.cpp
      WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/platform"
    )
  endif()

  if (BUILD_X11_SUPPORT)
    create_test(
      NAME XWindowsClipboardTests
      DEPENDS platform
      LIBS base arch
      SOURCE XWindowsClipboardTests.cpp
      WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/platform"
    )
    create_test(
      NAME XWindowsKeyStateTests
      DEPENDS platform
      LIBS base arch
      SOURCE XWindowsKeyStateTests.cpp
      WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/platform"
    )
    create_test(
      NAME XkbLayoutParserTests
      DEPENDS platform
      LIBS base arch
      SOURCE XkbLayoutParserTests.cpp
      WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/platform"
    )
    create_test(
      NAME XWindowsScreenTests
      DEPENDS platform
      LIBS base arch
      SOURCE XWindowsScreenTests.cpp
      WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/platform"
    )
  endif()
endif()
