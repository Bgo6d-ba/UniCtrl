# SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
# SPDX-License-Identifier: MIT

## Use To create tests
function(create_test)
  set(options)
  set(oneValueArgs
    NAME #NAME of new test
    WORKING_DIRECTORY #Working Dir
    DEPENDS #Library being tested
    SOURCE #Single Source File
  )
  set(multiValueArgs
    LIBS #Any Additional libs that are not Qt::Test or the DEPENDS lib
  )
  cmake_parse_arguments(m "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

  if("${m_NAME}" STREQUAL "")
    message(FATAL_ERROR "create_test, tests require a NAME")
  endif()

  if("${m_WORKING_DIRECTORY}" STREQUAL "")
    message(FATAL_ERROR "create_test, tests require a WORKING_DIRECTORY")
  endif()

  if("${m_SOURCE}" STREQUAL "")
    message(FATAL_ERROR "create_test, tests require a SOURCE")
  endif()

  if("${m_DEPENDS}" STREQUAL "")
    message(FATAL_ERROR "create_test, tests require a DEPENDS")
  endif()

  add_executable(${m_NAME} ${m_SOURCE})
  target_link_libraries(${m_NAME} ${m_DEPENDS} ${m_LIBS} Qt::Test)
  if (CMAKE_CXX_BYTE_ORDER STREQUAL "BIG_ENDIAN")
    target_compile_definitions(${m_NAME} PUBLIC WORDS_BIGENDIAN=1)
  endif()
  # the target name (not its file) so CMAKE_TEST_LAUNCHER applies, e.g. valgrind
  add_test(NAME ${m_NAME} COMMAND ${m_NAME} WORKING_DIRECTORY ${m_WORKING_DIRECTORY})
  # A suite that skips in initTestCase ran nothing; report it as skipped rather than passed
  set_tests_properties(${m_NAME} PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP   : [^\n]*::initTestCase\\(\\)")
  # The X11 suites need an X server: CI fails if one of them skips (run-ctest.sh), and they
  # share the one display (key grabs, selection ownership), so they don't run concurrently.
  if(m_NAME MATCHES "^XWindows")
    set_tests_properties(${m_NAME} PROPERTIES LABELS x11 RESOURCE_LOCK x11)
  endif()
  set_property(GLOBAL APPEND PROPERTY ${CMAKE_PROJECT_NAME}_tests ${m_NAME})

  if(ENABLE_COVERAGE)
    setup_target_for_coverage_gcovr_xml(
      NAME coverage-${m_NAME}
      EXECUTABLE ${m_NAME}
      BASE_DIRECTORY ${CMAKE_SOURCE_DIR}/src
      EXCLUDE ${CMAKE_SOURCE_DIR}/subprojects/* ${CMAKE_BINARY_DIR}/* ${CMAKE_SOURCE_DIR}/src/unittests/*
    )
  endif()

endfunction()

find_package(Qt6 ${REQUIRED_QT_VERSION} REQUIRED COMPONENTS Test)

option(ENABLE_INTERACTIVE_TESTS "Run tests that inject real keyboard/mouse input into the desktop session" OFF)

add_subdirectory(arch)
add_subdirectory(base)
add_subdirectory(client)
add_subdirectory(common)
add_subdirectory(deskflow)
add_subdirectory(gui)
add_subdirectory(net)
add_subdirectory(platform)
add_subdirectory(server)

include(${CMAKE_SOURCE_DIR}/cmake/CheckLayering.cmake)
if(NOT WIN32)
  set(layering_optional deskflow-daemon)
endif()
layering_write_links(${CMAKE_CURRENT_BINARY_DIR}/LayeringLinks.cmake ${layering_optional})
add_test(
  NAME LayeringCheck
  COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DLINKS_FILE=${CMAKE_CURRENT_BINARY_DIR}/LayeringLinks.cmake
          -P ${CMAKE_SOURCE_DIR}/cmake/CheckLayering.cmake
)

#make sure to use CI only plugin on Unix
if (UNIX AND NOT APPLE)
  set(qPA_Platform "QT_QPA_PLATFORM=minimal")
endif()

#Store all test that should be run post build in the property
get_property(PROJECT_TESTS GLOBAL PROPERTY ${CMAKE_PROJECT_NAME}_tests)

if(APPLE)
  set(guiApp UniCtrl)
else()
  set(guiApp unictrl)
endif()

option(SKIP_BUILD_TESTS "Skip build time test" OFF)

if(NOT SKIP_BUILD_TESTS)
  add_custom_target(run_tests ALL DEPENDS ${PROJECT_TESTS} ${guiApp})
  add_custom_command (
    TARGET run_tests
    POST_BUILD
    COMMAND ${qPA_Platform} ${CMAKE_CTEST_COMMAND} --test-dir  "${CMAKE_BINARY_DIR}/src/unittests" --output-on-failure
  )
endif()
