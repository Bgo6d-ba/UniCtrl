#!/bin/sh
# Two-pass loudness normalisation of the rendered reel to -14 LUFS / -1 dBTP (what Reels plays back at).
# Usage: tools/master.sh out/unictrl-intro-reel.mp4 out/unictrl-intro-reel-master.mp4
set -eu
in=$1
out=$2
target="I=-14:TP=-1:LRA=11"
stats=$(ffmpeg -hide_banner -i "$in" -af "loudnorm=$target:print_format=json" -f null - 2>&1 | sed -n '/^{/,/^}/p')
get() { printf '%s' "$stats" | sed -n "s/.*\"$1\" : \"\(.*\)\".*/\1/p"; }
ffmpeg -hide_banner -loglevel error -y -i "$in" -c:v copy -c:a aac -b:a 192k -ar 48000 \
  -af "loudnorm=$target:measured_I=$(get input_i):measured_TP=$(get input_tp):measured_LRA=$(get input_lra):measured_thresh=$(get input_thresh):offset=$(get target_offset):linear=true" \
  "$out"
