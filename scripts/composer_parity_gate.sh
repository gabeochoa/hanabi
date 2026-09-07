#!/usr/bin/env bash
# ===========================================================================
# scripts/composer_parity_gate.sh -- ONE composer, and it stays one.
#
# WHY THIS EXISTS. hanabi shipped two composers: the in-pane one and a modal
# "New task" sheet with its own field, its own draft and its own Start. By the
# time anyone counted, they were twelve affordances apart -- the sheet had no
# slash menu, no history walk, no per-file attachment chips, no send-key rule,
# no refusal caption -- and every one of those gaps was invisible to the test
# suite, because each composer passed its own tests.
#
# The sheet is gone. This is what stops the next one: the drift is silent by
# construction, so the guard has to be a BUILD STEP rather than a test anyone
# has to remember to write.
#
# WHAT IT ENFORCES, each with a stated reason per allowed site:
#   1. Exactly one composer text field in src/ -- one `text_area(` call site.
#   2. A closed set of api::attachments::stage callers, so a second staging
#      route cannot skip the size caps, the type allowlist or the retention.
#   3. A closed set of AppComponent::requestKickoff setters, so a second
#      create path cannot skip the outcome verdict and lose a draft.
#   4. Exactly one servicer of requestNewThread -- the entry points must all
#      arrive at one place that decides what "new thread" means.
#   5. ecs/composer_system.h stays deleted.
#
# A GUARD IS EVIDENCE ONLY IF A PLANTED DEFECT MAKES IT FAIL. `--selftest`
# builds a fake tree, plants one defect per rule, and requires each to be
# caught -- plus a clean tree that must pass. A gate whose self-test does not
# run is a gate nobody has shown can see.
# ===========================================================================
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# One allowed site per line: "<path>|<reason>". A site with no reason is a
# failure even when the path is right -- "why is this allowed" has to survive
# the person who added it.
COMPOSER_FIELDS="src/ecs/main_pane_system.h|the one composer; every New Thread entry point reaches this field"

STAGE_CALLERS="src/ecs/attachment_intake_system.h|the one intake: picker, paste and drop all drain here
src/ecs/main_pane_system.h|HANABI_ATTACH_DEMO, a screenshot hook that stages a fixture and never runs unset
src/main.cpp|HANABI_MEMLADDER, the memory-ladder diagnostic"

KICKOFF_SETTERS="src/ecs/main_pane_system.h|the composer's own submit and its slash router
src/ecs/e2e_commands.h|the scripted-UI harness, which drives the same submit
src/main.cpp|HANABI_KICKOFF_DEMO, a screenshot hook"

NEW_THREAD_SERVICERS="src/ecs/new_thread.h|the one place that decides what New Thread means"

# A draft is text OR staged files, everywhere. The first version of the escape
# rule took one string, so a composer holding files and no words answered
# "empty" and the empty arm CLOSED the surface over them. Two shapes hold that
# shut: the rule has to ASK about attachments, and the one call site has to
# hand them over.
ESCAPE_MODEL="src/ecs/composer_escape.h|the escape rule asks whether files are staged, not only whether text is"
ESCAPE_CALLERS="src/ecs/main_pane_system.h|the one composer builds the one escape input"

fail_count=0
note() { printf '  %s\n' "$*"; }
fail() { printf '  FAIL: %s\n' "$*"; fail_count=$((fail_count + 1)); }

# Which files under $1 contain a match for pattern $2, one path per line,
# relative to $1. Comment-only mentions do not count: a rule about call sites
# that a sentence can trip is a rule that gets worked around by rewording.
matching_files() {
    local root="$1" pattern="$2"
    ( cd "$root" && grep -rln --include='*.h' --include='*.cpp' --include='*.mm' \
        -e "$pattern" src 2>/dev/null | LC_ALL=C sort )
}

check_closed_set() {
    local root="$1" label="$2" pattern="$3" allowed="$4"
    local found allow_paths=""
    found="$(matching_files "$root" "$pattern")"
    while IFS= read -r line; do
        [ -n "$line" ] || continue
        local path="${line%%|*}" reason="${line#*|}"
        if [ -z "$reason" ] || [ "$reason" = "$path" ]; then
            fail "$label: '$path' is on the list but says nowhere why"
        fi
        allow_paths="$allow_paths$path
"
    done <<EOF
$allowed
EOF
    local path
    while IFS= read -r path; do
        [ -n "$path" ] || continue
        if ! printf '%s' "$allow_paths" | grep -qxF "$path"; then
            fail "$label: '$path' is not on the list"
        fi
    done <<EOF
$found
EOF
    while IFS= read -r line; do
        [ -n "$line" ] || continue
        local p="${line%%|*}"
        if ! printf '%s' "$found" | grep -qxF "$p"; then
            fail "$label: '$p' is on the list but has no such call site any more"
        fi
    done <<EOF
$allowed
EOF
}

