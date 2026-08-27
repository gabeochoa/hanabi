# UI chrome visual evidence

Each of the 70 merged screenshot states has a paired before/after PNG in this directory. Before is current main `4051207`; after is the semantically merged UI-chrome stack. Pixel percentages are diagnostic coverage, not quality scores.

| state | changed pixels | changed bounds | rationale |
|---|---:|---|---|
| `01_home_dark` | 89.87% | `(0, 0, 1100, 760)` | surface layers, navigation hierarchy, search, and section rhythm |
| `02_home_light` | 85.72% | `(0, 0, 1100, 760)` | surface layers, navigation hierarchy, search, and section rhythm |
| `03_transcript_dark` | 31.90% | `(0, 0, 1100, 760)` | titlebar layers and tab visual-state hierarchy; landed behavior retained |
| `04_transcript_light` | 31.91% | `(0, 0, 1100, 760)` | titlebar layers and tab visual-state hierarchy; landed behavior retained |
| `05_transcript_t6_dark` | 31.90% | `(0, 0, 1100, 760)` | titlebar layers and tab visual-state hierarchy; landed behavior retained |
| `06_hover_row_star_dark` | 89.87% | `(0, 0, 1100, 760)` | theme-aware navigation, selection, attention, pin, hover, and narrow frame states |
| `07_hover_tab_dark` | 31.90% | `(0, 0, 1100, 760)` | titlebar layers and tab visual-state hierarchy; landed behavior retained |
| `07b_hover_msg_copy_dark` | 31.90% | `(0, 0, 1100, 760)` | shared frame surfaces; owned transcript/composer/secondary renderer retained |
| `07c_hover_user_actions_dark` | 31.90% | `(0, 0, 1100, 760)` | shared frame surfaces; owned transcript/composer/secondary renderer retained |
| `08_view_blocked_dark` | 90.14% | `(0, 0, 1100, 760)` | theme-aware navigation, selection, attention, pin, hover, and narrow frame states |
| `09_view_review_dark` | 90.19% | `(0, 0, 1100, 760)` | theme-aware navigation, selection, attention, pin, hover, and narrow frame states |
| `10_view_starred_dark` | 90.26% | `(0, 0, 1100, 760)` | theme-aware navigation, selection, attention, pin, hover, and narrow frame states |
| `10b_view_pinned_row_dark` | 90.25% | `(0, 0, 1100, 760)` | theme-aware navigation, selection, attention, pin, hover, and narrow frame states |
| `11_view_archived_dark` | 90.25% | `(0, 0, 1100, 760)` | theme-aware navigation, selection, attention, pin, hover, and narrow frame states |
| `11b_view_archived_row_dark` | 90.25% | `(0, 0, 1100, 760)` | theme-aware navigation, selection, attention, pin, hover, and narrow frame states |
| `12_view_blocked_light` | 90.25% | `(0, 0, 1100, 760)` | theme-aware navigation, selection, attention, pin, hover, and narrow frame states |
| `13_chat_welcome_dark` | 85.17% | `(0, 0, 1100, 760)` | shared frame surfaces; owned transcript/composer/secondary renderer retained |
| `13a_chat_welcome_light` | 85.16% | `(0, 0, 1100, 760)` | landed secondary surface retained over the new frame layers |
| `13b_empty_transcript_dark` | 31.90% | `(0, 0, 1100, 760)` | shared frame surfaces; owned transcript/composer/secondary renderer retained |
| `13c_empty_transcript_light` | 31.91% | `(0, 0, 1100, 760)` | landed secondary surface retained over the new frame layers |
| `14_sidebar_folded_dark` | 12.98% | `(0, 0, 1100, 760)` | theme-aware navigation, selection, attention, pin, hover, and narrow frame states |
| `15_settings_dark` | 0.00% | `(1094, 328, 1100, 333)` | shared frame surfaces; owned transcript/composer/secondary renderer retained |
| `16_settings_light` | 0.00% | `(1094, 328, 1100, 333)` | shared frame surfaces; owned transcript/composer/secondary renderer retained |
| `17_newtask_dark` | 0.00% | `(1094, 328, 1100, 333)` | shared frame surfaces; owned transcript/composer/secondary renderer retained |
| `17b_shortcuts_dark` | 0.00% | `(1094, 328, 1100, 333)` | shared frame surfaces; owned transcript/composer/secondary renderer retained |
| `17c_shortcuts_light` | 0.00% | `(1094, 328, 1100, 333)` | shared frame surfaces; owned transcript/composer/secondary renderer retained |
| `17d_newtask_light` | 0.00% | `(1094, 328, 1100, 333)` | landed secondary surface retained over the new frame layers |
| `18_auth_dark` | 0.00% | `(1094, 328, 1100, 333)` | landed secondary surface retained over the new frame layers |
| `18a_auth_light` | 0.00% | `(1094, 328, 1100, 333)` | landed secondary surface retained over the new frame layers |
| `18b_palette_dark` | 0.00% | `(1094, 328, 1100, 333)` | landed secondary surface retained over the new frame layers |
| `18c_palette_light` | 0.00% | `(1094, 328, 1100, 333)` | landed secondary surface retained over the new frame layers |
| `18d_palette_empty_dark` | 0.00% | `(1094, 328, 1100, 333)` | landed secondary surface retained over the new frame layers |
| `18e_palette_empty_light` | 0.00% | `(1094, 328, 1100, 333)` | landed secondary surface retained over the new frame layers |
| `18f_search_dark` | 0.00% | `(1094, 328, 1100, 333)` | landed secondary surface retained over the new frame layers |
| `18g_search_light` | 0.00% | `(1094, 328, 1100, 333)` | landed secondary surface retained over the new frame layers |
| `18h_search_empty_dark` | 0.00% | `(1094, 328, 1100, 333)` | landed secondary surface retained over the new frame layers |
| `18i_search_empty_light` | 0.00% | `(1094, 328, 1100, 333)` | landed secondary surface retained over the new frame layers |
| `18j_model_picker_dark` | 33.81% | `(0, 0, 1100, 760)` | landed secondary surface retained over the new frame layers |
| `18k_model_picker_light` | 33.83% | `(0, 0, 1100, 760)` | landed secondary surface retained over the new frame layers |
| `18l_effort_picker_dark` | 33.87% | `(0, 0, 1100, 760)` | landed secondary surface retained over the new frame layers |
| `18m_effort_picker_light` | 33.89% | `(0, 0, 1100, 760)` | landed secondary surface retained over the new frame layers |
| `18n_slash_menu_dark` | 31.90% | `(0, 0, 1100, 760)` | landed secondary surface retained over the new frame layers |
| `18o_slash_menu_light` | 31.91% | `(0, 0, 1100, 760)` | landed secondary surface retained over the new frame layers |
| `18p2_tab_menu_dark` | 31.90% | `(0, 0, 1100, 760)` | landed secondary surface retained over the new frame layers |
| `18p_context_menu_dark` | 86.81% | `(0, 0, 1100, 760)` | landed secondary surface retained over the new frame layers |
| `18q2_tab_menu_light` | 31.91% | `(0, 0, 1100, 760)` | landed secondary surface retained over the new frame layers |
| `18q_context_menu_light` | 82.92% | `(0, 0, 1100, 760)` | landed secondary surface retained over the new frame layers |
| `18r_toast_dark` | 89.87% | `(0, 0, 1100, 760)` | landed secondary surface retained over the new frame layers |
| `18s_toast_light` | 85.72% | `(0, 0, 1100, 760)` | landed secondary surface retained over the new frame layers |
| `18t_auth_failed_dark` | 0.00% | `(1094, 328, 1100, 333)` | landed secondary surface retained over the new frame layers |
| `18u_auth_failed_light` | 0.00% | `(1094, 328, 1100, 333)` | landed secondary surface retained over the new frame layers |
| `18v_transcript_error_dark` | 80.25% | `(0, 0, 1100, 760)` | landed secondary surface retained over the new frame layers |
| `18w_transcript_error_light` | 80.27% | `(0, 0, 1100, 760)` | landed secondary surface retained over the new frame layers |
| `19_many_tabs_dark` | 31.88% | `(0, 0, 1100, 760)` | titlebar layers and tab visual-state hierarchy; landed behavior retained |
| `20_big_transcript_dark` | 31.88% | `(0, 0, 1100, 760)` | shared frame surfaces; owned transcript/composer/secondary renderer retained |
| `21_tools_expanded_dark` | 31.90% | `(0, 0, 1100, 760)` | shared frame surfaces; owned transcript/composer/secondary renderer retained |
| `21b_tools_expanded_detail_dark` | 31.88% | `(0, 0, 1100, 760)` | shared frame surfaces; owned transcript/composer/secondary renderer retained |
| `22_split_view_dark` | 32.18% | `(0, 0, 1100, 760)` | titlebar layers and tab visual-state hierarchy; landed behavior retained |
| `23_skeleton_dark` | 60.92% | `(0, 0, 1100, 760)` | shared frame surfaces; owned transcript/composer/secondary renderer retained |
| `24_thread_loading_dark` | 90.24% | `(0, 0, 1100, 760)` | shared frame surfaces; owned transcript/composer/secondary renderer retained |
| `25_load_older_dark` | 31.90% | `(0, 0, 1100, 760)` | shared frame surfaces; owned transcript/composer/secondary renderer retained |
| `26_thinking_dark` | 31.90% | `(0, 0, 1100, 760)` | shared frame surfaces; owned transcript/composer/secondary renderer retained |
| `27_streaming_dark` | 31.90% | `(0, 0, 1100, 760)` | shared frame surfaces; owned transcript/composer/secondary renderer retained |
| `28_composer_focus_dark` | 31.90% | `(0, 0, 1100, 760)` | shared frame surfaces; owned transcript/composer/secondary renderer retained |
| `29_find_dark` | 31.90% | `(0, 0, 1100, 760)` | shared frame surfaces; owned transcript/composer/secondary renderer retained |
| `30_find_light` | 31.91% | `(0, 0, 1100, 760)` | shared frame surfaces; owned transcript/composer/secondary renderer retained |
| `31_selection_dark` | 31.90% | `(0, 0, 1100, 760)` | shared frame surfaces; owned transcript/composer/secondary renderer retained |
| `32_new_messages_dark` | 31.89% | `(0, 0, 1100, 760)` | shared frame surfaces; owned transcript/composer/secondary renderer retained |
| `33_narrow_dark` | 53.64% | `(0, 0, 760, 620)` | theme-aware navigation, selection, attention, pin, hover, and narrow frame states |
| `34_narrow_many_tabs_dark` | 48.52% | `(0, 0, 760, 620)` | titlebar layers and tab visual-state hierarchy; landed behavior retained |

## semantic integration

1. Tab reorder, pinning, context menu, horizontal overflow, active reveal, split-pane reconciliation, futures, persistence, and the 20-tab allocation arm remain current-main implementations.
2. `hanabi::surface` remains the shared abstraction for sheets, menus, fields, option rows, and action buttons. Its base theme tokens are byte-identical to current main.
3. UI chrome contributes only frame-specific surface roles and uses those roles for the titlebar, sidebar, main canvas, navigation search, digest cards, and tab states.
4. All current-main secondary states plus the two narrow chrome states are baselined: 70 declared, 70 manifest entries, zero unbaselined.
