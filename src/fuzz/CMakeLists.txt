# SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
# SPDX-License-Identifier: MIT

# Clang links libFuzzer; other compilers get StandaloneMain.cpp, which replays the corpus and mutates it.
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  set(FUZZ_ENGINE_FLAGS -fsanitize=fuzzer)
  message(STATUS "Fuzz targets use libFuzzer")
else()
  message(STATUS "Fuzz targets use the standalone driver")
endif()

function(add_fuzzer name)
  if(FUZZ_ENGINE_FLAGS)
    add_executable(${name} ${name}.cpp)
    target_compile_options(${name} PRIVATE ${FUZZ_ENGINE_FLAGS})
    target_link_options(${name} PRIVATE ${FUZZ_ENGINE_FLAGS})
  else()
    add_executable(${name} ${name}.cpp StandaloneMain.cpp)
  endif()
  target_link_libraries(${name} PRIVATE ${ARGN})
  # replaying the committed corpus keeps every past crash fixed
  add_test(NAME ${name} COMMAND ${name} -runs=0 ${CMAKE_CURRENT_SOURCE_DIR}/corpus/${name})
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan.supp")
endfunction()

add_fuzzer(ProtocolUtilFuzzer domain)
add_fuzzer(ClipboardChunkFuzzer domain)
add_fuzzer(ClipboardUnmarshallFuzzer domain)
add_fuzzer(PacketStreamFilterFuzzer domain)
add_fuzzer(ServerConfigFuzzer server)
add_fuzzer(ClientProxyFuzzer server)
add_fuzzer(IpcServerFuzzer app)
add_fuzzer(IpcClientFuzzer gui)
