#!/usr/bin/env bash
cd "$(dirname "$0")/.."
git ls-files | grep "src" | grep -v "resources" | grep -v "vendor" | xargs wc -l | sort -rn