scan() {
    local root="$1"
    check_closed_set "$root" "composer field" 'text_area(' "$COMPOSER_FIELDS"
    check_closed_set "$root" "attachment staging" 'attachments::stage(' \
        "$STAGE_CALLERS"
    check_closed_set "$root" "create path" 'requestKickoff = ' \
        "$KICKOFF_SETTERS"
    # The member ACCESS, not the declaration: components.h owns the field and
    # is not a servicer of it.
    check_closed_set "$root" "new-thread servicer" \
        '[.>]requestNewThread = false' "$NEW_THREAD_SERVICERS"
    check_closed_set "$root" "escape content model" \
        'in\.attachments\.empty()' "$ESCAPE_MODEL"
    check_closed_set "$root" "escape input caller" \
        'escapeIn\.attachments\.push_back' "$ESCAPE_CALLERS"
    if [ -e "$root/src/ecs/composer_system.h" ]; then
        fail "the second composer is back: src/ecs/composer_system.h exists"
    fi
}

# --- the self-test ---------------------------------------------------------
# A fake tree with exactly the call sites the real one has, then one planted
# defect at a time. Each plant must make the scan fail, and the clean tree must
# make it pass; either half missing means the gate proves nothing.
plant_tree() {
    local dir="$1"
    mkdir -p "$dir/src/ecs"
    cat > "$dir/src/ecs/main_pane_system.h" <<'EOF'
auto inputRes = afterhours::ui::imm::text_area(ctx, mk(row, 1), draft);
auto staged = api::attachments::stage(a);
app.requestKickoff = std::move(message);
escapeIn.attachments.push_back(staged.name);
EOF
    cat > "$dir/src/ecs/attachment_intake_system.h" <<'EOF'
auto staged = api::attachments::stage(path);
EOF
    cat > "$dir/src/ecs/e2e_commands.h" <<'EOF'
app->requestKickoff = ecs::model::snapshot_outgoing(text);
EOF
    cat > "$dir/src/ecs/new_thread.h" <<'EOF'
app->requestNewThread = false;
EOF
    cat > "$dir/src/ecs/composer_escape.h" <<'EOF'
if (!in.attachments.empty()) return ComposerEscapeStep::KeptStaged;
EOF
    cat > "$dir/src/main.cpp" <<'EOF'
auto staged = api::attachments::stage(png);
app->requestKickoff = api::attachments::outgoing(d, {}, target);
EOF
}

selftest() {
    local tmp rc plants=0 caught=0
    tmp="$(mktemp -d)"
    trap 'rm -rf "${tmp:-}"' EXIT

    plant_tree "$tmp/clean"
    fail_count=0
    scan "$tmp/clean"
    if [ "$fail_count" -ne 0 ]; then
        printf 'SELFTEST FAIL: the clean tree does not pass (%d)\n' "$fail_count"
        return 1
    fi
    note "selftest: clean tree passes"

    # 1. A second composer field.
    # 2. A rogue staging route.
    # 3. A rogue create path.
    # 4. A second servicer of the new-thread request.
    # 5. The deleted sheet comes back.
    local names=("a second composer field" "a rogue attachment stage" \
                 "a rogue create path" "a second new-thread servicer" \
                 "the modal composer returning" \
                 "the escape rule going text-only" \
                 "the escape input dropping its attachments")
    local i
    for i in 0 1 2 3 4 5 6; do
        rm -rf "$tmp/plant"
        plant_tree "$tmp/plant"
        case "$i" in
            0) echo 'auto f = afterhours::ui::imm::text_area(ctx, k, d);' \
                   > "$tmp/plant/src/ecs/sidebar_system.h" ;;
            1) echo 'auto s = api::attachments::stage(p);' \
                   > "$tmp/plant/src/ecs/sidebar_system.h" ;;
            2) echo 'app.requestKickoff = std::move(m);' \
                   > "$tmp/plant/src/ecs/sidebar_system.h" ;;
            3) echo 'app->requestNewThread = false;' \
                   > "$tmp/plant/src/ecs/sidebar_system.h" ;;
            4) echo '// the sheet' > "$tmp/plant/src/ecs/composer_system.h" ;;
            5) echo 'if (!in.text.empty()) return ComposerEscapeStep::Armed;' \
                   > "$tmp/plant/src/ecs/composer_escape.h" ;;
            6) echo 'app.requestKickoff = std::move(m);' \
                   > "$tmp/plant/src/ecs/main_pane_system.h" ;;
        esac
        plants=$((plants + 1))
        fail_count=0
        scan "$tmp/plant" > /dev/null 2>&1
        if [ "$fail_count" -gt 0 ]; then
            caught=$((caught + 1))
            note "selftest: caught ${names[$i]}"
        else
            printf 'SELFTEST FAIL: missed %s\n' "${names[$i]}"
        fi
    done

    rc=0
    [ "$caught" -eq "$plants" ] || rc=1
    printf 'selftest: %d/%d planted defects caught\n' "$caught" "$plants"
    return "$rc"
}

if [ "${1:-}" = "--selftest" ]; then
    echo "=== composer parity gate: self-test ==="
    if selftest; then
        echo "PASS: the gate sees every planted defect"
        exit 0
    fi
    echo "FAIL: the gate is blind to at least one planted defect"
    exit 1
fi

echo "=== composer parity gate ==="
fail_count=0
scan "$ROOT"
if [ "$fail_count" -eq 0 ]; then
    echo "PASS: one composer, one staging route, one create path, one servicer"
    exit 0
fi
printf 'FAIL: %d composer-parity violation(s)\n' "$fail_count"
exit 1
