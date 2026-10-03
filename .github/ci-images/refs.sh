#!/usr/bin/env bash
# SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
# SPDX-License-Identifier: MIT

# Prints the digest-pinned CI images in a Dockerfile as a JSON object keyed by the reference
# without its digest, e.g. {"debian:stable-slim":"debian:stable-slim@sha256:..."}.
# Fails if any image is not pinned by digest. Usage: refs.sh [Dockerfile]

set -euo pipefail

file=${1:-"$(dirname "$0")/Dockerfile"}
pinned='^[a-z0-9./_-]+:[A-Za-z0-9._-]+@sha256:[0-9a-f]{64}$'
json=""
while read -r keyword ref _; do
  [[ "${keyword,,}" == from ]] || continue
  if ! [[ "$ref" =~ $pinned ]]; then
    echo "not pinned by tag and digest: $ref" >&2
    exit 1
  fi
  json+="${json:+,}\"${ref%@*}\":\"$ref\""
done <"$file"
if [ -z "$json" ]; then
  echo "no images in $file" >&2
  exit 1
fi
echo "{$json}"
