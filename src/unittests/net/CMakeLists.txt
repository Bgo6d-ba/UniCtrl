# SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
# SPDX-License-Identifier: MIT

if(WIN32)
  set(extra_libs version)
endif()

create_test(
  NAME SecureUtilsTests
  DEPENDS net
  LIBS base arch io ${extra_libs}
  SOURCE SecureUtilsTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/net"
)

create_test(
  NAME FingerprintTests
  DEPENDS net
  LIBS base arch io ${extra_libs}
  SOURCE FingerprintTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/net"
)

create_test(
  NAME FingerprintDatabaseTests
  DEPENDS net
  LIBS base arch io ${extra_libs}
  SOURCE FingerprintDatabaseTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/net"
)

create_test(
  NAME SocketTests
  DEPENDS net
  LIBS base arch io ${extra_libs}
  SOURCE SocketTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/net"
)

create_test(
  NAME TCPListenSocketTests
  DEPENDS net
  LIBS base arch io ${extra_libs}
  SOURCE TCPListenSocketTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/net"
)

create_test(
  NAME SecureSocketTests
  DEPENDS net
  LIBS base arch io ${extra_libs}
  SOURCE SecureSocketTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/net"
)

create_test(
  NAME Spake2Tests
  DEPENDS net
  LIBS base arch io ${extra_libs}
  SOURCE Spake2Tests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/net"
)
