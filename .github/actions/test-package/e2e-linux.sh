#!/usr/bin/env bash
# SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
# SPDX-License-Identifier: MIT

# Runs a unictrl-core server and client against each other over TLS on 127.0.0.1 under Xvfb,
# with peer fingerprint checks on both sides, and fails unless the client connects and both
# processes then shut down cleanly.
# Needs Xvfb and openssl. Usage: e2e-linux.sh [path/to/unictrl-core]

set -euo pipefail

core=${1:-unictrl-core}
# fixed defaults suit one run per CI container; set E2E_PORT/E2E_DISPLAY when sharing a host
port=${E2E_PORT:-24811}
display=${E2E_DISPLAY:-:99}
work=$(mktemp -d)
pids=()

cleanup() {
  for pid in "${pids[@]}"; do
    kill -KILL "$pid" 2>/dev/null || true
  done
  rm -rf "$work"
}
trap cleanup EXIT

fail() {
  echo "e2e: $*" >&2
  for log in "$work"/*.log; do
    echo "--- $(basename "$log")" >&2
    tail -n 40 "$log" >&2 || true
  done
  exit 1
}

# waits up to $1 seconds for the command in the remaining arguments to succeed
wait_for() {
  local deadline=$((SECONDS + $1))
  shift
  until "$@"; do
    ((SECONDS < deadline)) || return 1
    sleep 0.2
  done
}

Xvfb "$display" -screen 0 1280x1024x24 -nolisten tcp >"$work/xvfb.log" 2>&1 &
pids+=($!)
wait_for 10 test -e "/tmp/.X11-unix/X${display#:}" || fail "Xvfb did not start"
export DISPLAY=$display QT_QPA_PLATFORM=minimal

for role in server client; do
  tls="$work/$role/tls"
  mkdir -p "$tls"
  openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj /CN=Deskflow \
    -keyout "$tls/key.pem" -out "$tls/cert.pem" 2>/dev/null
  cat "$tls/key.pem" "$tls/cert.pem" >"$tls/deskflow.pem"
done

fingerprint() {
  echo "v2:sha256:$(openssl x509 -in "$work/$1/tls/cert.pem" -noout -fingerprint -sha256 | sed 's/.*=//; s/://g' | tr 'A-F' 'a-f')"
}
fingerprint server >"$work/client/tls/trusted-servers"
fingerprint client >"$work/server/tls/trusted-clients"

cat >"$work/server/screens.conf" <<EOF
section: links
  server:
    right = client
  client:
    left = server
end
EOF

cat >"$work/server/Deskflow.conf" <<EOF
[core]
computerName=server
interface=127.0.0.1
port=$port

[server]
externalConfig=true
externalConfigFile=$work/server/screens.conf

[screen_server]
name=server

[screen_client]
name=client

[security]
tlsEnabled=true
checkPeerFingerprints=true
certificate=$work/server/tls/deskflow.pem

[log]
level=Debug
EOF

cat >"$work/client/Deskflow.conf" <<EOF
[core]
computerName=client
port=$port

[client]
remoteHost=127.0.0.1

[security]
tlsEnabled=true
checkPeerFingerprints=true
certificate=$work/client/tls/deskflow.pem

[log]
level=Debug
EOF

"$core" server --new-instance --settings "$work/server/Deskflow.conf" >"$work/server.log" 2>&1 &
server=$!
pids+=("$server")
"$core" client --new-instance --settings "$work/client/Deskflow.conf" >"$work/client.log" 2>&1 &
client=$!
pids+=("$client")

connected() {
  kill -0 "$server" 2>/dev/null || fail "server exited early"
  kill -0 "$client" 2>/dev/null || fail "client exited early"
  grep -q 'client "client" has connected' "$work/server.log" && grep -q 'connected to server' "$work/client.log"
}
wait_for 30 connected || fail "client did not connect within 30s"

grep -q 'accepted secure socket' "$work/server.log" || fail "server did not use TLS"
grep -q 'connected to secure socket' "$work/client.log" || fail "client did not use TLS"
grep -q 'fingerprint matches trusted fingerprint' "$work/server.log" || fail "server did not check the client"
grep -q 'fingerprint matches trusted fingerprint' "$work/client.log" || fail "client did not check the server"
echo "e2e: client connected to server over TLS with trusted fingerprints"

kill -TERM "$client" "$server"
exited() {
  ! kill -0 "$client" 2>/dev/null && ! kill -0 "$server" 2>/dev/null
}
wait_for 15 exited || fail "server or client did not exit within 15s of SIGTERM"
echo "e2e: server and client shut down"
