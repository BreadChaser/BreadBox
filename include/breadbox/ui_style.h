#pragma once
#include "ui_font.h"
// Single UI style data source. Theme roles provide colors; no component owns a palette.
// Canonical PSX3D v0.5.2 library metrics, shared by BreadBox list surfaces.
#define UI_TOP_SCREEN_WIDTH 400
#define UI_GAME_NOTICE_WIDTH 320
#define UI_GAME_NOTICE_LEFT ((UI_TOP_SCREEN_WIDTH - UI_GAME_NOTICE_WIDTH) / 2)
#define UI_LIST_TOP 4
#define UI_LIST_ROW 24
#define UI_LIST_ROWS 8               // 8 x 24 from y 4, above the 40 px bar (no context line)
#define UI_LIST_TEXT UI_TEXT_FLOOR    // list rows, values and section heads
#define UI_LIST_CONTEXT_Y 176
#define UI_DIALOG_TEXT UI_TEXT_FLOOR  // dialog and help text, the progress message
#define UI_DIALOG_TEXT_X 16
#define UI_DIALOG_TEXT_Y 38           // under the header's rule (y 26)
#define UI_DIALOG_TEXT_W 288
#define UI_DIALOG_LINE 22             // a line at the floor
#define UI_DIALOG_LINES 7             // 7 x 22 from y 38: the page ends above the 40 px bar
#define UI_LIST_WIDTH 314
#define UI_3D_BADGE_Y_OFFSET 7
#define UI_3D_BADGE_W 30
#define UI_3D_BADGE_EYE_W 5
#define UI_3D_BADGE_EYE_H 4
#define UI_3D_BADGE_GAP 2
#define UI_3D_BADGE_ALPHA 220
#define UI_3D_BADGE_LINE 1
#define UI_FONT_BODY .5f
#define UI_FONT_VALUE .45f
#define UI_FONT_META .42f
#define UI_FONT_SMALL .4f
// Bottom-screen readability floor (owner 2026-11-05, review 2026-11-05): no text on the bottom screen is
// drawn smaller than UI_TEXT_FLOOR. Measured in native screenshots: capitals and ascenders 10 px, a line with
// descenders 13-14 px (the system font at .5 was 7 px). Long text is fitted by ui_fit.h: down to the floor,
// then cut with "..." (the full text shows where the item is focused).
#define UI_TEXT_FLOOR .70f
#define UI_TEXT_FLOOR_PX 21.0f        // the same floor for the room's pixel-sized text (font scale = px / 30)
#define UI_FONT_SECTION .40f
#define UI_FONT_CONTEXT .36f
#define UI_FONT_TITLE .9f
#define UI_FONT_SUBTITLE .55f
#define UI_FONT_PAUSED 1.1f
#define UI_ANIMATION_FPS 60.0f
// Calm room motion: one easing curve and shared durations, in seconds.
#define UI_MOTION_CAMERA_S .65f
#define UI_MOTION_UI_S .24f
#define UI_MOTION_DIM_S .15f
#define UI_MOTION_DIM_ALPHA .94f
// A destructive dialog answers only a press made after it has been up this long: the second tap of a double
// tap that opened it never confirms it (the same span the old OUT + IN dim gave).
#define UI_MOTION_CONFIRM_S .3f
#define UI_MOTION_CONFIRM_MS ((unsigned)(UI_MOTION_CONFIRM_S * UI_MOTION_CLOCK_HZ))
#define UI_MOTION_PHASE_OUT 1u
#define UI_MOTION_PHASE_WAIT 2u
#define UI_MOTION_PHASE_IN 3u
#define UI_MOTION_PHASE_HOLD 4u
#define UI_MOTION_BUFFER_MS ((unsigned)(UI_MOTION_DIM_S * UI_MOTION_CLOCK_HZ))
// The sole reveal exception: boot assembles the room under a static lamp before
// a longer shared camera reveal; both screens use the same wall-time clock.
#define UI_MOTION_BOOT_WAIT_S .3f
#define UI_MOTION_TURN_S .35f
#define UI_MOTION_CASE_S .65f
#define UI_MOTION_LAUNCH_S .4f
#define UI_MOTION_INTRO_S .65f
#define UI_MOTION_SETTLE_RATE 18.0f
#define UI_MOTION_CARD_TRAVEL 12.0f
#define UI_MOTION_QUIT_RADIANS .28f
#define UI_REPEAT_DELAY 18
#define UI_REPEAT_INTERVAL 4
// Frozen original composition coordinates and vector-asset geometry. These
// are shared compatibility tokens, not per-screen styling overrides.
#define UI_METRIC_0 0
#define UI_METRIC_0X00FFFFFF 0x00ffffff
#define UI_METRIC_0X40000000 0x40000000
#define UI_METRIC_0X80 0x80
#define UI_METRIC_0XC0 0xc0
#define UI_METRIC_0XE0 0xe0
#define UI_METRIC_0XF0 0xf0
#define UI_METRIC_0XFF000000 0xff000000
#define UI_METRIC_0XFFFFFF 0xffffff
#define UI_METRIC_0_001F 0.001f
#define UI_METRIC_0_004F 0.004f
#define UI_METRIC_0_006F 0.006f
#define UI_METRIC_0_007F 0.007f
#define UI_METRIC_0_01F 0.01f
#define UI_METRIC_0_02F 0.02f
#define UI_METRIC_0_03F 0.03f
#define UI_METRIC_0_04F 0.04f
#define UI_METRIC_0_05F 0.05f
#define UI_METRIC_0_06F 0.06f
#define UI_METRIC_0_07F 0.07f
#define UI_METRIC_0_08F 0.08f
#define UI_METRIC_0_09F 0.09f
#define UI_METRIC_0_0F 0.0f
#define UI_METRIC_0_11F 0.11f
#define UI_METRIC_0_12F 0.12f
#define UI_METRIC_0_13F 0.13f
#define UI_METRIC_0_14F 0.14f
#define UI_METRIC_0_15F 0.15f
#define UI_METRIC_0_16F 0.16f
#define UI_METRIC_0_17F 0.17f
#define UI_METRIC_0_18F 0.18f
#define UI_METRIC_0_1F 0.1f
#define UI_METRIC_0_21F 0.21f
#define UI_METRIC_0_22F 0.22f
#define UI_METRIC_0_24F 0.24f
#define UI_METRIC_0_25F 0.25f
#define UI_METRIC_0_26F 0.26f
#define UI_METRIC_0_2F 0.2f
#define UI_METRIC_0_32F 0.32f
#define UI_METRIC_0_33F 0.33f
#define UI_METRIC_0_34F 0.34f
#define UI_METRIC_0_35F 0.35f
#define UI_METRIC_0_36F 0.36f
#define UI_METRIC_0_38F 0.38f
#define UI_METRIC_0_39F 0.39f
#define UI_METRIC_0_3F 0.3f
#define UI_METRIC_0_42F 0.42f
#define UI_METRIC_0_45F 0.45f
#define UI_METRIC_0_48F 0.48f
#define UI_METRIC_0_4F 0.4f
#define UI_METRIC_0_51F 0.51f
#define UI_METRIC_0_52F 0.52f
#define UI_METRIC_0_55F 0.55f
#define UI_METRIC_0_5F 0.5f
#define UI_METRIC_0_62F 0.62f
#define UI_METRIC_0_63F 0.63f
#define UI_METRIC_0_64F 0.64f
#define UI_METRIC_0_655F 0.655f
#define UI_METRIC_0_65F 0.65f
#define UI_METRIC_0_665F 0.665f
#define UI_METRIC_0_66F 0.66f
#define UI_METRIC_0_67F 0.67f
#define UI_METRIC_0_68F 0.68f
#define UI_METRIC_0_6F 0.6f
#define UI_METRIC_0_70F 0.70f
#define UI_METRIC_0_71F 0.71f
#define UI_METRIC_0_725F 0.725f
#define UI_METRIC_0_75F 0.75f
#define UI_METRIC_0_7F 0.7f
#define UI_METRIC_0_825F 0.825f
#define UI_METRIC_0_82F 0.82f
#define UI_METRIC_0_84F 0.84f
#define UI_METRIC_0_85F 0.85f
#define UI_METRIC_0_86F 0.86f
#define UI_METRIC_0_88F 0.88f
#define UI_METRIC_0_8F 0.8f
#define UI_METRIC_0_95F 0.95f
#define UI_METRIC_0_96F 0.96f
#define UI_METRIC_0_9F 0.9f
#define UI_METRIC_1 1
#define UI_METRIC_10 10
#define UI_PROGRESS_PERCENT_MAX 100
#define UI_METRIC_100 100
#define UI_METRIC_104 104
#define UI_METRIC_108 108
#define UI_METRIC_11 11
#define UI_METRIC_110 110
#define UI_METRIC_111 111
#define UI_METRIC_112 112
#define UI_METRIC_116 116
#define UI_METRIC_116_0F 116.0f
#define UI_METRIC_12 12
#define UI_METRIC_120 120
#define UI_PROGRESS_BAR_Y 125
#define UI_METRIC_128 128
#define UI_METRIC_132 132
#define UI_METRIC_136 136
#define UI_METRIC_138 138
#define UI_METRIC_138_F 138.f
#define UI_METRIC_13_9F 13.9f
#define UI_METRIC_14 14
#define UI_METRIC_140 140
#define UI_METRIC_142 142
#define UI_METRIC_15 15
#define UI_METRIC_150 150
#define UI_METRIC_156 156
#define UI_METRIC_16 16
#define UI_PROGRESS_TEXT_CENTER_X 160
#define UI_METRIC_160 160
#define UI_METRIC_16_0F 16.0f
#define UI_METRIC_17 17
#define UI_METRIC_170 170
#define UI_METRIC_172 172
#define UI_METRIC_173 173
#define UI_METRIC_174 174
#define UI_METRIC_178 178
#define UI_METRIC_179 179
#define UI_METRIC_18 18
#define UI_METRIC_180 180
#define UI_METRIC_184 184
#define UI_METRIC_186 186
#define UI_METRIC_19 19
#define UI_METRIC_191 191
#define UI_METRIC_193 193
#define UI_METRIC_194 194
#define UI_METRIC_196 196
#define UI_METRIC_198 198
#define UI_METRIC_1_0F 1.0f
#define UI_METRIC_1_1F 1.1f
#define UI_METRIC_1_25F 1.25f
#define UI_METRIC_1_2F 1.2f
#define UI_METRIC_1_33F 1.33f
#define UI_METRIC_1_35F 1.35f
#define UI_METRIC_1_3F 1.3f
#define UI_METRIC_1_4F 1.4f
#define UI_METRIC_1_5F 1.5f
#define UI_METRIC_1_6F 1.6f
#define UI_METRIC_1_7F 1.7f
#define UI_METRIC_1_8F 1.8f
#define UI_METRIC_2 2
#define UI_METRIC_20 20
#define UI_METRIC_200 200
#define UI_METRIC_200_0F 200.0f
#define UI_METRIC_201 201
#define UI_METRIC_204 204
#define UI_METRIC_206 206
#define UI_METRIC_21 21
#define UI_METRIC_210 210
#define UI_METRIC_212 212
#define UI_METRIC_215 215
#define UI_METRIC_22 22
#define UI_METRIC_221 221
#define UI_METRIC_225 225
#define UI_METRIC_226 226
#define UI_METRIC_228_0F 228.0f
#define UI_METRIC_23 23
#define UI_METRIC_232 232
#define UI_METRIC_236 236
#define UI_METRIC_23_0F 23.0f
#define UI_METRIC_24 24
#define UI_PROGRESS_BAR_WIDTH 240
#define UI_SCREEN_HEIGHT 240
#define UI_PROGRESS_BAR_WIDTH_F 240.0f
#define UI_METRIC_255 255
#define UI_METRIC_256 256
#define UI_METRIC_26 26
#define UI_METRIC_260 260
#define UI_METRIC_264 264
#define UI_METRIC_27 27
#define UI_METRIC_28 28
#define UI_METRIC_286 286
#define UI_METRIC_288 288
#define UI_METRIC_288_0F 288.0f
#define UI_METRIC_292 292
#define UI_PROGRESS_TEXT_WIDTH 296
#define UI_METRIC_296 296
#define UI_METRIC_296_0F 296.0f
#define UI_METRIC_2_0F 2.0f
#define UI_METRIC_2_2F 2.2f
#define UI_METRIC_2_4F 2.4f
#define UI_METRIC_3 3
#define UI_METRIC_30 30
#define UI_METRIC_308 308
#define UI_METRIC_30_0F 30.0f
#define UI_METRIC_310 310
#define UI_METRIC_312 312
#define UI_METRIC_314 314
#define UI_METRIC_315 315
#define UI_METRIC_317 317
#define UI_METRIC_32 32
#define UI_BOTTOM_SCREEN_WIDTH 320
#define UI_BOTTOM_SCREEN_WIDTH_F 320.0f
#define UI_METRIC_352_0F 352.0f
#define UI_METRIC_36 36
#define UI_METRIC_368 368
#define UI_METRIC_376 376
#define UI_METRIC_38 38
#define UI_METRIC_380 380
#define UI_METRIC_384 384
#define UI_METRIC_39 39
#define UI_METRIC_390 390
#define UI_METRIC_392 392
#define UI_METRIC_396 396
#define UI_PROGRESS_BAR_HEIGHT 4
#define UI_METRIC_4 4
#define UI_PROGRESS_BAR_X 40
#define UI_METRIC_40 40
#define UI_METRIC_42 42
#define UI_METRIC_44 44
#define UI_METRIC_46 46
#define UI_METRIC_48 48
#define UI_METRIC_5 5
#define UI_METRIC_50 50
#define UI_METRIC_52 52
#define UI_METRIC_56 56
#define UI_METRIC_58 58
#define UI_METRIC_5_5F 5.5f
#define UI_METRIC_6 6
#define UI_METRIC_60_0F 60.0f
#define UI_METRIC_62 62
#define UI_METRIC_62_0F 62.0f
#define UI_METRIC_64 64
#define UI_METRIC_66 66
#define UI_METRIC_68 68
#define UI_METRIC_7 7
#define UI_METRIC_70 70
#define UI_METRIC_72 72
#define UI_METRIC_76 76
#define UI_METRIC_768 768
#define UI_METRIC_8 8
#define UI_METRIC_80 80
#define UI_METRIC_8192 8192
#define UI_METRIC_83 83
#define UI_METRIC_84 84
#define UI_METRIC_85 85
#define UI_METRIC_86 86
#define UI_METRIC_87 87
#define UI_METRIC_88 88
#define UI_METRIC_9 9
#define UI_METRIC_90 90
#define UI_METRIC_92 92
#define UI_PROGRESS_TEXT_Y 95
#define UI_METRIC_96 96
#define UI_METRIC__02F .02f
#define UI_METRIC__1F .1f
#define UI_METRIC__30F .30f
#define UI_METRIC__35F .35f
#define UI_METRIC__38F .38f
#define UI_METRIC__39F .39f
#define UI_METRIC__3F .3f
#define UI_METRIC__42F .42f
#define UI_METRIC__45F .45f
#define UI_METRIC__48F .48f
#define UI_METRIC__4F .4f
#define UI_METRIC__55F .55f
#define UI_METRIC__5F .5f
#define UI_METRIC__65F .65f
#define UI_METRIC__6F .6f
#define UI_METRIC__7F .7f
#define UI_METRIC__85F .85f
#define UI_METRIC__8F .8f
#define UI_METRIC__9F .9f
#define UI_METRIC_0_002F 0.002f
#define UI_METRIC_0_0005F 0.0005f
#define UI_METRIC_100_0F 100.0f
#define UI_METRIC_34 34
#define UI_METRIC_304 304
#define UI_METRIC_13 13
#define UI_METRIC_55 55
#define UI_METRIC_0_81F 0.81f
#define UI_METRIC_0_56F 0.56f
#define UI_METRIC_0_58F 0.58f
#define UI_METRIC_228 228
#define UI_METRIC_216 216
#define UI_METRIC_63_8176F 63.8176f
#define UI_METRIC_97 97
#define UI_METRIC_0U 0u
#define UI_METRIC_60 60
#define UI_METRIC_0_53F 0.53f
#define UI_METRIC_0_60F 0.60f
#define UI_METRIC_50_75F 50.75f
#define UI_METRIC_0_61F 0.61f
#define UI_METRIC_49_25F 49.25f
#define UI_METRIC_48_75F 48.75f
#define UI_METRIC_19_75F 19.75f
#define UI_METRIC_18_25F 18.25f
#define UI_METRIC_36_5F 36.5f
#define UI_METRIC_266 266
#define UI_METRIC_246 246
#define UI_METRIC_220 220
#define UI_METRIC_214 214
#define UI_METRIC_210_0F 210.0f
#define UI_METRIC_1024 1024
#define UI_METRIC_45 45
#define UI_METRIC_0_0002F 0.0002f
#define UI_METRIC_0_30F 0.30f
#define UI_METRIC_0_975F 0.975f
#define UI_METRIC_3_55F 3.55f
#define UI_METRIC_0_41F 0.41f
#define UI_METRIC_0_075F 0.075f
#define UI_METRIC_0_0004F 0.0004f
#define UI_METRIC_0_0006F 0.0006f
#define UI_METRIC_0_0008F 0.0008f

