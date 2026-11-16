# UI consistency checks

`check.py` enforces the controller / shared presentation / style-data boundary
without the SDK and inspects both conditional app paths in the original source.
Controllers cannot call raw drawing primitives. Components may not gain numeric
literals: they are counted per file (`literals.json`, lowered with `--record`, never raised), so a new
number needs a token in `ui_style.h`. Existing game-frame, backdrop and debug
renderers are reviewed, fingerprinted exceptions. Adding a new screen-specific
rectangle, text renderer, header or bar fails even if its geometry matches today.

`input.py` compiles the actual production list functions with ASan/UBSan and checks
canonical hit boxes, select-then-open touch, headings, repeat, clamped navigation,
value keys, empty lists and a 315-row scroll range.

See ``app/ui/components/``. The guard checks architecture; Azahar comparison is
still required when deliberately changing components or the shared style data.

Library feel: shelf_scene.c's reviewed exception changes only room_step camera
invalidation: preserve its current pose/velocity on catalog rebuilds and settle
with ui_motion. Drawing primitives, style roles and shared composition are unchanged.

Library review follow-up: shelf_scene.c adds one shared, state-dependent L/R
hint to the existing shelf card (L R glyphs: Jump 5 games / Turn the box; none when L/R do nothing).
components/gba_settings_ui.inc adds the existing Use default action to the shared
bar and updates the help text to X, matching the GBA controller and pause card.
The L/R hint sits at card-local y=128, inside the 150 px shelf card; y=158
put it under the bottom action bar. This is the same shared card on both systems.

Library GBA Options hides Use default for app-wide hotkeys; X only resets GBA fields. Pause defaults are unchanged.

Settings overhaul: `ui_mark.c` is a reviewed exception like `icons.c`: it draws the
shared highlighter stroke (a texture, multiplied) for `ui_card_draw` and shelf_scene.c's `list_mark`,
the selection on every notebook card and the room's lists. No screen draws its own selection.
`settings_ui.inc` lost the tabs and the Sections card; shelf_scene.c's change is `list_mark` only.

Place cards: `components/home.inc` fills the room's shared card (`shelf_scene_card`) with rows from
`place_cards.c` (Collection counts per system, Settings version / updates / room style, Quit's play today); no new
drawing. `components/postcards.inc` turns the Continue postcard by the token `UI_PC_CC_TILT` about its middle.

Box turn loop: shelf_scene.c passes the shelf's turn steps to the hands and the case's turn
(`hands_shelf_clip.yaw_mode`) to `room_hands_draw`; no drawing change.
