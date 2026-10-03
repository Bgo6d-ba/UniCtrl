#!/usr/bin/env bash
# SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
# SPDX-License-Identifier: MIT

# Self-test for run-ctest.sh against a scratch ctest project whose suites print QtTest's
# initTestCase skip line. Needs cmake (and xvfb-run on Linux).

set -euo pipefail

script="$(cd "$(dirname "$0")" && pwd)/run-ctest.sh"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# $1: suites to create, as name=PASS|SKIP; $2: suites labelled x11
project() {
  local src="$work/src" suite
  rm -rf "$src" "$work/build"
  mkdir -p "$src"
  {
    echo 'cmake_minimum_required(VERSION 3.16)'
    echo 'project(t NONE)'
    echo 'enable_testing()'
    for suite in $1; do
      echo "add_test(NAME ${suite%=*} COMMAND \${CMAKE_COMMAND} -E echo \"${suite#*=}   : ${suite%=*}::initTestCase() reason\")"
      printf 'set_tests_properties(%s PROPERTIES SKIP_REGULAR_EXPRESSION "%s")\n' "${suite%=*}" 'SKIP   : [^\n]*::initTestCase\\(\\)'
    done
    for suite in $2; do
      echo "set_tests_properties($suite PROPERTIES LABELS x11)"
    done
  } >"$src/CMakeLists.txt"
  cmake -S "$src" -B "$work/build" >/dev/null
}

expect() {
  local want=$1 what=$2 got=0
  MUST_RUN_LABEL=x11 bash "$script" "$work/build" >"$work/out" 2>&1 || got=$?
  if [ "$got" -ne "$want" ]; then
    cat "$work/out"
    echo "FAIL: $what: exit $got, expected $want" >&2
    exit 1
  fi
  echo "ok: $what"
}

project "PosixOnlyTests=SKIP XWindowsFooTests=PASS" "XWindowsFooTests"
expect 0 "a suite skipped by design is not a failure"

project "PosixOnlyTests=PASS XWindowsFooTests=SKIP" "XWindowsFooTests"
expect 1 "an x11 suite that skips fails the run"

project "PosixOnlyTests=PASS XWindowsFooTests=PASS" ""
expect 0 "passing suites pass"