// Original style-7 touch bar metrics.
#define BAR_TOP 200          // 40 px: the icon and a caption at the text floor (was 208, captions .36)
#define BAR_ICON_Y 210.0f    // icon centre
#define BAR_ICON_ALONE_Y 220.0f // icon centre of an icon-only tab (the bar's middle)
#define BAR_ICON_R 10        // icon size (bar_icon's r)
#define BAR_GLYPH_SIZE UI_TEXT_FLOOR   // the button's glyph beside an icon
#define BAR_GLYPH_GAP 4
#define BAR_PILL_PAD 4            // SELECT / START: the pill around the button's name
#define BAR_CAPTION_Y 230.0f // caption centre
#define BAR_CAPTION UI_TEXT_FLOOR    // caption size; at the floor a long word is cut with "..."
#define BAR_CAPTION_MIN UI_TEXT_FLOOR
#define UI_GAME_CLEAR C2D_Color32(0, 0, 0, 255)

// Compatibility metrics used by inherited components.


// Header placement shared by app and standalone.
#define UI_HEADER_LEFT 10
#define UI_HEADER_TITLE_Y 4
#define UI_HEADER_TITLE_SIZE .62f    // the title font (wider and lighter than the system font): 10 px capitals
#define UI_HEADER_RIGHT 310
#define UI_HEADER_RIGHT_Y 6
#define UI_HEADER_RULE_Y 26
#define UI_METRIC_238 238
#define UI_METRIC_276 276
#define UI_METRIC_250 250
#define UI_METRIC_270 270

