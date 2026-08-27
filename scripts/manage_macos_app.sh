#!/bin/bash
set -euo pipefail

[[ $# -eq 4 ]] || {
    echo "usage: $0 register|unregister|install|uninstall source-app destination branding-plist" >&2
    exit 2
}
action=$1
source_app=$2
destination=$3
branding_plist=$4
lsregister=/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister

plist_value() {
    /usr/libexec/PlistBuddy -c "Print :$2" "$1/Contents/Info.plist" 2>/dev/null
}
branding_value() {
    /usr/libexec/PlistBuddy -c "Print :$1" "$branding_plist"
}

expected_id=$(branding_value CFBundleIdentifier)
expected_name=$(branding_value CFBundleDisplayName)

require_expected() {
    [[ -d "$1" ]] || { echo "$expected_name.app not found: $1" >&2; exit 1; }
    [[ "$(plist_value "$1" CFBundleIdentifier)" == "$expected_id" ]] || {
        echo "refusing: $1 is not bundle $expected_id" >&2
        exit 1
    }
    [[ "$(plist_value "$1" CFBundleDisplayName)" == "$expected_name" ]] || {
        echo "refusing: $1 is not app $expected_name" >&2
        exit 1
    }
}

case "$action" in
    register)
        require_expected "$source_app"
        "$lsregister" -f "$source_app"
        ;;
    unregister)
        require_expected "$source_app"
        "$lsregister" -u "$source_app"
        ;;
    install)
        require_expected "$source_app"
        [[ "$destination" == *.app ]] || { echo "install destination must end in .app" >&2; exit 2; }
        mkdir -p "$(dirname "$destination")"
        if [[ -e "$destination" ]]; then require_expected "$destination"; fi
        stage=$(mktemp -d "$(dirname "$destination")/.app-install.XXXXXX")
        trap 'rm -rf "$stage"' EXIT
        staged_app="$stage/$(basename "$destination")"
        ditto "$source_app" "$staged_app"
        codesign --verify --deep --strict "$staged_app"
        if [[ -e "$destination" ]]; then
            "$lsregister" -u "$destination" || true
            rm -rf "$destination"
        fi
        mv "$staged_app" "$destination"
        "$lsregister" -f "$destination"
        echo "installed $destination"
        ;;
    uninstall)
        if [[ ! -e "$destination" ]]; then
            echo "not installed: $destination"
            exit 0
        fi
        require_expected "$destination"
        "$lsregister" -u "$destination" || true
        rm -rf "$destination"
        echo "removed $destination"
        ;;
    *)
        echo "usage: $0 register|unregister|install|uninstall source-app destination branding-plist" >&2
        exit 2
        ;;
esac
