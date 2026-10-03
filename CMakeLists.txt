# SPDX-FileCopyrightText: (C) 2024 - 2026 Deskflow Developers
# SPDX-FileCopyrightText: (C) 2012 - 2024 Symless Ltd
# SPDX-FileCopyrightText: (C) 2009 - 2012 Nick Bolton
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.24)

# Link items by fill path
cmake_policy(SET CMP0003 NEW)

# Fix define escaping
cmake_policy(SET CMP0005 NEW)

# De-duplicate libraries on linker calls
if(POLICY CMP0156)
  cmake_policy(SET CMP0156 NEW)
endif()

# Use native paths to collect dependents on windows
if(POLICY CMP0207)
  cmake_policy(SET CMP0207 NEW)
endif()

#Project level options
option(BUILD_INSTALLER "Build installer" ON)
option(ENABLE_COVERAGE "Enable test coverage" OFF)
if(UNIX)
  if(APPLE)
    option(BUILD_OSX_BUNDLE "Build mac os bundle" ON)
  else()
    option(BUILD_X11_SUPPORT "Build with x11 support" ON)
  endif()
elseif(WIN32)
  option (VCPKG_QT "Use Qt from VCPKG" OFF)
endif()

# Set CXX Requirements
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fallback for when git can not be found
set(DESKFLOW_VERSION_MAJOR 1)
set(DESKFLOW_VERSION_MINOR 0)
set(DESKFLOW_VERSION_PATCH 0)
set(DESKFLOW_VERSION_TWEAK 0)

# The version is fixed above; only the commit hash is read from git
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/.git)
  find_package(Git)
  if(GIT_FOUND)
    execute_process(
      COMMAND ${GIT_EXECUTABLE} rev-parse --short=8 HEAD
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      OUTPUT_VARIABLE GIT_SHA_SHORT
      ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE
    )
  endif()
endif()

#generate vcpkg file if needed
if(WIN32)
    if(VCPKG_QT)
        set(QT_LIBS ", \"qttranslations\", \"qtsvg\"")
    endif()
    configure_file(cmake/vcpkg.json.in ${CMAKE_SOURCE_DIR}/vcpkg.json @ONLY)
endif()

#Define our project
project(
  deskflow
  VERSION "${DESKFLOW_VERSION_MAJOR}.${DESKFLOW_VERSION_MINOR}.${DESKFLOW_VERSION_PATCH}.${DESKFLOW_VERSION_TWEAK}"
  DESCRIPTION "Keyboard and mouse sharing utility"
  HOMEPAGE_URL "https://deskflow.org"
  LANGUAGES C CXX
)

# Define Additional "PROJECT" vars for packaging and metadata
set(CMAKE_PROJECT_PROPER_NAME "UniCtrl")
set(CMAKE_PROJECT_VENDOR "${CMAKE_PROJECT_PROPER_NAME} Devs")
set(CMAKE_PROJECT_COPYRIGHT "(C) 2024-2026 ${CMAKE_PROJECT_VENDOR}")
set(CMAKE_PROJECT_CONTACT "${CMAKE_PROJECT_PROPER_NAME} <maintainers@deskflow.org>")
set(CMAKE_PROJECT_REV_FQDN "io.github.batmunkh0612.unictrl")

#Unset the vars used in the project call
unset(DESKFLOW_VERSION_MAJOR)
unset(DESKFLOW_VERSION_MINOR)
unset(DESKFLOW_VERSION_PATCH)
unset(DESKFLOW_VERSION_TWEAK)

message(STATUS "Building ${CMAKE_PROJECT_NAME}-${CMAKE_PROJECT_VERSION}")

# Set lib versions
set(REQUIRED_OPENSSL_VERSION 3.0)
set(REQUIRED_LIBEI_VERSION 1.3)
set(REQUIRED_LIBPORTAL_VERSION 0.9.1)
set(REQUIRED_QT_VERSION 6.7.0)