#define UI_TOUCH_STICK_X UI_PLAY_STICK_CENTER_X
#define UI_TOUCH_STICK_Y UI_PLAY_STICK_CENTER_Y
#define UI_HELP_TOP 150

// Standalone compatibility viewport metrics.
#define UI_BROWSER_LIST_Y 40
#define UI_BROWSER_ROW_H 23
#define UI_BROWSER_ROWS 6
#define UI_LIBUI_ROW_H 24
#define UI_LIBUI_ROWS 8
#define UI_SETTINGS_UI_VISIBLE_ROWS 6
#define UI_METRIC_94 94
#define UI_METRIC_149 149
#define UI_METRIC_163 163
#define UI_METRIC_190 190
#define UI_METRIC_152 152
#define UI_METRIC_2_1F 2.1f

#define UI_ICON_DRAW_SECONDS 0.8f
#define UI_ICON_BREATH_SECONDS 3.6f
#define UI_ICON_GLOW_SCALE 1.625f

// Controller animation/input timings use the same style data.
#define UI_METRIC_20_0F 20.0f
#define UI_METRIC_260_0F 260.0f
#define UI_METRIC_14_0F 14.0f
#define UI_METRIC_3_0F 3.0f
#define UI_METRIC_0_999F 0.999f
#define UI_METRIC_36_0F 36.0f
#define UI_METRIC_180_0F 180.0f
#define UI_METRIC_22_0F 22.0f
#define UI_METRIC_300_0F 300.0f
#define UI_METRIC_3600 3600
// Save slot postcards (components/postcards.inc). Bottom: a corkboard with
// the Continue card on the left and ten slot cards in a 4-wide grid (5 x 2 when
// a system has no Continue card); top: the chosen card large.
#define UI_PC_BOARD_X 6
#define UI_PC_BOARD_Y 6
#define UI_PC_BOARD_W 308
#define UI_PC_BOARD_H 166              // to 172: the foot line and the 40 px bar below
#define UI_PC_FRAME 4
#define UI_PC_CORK_SHADE 90
#define UI_PC_FLECKS 70
#define UI_PC_FLECK 2
#define UI_PC_FLECK_MX 137
#define UI_PC_FLECK_MY 61
#define UI_PC_FLECK_ALPHA 60
#define UI_PC_CONT_X 56
#define UI_PC_CONT_Y 64
#define UI_PC_CONT_W 92
#define UI_PC_CONT_H 76
#define UI_PC_CONT_TILT 0.05f
#define UI_PC_CONT_EDGE 3
#define UI_PC_CONT_STAMP_W 14
#define UI_PC_CONT_STAMP_H 18
#define UI_PC_CONT_STAMP_UP 4
#define UI_PC_GRID_X 138
#define UI_PC_GRID_Y 38
#define UI_PC_GRID_DX 50
#define UI_PC_GRID_DY 56
#define UI_PC_CARD_W 42
#define UI_PC_CARD_H 40
#define UI_PC_FLAT_X 36
#define UI_PC_FLAT_Y 56
#define UI_PC_FLAT_DX 50
#define UI_PC_FLAT_DY 76
#define UI_PC_FLAT_W 44
#define UI_PC_FLAT_H 42
#define UI_PC_NOTE_X 18
#define UI_PC_NOTE_Y 130
#define UI_PC_NOTE_W 84
#define UI_PC_NOTE_H 42
#define UI_PC_NOTE_TILT 0.03f
#define UI_PC_NOTE_RULE 14
#define UI_PC_NOTE_PAD 5
#define UI_PC_NOTE_TITLE_Y 3
#define UI_PC_NOTE_LINE_Y 22
#define UI_PC_TILT_MUL 37
#define UI_PC_TILT_MOD 7
#define UI_PC_TILT_MID 3
#define UI_PC_TILT_STEP 0.018f
#define UI_PC_INSET 2
// A card's caption: its handwriting (at the floor size) stands on a baseline UI_PC_CAPTION_PAD above the card's
// foot, its capitals UI_PC_CAPTION_PAD under the picture. The strip is made from the font's own measures: it was
// 11 px for the .38 hand, and at the floor the baseline fell 3 px below the card (owner 2026-11-11).
#define UI_PC_CAPTION_PAD 2
#define UI_PC_CAPTION (UI_PC_CAPTION_PAD * 2 + UI_PC_HAND_TINY * UI_FONT_CAP)   // 14
#define UI_PC_CAPTION_X (UI_PC_CONT_EDGE + UI_PC_CAPTION_PAD)   // the same margin on every card: clear of Continue's edge
#define UI_PC_SLANT_ROOM (UI_PC_HAND_SLANT * UI_PAPER_CAP)    // slanted capitals lean this far right of the baseline
#define UI_PC_PIN_R 2.6f
#define UI_PC_PIN_DOWN 3
#define UI_PC_GLOW 3
#define UI_PC_GLOW_ALPHA 110
#define UI_PC_LIFT_SCALE 0.035f
#define UI_PC_LIFT_Y 3
#define UI_PC_CARD_SHADOW_X 2
#define UI_PC_CARD_SHADOW_Y 3
#define UI_PC_SHADOW_X 4
#define UI_PC_SHADOW_Y 5
#define UI_PC_SHADOW_ALPHA 120
#define UI_PC_EMPTY_ALPHA 150
#define UI_PC_RULE_ALPHA 90
#define UI_PC_TOSS_DX 8
#define UI_PC_TOSS_DY 18
#define UI_PC_TOSS_SPIN 0.04f
#define UI_PC_STAMP_SPEED 2.5f
#define UI_PC_STAMP_DROP 0.06f
#define UI_PC_STAMP_TILT 0.08f
#define UI_PC_SAVED_W 34
#define UI_PC_SAVED_H 14
#define UI_PC_SAVED_LINE 1.5f
#define UI_PC_LINE_Y 189              // the foot line's baseline, above the 40 px bar
#define UI_PC_LINE_W 300
#define UI_PC_HAND_SLANT 0.22f
#define UI_PC_HAND_BIG 0.62f
#define UI_PC_HAND_SMALL UI_TEXT_FLOOR   // the corkboard's handwriting on the bottom screen: the floor (was .46 / .38)
#define UI_PC_HAND_TINY UI_TEXT_FLOOR
#define UI_PC_TOP_CY 116
#define UI_PC_TOP_W 340
#define UI_PC_TOP_H 206
#define UI_PC_TOP_TILT 0.02f
#define UI_PC_TOP_DEPTH 0.6f
#define UI_PC_TOP_BORDER 4
#define UI_PC_TOP_PIC_X 12
#define UI_PC_TOP_PIC_Y 12
#define UI_PC_TOP_PIC_W 228
#define UI_PC_TOP_PIC_H 160
// the printed footer: its capitals centred between the picture's border and the card's foot, the same 12 px in
// from either side as the picture
#define UI_PC_TOP_FOOT_BASE ((UI_PC_TOP_PIC_Y + UI_PC_TOP_PIC_H + UI_PC_TOP_BORDER + UI_PC_TOP_H + UI_FONT_VALUE * UI_FONT_CAP) / 2)
#define UI_PC_TOP_FOOT_W 316
#define UI_PC_TOP_STAMP_X 282
#define UI_PC_TOP_STAMP_Y 10
#define UI_PC_TOP_STAMP_W 46
#define UI_PC_TOP_STAMP_H 46
#define UI_PC_STAMP_INSET 3
#define UI_PC_STAMP_PAD 2              // the label's baseline and sides, inside the stamp's field
#define UI_PC_STAMP_LABEL_MIN .24f     // a long label ("CONTINUE") shrinks to fit, never cut
#define UI_PC_PERF_STEP 4
#define UI_PC_PERF_R 1.2f
#define UI_PC_TOP_MARK_X 271            // clear of the picture's border; the waves end inside the card
#define UI_PC_TOP_MARK_Y 40
#define UI_PC_TOP_MARK_R 25             // the widest date ("MAY 30") and the time fit the inner ring at UI_PC_MARK_TEXT
#define UI_PC_MARK_GAP 3
#define UI_PC_MARK_SEGS 24
#define UI_PC_MARK_LINE 1.2f
#define UI_PC_MARK_TEXT UI_FONT_CONTEXT
#define UI_PC_MARK_TEXT_GAP 3           // the date's baseline to the time's capitals; the pair centred in the ring
#define UI_PC_WAVES 4
#define UI_PC_WAVE_SEGS 9
#define UI_PC_WAVE_GAP 2
#define UI_PC_WAVE_DX 4
#define UI_PC_WAVE_DY 5
#define UI_PC_WAVE_SPAN 8
#define UI_PC_WAVE_FREQ 0.9f
#define UI_PC_WAVE_AMP 1.4f
#define UI_PC_TAU 6.2831853f
#define UI_PC_TOP_TEXT_X 252
#define UI_PC_TOP_TEXT_W 76
#define UI_PC_TOP_RULE_Y 92
#define UI_PC_TOP_RULE_DY UI_PAPER_LINE   // the address lines on the back: the paper pitch; the writing sits on the first
#define UI_PC_TOP_RULES 4
#define UI_PC_HOUR 3600
#define UI_PC_MINUTE 60
#define UI_PC_Z_BACK 0.10f
#define UI_PC_Z_FRAME 0.11f
#define UI_PC_Z_CORK 0.12f
#define UI_PC_Z_FLECK 0.13f
#define UI_PC_Z_GLOW 0.18f
#define UI_PC_Z_SHADOW 0.2f
#define UI_PC_Z_PAPER 0.3f
#define UI_PC_Z_BORDER 0.31f
#define UI_PC_Z_INK 0.32f
#define UI_PC_Z_PIC 0.34f
#define UI_PC_Z_STAMP 0.36f
#define UI_PC_Z_PERF 0.37f
#define UI_PC_Z_FIELD 0.38f
#define UI_PC_Z_MARK 0.40f
#define UI_PC_Z_SAVED 0.42f
#define UI_PC_Z_PIN 0.45f
// The Continue postcard (postcards.inc pc_continue_card): the room's Continue place and the Continue? question on
// the bottom screen, flat on the desk (owner 2026-11-12 picked mock A, devtools continue-card NOTES.md fix round).
// Laid out bottom-up: the printed foot line, the name on 1-3 ruled lines, the picture in the space left; the
// system's stamp top right with the postmark over it, "Continue" handwritten in red under the stamp.
#define UI_PC_CC_X 8
#define UI_PC_CC_Y 6
#define UI_PC_CC_W 304
#define UI_PC_CC_H 190                // to 196: the 40 px bar below
// its tilt (radians, turned the hall postcard's way; owner 2026-11-12 "needs to be angled a bit"): 0.86 degrees, about 4 px
// over the name's line. Enough to read as lying on the desk; the mock's 0.02 stepped the 1x letters more. Turned about
// its middle the corners stay on the screen and above the bar (tests/postcards/continue_card.py).
#define UI_PC_CC_TILT 0.015f
#define UI_PC_CC_PAD 12               // the paper's margin: picture, ruled lines, stamp
#define UI_PC_CC_TEXT_X 14            // the printed lines
#define UI_PC_CC_NAME_W 284           // the ruled lines from the pad to 8 px short of the edge, and the name on them
#define UI_PC_CC_FOOT (UI_PC_CC_H - UI_PC_CC_PAD)    // the foot line's baseline ("Left off ...")
#define UI_PC_CC_NAME_LINES 3
#define UI_PC_CC_ALERT_LINES 2        // a red foot (Start over failed) may take two lines
#define UI_PC_CC_COL_W 100            // the right column the picture leaves free (stamp, "Continue")
#define UI_PC_CC_COL_GAP 8
#define UI_PC_CC_FRAME 4              // the picture's wax frame
#define UI_PC_CC_ASPECT (4.0f / 3.0f)   // the frame without a picture
#define UI_PC_CC_STAMP_W 48
#define UI_PC_CC_STAMP_H 54
#define UI_PC_CC_STAMP_Y 10
#define UI_PC_CC_STAMP_SIDE 0         // the label at the floor takes the field's whole width (three capitals)
#define UI_PC_CC_MARK_DX 4            // the postmark's centre: left of the stamp's edge, over it
#define UI_PC_CC_MARK_Y 30
#define UI_PC_CC_MARK_R 17
#define UI_PC_CC_TITLE_SIZE UI_CARD_TITLE_SIZE       // "Continue" / "Continue?": the card title's size, down to the floor
#define UI_PC_CC_TITLE_Y (UI_PC_CC_STAMP_Y + UI_PC_CC_STAMP_H + 30)   // its baseline, under the stamp
#define UI_PC_CC_TITLE_R 14           // its right end, in from the card's edge
#define UI_PC_CC_TITLE_GAP 12         // its room starts this far right of the picture's frame
#define UI_PC_CC_EMPTY_LINE 122       // the blank card: "Nothing to continue" on this ruled line, one above and below
#define UI_PC_CC_EMPTY_ALPHA 153      // the blank stamp's outline (.6)
#define UI_PC_CC_PAPER_MID 115        // the paper's two other corners: paper .55, edge .45
#define UI_PC_CC_FLECK_MX 97          // paper flecks from a fixed pattern
#define UI_PC_CC_FLECK_MY 53
#define UI_PC_CC_FLECK_ALPHA 22
// The room's paper cards on the bottom screen (shelf_scene.c): text sizes in pixels (font scale = px / 30)
// and the step between their lines. Readable on a real Old 3DS (owner 2026-11-05: was 8-10 px).
#define UI_ROOM_ROW_PX UI_TEXT_FLOOR_PX      // the Collection card's rows
#define UI_ROOM_VALUE_PX UI_TEXT_FLOOR_PX    // their counts
#define UI_ROOM_LINE_PX UI_TEXT_FLOOR_PX     // "Last played", "Play time", a place's rows
#define UI_ROOM_HINT_PX UI_TEXT_FLOOR_PX     // the line along the card's foot
#define UI_ROOM_MIN_PX UI_TEXT_FLOOR_PX      // every line shrinks only to this, then is cut with "..."
#define UI_ROOM_TINY_PX UI_TEXT_FLOOR_PX     // the picture's label
// The paper cards' geometry (card-local coordinates; text_at's y is the baseline region, see shelf_scene.c).
#define UI_ROOM_CARD_X 26             // the place / catalog card
#define UI_ROOM_CARD_Y 22
#define UI_ROOM_CARD_W 268
#define UI_ROOM_CARD_H 170            // to 192: the bar starts at 200
#define UI_ROOM_GAME_X 22             // a shelf's game card, under the mini shelf
#define UI_ROOM_GAME_Y 50
#define UI_ROOM_GAME_W 276
#define UI_ROOM_GAME_H 150            // to 200
#define UI_ROOM_MARGIN_X 42           // the red margin line (card coordinates)
#define UI_ROOM_TEXT_X (UI_ROOM_MARGIN_X + UI_PAPER_MARGIN_GAP)   // text starts right of the red margin
#define UI_ROOM_TEXT_R 260            // and ends here (right-aligned values, the corner)
#define UI_ROOM_VALUE_X 150           // the value column of a card's info rows
#define UI_ROOM_RULE_Y 24             // the red rule: the header row's title stands on it (ui_card_header, as the pause card)
#define UI_ROOM_ROWS_Y (UI_ROOM_RULE_Y + UI_PAPER_LINE)   // the body starts on the first ruled line under the rule
#define UI_ROOM_LINE_STEP UI_PAPER_LINE
#define UI_ROOM_LIST_STEP UI_PAPER_LINE   // the Collection card's list rows
#define UI_ROOM_HINT_Y (UI_ROOM_RULE_Y + 6 * UI_PAPER_LINE)        // the foot line of the place card (its last ruled line)
#define UI_ROOM_GAME_HINT_Y (UI_ROOM_RULE_Y + 5 * UI_PAPER_LINE)   // the foot line of the game card (L/R, or a notice)
#define UI_ROOM_RULE_FOOT 6           // ruled lines stop this far above the card's bottom edge
#define UI_ROOM_PICTURE_W 64          // the save picture taped to the card (4:3)
#define UI_ROOM_TITLE_W_PICTURE 140   // a title beside the picture (clear of its tape)
#define UI_ROOM_PICTURE_X 197
#define UI_ROOM_PICTURE_Y 29
#define UI_ROOM_GAP 10                // between a label and its value
#define UI_ROOM_LABEL_MAX .55f        // an info row's label takes at most this share of the line
#define UI_ROOM_STAR_UP (UI_PAPER_CAP / 2)   // the Favorites star beside its row: centred on the capitals
#define UI_ROOM_STAR_R 6
#define UI_ROOM_STAR_W 16
#define UI_ROOM_DOT_R 4               // a card's page dots (the welcome), bottom right on the foot line
#define UI_ROOM_DOT_STEP 12
#define UI_ROOM_DOT_Y (UI_ROOM_HINT_Y - UI_PAPER_CAP / 2)
#define UI_ROOM_ICON_PX 14            // a row's icon in the card's margin
#define UI_ROOM_STAMP_X 222           // the FAVORITE stamp
#define UI_ROOM_STAMP_Y 98            // in the empty ruled lines between the title and the foot line (never over text)
// the bookshelf close-up: the selected box tips out this far (mm) as the camera reaches it;
// after this many seconds still it would come out by itself (0: never, A takes it out; owner)
#define UI_SHELF_TIP_MM 18.0f
#define UI_SHELF_AUTO_TAKEOUT_S 0.0f
#define UI_SHELF_COVER_WAIT_S 0.6f   // A waits at most this long for the cover, back and case texture before the take-out (the case moves out in 0.65 s)
// Pause card (ui_pause_scene, ui_tag, ui_card_*): the paused game in a wooden CRT on the wall, its title
// on a hang tag, and the menu on a ruled notebook card. Every number the three components draw.
#define UI_CRT_CX 156                 // the CRT's centre on the top screen
#define UI_CRT_CY 114
#define UI_CRT_GLASS_W 262            // the largest glass: the game's box is fitted inside
#define UI_CRT_GLASS_H 196
#define UI_CRT_PAD 13                 // bezel width around the glass
#define UI_CRT_RADIUS 17
#define UI_CRT_LIP 3                  // inner lip around the glass
#define UI_CRT_SHADOW_X 2
#define UI_CRT_SHADOW_Y 4
#define UI_CRT_SHADOW_ALPHA 110
#define UI_CRT_PLANK 24               // wall boards
#define UI_CRT_PLANK_ALPHA 70
#define UI_CRT_GLOW_ALPHA 90          // light along the bezel's top edge
#define UI_CRT_PAUSE_X 9              // the pause mark in the glass corner, from the glass's bottom left
#define UI_CRT_PAUSE_Y 20
#define UI_CRT_PAUSE_W 3
#define UI_CRT_PAUSE_H 11
#define UI_CRT_PAUSE_GAP 6
#define UI_CRT_Z_WALL 0.1f
#define UI_CRT_Z_BEZEL 0.3f
#define UI_CRT_Z_GLASS 0.4f
#define UI_CRT_Z_MARK 0.6f
#define UI_TAG_X 297                  // left edge of the hang tag on the top screen
#define UI_TAG_Y 16
#define UI_TAG_W 100
#define UI_TAG_PAD 7
#define UI_TAG_TOP 21                 // paper above the first line (the string hole sits in it)
#define UI_TAG_CUT 11                 // clipped upper corners
#define UI_TAG_HOLE_Y 9
#define UI_TAG_HOLE_R 3.2f
#define UI_TAG_STRING_DX 26
#define UI_TAG_STRING_W 1.4f
#define UI_TAG_TILT 0.055f            // radians, clockwise
#define UI_TAG_TITLE_SIZE 0.5f
#define UI_TAG_TITLE_LINE 15
#define UI_TAG_TITLE_LINES 3
#define UI_TAG_LINE_SIZE 0.40f        // a line too wide shrinks to UI_FONT_CONTEXT before it is cut (was .36, cut)
#define UI_TAG_LINE 13
#define UI_TAG_LINES 3
#define UI_TAG_BODY_LINES 4           // a sentence wrapped by width under the lines (the settings help)
#define UI_TAG_GAP 6                  // above and below the rule
#define UI_TAG_DEPTH -1.2f            // pops out of the screen (negative: toward the viewer)
#define UI_TAG_SHADOW 2
#define UI_TAG_SHADOW_ALPHA 90
#define UI_TAG_Z 0.62f
// Ruled paper (owner 2026-11-05: "why doesn't the text actually go on the lines?"). Every ruled card (the Settings /
// pause card, the room and game cards, the postcard's back) uses one pitch for its ruled lines and its lines of
// text, and each text's baseline sits on a ruled line like handwriting (descenders cross it). Headers sit on the
// red rule; tabs stay in the unruled top margin. tests/ui_paper checks it from these tokens.
#define UI_PAPER_LINE 22              // the pitch: one line of text at the floor per ruled line
// Where the letters stand in the font's 30 px cell is the loaded font's own (ui_font.h): Nintendo's system font
// stands lower in its cell than Azahar's replacement (owner 2026-11-12: text ~3 px under the lines on hardware).
#define UI_FONT_BASELINE ui_face_baseline(UI_FACE_SYSTEM)
#define UI_TITLE_FONT_BASELINE ui_face_baseline(UI_FACE_TITLE)
#define UI_PAPER_CAP 10               // capitals at the floor stand this tall on their line
#define UI_FONT_CAP (UI_PAPER_CAP / UI_TEXT_FLOOR)   // system font: capitals per unit of text size (14.3 px at 1.0)
#define UI_TEXT_CENTRED_TOP(cy, size) ((cy) + (size) * (UI_FONT_CAP / 2 - UI_FONT_BASELINE))   // the cell top that centres capitals on cy
#define UI_PAPER_MARGIN_GAP 6         // text starts this far right of the red margin line
#define UI_PAPER_TOP(line, size) ((line) - (size) * UI_FONT_BASELINE)          // the cell top that puts a baseline on a line
#define UI_PAPER_TITLE_TOP(line, size) ((line) - (size) * UI_TITLE_FONT_BASELINE)
#define UI_PAPER_MARK_Y(line) ((line) - UI_PAPER_CAP / 2.0f)                     // icons and arrows: centred on the capitals
#define UI_CARD_X 9                   // the notebook card on the bottom screen
#define UI_CARD_Y 7
#define UI_CARD_W 302
#define UI_CARD_H 191                 // to 198: the bar starts at 200
#define UI_CARD_SHADOW 3
#define UI_CARD_MARGIN_X 48           // red margin line
#define UI_CARD_ICON_X 30             // centre of the row icons
#define UI_CARD_TEXT_X (UI_CARD_MARGIN_X + UI_PAPER_MARGIN_GAP)
#define UI_CARD_RIGHT 12              // gap between values and the card's right edge
#define UI_VALUE_CARD_X 12            // top screen: the slip with a cut row's whole text (ui_value_card)
#define UI_VALUE_CARD_W 376
#define UI_VALUE_CARD_BOTTOM 234
#define UI_VALUE_CARD_PAD 6
#define UI_VALUE_CARD_LINES 9         // includes one continuation line for a paged value
#define UI_VALUE_CARD_PAGE_MARK_W 60  // SELECT cue and next-page arrow
#define UI_CARD_RULE_Y 43             // the red rule under the header: the title and the right-hand eyebrow sit on it
#define UI_CARD_TITLE_SIZE 0.8f
#define UI_CARD_TITLE_MIN UI_TEXT_FLOOR
#define UI_CARD_ROWS_Y (UI_CARD_RULE_Y + 1)   // row k's band runs from line k to line k+1; its text sits on line k+1
#define UI_CARD_ROW UI_PAPER_LINE     // 6 rows at the text floor (a longer list scrolls)
#define UI_CARD_ROWS 6
#define UI_CARD_ROW_SIZE UI_TEXT_FLOOR
#define UI_CARD_VALUE_SIZE UI_TEXT_FLOOR
#define UI_CARD_VALUE_MAX .5f         // a value takes at most this share of the row; the label gets the rest
#define UI_CARD_ICON_SIZE 11
#define UI_CARD_DIM_ALPHA 110
#define UI_CARD_CLIP_X 26             // paper clip
#define UI_CARD_CLIP_Y 1
#define UI_CARD_CLIP_W 11
#define UI_CARD_CLIP_H 34
#define UI_CARD_CLIP_LINE 1.5f
#define UI_PAUSE_STAMP_PAPER 150      // the SAVED postmark (on the paused picture's corner): its paper disc's alpha
#define UI_CARD_STAMP_R 21
#define UI_CARD_STAMP_TILT -0.2f
#define UI_CARD_STAMP_SEGS 28
#define UI_CARD_ARROW 3.5f            // arrows around a value that left / right change
#define UI_CARD_Z_SHADOW 0.2f
#define UI_CARD_Z_PAPER 0.3f
#define UI_CARD_Z_RULE 0.32f
#define UI_CARD_Z_SELECT 0.34f
#define UI_CARD_Z_CLIP 0.5f
#define UI_CARD_SUN_STEP 0.7853982f
#define UI_CARD_NUDGE 2               // the selected row's text drifts right
#define UI_CARD_NOTICE_H UI_PAPER_LINE   // the notice line along the foot: its text on the last ruled line
#define UI_CARD_NOTICE_SIZE UI_TEXT_FLOOR
// The selection highlighter (ui_mark.h): a yellow marker stroke multiplied over the paper, from a little left
// of the row's text to a little past it (or past the value's arrow), on the text's x-height.
#define UI_MARK_BEFORE 4              // starts this far left of the label
#define UI_MARK_AFTER 6               // and ends this far past the text
#define UI_MARK_MIN_W 44              // never shorter than its cap and tail (a two-letter label)
#define UI_MARK_ABOVE 13              // the quads from line - 13 ...
#define UI_MARK_HEIGHT 17             // ... 17 px down: the ink about 15 px tall around the capitals
#define UI_MARK_DRAW_S .1f            // drawn left to right in this time when the selection moves
#define UI_MARK_YELLOW_R 255          // marker yellow, multiplied at UI_MARK_STRENGTH: card paper -> (243, 203, 73)
#define UI_MARK_YELLOW_G 214
#define UI_MARK_YELLOW_B 30
#define UI_MARK_STRENGTH .72f
#define UI_CRT_SHINE 58               // glass reflection in the top left corner
#define UI_CRT_SHINE_ALPHA 22
#define UI_CARD_SCROLLBAR_W 2

