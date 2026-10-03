# SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
# SPDX-License-Identifier: MIT

# Runs the core without a mode and with an unknown one; both must exit with s_exitArgs (3).
foreach(mode "" "bogus")
  execute_process(COMMAND "${CORE}" ${mode} --new-instance RESULT_VARIABLE result OUTPUT_QUIET ERROR_QUIET)
  if(NOT result EQUAL 3)
    message(FATAL_ERROR "unictrl-core ${mode}: exit code ${result}, expected 3")
  endif()
endforeach()