if (WIN32)
  # VSCMD_ARG_TGT_ARCH is set on CI
  if ("$ENV{VSCMD_ARG_TGT_ARCH}" STREQUAL "")
    # NOT on CI
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "[Aa][Rr][Mm]64")
      set(BUILD_ARCHITECTURE arm64)
    else()
      set(BUILD_ARCHITECTURE x64)
    endif()
  else()
    set (BUILD_ARCHITECTURE $ENV{VSCMD_ARG_TGT_ARCH})
  endif()
else()
  set (BUILD_ARCHITECTURE ${CMAKE_SYSTEM_PROCESSOR})
endif()

if (MSVC)
  # Require the same MSVC runtime that built the binary. Mitigates access violations
  # from accidental ABI-compatibility breakage across runtime minor versions.
  #
  # Derive the minor from the compiler toolset (cl major 19 maps to CRT major 14 and
  # the minor matches, e.g. 19.44 -> 14.44) rather than the host's installed redist
  # from the registry. The toolset version tracks the runtime it actually produces,
  # including the debug CRT (vcruntime140d.dll); the registry redist can be newer than
  # the loaded DLL and make the runtime guard reject a valid local build.
  set(REQUIRED_MSVC_RUNTIME_MAJOR 14)
  if (CMAKE_CXX_COMPILER_VERSION MATCHES "^[0-9]+\\.([0-9]+)")
    set(REQUIRED_MSVC_RUNTIME_MINOR "${CMAKE_MATCH_1}")
    message(STATUS "MSVC runtime: ${REQUIRED_MSVC_RUNTIME_MAJOR}.${REQUIRED_MSVC_RUNTIME_MINOR}")
  else()
    message(FATAL_ERROR "Failed to parse MSVC compiler version: ${CMAKE_CXX_COMPILER_VERSION}")
  endif()
endif()

# Control debug item visibility
# When not set logging is forced to DEBUG and show code locations
# Also exposes a test menu
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
  message(STATUS "Disabling debug build")
  add_definitions(-DNDEBUG)
endif()

# Set Output Folders
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")

# disables the use of signals,slots and emit
# Instead use Q_SIGNAL, Q_SLOT and Q_EMIT
# prevents issues when used with glib for libportal
add_definitions(-DQT_NO_KEYWORDS)

#Find and setup Qt
find_package(Qt6 ${REQUIRED_QT_VERSION} REQUIRED COMPONENTS Core Widgets Network)
if(UNIX AND NOT APPLE)
    find_package(Qt6 ${REQUIRED_QT_VERSION} REQUIRED COMPONENTS DBus)
endif()

# Define the location of Qt deployment tool
if(WIN32)
  if (CMAKE_BUILD_TYPE STREQUAL "Debug" AND VCPKG_QT)
    set(DEPLOY_TOOL windeployqt.debug.bat)
  else()
    set(DEPLOY_TOOL windeployqt)
  endif()
elseif(APPLE)
    set(DEPLOY_TOOL macdeployqt)
endif()

if (WIN32 OR APPLE)
  find_program(DEPLOYQT ${DEPLOY_TOOL})
  if(DEPLOYQT STREQUAL "DEPLOYQT-NOTFOUND")
    message(FATAL_ERROR "Unable to locate the Qt Deploy Tool: \"${DEPLOY_TOOL}\"")
  endif()
  unset(DEPLOY_TOOL)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

message(STATUS "Qt version: ${Qt6_VERSION}")

include(cmake/Libraries.cmake)
configure_libs()

if(ENABLE_COVERAGE)
  message(STATUS "Enabling code coverage")
  include(cmake/CodeCoverage.cmake)
  append_coverage_compiler_flags()
  # Apparently solves the bug in gcov where it returns negative counts and confuses gcovr.
  # > Got negative hit value in gcov line 'branch  2 taken -1' caused by a bug in gcov tool
  # Bug report: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=68080
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-update=atomic")
endif()