#define UI_MOTION_CLOCK_HZ 1000

/* Shared play-panel presentation. */
#define UI_PLAY_BIT_ONE 1u
#define UI_PLAY_CLIP_EPSILON 0.0001f

/* Play control geometry: shared drawing and hit capture use these role tokens. */
#define UI_PLAY_CLUSTER_X 92
#define UI_PLAY_TOP 12
#define UI_PLAY_TILE_STEP 111
#define UI_PLAY_TILE_SIZE 105
#define UI_PLAY_CLUSTER_SIZE 216
#define UI_PLAY_EDGE_X 208
#define UI_PLAY_EDGE_W 100
#define UI_PLAY_EDGE_H 102
#define UI_PLAY_SECOND_Y 126
#define UI_PLAY_RAIL_LEFT 12
#define UI_PLAY_RAIL_RIGHT 252
#define UI_PLAY_RAIL_TOP 64
#define UI_PLAY_RAIL_SECOND 128
#define UI_PLAY_RAIL_W 56
#define UI_PLAY_RAIL_H 48
#define UI_PLAY_RAIL_SINGLE_H 112
#define UI_PLAY_MENU_X 0
#define UI_PLAY_MENU_MIRROR_X 238
#define UI_PLAY_MENU_W 82
#define UI_PLAY_MENU_H 40
#define UI_PLAY_DETAIL_MENU_X 125
#define UI_PLAY_DETAIL_MENU_Y 184
#define UI_PLAY_DETAIL_MENU_W 70
#define UI_PLAY_DETAIL_MENU_H 30
#define UI_PLAY_DETAIL_TEXT_X 142
#define UI_PLAY_DETAIL_TEXT_Y 191
#define UI_PLAY_STATS_X 12
#define UI_PLAY_STATS_MIRROR_X 246
#define UI_PLAY_STATS_Y 184
#define UI_PLAY_STATS_W 62
#define UI_PLAY_STATS_H 40
#define UI_PLAY_STATS_FOOTER_Y 218
#define UI_PLAY_STICK_CENTER_X 200
#define UI_PLAY_STICK_CENTER_Y 120
#define UI_PLAY_STICK_HOLE_SQUARED 3600
#define UI_PLAY_STICK_CAPTURE_SQUARED 2500
#define UI_PLAY_STICK_DEAD_SQUARED 16
#define UI_PLAY_STICK_RANGE_SQUARED 961
#define UI_PLAY_STICK_RANGE 31.0f

