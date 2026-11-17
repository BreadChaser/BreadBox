// The room styles as data (Day, Night): the menus' semantic roles (the rustic room's) on the
// Original artwork defaults; the room itself takes its style from the room file (room_scene_style).
#include <string.h>
#include <3ds.h>
#include <math.h>
#include "libretro.h"
#include "theme.h"
#include "app_config.h"
#include "ui.h"
#include "theme.h"

#define C TH_RGB
#define CA TH_RGBA

#define ORIGINAL_GAME { \
 .bg=C(6,9,24), .title=C(235,238,255), .ink=C(43,51,96), .mark=C(200,207,240), \
 .border=C(31,42,90), .face=C(12,18,48), .gradient={C(40,58,130),C(24,34,84)}, .pressed_add=C(89,89,89), \
 .stick_rim=C(43,58,140), .stick_face=C(8,12,34), .stick_grid=CA(90,63,184,179), .stick_thumb=C(111,134,232), \
 .stripe={C(208,49,59),C(232,178,28),C(47,163,107),C(47,111,196)}, \
 .stats=C(175,183,198), .stats_focus=C(125,215,185), .budget_bg=C(30,35,42), .budget_over=C(220,130,90), .budget_ok=C(85,170,140), \
 .text=C(230,230,240), .panel=C(28,30,42), .notice=C(255,200,120), .warning=C(255,160,120), \
 .alert_bg=CA(20,8,4,225), .alert_rule=C(255,170,60), .alert_text=C(255,225,170), \
 .black=C(0,0,0), .osd_bg=CA(0,0,0,200), .speed_ink=C(255,255,255), .speed_shadow=CA(0,0,0,120), .speed_bg=CA(0,0,0,90) }
// The Original (v0.5.0) look. Every other theme starts from it.
#define ORIGINAL ORIGINAL_ROLES, ORIGINAL_ART
// Semantic UI roles (theme.h)
#define ORIGINAL_ROLES \
	.surface = C(6, 9, 24), .heading = C(235, 238, 255), .heading_scope = C(110, 120, 160), \
	.accent = { C(230, 30, 60), C(250, 205, 20), C(30, 190, 120), C(40, 110, 230) }, \
	.select_glow = CA(60, 90, 200, 40), .select_top = CA(40, 58, 130, 235), .select_bottom = CA(24, 34, 84, 235), \
	.row_dim = { 150, 150, 180 }, .row_lit = { 255, 255, 255 }, \
	.value = C(130, 160, 230), .section = C(125, 145, 190), .section_line = C(30, 42, 72), \
	.text_muted = C(130, 150, 190), .text_bright = C(230, 235, 255), \
	.hint_lib = C(90, 100, 135), .hint_menu = C(110, 120, 160), \
	.message = C(255, 210, 120), .danger = C(255, 130, 130), .scrollbar = C(60, 80, 140), \
	.help_bg = CA(6, 9, 24, 225), .help_bar = C(40, 110, 230), .help_title = C(235, 238, 255), \
	.help_impact = C(250, 205, 20), .help_text = C(170, 180, 215), \
	.banner_bg = CA(40, 22, 14, 230), .banner_text = C(255, 170, 120), \
	.game_title = C(255, 255, 255), .game_sub = C(170, 185, 230), .veil = CA(2, 4, 16, 170), \
	.touch_face = C(12, 18, 48), .touch_border = C(31, 42, 90), \
	.card_top = C(20, 26, 60), .card_bottom = C(8, 10, 28), \
	.card_stack = C(40, 46, 70), .version_text = CA(150, 160, 200, 160), \
	.wordmark = CA(245, 240, 255, 200), .wordmark_accent = CA(255, 90, 190, 200), \
	.folder = C(170, 185, 220), .shadow = C(0, 0, 0), .light = C(255, 255, 255), \
	.icon_a = C(90, 215, 255), .icon_b = C(255, 80, 200), .icon_glow = CA(255, 90, 210, 150)
