#!/usr/bin/env python3
"""kt-huok: every Hanabi setting must appear in the Settings page -- or be
classified, explicitly, as something else.

The standing rule (Gabe, 2026-09-04): a preference the reader cannot see,
cannot find, and can only undo with a terminal command is not a setting --
it is a trap. The proof case was `restoreManyAnswer`, a
"don't ask again" answer with no way back. Hanabi's proof case already has
its way back: the quit modal's "Don't ask again" writes
`confirm_quit=false`, and Settings > General > "Ask before quitting with
Cmd+Q" re-enables it (menubar.mm).

The rule, as a gate: every key `Settings::write_save_file` persists must
appear in CLASS below, with one of:

  settings   -- a real preference: CONTROL names the catalog row id in
                src/ui/settings_catalog.h that owns it, and the row must
                be dispatched in src/ecs/settings_system.h (a catalog row
                that exists but never renders is not a control).
  ui_state   -- remembered UI state (window size, split layout, last view):
                toggled from the chrome itself, not a hidden preference.
  per_thread -- per-thread / per-turn state (stars, mutes, per-thread theme):
                set from rows, menus and interaction, not a global pref.
  account    -- mirrored from the account; not locally settable.
  internal   -- bookkeeping, not a user preference at all.

A key added to settings.json without a CLASS entry fails this gate --
that is the whole point: the audit cannot quietly go stale. A CONTROL
that names a row that does not exist, or is never dispatched, fails too.

Usage: python3 scripts/check_settings_parity.py [--selftest]
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# key -> (class, control-or-reason)
CLASS = {
    # --- settings: real preferences, each with its Settings-page row ---
    "theme": ("settings", "palette"),
    "theme_accent": ("settings", "accent"),
    "theme_highlight": ("settings", "highlight"),
    "font": ("settings", "font"),
    "font_weight": ("settings", "font_weight"),
    "background": ("settings", "background"),
    "background_dim": ("settings", "background_dim"),
    "icon_set": ("settings", "icon_set"),
    "user_font": ("settings", "user_font"),
    "assistant_font": ("settings", "assistant_font"),
    "user_text_scale": ("settings", "user_text_size"),
    "assistant_text_scale": ("settings", "assistant_text_size"),
    "line_spacing": ("settings", "line_spacing"),
    "transcript_width_choice": ("settings", "transcript_width"),
    "context_detail": ("settings", "context_detail"),
    "disclosure_chips_open": ("settings", "disclosure_chips"),
    "theme_rotate_secs": ("settings", "theme_rotate"),
    "restore_tabs": ("settings", "restore_tabs"),
    "confirm_quit": ("settings", "confirm_quit"),
    "escape_stops": ("settings", "escape_stops"),
    "inbox_triage": ("settings", "inbox_triage"),
    # Bindings are displayed in the inbox_triage row; the setter is unused
    # (no caller), so there is nothing a reader set that they cannot undo.
    "triage_bindings": ("settings", "inbox_triage"),
    "send_chords": ("settings", "send_chords"),
    "changed_file_rows": ("settings", "changed_file_rows"),
    "diff_cards": ("settings", "diff_cards"),
    "send_key": ("settings", "send_key"),
    "session_sort_order": ("settings", "sort_order"),
    "session_grouping": ("settings", "group_by"),
    "only_ungrouped": ("settings", "only_ungrouped"),
    "compress_runs": ("settings", "compress_runs"),
    "jump_to_latest": ("settings", "jump_latest"),
    "show_minimap": ("settings", "minimap"),
    "minimap_hidden_marks": ("settings", "minimap_marks"),
    "show_reasoning": ("settings", "reasoning"),
    "fold_long_messages": ("settings", "fold_long"),
    "show_date_dividers": ("settings", "date_dividers"),
    "show_finished_subagents": ("settings", "subagents"),
    "yap_level": ("settings", "yap"),
    "find_newest_first": ("settings", "find_newest"),
    "notifications_enabled": ("settings", "notify_show"),
    "notify_finished": ("settings", "notify_finished"),
    "notify_blocked": ("settings", "notify_blocked"),
    "notify_waiting": ("settings", "notify_waiting"),
    "notify_updates": ("settings", "notify_updates"),
    "notify_subagents": ("settings", "notify_subagents"),
    "notification_sound": ("settings", "notify_sound"),
    "run_chime": ("settings", "run_chime"),
    "quiet_start_minutes": ("settings", "quiet_hours"),
    "quiet_end_minutes": ("settings", "quiet_hours"),
    "send_usage_data": ("settings", "usage_data"),
    "export_dir": ("settings", "export"),
    "cache_cap_bytes": ("settings", "disk_cap"),
    "auto_archive_days": ("settings", "auto_archive"),
    "memory_backend": ("settings", "memory_backend"),
    "default_model": ("settings", "default_model"),
    "default_effort": ("settings", "default_effort"),
    "native_diff_companion": ("settings", "native_diffs"),
    "composer_templates": ("settings", "templates"),
    "shortcuts": ("settings", "command_chords"),
    "shortcuts_off": ("settings", "command_chords"),
    "global_shortcuts": ("settings", "global_chords"),
    "global_shortcuts_off": ("settings", "global_chords"),
    "capture_hotkey": ("settings", "global_chords"),
    "show_timestamps": ("settings", "timestamps"),
    "thread_colours": ("settings", "thread_colours"),
    # --- ui_state: remembered UI state, toggled from the chrome itself ---
    "window_width": ("ui_state", "window frame"),
    "window_height": ("ui_state", "window frame"),
    "sidebar_collapsed": ("ui_state", "sidebar collapse button"),
    # View zoom (Zoom In / Out / Actual palette commands), like a window
    # size: a view adjustment, not a hidden preference.
    "text_scale": ("ui_state", "Zoom commands"),
    "settings_pane": ("ui_state", "sheet reopens on its last pane"),
    "selected_view": ("ui_state", "last-selected view"),
    "split_open": ("ui_state", "split controls"),
    "split_ratio": ("ui_state", "split drag"),
    "split_focused_pane": ("ui_state", "split focus"),
    "split_panes": ("ui_state", "split controls"),
    "subagent_sidebar_open": ("ui_state", "sub-agent sidebar toggle"),
    "open_tabs": ("ui_state", "restored tabs"),
    "active_tab": ("ui_state", "restored tabs"),
    "pinned_tabs": ("ui_state", "restored tabs"),
    "tab_layout": ("ui_state", "restored tab layout"),
    "last_session": ("ui_state", "restored session"),
    "collapsed_shelves": ("ui_state", "sidebar shelves"),
    "acknowledged_blocked": ("ui_state", "dismissed blocked badges"),
    "expanded_parents": ("ui_state", "sidebar expansion"),
    "row_order": ("ui_state", "sidebar drag order"),
    # --- per_thread: per-thread / per-turn state, set from rows + menus ---
    "starred": ("per_thread", "star toggles on rows"),
    "archived": ("per_thread", "archive actions on rows"),
    "muted": ("per_thread", "mute actions on rows"),
    "session_theme": ("per_thread", "per-thread theme menu"),
    "sound_override": ("per_thread", "per-thread sound menu"),
    "tool_fold": ("per_thread", "tool-section folding"),
    "starred_turns": ("per_thread", "starred turns in transcripts"),
    "last_read": ("per_thread", "read position per thread"),
    "read_seq": ("per_thread", "read sequence per thread"),
    # --- account: mirrored from the account, not locally settable ---
    "account_autocompact": ("account", "server prefs + /autocompact"),
    # --- internal: bookkeeping, not a user preference ---
    "pins_migrated_to_web": ("internal", "one-time migration marker"),
}


def persisted_keys():
    src = open(os.path.join(ROOT, "src/settings.cpp"), encoding="utf-8").read()
    m = re.search(r"void Settings::write_save_file.*?bool Settings::export_to",
                  src, re.S)
    assert m, "write_save_file not found"
    return sorted(set(re.findall(r'j\["([a-z_0-9]+)"\]', m.group(0))))


def catalog_rows():
    cat = open(os.path.join(ROOT, "src/ui/settings_catalog.h"),
               encoding="utf-8").read()
    return set(re.findall(r'\{"([a-z_0-9]+)", Pane::', cat))


def dispatched_rows():
    sheet = open(os.path.join(ROOT, "src/ecs/settings_system.h"),
                 encoding="utf-8").read()
    return set(re.findall(r'id == "([a-z_0-9]+)"', sheet))


def check(persisted=None, rows=None, dispatched=None):
    persisted = persisted if persisted is not None else persisted_keys()
    rows = rows if rows is not None else catalog_rows()
    dispatched = dispatched if dispatched is not None else dispatched_rows()
    errors = []
    for key in persisted:
        if key not in CLASS:
            errors.append(f"unclassified persisted key: {key} "
                          f"(add it to CLASS in check_settings_parity.py)")
    for key in sorted(CLASS):
        if key not in persisted:
            errors.append(f"stale CLASS entry, no longer persisted: {key}")
    for key, (cls, control) in sorted(CLASS.items()):
        if cls != "settings":
            continue
        if control not in rows:
            errors.append(f"{key}: control row '{control}' not in "
                          f"settings_catalog.h kRows")
        elif control not in dispatched:
            errors.append(f"{key}: control row '{control}' in the catalog "
                          f"but never dispatched in settings_system.h")
    return errors


def main():
    if "--selftest" in sys.argv:
        # The checker must fail on an unclassified key and on a control row
        # that is not dispatched -- otherwise a future gap would read green.
        errs = check(persisted=["theme", "brand_new_hidden_key"],
                     rows={"palette"}, dispatched={"palette"})
        if not any("brand_new_hidden_key" in e for e in errs):
            print("selftest FAIL: unclassified key passed", file=sys.stderr)
            return 1
        errs = check(persisted=["theme"], rows={"palette"},
                     dispatched=set())
        if not any("never dispatched" in e for e in errs):
            print("selftest FAIL: undispatched row passed", file=sys.stderr)
            return 1
        errs = check()
        if errs:
            print("selftest FAIL: real tree has errors", file=sys.stderr)
            for e in errs:
                print(f"  {e}", file=sys.stderr)
            return 1
        print("check_settings_parity --selftest: PASS")
        return 0
    errs = check()
    if errs:
        print("check_settings_parity: FAIL", file=sys.stderr)
        for e in errs:
            print(f"  {e}", file=sys.stderr)
        return 1
    n_settings = sum(1 for c, _ in CLASS.values() if c == "settings")
    print(f"check_settings_parity: {len(CLASS)} persisted keys, "
          f"{n_settings} with a Settings-page row, "
          f"every key classified")
    return 0


if __name__ == "__main__":
    sys.exit(main())