#define UI_PLAY_BOTTOM (UI_PLAY_TOP+UI_PLAY_CLUSTER_SIZE)

/* Menu and wordmark roles; hit capture and rendering share the menu frame. */
#define UI_PLAY_MENU_Y 0
#define UI_PLAY_MENU_LABEL_INSET 12
#define UI_PLAY_MENU_LABEL_Y 22
#define UI_PLAY_MENU_LABEL_SIZE 11
#define UI_PLAY_MENU_LABEL_SPACING 1.5f
#define UI_PLAY_MENU_ICON 14          // the menu button's pause mark
#define UI_PLAY_ICON_SCALE 1.2f       // an action icon (FF, rewind) for a slot's mark size
#define UI_PLAY_LABEL_PX 23.0f        // a slot's word mark: the 30 px-cell size whose capitals draw 10-11 px (13 drew 6)
#define UI_PLAY_LABEL_PAD 6           // the word's room inside its slot, each side
#define UI_PLAY_LABEL_DROP .24f       // the baseline under the slot's centre (half a capital), in label sizes
#define UI_PLAY_MENU_STRIPE_Y 27
#define UI_PLAY_MENU_STRIPE_STEP 11
#define UI_PLAY_MENU_STRIPE_W 9
#define UI_PLAY_MENU_STRIPE_H 2
#define UI_PLAY_MENU_STRIPES_MIRROR_INSET 28
#define UI_PLAY_WORDMARK_X 12
#define UI_PLAY_WORDMARK_MIRROR_X 246
#define UI_PLAY_WORDMARK_Y 214
#define UI_PLAY_WORDMARK_STRIPE_Y 220
#define UI_PLAY_WORDMARK_STRIPE_STEP 16
#define UI_PLAY_WORDMARK_STRIPE_W 14