// Artwork palettes: gameplay, controls
#define ORIGINAL_ART \
	.game=ORIGINAL_GAME, \
	.input.face={C(47,163,107),C(208,49,59),C(47,111,196),C(215,127,181)}, \
	.input.shoulder=C(176,90,222), .input.stick=C(69,191,208), .input.other=C(154,165,200), .input.hide=C(43,51,96)


// Gameplay keeps a quiet charcoal bottom screen in every room style.
#define NEUTRAL_GAME { \
 .bg=C(18,18,20), .title=C(232,233,235), .ink=C(66,68,73), .mark=C(196,199,204), \
 .border=C(58,60,65), .face=C(30,31,34), .gradient={C(72,77,86),C(54,57,64)}, .pressed_add=C(80,80,80), \
 .stick_rim=C(72,77,86), .stick_face=C(22,23,26), .stick_grid=CA(138,154,176,150), .stick_thumb=C(150,165,186), \
 .stripe={C(138,154,176),C(112,124,142),C(92,101,116),C(72,79,91)}, \
 .stats=C(176,179,184), .stats_focus=C(170,185,206), .budget_bg=C(40,42,46), .budget_over=C(214,130,110), .budget_ok=C(138,154,176), \
 .text=C(228,229,232), .panel=C(30,31,34), .notice=C(226,190,120), .warning=C(230,160,130), \
 .alert_bg=CA(28,24,18,225), .alert_rule=C(226,190,120), .alert_text=C(240,226,196), \
 .black=C(0,0,0), .osd_bg=CA(0,0,0,200), .speed_ink=C(255,255,255), .speed_shadow=CA(0,0,0,120), .speed_bg=CA(0,0,0,90) }
// The menus' room colours (the rustic room of the approved bookshelf mock, js/breadbox-r8.js).
static const theme_room_t rustic_room = {
	.wall = C(84, 61, 44), .floor = C(110, 72, 40), .skirting = C(42, 22, 10),
	.wood = C(176, 118, 66), .back = C(104, 66, 36), .upright = C(160, 106, 60),
	.brass = C(240, 208, 138), .plate_ink = C(58, 36, 12),
	.desk = C(104, 64, 36), .paper = C(243, 230, 200), .paper_edge = C(216, 192, 143),
	.ink = C(30, 18, 8), .ink_soft = C(74, 54, 34), .rule = CA(150, 40, 30, 180), .rule_soft = CA(70, 100, 140, 56),
	.stamp = CA(163, 36, 28, 210),
	.light = C(255, 214, 150), .shade = CA(8, 3, 0, 184),
	.sleeve = C(122, 68, 48),
	.leaf = { C(63, 106, 50), C(90, 138, 68) }, .wax = C(240, 226, 196),
	.cork = C(178, 128, 80), .pin = { C(200, 48, 40), C(56, 140, 84), C(52, 96, 176), C(216, 170, 48) }, .postmark = CA(44, 40, 70, 190), .card_focus_icon=C(176,118,66), .card_dim_opacity=110.0f/255 };
// Warm walnut controls in game; the menus are the room.
#define ROOM_RUSTIC \
	.game = NEUTRAL_GAME, .input.shoulder = C(176,120,70), .input.stick = C(214,166,92), \
	.input.face = { C(86,140,84), C(196,82,68), C(84,112,170), C(196,120,150) }

