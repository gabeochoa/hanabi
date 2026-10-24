# Settings audit (kt-huok)

kt-huok is the standing rule from the `restoreManyAnswer`
incident (Gabe, 2026-09-04): **every setting must appear in the
Settings page -- no hidden defaults keys.** A preference the reader
cannot see, cannot find, and can only undo with a terminal command is
not a setting, it is a trap.

## Verdict on Hanabi

**No hidden keys. Zero (a)-class gaps** (a real user preference with no
Settings control and no other visible surface). All 96 keys persisted
in `settings.json` (`Settings::write_save_file`) are classified below.
The gate `scripts/check_settings_parity.py` (wired into
`scripts/source_checks.sh`) enforces it: a persisted key with no CLASS
entry, or a Settings-class key whose row does not exist or is never
dispatched, fails source-checks. The reverse gate already existed:
`scripts/check_settings_readers.py` (every setting has a reader
outside the sheet).

## The proof case, already closed on Hanabi

The proof case was a "Don't ask again" answer with no way back.
Hanabi's only "Don't ask again" is the quit modal
(`src/quit_confirmation.h`, `menubar.mm`): checking it writes
`confirm_quit=false`, and Settings > General > "Ask before quitting
with Cmd+Q" turns it back on. The modal is a settings write with a
Settings entry. No other "don't ask again" exists in Hanabi (grep:
`do not ask` / `don't ask` / `never ask` -- only the quit modal).

## What is NOT in the table, and why

- **macOS UserDefaults**: Hanabi reads two OS keys
  (`com.apple.swipescrolldirection`, `AppleInterfaceStyle`) -- the OS's
  own settings, read-only -- plus a Spotlight index manifest (index
  bookkeeping, not a preference). It writes no Hanabi preference to any
  defaults domain. There is no `com.meta.*` Hanabi domain to audit.
- **`HANABI_*` environment variables** (~287 `getenv` sites): launch-time
  debug/test knobs, not persisted settings. Not in the table for the
  same reason category-(c) debug flags belong in Experiment
  Labs rather than Settings -- except Hanabi has no Experiment Labs,
  and these are not settings at all: nothing persists them, and a
  reader cannot "set" one by accident.
- **Other files** (`sessions.json`, `drafts.json`, transcripts,
  `created_at.json`, outbox): data and per-thread state, not
  preferences.
- **Part 1 of the original knot** (`restoreManyAnswer = all` on juniper): a
  `com.meta.puffin` key on Gabe's machine. It needs Gabe's call, and no
  lane writes to his daily driver without his say-so. It is not a
  Hanabi key and this lane did not touch it.

## The list: all 96 persisted keys

`class` is the knot's classification: `settings` = a real preference
with a Settings-page row (third column names the catalog row id);
`ui_state` = remembered UI state, toggled from the chrome itself;
`per_thread` = per-thread / per-turn state, set from rows, menus and
interaction; `account` = mirrored from the account, not locally
settable; `internal` = bookkeeping, not a user preference.

