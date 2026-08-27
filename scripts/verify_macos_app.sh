#!/bin/bash
set -euo pipefail

[[ $# -eq 2 ]] || { echo "usage: $0 app branding-plist" >&2; exit 2; }
app=$1
branding_plist=$2
plist="$app/Contents/Info.plist"

value() {
    /usr/libexec/PlistBuddy -c "Print :$1" "$branding_plist"
}
actual() {
    /usr/libexec/PlistBuddy -c "Print :$1" "$plist"
}

expected_name=$(value CFBundleDisplayName)
expected_id=$(value CFBundleIdentifier)
expected_executable=$(value CFBundleExecutable)
expected_url_name=$(value CFBundleURLTypes:0:CFBundleURLName)
expected_scheme=$(value CFBundleURLTypes:0:CFBundleURLSchemes:0)
exe="$app/Contents/MacOS/$expected_executable"

plutil -lint "$plist" >/dev/null
[[ "$(basename "$app")" == "$expected_name.app" ]]
[[ "$(actual CFBundleDisplayName)" == "$expected_name" ]]
[[ "$(actual CFBundleName)" == "$expected_name" ]]
[[ "$(actual CFBundleIdentifier)" == "$expected_id" ]]
[[ "$(actual CFBundleExecutable)" == "$expected_executable" ]]
[[ "$(actual CFBundleURLTypes:0:CFBundleURLName)" == "$expected_url_name" ]]
[[ "$(actual CFBundleURLTypes:0:CFBundleURLSchemes:0)" == "$expected_scheme" ]]
[[ -x "$exe" ]]
[[ -f "$app/Contents/Resources/fonts/Roboto-Regular.ttf" ]]
[[ ! -e "$app/Contents/Resources/macos/branding.json" ]]
codesign --verify --deep --strict "$app"
actual_sign_id=$(codesign -dv --verbose=4 "$app" 2>&1 | awk -F= '/^Identifier=/{print $2; exit}')
[[ "$actual_sign_id" == "$expected_id" ]]
runtime_status=$("$exe" --native-diagnostics)
[[ "$runtime_status" == bundle="$expected_id"* ]]
[[ "$runtime_status" == *"scheme=$expected_scheme"* ]]
[[ "$runtime_status" == *"notification-prefix=$expected_id.notification."* ]]
[[ "$runtime_status" == *"spotlight-domain=$expected_id.threads"* ]]
parsed=$("$exe" --parse-thread-url "$expected_scheme://thread/branding%20check?source=verify")
[[ "$parsed" == "branding check" ]]
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
printf 'bundle ok: name=%s id=%s executable=%s scheme=%s signature=ad-hoc resources=yes local-dependencies=none\n' \
    "$expected_name" "$expected_id" "$expected_executable" "$expected_scheme"
printf 'signing scope: local ad-hoc only; not Developer ID signed or notarized\n'