// later designators override the Original's
#pragma GCC diagnostic ignored "-Woverride-init"
// The menus' roles, the same in every room style (the rustic room's)
#define ROOM_MENU_ROLES \
		.surface = C(40, 24, 13), .heading = C(246, 232, 206), .heading_scope = C(196, 160, 112), \
		.accent = { C(214, 166, 92), C(214, 166, 92), C(214, 166, 92), C(214, 166, 92) }, \
		.select_glow = CA(240, 190, 110, 30), .select_top = CA(132, 84, 40, 235), .select_bottom = CA(96, 58, 28, 235), \
		.row_dim = { 214, 194, 160 }, .row_lit = { 255, 242, 216 }, \
		.value = C(232, 190, 120), .section = C(196, 160, 112), .section_line = C(92, 60, 32), \
		.text_muted = C(184, 156, 118), .text_bright = C(250, 236, 210), \
		.hint_lib = C(170, 140, 100), .hint_menu = C(170, 140, 100), \
		.message = C(255, 214, 140), .danger = C(240, 128, 110), .scrollbar = C(140, 98, 56), \
		.help_bg = CA(30, 18, 9, 235), .help_bar = C(214, 166, 92), .help_title = C(250, 236, 210), \
		.help_impact = C(255, 206, 120), .help_text = C(220, 198, 164), \
		.banner_bg = CA(60, 30, 14, 235), .banner_text = C(255, 190, 130), \
		.game_title = C(255, 244, 224), .game_sub = C(206, 176, 136), .veil = CA(16, 9, 4, 200), \
		.touch_face = C(52, 32, 16), .touch_border = C(108, 72, 40), \
		.card_top = C(84, 54, 30), .card_bottom = C(52, 32, 16), \
		.card_stack = C(110, 74, 42), .version_text = CA(214, 180, 130, 170), \
		.wordmark = CA(250, 236, 210, 230), .wordmark_accent = CA(232, 170, 90, 240), .folder = C(214, 190, 150), \
		.icon_a = C(255, 214, 150), .icon_b = C(214, 150, 80), .icon_glow = CA(255, 190, 110, 120), \
		.input.other = C(184, 156, 118), .input.hide = C(70, 46, 26), \
		.fx[0] = C(232, 170, 90), .fx[1] = C(140, 90, 50), \
		.bar = CA(20, 10, 4, 150), .title_shadow = C(0, 0, 0), .cover_dim = C(30, 18, 9)

// Room styles (owner 2026-11-06): a theme is the room's style, its walls, wood, floor and light
// (the room file's colour stream of the same id, room_scene_style); the menus keep the rustic
// roles in every style. Day is the default. Old theme ids (the rustic room, the plain and the
// animated themes) load as Day (theme_from_saved_id).
// Cards, postcards and notices retain their shared geometry in every look.
static const theme_room_t psx3d_cards = {
 .wall=C(6,9,24), .floor=C(6,9,24), .skirting=C(6,9,24),
 .wood=C(12,18,48), .back=C(12,18,48), .upright=C(12,18,48),
 .desk=C(6,9,24), .paper=C(20,26,60), .paper_edge=C(30,40,82),
 .ink=C(235,238,255), .ink_soft=C(180,190,225), .rule=C(170,185,230), .rule_soft=CA(170,185,230,45),
 .stamp=C(180,190,225), .brass=C(235,238,255), .plate_ink=C(6,9,24),
 .light=C(235,238,255), .shade=CA(0,0,0,90), .sleeve=C(20,26,60),
 .leaf={C(160,190,230),C(170,190,230)}, .wax=C(6,9,24),
 .cork=C(6,9,24), .pin={C(230,30,60),C(250,205,20),C(30,190,120),C(40,110,230)}, .postmark=C(180,190,225),
 .card_focus_icon=C(235,238,255), .card_dim_opacity=1.0f,
};
static const theme_room_t simple_cards = {
 .wall=C(16,17,19), .floor=C(16,17,19), .skirting=C(16,17,19),
 .wood=C(26,28,32), .back=C(26,28,32), .upright=C(26,28,32),
 .desk=C(16,17,19), .paper=C(26,28,32), .paper_edge=C(40,43,50),
 .ink=C(241,242,244), .ink_soft=C(195,198,204), .rule=C(185,189,197), .rule_soft=CA(185,189,197,45),
 .stamp=C(195,198,204), .brass=C(241,242,244), .plate_ink=C(16,17,19),
 .light=C(241,242,244), .shade=CA(0,0,0,90), .sleeve=C(26,28,32),
 .leaf={C(195,198,204),C(195,198,204)}, .wax=C(16,17,19),
 .cork=C(16,17,19), .pin={C(195,198,204),C(195,198,204),C(195,198,204),C(195,198,204)}, .postmark=C(195,198,204),
 .card_focus_icon=C(241,242,244), .card_dim_opacity=1.0f,
};
#define LIST_PSX3D_ROLES \
 .game.ink=C(170,185,230), .heading_scope=C(170,185,230), .row_dim={180,190,225}, .value=C(180,195,240), \
 .section=C(170,185,230), .text_muted=C(170,185,230), .hint_lib=C(180,190,225), .hint_menu=C(180,190,225), \
 .game_sub=C(180,195,235), .version_text=C(170,185,230), .bar=C(12,18,48)
