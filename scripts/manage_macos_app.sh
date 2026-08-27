#!/bin/bash
set -euo pipefail

expected_id=io.github.gabeochoa.hanabi
lsregister=/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister
action=$1
source_app=${2:-output/Hanabi.app}
destination=${3:-$HOME/Applications/Hanabi.app}

bundle_id() {
    /usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$1/Contents/Info.plist" 2>/dev/null
}

require_hanabi() {
    [[ -d "$1" ]] || { echo "Hanabi.app not found: $1" >&2; exit 1; }
    [[ "$(bundle_id "$1")" == "$expected_id" ]] || {
        echo "refusing: $1 is not bundle $expected_id" >&2
        exit 1
    }
}

case "$action" in
    register)
        require_hanabi "$source_app"
        "$lsregister" -f "$source_app"
        ;;
    unregister)
        require_hanabi "$source_app"
        "$lsregister" -u "$source_app"
        ;;
    install)
        require_hanabi "$source_app"
        mkdir -p "$(dirname "$destination")"
        if [[ -e "$destination" ]]; then require_hanabi "$destination"; fi
        stage=$(mktemp -d "$(dirname "$destination")/.hanabi-install.XXXXXX")
        trap 'rm -rf "$stage"' EXIT
        ditto "$source_app" "$stage/Hanabi.app"
        codesign --verify --deep --strict "$stage/Hanabi.app"
        if [[ -e "$destination" ]]; then
            "$lsregister" -u "$destination" || true
            rm -rf "$destination"
        fi
        mv "$stage/Hanabi.app" "$destination"
        "$lsregister" -f "$destination"
        echo "installed $destination"
        ;;
    uninstall)
        if [[ ! -e "$destination" ]]; then
            echo "not installed: $destination"
            exit 0
        fi
        require_hanabi "$destination"
        "$lsregister" -u "$destination" || true
        rm -rf "$destination"
        echo "removed $destination"
        ;;
    *)
        echo "usage: $0 register|unregister|install|uninstall [source-app] [destination]" >&2
        exit 2
        ;;
esac