| key | class | control / reason |
|---|---|---|
| `account_autocompact` | account | server prefs + /autocompact |
| `acknowledged_blocked` | ui_state | dismissed blocked badges |
| `active_tab` | ui_state | restored tabs |
| `archived` | per_thread | archive actions on rows |
| `assistant_font` | settings | assistant_font |
| `assistant_text_scale` | settings | assistant_text_size |
| `auto_archive_days` | settings | auto_archive |
| `background` | settings | background |
| `background_dim` | settings | background_dim |
| `cache_cap_bytes` | settings | disk_cap |
| `capture_hotkey` | settings | global_chords |
| `changed_file_rows` | settings | changed_file_rows |
| `collapsed_shelves` | ui_state | sidebar shelves |
| `composer_templates` | settings | templates |
| `compress_runs` | settings | compress_runs |
| `confirm_quit` | settings | confirm_quit |
| `context_detail` | settings | context_detail |
| `default_effort` | settings | default_effort |
| `default_model` | settings | default_model |
| `diff_cards` | settings | diff_cards |
| `disclosure_chips_open` | settings | disclosure_chips |
| `escape_stops` | settings | escape_stops |
| `expanded_parents` | ui_state | sidebar expansion |
| `export_dir` | settings | export |
| `find_newest_first` | settings | find_newest |
| `fold_long_messages` | settings | fold_long |
| `font` | settings | font |
| `font_weight` | settings | font_weight |
| `global_shortcuts` | settings | global_chords |
| `global_shortcuts_off` | settings | global_chords |
| `icon_set` | settings | icon_set |
| `inbox_triage` | settings | inbox_triage |
| `jump_to_latest` | settings | jump_latest |
| `last_read` | per_thread | read position per thread |
| `last_session` | ui_state | restored session |
| `line_spacing` | settings | line_spacing |
| `memory_backend` | settings | memory_backend |
| `minimap_hidden_marks` | settings | minimap_marks |
| `muted` | per_thread | mute actions on rows |
| `native_diff_companion` | settings | native_diffs |
| `notification_sound` | settings | notify_sound |
| `notifications_enabled` | settings | notify_show |
| `notify_blocked` | settings | notify_blocked |
| `notify_finished` | settings | notify_finished |
| `notify_subagents` | settings | notify_subagents |
| `notify_updates` | settings | notify_updates |
| `notify_waiting` | settings | notify_waiting |
| `only_ungrouped` | settings | only_ungrouped |
| `open_tabs` | ui_state | restored tabs |
| `pinned_tabs` | ui_state | restored tabs |
| `pins_migrated_to_web` | internal | one-time migration marker |
| `quiet_end_minutes` | settings | quiet_hours |
| `quiet_start_minutes` | settings | quiet_hours |
| `read_seq` | per_thread | read sequence per thread |
| `restore_tabs` | settings | restore_tabs |
| `row_order` | ui_state | sidebar drag order |
| `run_chime` | settings | run_chime |
| `selected_view` | ui_state | last-selected view |
| `send_chords` | settings | send_chords |
| `send_key` | settings | send_key |
| `send_usage_data` | settings | usage_data |
| `session_grouping` | settings | group_by |
| `session_sort_order` | settings | sort_order |
| `session_theme` | per_thread | per-thread theme menu |
| `settings_pane` | ui_state | sheet reopens on its last pane |
| `shortcuts` | settings | command_chords |
| `shortcuts_off` | settings | command_chords |
| `show_date_dividers` | settings | date_dividers |
| `show_finished_subagents` | settings | subagents |
| `show_minimap` | settings | minimap |
| `show_reasoning` | settings | reasoning |
| `show_timestamps` | settings | timestamps |
| `sidebar_collapsed` | ui_state | sidebar collapse button |
| `sound_override` | per_thread | per-thread sound menu |
| `split_focused_pane` | ui_state | split focus |
| `split_open` | ui_state | split controls |
| `split_panes` | ui_state | split controls |
| `split_ratio` | ui_state | split drag |
| `starred` | per_thread | star toggles on rows |
| `starred_turns` | per_thread | starred turns in transcripts |
| `subagent_sidebar_open` | ui_state | sub-agent sidebar toggle |
| `tab_layout` | ui_state | restored tab layout |
| `text_scale` | ui_state | Zoom commands |
| `theme` | settings | palette |
| `theme_accent` | settings | accent |
| `theme_highlight` | settings | highlight |
| `theme_rotate_secs` | settings | theme_rotate |
| `thread_colours` | settings | thread_colours |
| `tool_fold` | per_thread | tool-section folding |
| `transcript_width_choice` | settings | transcript_width |
| `triage_bindings` | settings | inbox_triage |
| `user_font` | settings | user_font |
| `user_text_scale` | settings | user_text_size |
| `window_height` | ui_state | window frame |
| `window_width` | ui_state | window frame |
| `yap_level` | settings | yap |

## Judgment calls, stated rather than buried

- `text_scale` (global zoom) is `ui_state`, not `settings`: it is a
  view adjustment, changed via the Zoom In / Out / Actual palette
  commands, in the same family as the window frame. The per-side text
  sizes *are* Settings rows (`user_text_size`,
  `assistant_text_size`).
- `triage_bindings` is `settings` via the `inbox_triage` row, which
  displays the bindings. Its setter has no caller -- the bindings are
  the defaults, displayed, and nothing a reader set can be lost.
- `account_autocompact` is `account`: it mirrors the account's prefs
  (read in `memory_system.h`) and surfaces through `/autocompact`. No
  local control hides anything; there is no local value to trap.
- `pins_migrated_to_web` is `internal`: a one-time migration marker.
  It is not a preference, and no reader action depends on it.
