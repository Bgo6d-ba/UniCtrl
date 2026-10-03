#!/bin/bash
# Builds a self-contained, ad-hoc signed UniCtrl.app and packs it into a DMG (Apple Silicon).
# Does NOT launch the GUI (that would auto-start the core and trigger macOS permission prompts).
set -uo pipefail

REPO=$(git rev-parse --show-toplevel)
SP=${TMPDIR:-/tmp}/deskflow-dmg-work; mkdir -p "$SP"
QT=/opt/homebrew/opt/qtbase
ST=$SP/mac-stage
A=$ST/UniCtrl.app
F=$A/Contents/Frameworks
LABEL=${1:-hostjoin}

cd "$REPO"
echo "== build (Release)"
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DSKIP_BUILD_TESTS=ON >/dev/null
cmake --build build-release -j10 2>&1 | grep -E 'error|warning:|Built target Deskflow$' | head -20

echo "== stage bundle"
rm -rf "$ST" && mkdir -p "$ST"
cp -R build-release/bin/UniCtrl.app "$ST/"
chmod -R u+w "$A"

echo "== macdeployqt (its QtSvg/codesign complaints are expected and fixed below)"
"$QT/bin/macdeployqt" "$A" -executable="$A/Contents/MacOS/unictrl-core" \
  -libpath=/opt/homebrew/opt/qtsvg/lib -libpath=/opt/homebrew/lib >/dev/null 2>&1 || true
chmod -R u+w "$A"

echo "== add QtSvg"
ditto /opt/homebrew/opt/qtsvg/lib/QtSvg.framework "$F/QtSvg.framework"
rm -rf "$F/QtSvg.framework/Headers" "$F/QtSvg.framework/Versions/A/Headers"

echo "== rewrite install names and rpaths"
find "$A" -type f -print0 | xargs -0 file | grep Mach-O | cut -d: -f1 | while read -r f; do
  id=$(otool -D "$f" 2>/dev/null | sed -n 2p)
  if echo "$id" | grep -qE '^/opt/homebrew|^/usr/local'; then
    if echo "$id" | grep -q '\.framework/'; then new="@rpath/${id#*/lib/}"; else new="@rpath/$(basename "$id")"; fi
    install_name_tool -id "$new" "$f" 2>/dev/null || true
  fi
  otool -L "$f" | awk 'NR>1{print $1}' | grep -E '^/opt/homebrew|^/usr/local' | while read -r dep; do
    if echo "$dep" | grep -q '\.framework/'; then
      fw=$(echo "$dep" | sed -E 's#.*/([^/]+\.framework/.*)#\1#'); tgt="@executable_path/../Frameworks/$fw"; chk="$F/$fw"
    else
      b=$(basename "$dep"); tgt="@executable_path/../Frameworks/$b"; chk="$F/$b"
    fi
    [ -e "$chk" ] || echo "MISSING dependency in bundle: $chk"
    install_name_tool -change "$dep" "$tgt" "$f" 2>/dev/null || true
  done
  otool -l "$f" | awk '/LC_RPATH/{getline;getline;print $2}' | grep -E '^/opt/homebrew|^/usr/local|^/Users/' | while read -r rp; do
    install_name_tool -delete_rpath "$rp" "$f" 2>/dev/null || true
  done
done

echo "== verify no host paths remain"
bad=$(find "$A" -type f -print0 | xargs -0 file | grep Mach-O | cut -d: -f1 | while read -r f; do otool -L "$f" | grep -E '/opt/homebrew|/usr/local' | sed "s#^#$(basename "$f"): #"; done | sort -u)
[ -z "$bad" ] || { echo "HOST PATHS LEFT:"; echo "$bad"; exit 1; }
unres=$(find "$A" -type f -print0 | xargs -0 file | grep Mach-O | cut -d: -f1 | while read -r f; do otool -L "$f" | grep -oE '@rpath/[^ ]+' ; done | sort -u | while read -r r; do [ -e "$F/${r#@rpath/}" ] || echo "UNRESOLVED $r"; done)
[ -z "$unres" ] || { echo "$unres"; exit 1; }
for p in platforms/libqcocoa.dylib iconengines/libqsvgicon.dylib; do [ -e "$A/Contents/PlugIns/$p" ] || { echo "MISSING plugin $p"; exit 1; }; done

echo "== sign (ad-hoc) + verify"
codesign --force --deep --sign - "$A" 2>&1 | tail -1
codesign --verify --deep --strict "$A" || exit 1
"$A/Contents/MacOS/unictrl-core" --version | head -1

echo "== pack DMG"
VER=$("$A/Contents/MacOS/unictrl-core" --version | head -1 | sed -E 's/^unictrl-core v([0-9.]+).*/\1/')
OUT="$REPO/build-release/unictrl-$VER-$LABEL-macos-arm64.dmg"
D=$SP/dmg-src; rm -rf "$D" && mkdir -p "$D"
ditto "$A" "$D/UniCtrl.app"; ln -s /Applications "$D/Applications"
rm -f "$OUT"; hdiutil create -volname UniCtrl -srcfolder "$D" -ov -format UDZO "$OUT" >/dev/null || exit 1
M=$SP/dmgtest; mkdir -p "$M"
hdiutil attach "$OUT" -nobrowse -readonly -mountpoint "$M" >/dev/null
codesign --verify --deep --strict "$M/UniCtrl.app" && echo "signature OK inside DMG" || exit 1
"$M/UniCtrl.app/Contents/MacOS/unictrl-core" --version | head -1
hdiutil detach "$M" >/dev/null
ls -la "$OUT"; shasum -a 256 "$OUT"
echo "DMG=$OUT"