#define SIMPLE_GAME { \
 .bg=C(16,17,19), .title=C(241,242,244), .ink=C(185,189,197), .mark=C(210,210,210), \
 .border=C(100,100,100), .face=C(26,28,32), .gradient={C(70,70,70),C(50,50,50)}, .pressed_add=C(80,80,80), \
 .stick_rim=C(100,100,100), .stick_face=C(26,28,32), .stick_grid=CA(160,160,160,150), .stick_thumb=C(190,190,190), \
 .stripe={C(190,190,190),C(160,160,160),C(130,130,130),C(100,100,100)}, \
 .stats=C(195,198,204), .stats_focus=C(241,242,244), .budget_bg=C(40,40,40), .budget_over=C(241,242,244), .budget_ok=C(185,189,197), \
 .text=C(241,242,244), .panel=C(26,28,32), .notice=C(241,242,244), .warning=C(241,242,244), \
 .alert_bg=C(26,28,32), .alert_rule=C(185,189,197), .alert_text=C(241,242,244), \
 .black=C(0,0,0), .osd_bg=CA(0,0,0,200), .speed_ink=C(255,255,255), .speed_shadow=CA(0,0,0,120), .speed_bg=CA(0,0,0,90) }
#define LIST_SIMPLE_ROLES \
 .surface=C(16,17,19), .heading=C(241,242,244), .heading_scope=C(185,189,197), \
 .accent={C(210,212,216),C(210,212,216),C(210,212,216),C(210,212,216)}, \
 .select_glow=CA(210,212,216,20), .select_top=C(56,61,70), .select_bottom=C(56,61,70), \
 .row_dim={195,198,204}, .row_lit={241,242,244}, .value=C(210,212,216), \
 .section=C(185,189,197), .section_line=C(92,96,104), .text_muted=C(185,189,197), .text_bright=C(241,242,244), \
 .hint_lib=C(195,198,204), .hint_menu=C(195,198,204), .message=C(241,242,244), .danger=C(241,242,244), .scrollbar=C(140,145,154), \
 .help_bg=C(16,17,19), .help_bar=C(210,212,216), .help_title=C(241,242,244), .help_impact=C(241,242,244), .help_text=C(195,198,204), \
 .banner_bg=C(26,28,32), .banner_text=C(241,242,244), .game_title=C(241,242,244), .game_sub=C(185,189,197), .veil=CA(0,0,0,170), \
 .touch_face=C(26,28,32), .touch_border=C(92,96,104), .card_top=C(26,28,32), .card_bottom=C(26,28,32), \
 .card_stack=C(56,61,70), .version_text=C(185,189,197), .wordmark=C(241,242,244), .wordmark_accent=C(241,242,244), .folder=C(210,212,216), \
 .icon_a=C(241,242,244), .icon_b=C(185,189,197), .icon_glow=CA(255,255,255,0), \
 .game=SIMPLE_GAME, .input.face={C(210,212,216),C(210,212,216),C(210,212,216),C(210,212,216)}, \
 .input.shoulder=C(195,198,204), .input.stick=C(195,198,204), .input.other=C(195,198,204), .input.hide=C(92,96,104), \
 .fx={C(185,189,197),C(92,96,104)}, .bar=C(26,28,32), .title_shadow=C(0,0,0), .cover_dim=C(16,17,19)