if(BUILD_OSX_BUNDLE AND APPLE_CODESIGN_DEV)
  include(cmake/MacCodesign.cmake)
endif()

# setup install paths
include(GNUInstallDirs)
if (WIN32)
  set(CMAKE_INSTALL_BINDIR .)
  set(CMAKE_INSTALL_LIBDIR .)
  set(CMAKE_INSTALL_DOCDIR docs)
  set(CMAKE_INSTALL_LICENSE_DIR .)
  set(CMAKE_INSTALL_I18N_DIR translations)
elseif(BUILD_OSX_BUNDLE)
  set(CMAKE_INSTALL_BINDIR ${CMAKE_PROJECT_PROPER_NAME}.app/Contents/MacOS)
  set(CMAKE_INSTALL_LICENSE_DIR ${CMAKE_PROJECT_PROPER_NAME}.app/Contents/Resources)
  set(CMAKE_INSTALL_DOCDIR ${CMAKE_INSTALL_LICENSE_DIR}/docs)
else()
  set(CMAKE_INSTALL_LICENSE_DIR ${CMAKE_INSTALL_DATADIR}/licenses/${CMAKE_PROJECT_NAME})
  set(CMAKE_INSTALL_I18N_DIR ${CMAKE_INSTALL_DATADIR}/${CMAKE_PROJECT_NAME}/translations)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT MSVC)
  # Interface overrides (IScreen, IClient, platform backends) routinely ignore parameters
  add_compile_options(-Wall -Wextra -Wno-unused-parameter)
elseif(MSVC)
  # CMP0092 dropped CMake's implicit /W3, which left MSVC at /W1.
  # Narrowing (C4244/C4267) is off to match GCC/Clang, which don't enable -Wconversion;
  # C4062 (enumerator not handled in switch) is -Wswitch's counterpart.
  # Language-guarded so the flags never reach rc.exe.
  # ponytail: /W3 not /W4, raise it once a Windows build is warning-clean at /W4
  add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:/W3;/wd4244;/wd4267;/w34062>")
endif()

set(ENABLE_SANITIZERS "" CACHE STRING "Sanitizers to build with, e.g. \"address;undefined\" or \"thread\" (GCC/Clang)")
if(ENABLE_SANITIZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" OR MSVC)
    message(FATAL_ERROR "ENABLE_SANITIZERS needs GCC or Clang")
  endif()
  list(JOIN ENABLE_SANITIZERS "," sanitizers)
  message(STATUS "Enabling sanitizers: ${sanitizers}")
  add_compile_options(-fsanitize=${sanitizers} -fno-omit-frame-pointer)
  if("undefined" IN_LIST ENABLE_SANITIZERS)
    add_compile_options(-fno-sanitize-recover=undefined)
    add_compile_definitions(DESKFLOW_UBSAN)
  endif()
  add_link_options(-fsanitize=${sanitizers})
endif()

option(ENABLE_FUZZING "Build the fuzz targets in src/fuzz" OFF)
if(ENABLE_FUZZING AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  # libFuzzer follows coverage, so the libraries under test need it as well as the targets
  add_compile_options(-fsanitize=fuzzer-no-link)
endif()

# Top level so `ctest --test-dir <build>` finds the tests
enable_testing()

add_subdirectory(docs)

# build translations before source, I18N unit tests fail if they are missing
add_subdirectory(translations)

add_subdirectory(src)

install(
  FILES ${PROJECT_SOURCE_DIR}/LICENSE
  DESTINATION ${CMAKE_INSTALL_LICENSE_DIR}
)

install(
  FILES ${PROJECT_SOURCE_DIR}/LICENSES/LicenseRef-OpenSSL-Exception.txt
  DESTINATION ${CMAKE_INSTALL_LICENSE_DIR}
  RENAME LICENSE_EXCEPTION
)

if(BUILD_INSTALLER)
    add_subdirectory(deploy)
endif()
