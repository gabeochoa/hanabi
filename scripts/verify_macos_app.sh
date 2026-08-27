#!/bin/bash
set -euo pipefail

app=${1:-output/Hanabi.app}
expected_id=io.github.gabeochoa.hanabi
plist="$app/Contents/Info.plist"
exe="$app/Contents/MacOS/hanabi"

plutil -lint "$plist" >/dev/null
[[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$plist")" == "$expected_id" ]]
[[ -x "$exe" ]]
[[ -f "$app/Contents/Resources/fonts/Roboto-Regular.ttf" ]]
codesign --verify --deep --strict "$app"
actual_sign_id=$(codesign -dv --verbose=4 "$app" 2>&1 | awk -F= '/^Identifier=/{print $2; exit}')
[[ "$actual_sign_id" == "$expected_id" ]]
runtime_status=$("$exe" --native-diagnostics)
[[ "$runtime_status" == bundle="$expected_id"* ]]
for payload in "$exe" "$app"/Contents/Frameworks/*.dylib; do
    [[ -e "$payload" ]] || continue
    if otool -L "$payload" | awk 'NR > 1 {print $1}' | grep -Eq '^(/opt/homebrew|/usr/local|/Users/)'; then
        echo "bundle has a machine-local runtime dependency in $payload" >&2
        otool -L "$payload" >&2
        exit 1
    fi
done
if otool -L "$exe" | grep -q 'libssl'; then
    [[ -f "$app/Contents/Frameworks/libssl.3.dylib" ]]
    [[ -f "$app/Contents/Frameworks/libcrypto.3.dylib" ]]
fi
printf 'bundle ok: id=%s signature=ad-hoc resources=yes local-dependencies=none\n' "$expected_id"
printf 'signing scope: local ad-hoc only; not Developer ID signed or notarized\n'