static const theme_t themes[] = {
	{ ORIGINAL, ROOM_RUSTIC, .id = "day", .name = "Day", .room = &rustic_room, .icon = UI_ICON_SUN, ROOM_MENU_ROLES },
	{ ORIGINAL, ROOM_RUSTIC, .id = "night", .name = "Night", .room = &rustic_room, .icon = UI_ICON_MOON, ROOM_MENU_ROLES },
 { ORIGINAL, LIST_PSX3D_ROLES, .id="psx3d", .name="PSX3D", .room=&psx3d_cards, .look=BB_LOOK_PSX3D },
 { ORIGINAL, LIST_SIMPLE_ROLES, .id="simple", .name="Simple", .room=&simple_cards, .look=BB_LOOK_SIMPLE },
};
#define NTHEMES ((int)(sizeof(themes) / sizeof(themes[0])))

int theme_count(void) { return NTHEMES; }
const theme_t *theme_get(int i) { return &themes[i >= 0 && i < NTHEMES ? i : 0]; }
// Refreshed only on config load, theme selection or game launch. ui_input_color itself
// has no config/theme lookup, including when called by the touch controller.
u32 g_input_palette[20] __attribute__((section(".theme_game.bss")));
const theme_t *theme(void) { return theme_get(g_cfg.theme); }
void libui_look_changed(void) __attribute__((weak));
void theme_menu_refresh(void)
{
 if(libui_look_changed)libui_look_changed();
 const theme_t *t = theme_get(g_cfg.theme);
 g_input_palette[0] = t->input.hide;
 for (int i = 1; i < 20; i++) g_input_palette[i] = t->input.other;
 g_input_palette[RETRO_DEVICE_ID_JOYPAD_X+1] = t->input.face[0];
 g_input_palette[RETRO_DEVICE_ID_JOYPAD_A+1] = t->input.face[1];
 g_input_palette[RETRO_DEVICE_ID_JOYPAD_B+1] = t->input.face[2];
 g_input_palette[RETRO_DEVICE_ID_JOYPAD_Y+1] = t->input.face[3];
 g_input_palette[RETRO_DEVICE_ID_JOYPAD_L+1] = t->input.shoulder;
 g_input_palette[RETRO_DEVICE_ID_JOYPAD_R+1] = t->input.shoulder;
 g_input_palette[RETRO_DEVICE_ID_JOYPAD_L2+1] = t->input.shoulder;
 g_input_palette[RETRO_DEVICE_ID_JOYPAD_R2+1] = t->input.shoulder;
 g_input_palette[RETRO_DEVICE_ID_JOYPAD_L3+1] = t->input.stick;
 g_input_palette[RETRO_DEVICE_ID_JOYPAD_R3+1] = t->input.stick;
}

int theme_from_id(const char *id)
{
	for (int i = 0; i < NTHEMES; i++)
		if (!strcmp(themes[i].id, id))
			return i;
	return -1;
}

u32 theme_alpha(u32 c, int a)
{
	return (c & 0x00ffffff) | ((u32)(a < 0 ? 0 : a > 255 ? 255 : a) << 24);
}

u32 theme_row(const theme_t *t, float lift, int a)
{
	int v[3];
	for (int i = 0; i < 3; i++) {
		v[i] = (int)(t->row_dim[i] + (t->row_lit[i] - t->row_dim[i]) * lift);
		v[i] = v[i] < 0 ? 0 : v[i] > 255 ? 255 : v[i];
	}
	return TH_RGBA(v[0], v[1], v[2], a);
}

u32 theme_icon_dim(const theme_t *t, int a)
{
	int v[3];
	for (int i = 0; i < 3; i++)
		v[i] = t->row_dim[i] + ((int)((t->surface >> (8 * i)) & 255) - t->row_dim[i]) * 15 / 100;
	return TH_RGBA(v[0], v[1], v[2], a < 0 ? 0 : a > 255 ? 255 : a);
}

int theme_default(void)
{
	int i = theme_from_id(THEME_DEFAULT_ID);
	return i < 0 ? 0 : i;
}

int theme_from_saved_id(const char *id)
{
	int i = id ? theme_from_id(id) : -1;
	return i < 0 ? theme_default() : i;   // a retired theme (rustic-room, light, dark, original, ...): Day
}
