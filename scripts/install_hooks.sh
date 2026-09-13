#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")/.."
dir="$(git rev-parse --git-common-dir)/hooks"
mkdir -p "$dir"
cp scripts/hooks/pre-push "$dir/pre-push"
chmod +x "$dir/pre-push"
echo "installed $dir/pre-push -- 'git push' now runs 'zig build gate'"
echo "skip it for one push with: git push --no-verify"
