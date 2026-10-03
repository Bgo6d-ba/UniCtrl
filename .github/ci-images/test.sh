#!/usr/bin/env bash
# SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
# SPDX-License-Identifier: MIT

# Checks refs.sh, that jobs take their container from it, and that every image the workflows
# look up is pinned in the Dockerfile: a missing key would run the job on the host instead.

set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
workflows="$here/../workflows"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

refs=$(bash "$here/refs.sh")
python3 -c 'import json, sys; json.loads(sys.argv[1])' "$refs"

echo "FROM debian:trixie-slim AS unpinned" >"$work/Dockerfile"
if bash "$here/refs.sh" "$work/Dockerfile" 2>/dev/null; then
  echo "FAIL: refs.sh accepted an image without a digest" >&2
  exit 1
fi

# a job's own container must come from the pinned refs, not a literal tag
if grep -nE '^    (container|  image): [a-z]' "$workflows"/*.yml; then
  echo "FAIL: job container above is not taken from the ci-images refs" >&2
  exit 1
fi

# lookups like refs)['debian:trixie-slim'], and matrix entries like `container: fedora:latest`
used=$(
  grep -hoE "refs\)\['[^']+'\]" "$workflows"/*.yml | sed -E "s/^refs\)\['//; s/'\]$//"
  grep -hE '^ +container: [a-z]' "$workflows"/*.yml | awk '{print $2}'
)
missing=0
while read -r image; do
  if ! python3 -c 'import json, sys; sys.exit(sys.argv[2] not in json.loads(sys.argv[1]))' "$refs" "$image"; then
    echo "FAIL: $image is used by a workflow but not pinned in .github/ci-images/Dockerfile" >&2
    missing=1
  fi
done < <(sort -u <<<"$used")
exit "$missing"
