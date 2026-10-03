#!/usr/bin/env bash
# SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
# SPDX-License-Identifier: MIT

# Runs ctest on a build tree. On Linux the tests run under Xvfb and the suites labelled x11 must
# not skip entirely, so a missing X server fails the run instead of passing silently. Suites that
# skip by design elsewhere (e.g. a POSIX-only suite on Windows) are not affected.
# Usage: run-ctest.sh <test dir> [extra ctest args...]
# MUST_RUN_LABEL overrides the label of the suites that must not skip (empty: none).

set -o pipefail

dir=$1
shift
run=()
default_label=""
if [ "$(uname -s)" = "Linux" ]; then
  run=(xvfb-run -a -s "-screen 0 1280x1024x24")
  default_label=x11
fi
label=${MUST_RUN_LABEL-$default_label}
log=$(mktemp)
trap 'rm -f "$log"' EXIT

result=0
"${run[@]}" ctest --test-dir "$dir" --output-on-failure --timeout 300 "$@" 2>&1 | tee "$log" || result=$?

# a suite whose initTestCase skips is reported as Skipped (see src/unittests/CMakeLists.txt)
if [ -n "$label" ]; then
  while read -r name; do
    if grep -Eq "Test +#[0-9]+: $name \.* *\*\*\*Skipped" "$log"; then
      echo "::error::$name skipped entirely; is its prerequisite (e.g. an X server) missing?"
      result=1
    fi
  done < <(ctest --test-dir "$dir" -N -L "^$label\$" | sed -n 's/^ *Test *#[0-9]*: //p')
fi

if [ "$result" -ne 0 ] && [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
  echo "Unit tests failed with code: $result" >>"$GITHUB_STEP_SUMMARY"
fi
exit "$result"
