#!/bin/bash
set -euo pipefail

[[ $# -eq 5 ]] || { echo "usage: $0 app binary resources plist version" >&2; exit 2; }
app=$1
binary=$2
resources=$3
plist=$4
version=$5

plist_value() {
    /usr/libexec/PlistBuddy -c "Print :$1" "$plist"
}

app_name=$(plist_value CFBundleDisplayName)
executable_name=$(plist_value CFBundleExecutable)
[[ "$app" == *.app && "$(basename "$app")" == "$app_name.app" ]] || {
    echo "bundle path must end in $app_name.app: $app" >&2
    exit 2
}
[[ -f "$binary" && -d "$resources" && -f "$plist" ]] || {
    echo "packaging input is missing" >&2
    exit 2
}

rm -rf "$app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
install -m 0755 "$binary" "$app/Contents/MacOS/$executable_name"
rsync -a --delete --exclude '/macos/branding.json' --exclude '/macos/Info.plist' \
    "$resources/" "$app/Contents/Resources/"
cp "$plist" "$app/Contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $version" "$app/Contents/Info.plist"

exe="$app/Contents/MacOS/$executable_name"
deps=$(otool -L "$exe" | awk 'NR > 1 {print $1}' | grep -E '/lib(ssl|crypto)\.[^/]*\.dylib$' || true)

if [[ -n "$deps" ]]; then
    mkdir -p "$app/Contents/Frameworks" "$app/Contents/Resources/licenses"
    while IFS= read -r dep; do
        [[ -n "$dep" ]] || continue
        cp -L "$dep" "$app/Contents/Frameworks/$(basename "$dep")"
        install_name_tool -change "$dep" "@rpath/$(basename "$dep")" "$exe"
    done <<< "$deps"
    for dylib in "$app"/Contents/Frameworks/*.dylib; do
        install_name_tool -id "@rpath/$(basename "$dylib")" "$dylib"
        while IFS= read -r dep; do
            case "$dep" in
                */libssl.*.dylib|*/libcrypto.*.dylib)
                    install_name_tool -change "$dep" "@rpath/$(basename "$dep")" "$dylib"
                    ;;
            esac
        done < <(otool -L "$dylib" | awk 'NR > 1 {print $1}')
        codesign --force --sign - --timestamp=none "$dylib"
    done
    install_name_tool -add_rpath '@executable_path/../Frameworks' "$exe"
    openssl_prefix=$(brew --prefix openssl@3 2>/dev/null || true)
    if [[ -n "$openssl_prefix" && -f "$openssl_prefix/LICENSE.txt" ]]; then
        cp "$openssl_prefix/LICENSE.txt" "$app/Contents/Resources/licenses/OpenSSL.txt"
    fi
fi

codesign --force --sign - --timestamp=none "$exe"
codesign --force --sign - --timestamp=none "$app"
codesign --verify --deep --strict "$app"
