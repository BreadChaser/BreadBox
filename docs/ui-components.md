# Shared BreadBox UI

BreadBox takes its list surface from PSX3D v0.5.2's game library. `ui.c` owns
rendering geometry and input; screens supply content and actions.

* `ui_list_draw`: y4, 24px rows, eight visible rows, width314, spring selection
  and scroll, .5 body / .45 values, section labels and original 3px scrollbar.
* `ui_list_input` / `ui_list_input_sections`: clamped D-pad/circle selection,
  original frame repeat, select-then-open touch and section skipping. Settings
  reserve Left/Right for values; libraries use them to move. L/R actions stay
  with the content controller (letters, pages, categories, or state histories).
* `ui_list_context`: the quiet strip y198..208; a list never loses its first
  rows to a second header. Back remains in the shared touch bar.
* `ui_carousel_draw`, `ui_companion_title[_at]`: original cover positions,
  neighbor sizing/dimming, bob/depth, title typography and subtitle placement.
  Screens supply the artwork callback; PS1 keeps its animated launch/stack.
* `ui_pause_title`: the original PAUSED veil, shadow, game title and accents.
* `ui_dialog_draw`, `ui_progress_draw`, `ui_list_empty`: shared header/body,
  progress/empty content and the existing touch bar.
* `ui_game_notice`: original bottom-screen gameplay OSD palette and position.
* `pc_continue_card` (`components/postcards.inc`): the Continue point as one
  postcard on the desk, for the room's Continue place and the Continue?
  question (bar drawn by the caller). Metrics are the `UI_PC_CC_*` tokens; the
  name wraps on the shared `ui_fit_lines` (ui_fit.h: balanced lines, the floor,
  then "..."). Host tests: `tests/postcards/continue_card.py`, `tests/ui_fit`.
* Bar glyphs: `tools/icons/icons.tsv` in `UI_ICON_*` order; rows with the id
  `drawn` (Start over) are drawn by `tools/icons/drawn.py`, which also updates
  `dreamstale.bin` in place.
* `ui_card_draw` (the notebook card: pause, Settings, Controls, Cheats, Updates, Share): the title on the red
  rule, ruled rows, red section headings (`ui_card_row_t.heading`), no tabs. The selected row carries the
  highlighter stroke, `ui_mark_row` (`ui_mark.h`, `app/ui/ui_mark.c`): one of four 128 x 16 L8 masks
  (`tools/ui/mark_masks.py`), three quads multiplied over the paper, drawn left to right in 0.1 s. The room's
  and Collection's lists (`shelf_scene.c` `list_mark`) use the same routine.
* `ui_menu_header` and `ui_bar_draw/input`: original headers and style7 bar.
  Home tiles, QR sharing, touch controls and multiplayer use the inherited
  PSX3D components; content differences do not create a second theme.

Use theme roles, never screen-specific colors. Keep art, game-format details,
option values, source/help text and state previews in their content modules.

`tests/ui_consistency` is in the host suites. It rejects private list calls,
requires common component usage, and fingerprints reviewed remaining drawing
functions. Any changed raw geometry/font/theme role needs screenshot review
before the manifest is updated. Its ASan/UBSan input test exercises production
functions, including 315-entry scrolling and canonical touch hitboxes.