#define UI_PLAY_NOTICE_X 12
#define UI_PLAY_NOTICE_Y 64
#define UI_PLAY_NOTICE_W 296
#define UI_PLAY_NOTICE_H 112
#define UI_PLAY_NOTICE_TEXT_X 24
#define UI_PLAY_NOTICE_TEXT_Y 80
#define UI_PLAY_NOTICE_TEXT_W 272

// Scanning surfaces: theme-independent black and white, four quiet modules.
#define UI_COLOR_QR_WHITE 0xffffffffu
#define UI_COLOR_QR_BLACK 0xff000000u
// Shared settings provenance marks and scope tag, independent of theme color.
#define UI_SOURCE_Z 0.7f
#define UI_SOURCE_RADIUS 4
#define UI_SOURCE_DOT 1.5f
#define UI_SOURCE_STROKE 2
#define UI_SETTINGS_TAG_X 282
#define UI_SETTINGS_TAG_Y 28
#define UI_SETTINGS_TAG_W 112         // a whole short sentence in four lines (the pause tag keeps UI_TAG_W)
#define UI_HELP_PAGE_CHARS 240          // a help page (a dialog pages it again when it is longer than one screen)
#define UI_HELP_MAX_PAGES 16
#define UI_TEXT_UTF8_MASK 0xc0
#define UI_TEXT_UTF8_CONT 0x80

// The channel on the scope's glass, centred on the room file's label strip ("slabel" anchor, projected; it was
// drawn from y 132 down, below the strip). It may run UI_SETTINGS_SCOPE_SPAN strips wide (the glass is wider
// there); the fixed values only when the anchor is not on screen.
#define UI_SETTINGS_SCOPE_X 167
#define UI_SETTINGS_SCOPE_Y 127       // the line's centre
#define UI_SETTINGS_SCOPE_W 78
#define UI_SETTINGS_SCOPE_SPAN 1.6f
#define UI_SETTINGS_SCOPE_MAX_W 130     // but never wider than the glass's dark face ("Game Boy Advance · Picture" ran off it)
#define UI_SETTINGS_SCOPE_SIZE .44f   // top screen: shrinks to UI_FONT_CONTEXT to fit (was .3)
#define UI_COLOR_SCOPE_PHOSPHOR 0xffa5f09a
