# UI chrome visual evidence

Every changed screenshot state has a paired before/after PNG in this directory. Pixel percentages are diagnostic coverage, not quality scores.

| state | changed pixels | changed bounds | rationale |
|---|---:|---|---|
| `01_home_dark` | 89.97% | `(0, 0, 1100, 760)` | surface layers, navigation hierarchy, search, and section rhythm |
| `02_home_light` | 87.85% | `(0, 0, 1100, 760)` | surface layers, navigation hierarchy, search, and section rhythm |
| `03_transcript_dark` | 38.47% | `(0, 0, 1100, 760)` | compact titlebar, tab state hierarchy, and frame surfaces |
| `04_transcript_light` | 39.14% | `(0, 0, 1100, 760)` | compact titlebar, tab state hierarchy, and frame surfaces |
| `05_transcript_t6_dark` | 33.98% | `(0, 0, 1100, 760)` | compact titlebar, tab state hierarchy, and frame surfaces |
| `06_hover_row_star_dark` | 89.97% | `(0, 0, 1100, 760)` | theme-aware navigation, selection, attention, pin, hover, and rail states |
| `07_hover_tab_dark` | 38.71% | `(0, 0, 1100, 760)` | compact titlebar, tab state hierarchy, and frame surfaces |
| `07b_hover_msg_copy_dark` | 38.67% | `(0, 0, 1100, 760)` | shared frame surfaces and titlebar geometry; owned content renderer unchanged |
| `07c_hover_user_actions_dark` | 38.62% | `(0, 0, 1100, 760)` | shared frame surfaces and titlebar geometry; owned content renderer unchanged |
| `08_view_blocked_dark` | 90.38% | `(0, 0, 1100, 760)` | theme-aware navigation, selection, attention, pin, hover, and rail states |
| `09_view_review_dark` | 90.38% | `(0, 0, 1100, 760)` | theme-aware navigation, selection, attention, pin, hover, and rail states |
| `10_view_starred_dark` | 90.38% | `(0, 0, 1100, 760)` | theme-aware navigation, selection, attention, pin, hover, and rail states |
| `10b_view_pinned_row_dark` | 90.38% | `(0, 0, 1100, 760)` | theme-aware navigation, selection, attention, pin, hover, and rail states |
| `11_view_archived_dark` | 90.37% | `(0, 0, 1100, 760)` | theme-aware navigation, selection, attention, pin, hover, and rail states |
| `11b_view_archived_row_dark` | 90.38% | `(0, 0, 1100, 760)` | theme-aware navigation, selection, attention, pin, hover, and rail states |
| `12_view_blocked_light` | 86.39% | `(0, 0, 1100, 760)` | theme-aware navigation, selection, attention, pin, hover, and rail states |
| `13_chat_welcome_dark` | 87.50% | `(0, 0, 1100, 760)` | shared frame surfaces and titlebar geometry; owned content renderer unchanged |
| `13b_empty_transcript_dark` | 31.36% | `(0, 0, 1100, 760)` | shared frame surfaces and titlebar geometry; owned content renderer unchanged |
| `14_sidebar_folded_dark` | 19.23% | `(0, 0, 1100, 760)` | theme-aware navigation, selection, attention, pin, hover, and rail states |
| `15_settings_dark` | 0.01% | `(1094, 104, 1100, 333)` | shared frame surfaces and titlebar geometry; owned content renderer unchanged |
| `16_settings_light` | 0.01% | `(1094, 104, 1100, 333)` | shared frame surfaces and titlebar geometry; owned content renderer unchanged |
| `17_newtask_dark` | 0.01% | `(1094, 104, 1100, 333)` | shared frame surfaces and titlebar geometry; owned content renderer unchanged |
| `17b_shortcuts_dark` | 0.01% | `(1094, 104, 1100, 333)` | shared frame surfaces and titlebar geometry; owned content renderer unchanged |
| `17c_shortcuts_light` | 0.01% | `(1094, 104, 1100, 333)` | shared frame surfaces and titlebar geometry; owned content renderer unchanged |
| `18_auth_dark` | 0.01% | `(1094, 104, 1100, 333)` | shared frame surfaces and titlebar geometry; owned content renderer unchanged |
| `19_many_tabs_dark` | 34.08% | `(0, 0, 1100, 760)` | compact titlebar, tab state hierarchy, and frame surfaces |
| `20_big_transcript_dark` | 31.41% | `(0, 0, 1100, 760)` | shared frame surfaces and titlebar geometry; owned content renderer unchanged |
| `21_tools_expanded_dark` | 39.20% | `(0, 0, 1100, 760)` | shared frame surfaces and titlebar geometry; owned content renderer unchanged |
| `21b_tools_expanded_detail_dark` | 42.61% | `(0, 0, 1100, 760)` | shared frame surfaces and titlebar geometry; owned content renderer unchanged |
| `22_split_view_dark` | 38.30% | `(0, 0, 1100, 760)` | compact titlebar, tab state hierarchy, and frame surfaces |
| `23_skeleton_dark` | 74.93% | `(0, 0, 1100, 760)` | shared frame surfaces and titlebar geometry; owned content renderer unchanged |
| `24_thread_loading_dark` | 90.38% | `(0, 0, 1100, 760)` | shared frame surfaces and titlebar geometry; owned content renderer unchanged |
| `25_load_older_dark` | 39.10% | `(0, 0, 1100, 760)` | shared frame surfaces and titlebar geometry; owned content renderer unchanged |
| `26_thinking_dark` | 40.34% | `(0, 0, 1100, 760)` | shared frame surfaces and titlebar geometry; owned content renderer unchanged |
| `27_streaming_dark` | 40.71% | `(0, 0, 1100, 760)` | shared frame surfaces and titlebar geometry; owned content renderer unchanged |
| `28_composer_focus_dark` | 38.47% | `(0, 0, 1100, 760)` | shared frame surfaces and titlebar geometry; owned content renderer unchanged |
| `29_find_dark` | 38.95% | `(0, 0, 1100, 760)` | shared frame surfaces and titlebar geometry; owned content renderer unchanged |
| `30_find_light` | 39.61% | `(0, 0, 1100, 760)` | shared frame surfaces and titlebar geometry; owned content renderer unchanged |
| `31_selection_dark` | 38.62% | `(0, 0, 1100, 760)` | shared frame surfaces and titlebar geometry; owned content renderer unchanged |
| `32_new_messages_dark` | 32.13% | `(0, 0, 1100, 760)` | shared frame surfaces and titlebar geometry; owned content renderer unchanged |
| `33_narrow_dark` | 52.52% | `(0, 0, 760, 620)` | new narrow-width coverage for frame and tab overflow |
| `34_narrow_many_tabs_dark` | 46.34% | `(0, 0, 760, 620)` | new narrow-width coverage for frame and tab overflow |

## decisions

1. The titlebar is 56 px instead of 67 px, recovering 11 px of vertical workspace while keeping a 32 px tab target.
2. Dark and light chrome each use four ordered surfaces; content-owned tokens remain unchanged.
3. Inactive tab outlines and the sidebar section rule are removed; hierarchy comes from surface, type, and spacing.
4. Sidebar navigation and thread titles use 13 px roles; page titles remain 20 px and metadata stays 10.5–12.5 px.
5. Search and filter share one outlined control; selected navigation uses an accent tint; status colors remain semantic.

## unchanged owned areas

Transcript message/tool rendering, composer internals, settings/palette sheet internals, native packaging, and `vendor/afterhours` were not edited.
