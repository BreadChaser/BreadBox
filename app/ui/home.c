#include "ht_trace.h"
#include "ui_motion.h"
#include "share.h"
#include "bb_settings.h"
#include "app_policy.h"
// The main menu BreadBox opens to: the room (room.h). Its places are the menu's
// actions (the tiles below: Continue, Library, Play together, Share, Settings, Quit);
// without the room's file there is no main menu (room_missing: one card, then close).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "theme.h"
#include "icons.h"
#include "ui_format.h"
#include "app_paths.h"
#include "app_log.h"
#include "app_config.h"
#include "settings_ui.h"
#include "last_picture.h"
#include "bb_platform.h"
#include "platform_video.h"
#include "dev_switches.h"
#include "savestates.h"
#include "app_input.h"
#include "ui_pause.h"
#include "ui.h"
#include "library.h"
#include "covers.h"
#include "uisound.h"
#include "libui.h"
#include "play_together.h"
#include "hand_styles.h"
#include "home.h"
#include "games_dir.h"
#include "app_main.h"
#include "devtools.h"
#include "scene.h"
#include "bb_registry.h"
#include "bb_save.h"
#include "bb_index.h"
#include "postcards.h"
#include "breadbox_formats.h"
#include "room.h"
#include "bb_update.h"
#include "breadbox_import.h"
#include "shelf.h"
#include "welcome.h"
#include "cold_code.h"
#include "place_cards.h"
#include "playlog.h"
int breadbox_systems_run(char *path, size_t len);

// the one card when the room cannot load (home_run): no main menu without it
#define HOME_ROOM_MISSING_TEXT "Its room file is missing or there is not enough memory. Start BreadBox again; if this keeps happening, reinstall it."

enum { E_CONTINUE, E_LIBRARY, E_HOST, E_JOIN, E_SHARE, E_SETTINGS, E_QUIT, E_TOGETHER };   // E_TOGETHER: the room's Play together tab, "coming soon" (no Host / Join, no network code)
enum { K_HERO, K_MID, K_SMALL, K_SUB };

typedef struct { int id, kind; float x, y, w, h; } tile_t;

static u32 accent(int i, u8 a)
{
	return theme_alpha(theme()->accent[i & 3], a);
}
#define MENU_BG (theme()->surface)

// ---------------------------------------------------------------- state

static bool have_games;
static bool cont_ok;                 // a last game with a Continue auto-save
static u32 cont_saved;
static int cont_band;                // black rows above and below the game in the Continue picture
static const char *cont_stamp;       // the Continue game's system on the postcard's stamp
static C3D_Tex cont_tex;
static bool cont_tex_ok;
static C3D_Tex card_tex;             // the TV's picture on the home card (a GBA game's: it has no cont_tex)
static bool card_tex_ok;
static char continue_save[PATH_LEN], continue_title[PATH_LEN];
static char cont_path[PATH_LEN];

static int sel_id = -1;              // selected entry; the first one at start
static float clock_s, fade = 1, pop = 1, pop_v, title_t = 1;
static int title_for = -1;
static C2D_TextBuf mbuf;             // measuring only

static tile_t tiles[6];
static int ntiles;
static tile_t group;                 // the Play together frame around Host and Join
static bool have_group;
static int picks[3], npicks;         // the Library tile's covers, chosen each start

// three different games at random for the Library tile
static void pick_covers(void)
{
	int n = library_count();
	npicks = 0;
	if (n <= 0)
		return;
	u32 seed = (u32)svcGetSystemTick();   // a small generator of our own: no new library code
	while (npicks < 3 && npicks < n) {
		seed = seed * 1664525u + 1013904223u;
		int g = (int)((seed >> 8) % (u32)n), dup = 0;
		for (int i = 0; i < npicks; i++)
			dup |= picks[i] == g;
		if (!dup)
			picks[npicks++] = g;
	}
}

static float text_w(C2D_Font f, float size, const char *s); /* shared presentation */

static const char *cont_title(void)
{
 if(cont_ok && continue_title[0])return continue_title;
	return g_cfg.last_game[0] ? g_cfg.last_game : g_cfg.last_game_state;
}

// Capture the existing legacy metadata without reading files. Both Continue
// implementations use this one compatibility boundary until typed resume refs.
static void cont_capture_prefix(char *out, size_t cap)
{
 out[0]=0;
	if (!g_cfg.last_game_state[0] || !g_cfg.last_game_path[0])
		return;
	char prefix[PATH_LEN];
	snprintf(prefix, sizeof(prefix), "%s/%.*s", PSX3D_STATE_DIR, 400, g_cfg.last_game_state);
 snprintf(out,cap,"%s",prefix);
}

// The game the main menu would continue: its auto-save, picture and a path
// that still exists (the disc may have moved; then the library's copy).
#include "home_continue.inc"

// Play together is a frame with two buttons, Host and Join (owner: no sub-menu).
static void together_at(float x, float y, float w, float h); /* shared presentation */

// Play together arrives with multiplayer (v0.6): until then the tile is not
// shown and the main menu is Continue, Library, Share PSX3D and Settings.
#define HOME_PLAY_TOGETHER 0

static void layout(void); /* shared presentation */

static int sel_index(void)
{
	for (int i = 0; i < ntiles; i++)
		if (tiles[i].id == sel_id)
			return i;
	return 0;
}

// The main menu is a 3D room (room.h). Each tile is a place in it
// (Continue the TV, Library the bookcase, Settings the scope, Play together the
// cushions, Share the coffee table); the tiles stay on the bottom screen.
// D-pad and L/R move the focus, A glides to the place, B glides back. Arriving
// opens the place's own screen; at the TV A continues, at the cushions left and
// right pick Host or Join.
static room_view room;
static int room_return = -1;   // left to this place's own screen: the next start glides back from it
static bool room_loading;          // the Library's screen is being built: the room shows a cue
#define ROOM_QUIT_PLACE (ROOM_BOARD + 1)   // the Quit tab's card: no place of the room
static bool room_quit_on;          // the bar's last tab, Quit, is the focus: no place picked out, no sign, the camera stays at the plain home view
#define ROOM_INTRO_SECONDS UI_MOTION_INTRO_S           // the lamp comes on, then the room
#define HOME_OFFER_FRAMES 20              // the PSX3D import offer waits this many room frames after the intro
static float room_intro_wait;
static u64 room_intro_tick;
static float room_intro = 1;       // 0..1 while the room comes up out of the dark at the app's start (1: not now)

// Sounds by the UI mock's bb-sfx event names. The room's own sounds are not in
// the app yet: these play the menu's (arrive: none).
static void room_sfx(const char *name)
{
	if (!strcmp(name, "focus")) sfx_play(SFX_MOVE);
	else if (!strcmp(name, "glide")) sfx_play(SFX_SELECT);
	else if (!strcmp(name, "back")) sfx_play(SFX_BACK);
}

static int place_of(int id)
{
	switch (id) {
	case E_CONTINUE: return ROOM_CONTINUE;
	case E_LIBRARY: return ROOM_LIBRARY;
	case E_HOST:
	case E_JOIN:
	case E_TOGETHER:
		return ROOM_TOGETHER;
	case E_SHARE: return ROOM_SHARE;
	}
	return ROOM_SETTINGS;
}

static int tile_of(int place)
{
	for (int i = 0; i < ntiles; i++)
		if (place_of(tiles[i].id) == place)
			return i;
	return -1;
}

// the next shown place that way (a place without its tile is skipped)
static int room_move(int place, int dir)
{
	int q = room_nav(place, dir);
	for (int n = 0; q >= 0 && tile_of(q) < 0 && n < ROOM_PLACES; n++)
		q = room_nav(q, dir);
	return q >= 0 && tile_of(q) >= 0 ? q : -1;
}

// The places in the bottom bar's order (owner 2026-10-30: Settings before Share); left / right
// and L / R go through them in this order too, each one a stop (Share was only below the
// bookcase on the map, so left / right never reached it)
static const int place_order[ROOM_PLACES] = { ROOM_CONTINUE, ROOM_LIBRARY, ROOM_TOGETHER, ROOM_SETTINGS, ROOM_SHARE };
// the next shown place that way in the bar's order; -1 past the first / last one
static int room_next(int place, int step)
{
	int at = 0;
	while (at < ROOM_PLACES && place_order[at] != place) at++;
	for (at += step; at >= 0 && at < ROOM_PLACES; at += step)
		if (tile_of(place_order[at]) >= 0)
			return place_order[at];
	return -1;
}

// The bottom bar at home: one tab per shown place, in the mock's order.
static int room_tabs(int *place)
{
	int n = 0;
	for (int i = 0; i < ROOM_PLACES; i++)
		if (tile_of(place_order[i]) >= 0) place[n++] = place_order[i];
	return n;
}

// At a place that stays in the room: its actions (B back to the room on the left, A on the right).
static const ui_button_t room_at_tv[] = { { KEY_B, "Room", UI_ICON_BACK, false }, { KEY_A, "Continue", UI_ICON_PLAY, false } };
static const ui_button_t room_at_cushions[] = { { KEY_B, "Room", UI_ICON_BACK, false }, { KEY_DLEFT, "Host", UI_ICON_PADS, false },
	{ KEY_DRIGHT, "Join", UI_ICON_PADS, false }, { KEY_A, "Start", UI_ICON_OPEN, false } };
static const ui_button_t room_at_wall[] = { { KEY_B, "Room", UI_ICON_BACK, false } };
static const ui_button_t *room_actions(int *n)
{
	int place = room.mode == ROOM_GO || room.mode == ROOM_AT ? room.place : -1;
	if (place >= ROOM_PLACES) { *n = 1; return room_at_wall; }
	if (place == ROOM_CONTINUE) { *n = 2; return room_at_tv; }
	if (place == ROOM_TOGETHER) { *n = HOME_PLAY_TOGETHER ? 4 : 1; return HOME_PLAY_TOGETHER ? room_at_cushions : room_at_wall; }   // coming soon: only B
	*n = 0;
	return NULL;
}

// The TV is where Continue lives: it shows where you left off (the Continue
// save's picture, else the last game's picture taken at quit), else that game's
// cover, else snow. Baked into a texture once, when it changes.
static int room_tv_cover = -1;   // the cover the TV waits for (covers load in the background)
static int cover_of(const char *path)
{
	const bb_system_t *s = path && path[0] ? bb_system_for_path(path) : NULL;
	if (!s) return -1;
	int n = bb_system_library_count(s);
	for (int row = 0; row < n; row++) {
		bb_catalog_item it;
		memset(&it, 0, sizeof(it));
		if (s->shell && s->shell->catalog) {
			if (!s->shell->catalog(row, &it)) continue;
		} else {
			bb_entry e;
			if (!bb_index_get(s, row, it.path, sizeof(it.path), &e)) continue;
			it.cover_id = bb_system_cover_id(s, row);
		}
		if (!strcmp(it.path, path)) return it.cover_id;
	}
	return -1;
}

static char room_tv_key[300];   // what the TV shows now: made again only when it changes
static void room_tv(void)
{
	char key[sizeof(room_tv_key)];
	snprintf(key, sizeof(key), "%d|%d|%lu|%s", cont_ok, cont_tex_ok, (unsigned long)cont_saved, cont_ok ? cont_path : "");
	if (!strcmp(key, room_tv_key)) return;
	snprintf(room_tv_key, sizeof(room_tv_key), "%s", key);
	room_tv_cover = -1;
	if (card_tex_ok) video_gpu_idle();   // the last frame may still read the card's picture
	if (card_tex_ok) C3D_TexDelete(&card_tex);
	card_tex_ok = false;
	if (cont_ok && cont_tex_ok) {   // the PS1 Continue save's own picture
		static const Tex3DS_SubTexture thumb = { 256, 192, 0, 1, 1, 1 - 192 / 256.0f };
		if(continue_tv)room_scene_tv_upload(continue_tv,false);
		else room_scene_tv_tex(&cont_tex, &thumb);
		return;
	}
	// the last game's picture, at once: right after a quit the library may not know the
	// Continue game yet (cont_ok comes later), the picture taken at the quit is that game's
	if (continue_picture && continue_pixels) {
        if(continue_tv)room_scene_tv_upload(continue_tv,false);
        else room_scene_tv(continue_pixels);
        if(continue_card)card_tex_ok=room_scene_picture_upload(&card_tex,continue_card);
        return;
    }
	room_tv_cover = cont_ok ? cover_of(cont_path) : -1;
	room_scene_tv(NULL);   // snow, until the cover has loaded (room_tv_poll)
}

static void room_tv_poll(int frame)
{
	room_scene_tv_poll();
	if (room_tv_cover < 0 || frame % 30) return;
	Tex3DS_SubTexture sub;
	C3D_Tex *t = covers_get(room_tv_cover, &sub);
	if (t) {
		room_scene_tv_tex(t, &sub);
		room_tv_cover = -1;
	}
}

// Your hands while Skin tone or Gloves is chosen in Settings: raised in front of
// the scope (picker_in, then picker_idle in a loop, picker_out when you move on).
static int picker_state;     // 0 none, 1 in / idle, 2 out
static double picker_t0;
static const hands_clip *room_picker(float *t)
{
	if (!room_show_hands) return NULL;
	const hands_file *f = room_hands_open() ? room_hands_file() : NULL;
	if (!f) return NULL;
	bool mitten = g_cfg.hand_glove == HAND_MITTENS;
	double now = (double)svcGetSystemTick() / SYSCLOCK_ARM11;   // seconds; double keeps the fractions
	bool want = settings_ui_hands_row();
	if (want && picker_state != 1) { picker_state = 1; picker_t0 = now; }
	if (!want && picker_state == 1) { picker_state = 2; picker_t0 = now; }
	if (!picker_state) return NULL;
	*t = (float)(now - picker_t0);
	if (picker_state == 2) {
		const hands_clip *out = hands_clip_named(f, "picker_out", mitten);
		if (!out || *t >= hands_clip_seconds(f, out)) { picker_state = 0; return NULL; }
		return out;
	}
	const hands_clip *in = hands_clip_named(f, "picker_in", mitten);
	float d = hands_clip_seconds(f, in);
	if (in && *t < d) return in;
	const hands_clip *idle = hands_clip_named(f, "picker_idle", mitten);
	if (!idle) return NULL;
	*t = fmodf(*t - d, hands_loop_seconds(f, idle));
	return idle;
}

// The room is the main menu: false when its file is missing or there is no memory for it
static bool room_start(void)
{
	bool ok = room_scene_open();
	boot_mark("room file");
	if (!ok)
		return false;
	room_scene_library_follow(true);   // the bookcase it had keeps showing while the library is read in again
	boot_mark("bookcase");
	room_tv_key[0] = 0;   // back in the room (maybe from a game): its picture again
	room_tv();
	boot_mark("TV picture");
	room_view_init(&room, room_scene_file(), place_of(sel_id), room_return);
	room_pose wall;   // the Library is the bookcase's wall view (back from it: glide from there)
	if (shelf_wall_pose(&wall)) room_view_set_library(&room, &wall);
	room_return = -1;
	return true;
}

// The room's controls; returns the tile action to run now, or -1.
static int room_input(u32 k)
{
	static const u32 pads[4] = { KEY_DUP | KEY_CPAD_UP, KEY_DDOWN | KEY_CPAD_DOWN, KEY_DLEFT | KEY_CPAD_LEFT, KEY_DRIGHT | KEY_CPAD_RIGHT };
	static const int dirs[4] = { ROOM_UP, ROOM_DOWN, ROOM_LEFT, ROOM_RIGHT };
	if (room.mode == ROOM_HOME) {
		int focus = room.focus;
		// left / right: the bar's order, every place a stop (Share too), Quit the last; the ends stop there.
		// The poster and the corkboard (room_walls.h) are seen from the room, not visited: nothing there
		// opens (deep UI test F02/F18), so the D-pad never takes the camera to them.
		if (room_quit_on) {   // the bar's last tab, Quit: A asks, any other way back to Share, a tap on the bar
			if (k & KEY_A) return E_QUIT;
			if (k & (pads[3] | KEY_R)) return -1;   // the last stop
			if (k & (pads[0] | pads[1] | pads[2] | KEY_L)) {
				room_quit_on = false;
				room_sfx("focus");
				return -1;
			}
			if (k & KEY_X) room_quit_on = false;
			else if (!(k & KEY_TOUCH)) return -1;
		}
		if (room_next(focus, 1) == -1 && (k & (pads[3] | KEY_R))) {   // past the last place (Share): the Quit tab
			room_quit_on = true;
			room_sfx("focus");
			return -1;
		}
		for (int d = 0; d < 2; d++)   // up / down: the room's map (the cushions, the table in front)
			if ((k & pads[d]) && room_move(focus, dirs[d]) >= 0)
				focus = room_move(focus, dirs[d]);
		if ((k & pads[2]) && room_next(focus, -1) >= 0) focus = room_next(focus, -1);
		if ((k & pads[3]) && room_next(focus, 1) >= 0) focus = room_next(focus, 1);
		if ((k & KEY_L) && room_next(focus, -1) >= 0) focus = room_next(focus, -1);
		if ((k & KEY_R) && room_next(focus, 1) >= 0) focus = room_next(focus, 1);
		int tabs[ROOM_PLACES], ntabs = room_tabs(tabs), hit = ui_tabs_hit(ntabs + 1, k);
		if (hit == ntabs) {   // the Quit tab: tapped once it is the focus, tapped again it asks
			if (room_quit_on) return E_QUIT;
			room_quit_on = true;
			room_sfx("focus");
			return -1;
		}
		if (hit >= 0) {   // a tab: that place; tapped again, go there
			room_quit_on = false;
			if (tabs[hit] == focus) k |= KEY_A;
			focus = tabs[hit];
		}
		if (tile_of(focus) < 0)
			focus = place_of(tiles[0].id);
		if (focus != room.focus) {
			room.focus = focus;
			if (!(k & KEY_A)) room_sfx("focus");
		}
		if (place_of(sel_id) != focus)
			sel_id = tiles[tile_of(focus)].id;
		if (k & KEY_X) room.focus = ROOM_SETTINGS;
		if (k & (KEY_A | KEY_X)) {
			if (room.focus == ROOM_SHARE) return E_SHARE;   // Share owns the continuous entry and B reversal
			if (room.focus == ROOM_SETTINGS) return E_SETTINGS;   // Settings owns entry, B reversal and the return glide
			room_view_go(&room, room.focus);
			room_sfx("glide");
		}
		return -1;
	}
	if (room.mode != ROOM_AT)   // gliding
		return -1;
	int nact;
	const ui_button_t *act = room_actions(&nact);
	if (act) k = ui_bar_input(act, nact, k);
	if (k & KEY_B) {
		room_view_back(&room);
		room_sfx("back");
		return -1;
	}
	switch (room.place) {
	case ROOM_CONTINUE:
		return (k & KEY_A) ? E_CONTINUE : -1;
	case ROOM_TOGETHER:
		if (!HOME_PLAY_TOGETHER) return -1;   // coming soon: A does nothing
		if ((k & (KEY_DLEFT | KEY_CPAD_LEFT)) && sel_id != E_HOST) { sel_id = E_HOST; room_sfx("focus"); }
		if ((k & (KEY_DRIGHT | KEY_CPAD_RIGHT)) && sel_id != E_JOIN) { sel_id = E_JOIN; room_sfx("focus"); }
		return (k & KEY_A) ? sel_id : -1;
	case ROOM_LIBRARY:
		return E_LIBRARY;
	case ROOM_SHARE:
		return E_SHARE;
	}
	return E_SETTINGS;
}

// Borrow this exact room, not a second camera or scene instance.
static void share_room_enter(void *ud) { room_view_go(ud, ROOM_SHARE); room_sfx("glide"); }
static void share_room_leave(void *ud) { room_view_back(ud); room_sfx("back"); }
static bool share_room_settled(void *ud)
{
 room_view *v = ud;
 return v->mode == ROOM_AT || v->mode == ROOM_HOME;
}
static void share_room_tick(void *ud, float dt)
{
 room_view *v = ud;
 if (v->mode == ROOM_BACK) v->invitation = fmaxf(0, v->invitation - dt / UI_MOTION_UI_S);
 if (v->mode != ROOM_AT && v->mode != ROOM_HOME) room_view_step(v, dt);
 if (v->mode == ROOM_GO || v->mode == ROOM_AT)
  v->invitation = v->mode == ROOM_AT ? 1 : ui_motion_ease((v->t - v->t0 - (UI_MOTION_CAMERA_S - UI_MOTION_UI_S)) / UI_MOTION_UI_S);
}
static void share_room_top(void *ud) { room_scene_draw(ud, NULL, 0); }
// Library, first visit: its shelves are built here (the cue is on screen), then the room glides back home after it.
static void room_to_library(void)
{
	shelf_build_now();
	room_return = ROOM_LIBRARY;
}



// ---------------------------------------------------------------- shapes







// "No games yet": a dashed card with a question mark.

// One library cover (or a plain card with the initial until its art is in).

// Five covers fanned out, the middle one in front.

// ---------------------------------------------------------------- top screen

static void slow_banner(void); /* shared presentation */

static void big_title(const char *title, const char *line, float ease); /* shared presentation */
/* the empty library's one sentence: where games go (the games folder, "/roms" before one is chosen) */
static const char *empty_help(bool at_library)
{
	static char text[96];
	ui_empty_help(text, sizeof(text), g_cfg.games_dir, at_library);
	return text;
}

static void top_common(void); /* shared presentation */

static bool no_title;
static void draw_top(void *ud); /* rendered by the shared home component */

// ---------------------------------------------------------------- bottom screen


// The Library tile: three random covers fanned, the front one undimmed.


// Play together: a frame with its name, Host and Join as buttons inside it.


static void draw_bottom(void *ud); /* rendered by the shared home component */


// behind Settings: the picture without its title (Settings' help panel goes there)
static void settings_top(void *ud)
{
	HT("covers get done"); covers_pump(); HT("covers pump done");
	title_t = 1;
	no_title = true;
	draw_top(ud);
	no_title = false;
}

// Settings borrows this room and camera through Share's neutral destination API.
static void settings_room_enter(void *ud){room_view_go(ud,ROOM_SETTINGS);room_sfx("glide");}
static void settings_room_leave(void *ud){room_view_back(ud);room_sfx("back");}
static bool settings_room_settled(void *ud){room_view *v=ud;return v->mode==ROOM_AT || v->mode==ROOM_HOME;}
static void settings_room_tick(void *ud,float dt)
{
 room_view *v=ud;
 if(v->mode!=ROOM_AT && v->mode!=ROOM_HOME)room_view_step(v,dt);
}
static void settings_room_top(void *ud){settings_top(ud);}

// the room on the top screen: drawn straight on it at 1x on both models (video_room_plain)
static void room_plain(bool on)
{
	video_room_plain(on);
	ui_wants_plain = on;
}
// the room's own frames (not the dialogs over it): the Old 3DS draws them in 3D (video.c)
static void room_eyes(bool on) { ui_room_eyes = on; }

// The app's first picture (main.c, right after the screens work): the lamp's glow in the dark,
// until the room is loaded and fades up from the same picture.
static void dawn_top(void *ud) { (void)ud; shelf_scene_dawn(true); }
static void dawn_bottom(void *ud) { (void)ud; shelf_scene_dawn(false); }
void home_dawn(void)
{
	for (int i = 0; i < 2; i++) {   // both frame buffers
		ui_begin();
		video_present_vsync(false, dawn_top, dawn_bottom, NULL);
	}
	boot_mark("first picture");
}

static void present_with(void (*bottom)(void *))
{
 HT("present");
	ui_begin();
	room_pace_hook();   // frame pacing by moment (the log's "pace:" lines)
	room_pace_frame(room_intro < 1 ? "room boot" : room.mode == ROOM_HOME ? "room home" : room.mode == ROOM_AT ? (picker_state ? "room picker" : "room place") : "room glide");
	room_plain(true);
	room_eyes(true);
	video_present_vsync(false, draw_top, bottom, NULL);
	room_eyes(false);
	room_plain(false);
}
static void present(void) { present_with(draw_bottom); }

// the library entry of a disc path (Play together > Host a game)
static void launch_info(const char *path, home_launch_t *out)
{
	for (int i = 0; i < library_count(); i++) {
		const game_t *g = library_get(i);
		for (int d = 0; d < g->ndiscs; d++)
			if (!strcmp(g->discs[d], path)) {
				snprintf(out->save, sizeof(out->save), "%s", g->cover_key);
				snprintf(out->title, sizeof(out->title), "%s", g->title);
				return;
			}
	}
}

// Confirmed Quit uses the ordinary room loop for the final glance. Input, HOME
// and music continue to be serviced; no private presentation loop owns the GPU.
static u64 quit_tick;

static bool welcome_live;   // home_run's room is up (bb_welcome_open uses it)
static void leave(void)
{
	welcome_live = false;
	shelf_preload_end(false); // gameplay/Library/lifecycle takes over a pending pause
	room_pace_priority(false);   // back to the menus' usual priority (before a game too)
	aptSetHomeAllowed(true);
	music_play(false);
	ui_wants_3d = false;
	if (cont_tex_ok)
		C3D_TexDelete(&cont_tex);
	cont_tex_ok = false;
	if (card_tex_ok)
		C3D_TexDelete(&card_tex);
	card_tex_ok = false;
	room_tv_key[0] = 0;   // the next start makes the TV (and the card's picture) again
}

// as the library does before a game: no network during it, music faded out
static void launch_leave(void)
{
    // Source actions (Continue, shelf launch or multiplayer) finish OUT before
    // handing over. A matching token reveals only when the game can draw.
    ui_swap_enter();
    leave();
}

// The first-launch welcome (welcome.h): its cards over this room, the camera visiting each place.
#include "home_welcome.inc"

// Play together screens (mp_ui.c, the multiplayer work) replace these.
static void soon_bottom(void *ud); /* shared presentation */

static void soon_top(void *ud); /* shared presentation */

static void coming_soon(const char *what)
{
	static const ui_button_t back = { KEY_B, "Back", 0, false };
	while (aptMainLoop()) {
 HT("loop");
		if (dev_tick(false) || dev_ui_pending())
			return;
		input_poll(); HT("input");
		u32 k = ui_header_back(ui_bar_input(&back, 1, input_keys_down()));
        k=ui_swap_keys(k,UI_SWAP_MENU);
		if (k & (KEY_B | KEY_A)) {
			sfx_play(SFX_BACK);
			return;
		}
		clock_s += UI_METRIC_1_0F / UI_METRIC_60_0F;
		music_update(); HT("music");
		ui_begin();
		video_present_vsync(false, soon_top, soon_bottom, (void *)what);
	}
}

__attribute__((weak)) void mp_host_run(void) { coming_soon("Host a game"); }
__attribute__((weak)) void mp_join_run(void) { coming_soon("Join a game"); }

// Without its room BreadBox has no main menu: one card says so, then the app closes.
static int room_missing(void)
{
	log_printf("home: no room (%s missing or out of memory): the error card, then quit\n", "romfs:/room/room.bbr");
	ui_confirm_action("BreadBox can't open its room", HOME_ROOM_MISSING_TEXT, "A  Close BreadBox", false);
	leave();
	return HOME_QUIT;
}

int home_run(bool games, char *path, size_t len, home_launch_t *out)
{
	memset(out, 0, sizeof(*out));
	have_games = games;
	boot_mark("home");
	// Keep the published Continue state while the low-priority worker checks it.
	cont_request_refresh();
	layout();
	pick_covers();
	if (sel_id < 0 || sel_index() == 0)
		sel_id = tiles[0].id;
	title_for = -1;
	fade = 1;
	music_play(true);
	ui_wants_3d = true;
	if (!room_start())
		return room_missing();
	room_pace_priority(true);
	room_pace_restart();   // back from a game or the Library: the time away is not a frame
	ui_wants_3d = video_room_3d();   // Old 3DS: the room in 3D at 30 fps (round 16; room3d=off: mono)
	fade = 0;                 // the room glides in instead
	boot_mark("room opened");
    breadbox_import_start(true);

	bool first_frame = true;
	static bool cold = true;     // the app's first main menu: the room fades up after its first frame
	if (cold) { room_intro = 0; room_intro_wait = 0; room_intro_tick = 0; }
	cold = false;
	covers_resume();
 int swap_act=-1;
 quit_tick=0;
	welcome_live = true;
	while (aptMainLoop() || bb_update_installing() || bb_update_committed()) {
 if(bb_update_committed())return HOME_QUIT;
 if(bb_update_installing() || bb_update_committed()){bb_update_install_frame();continue;}
 HT("loop");

        HT("cont begin");
        if(cont_poll()) {
            HT("cont publish done");layout();pick_covers();HT("layout done");
            // the Continue postcard: the game in its own shape, its system on the stamp
            cont_band=continue_picture?bb_picture_band(continue_pixels):0;
            const bb_system_t *cont_system=cont_ok?bb_system_for_path(cont_path):NULL;
            cont_stamp=cont_system?cont_system->short_name:NULL;
            u64 tv0=svcGetSystemTick();
            if(!continue_same || !room_tv_key[0] || (!card_tex_ok && !cont_tex_ok)) { room_tv_key[0]=0; room_tv(); }
            room_pace_add(ROOM_PACE_TV,svcGetSystemTick()-tv0);
        }
        HT("cont/layout/tv done");
        // Publish the grouped PS1 presentation when the shared snapshot settles.
        // Never rescan the ROM root with the legacy directory walker.
        if (g_cfg.games_dir[0]) {
            u64 l0 = svcGetSystemTick();
            static int laid_count = -1, since;
            if(bb_index_poll(true))shelf_library_changed();
            u64 l1 = svcGetSystemTick();
            have_games = bb_system_total()!=0;
            // while a library is published game by game, the menu follows it twice a second
            // Continue is probed on entry/explicit actions, never on an index publication.
            if (laid_count != library_count() && (++since >= 30 || laid_count < 0)) {
                laid_count = library_count();
                since = 0;
                layout(); pick_covers();
                u64 v0 = svcGetSystemTick();
                room_tv();
                room_pace_add(ROOM_PACE_TV, svcGetSystemTick() - v0);
            }
            if (svcGetSystemTick() - l0 > SYSCLOCK_ARM11 / 125)   // over 8 ms: which part (the pace line only says "library")
                log_printf("home: library %.1f ms: poll %.1f, rest %.1f\n", (svcGetSystemTick() - l0) * 1000.0 / SYSCLOCK_ARM11,
                    (l1 - l0) * 1000.0 / SYSCLOCK_ARM11, (svcGetSystemTick() - l1) * 1000.0 / SYSCLOCK_ARM11);
            room_pace_add(ROOM_PACE_LIBRARY, svcGetSystemTick() - l0);
        }
        if(bb_update_tick())return HOME_QUIT;
        if(bb_update_installing() || bb_update_committed()){bb_update_install_frame();continue;}
        HT("index done");
        room_scene_library_follow(false);   // the bookcase follows the library
        {
            // the Library is built in the background, one part per quiet frame at home, so
            // opening it is only the glide; its camera is the bookcase's wall view
            u64 p0 = svcGetSystemTick();
            if (!first_frame && room.mode == ROOM_HOME && !hidKeysHeld()) shelf_preload_step();
            else shelf_preload_end(true);
            room_pace_add(ROOM_PACE_PRELOAD, svcGetSystemTick() - p0);
            room_pose wall;
            if (shelf_wall_pose(&wall)) room_view_set_library(&room, &wall);
        }
		HT("preload/dev begin");
        if (dev_tick(false)) {
			leave();
			return HOME_AGAIN;
		}
		HT("dev done");
        int screen = dev_ui_take();

		if (screen == DEV_UI_LIBRARY) { leave(); return HOME_LIBRARY; }
		if (screen == DEV_UI_WELCOME && bb_welcome_open()) { leave(); return HOME_FOLDERS; }
		input_poll(); HT("input");
        if(quit_tick) {
            float u=(float)(svcGetSystemTick()-quit_tick)/SYSCLOCK_ARM11/sw_quitturn;
            if(u>=1) {leave();return HOME_QUIT;}
            room_view_turn(&room,u);room_view_step(&room,1.0f/60.0f*video_ui_steps());
            fade=ui_motion_ease(u);music_update(); HT("music");present();continue;
        }
		u32 k = input_keys_down();
        if(k || screen != DEV_UI_NONE)shelf_preload_end(true);

		if (covers_home_frame()) continue;   // HOME: held back while the covers use the network (as in the Library)
		if (welcome_ready()) {   // the first start's tour (or its games card again after the folder picker)
			int w = welcome_run(welcome_resume >= 0 ? welcome_resume : 0);
			if (w == WELCOME_FOLDERS) { leave(); return HOME_FOLDERS; }
			if (w == WELCOME_CLOSED) { leave(); return aptMainLoop() ? HOME_AGAIN : HOME_QUIT; }
			continue;
		}

		int act = room_input(k);
		if (act >= 0 && act != E_LIBRARY && act != E_SETTINGS && act != E_SHARE)
			sfx_play(act == E_CONTINUE ? SFX_LAUNCH : SFX_SELECT);
        if(screen==DEV_UI_SETTINGS)act=E_SETTINGS;
        u32 swap_key=UI_METRIC_0;
        if(swap_act<0 && act==E_CONTINUE) {swap_act=act;swap_key=UI_SWAP_REMOTE;}
        if(ui_swap_keys(swap_key,UI_SWAP_REMOTE)&UI_SWAP_REMOTE) {act=swap_act;swap_act=-1;}
        else if(swap_act>=0)act=-1;
		switch (act) {
		case E_CONTINUE: {
            const char *save_name=continue_save,*title=continue_title;
			snprintf(path, len, "%s", cont_path);
			snprintf(out->save, sizeof(out->save), "%s", save_name);
			snprintf(out->title, sizeof(out->title), "%s", title);
			out->resume = true;
			room_return = ROOM_LIBRARY;   // after the game: the library, then back into the room from there
			launch_leave();
			return HOME_PLAY;
        }
		case E_LIBRARY:
			room_pose wall;
			if (shelf_wall_pose(&wall)) {   // the room's own bookcase: the same scene goes on
				int r = shelf_run(path, len);
				ui_wants_3d = video_room_3d();
				if (r == 1) {
					room_return = ROOM_LIBRARY;   // after the game: its shelf, then the room
					launch_leave();
					return HOME_PLAY;
				}
				if (r == 4) {   // B on the wall: glide home from where the camera is
					if (shelf_wall_pose(&wall)) room_view_set_library(&room, &wall);
					room_view_back(&room);
					log_printf("room: library returned, gliding home\n");
					music_play(true);
					covers_resume();
					continue;
				}
				leave();
				return r == 2 ? HOME_FOLDERS : r == 0 && aptShouldClose() ? HOME_QUIT : HOME_AGAIN;   // Start closes the shelf: back in the room (only the system closing leaves)
			}
			room_loading = true;   // building the shelves blocks: leave the cue on screen first
			present();
			present();
			room_loading = false;
			room_to_library();
			leave();
			return HOME_LIBRARY;
		case E_HOST:
		case E_JOIN:
			if (act == E_HOST)
				mp_host_run();
			else
				mp_join_run();
			if (mp_launch_request(path, len)) {
				launch_info(path, out);
				launch_leave();
				return HOME_PLAY;
			}
			if (dev_transition()) { leave(); return HOME_AGAIN; }
			room_view_back(&room);
			continue;
		case E_SHARE: {
			const ui_destination destination = { &room, share_room_enter, share_room_leave, share_room_tick, share_room_settled, share_room_top };
			room_plain(true); room_eyes(true);
			share_destination_run(&destination);
			room_eyes(false); room_plain(false);
			if (dev_transition()) { leave(); return HOME_AGAIN; }
			if (room.mode != ROOM_HOME) room_view_back(&room);
			continue;
		}
			continue;
		case E_QUIT:   // the room's Quit: asked first, then the way Start used to close the app
			if (ui_confirm_pick("Quit BreadBox?", "BreadBox closes and takes you back to the HOME Menu. Your games and saves stay as they are.", "Quit", "Cancel")) {
				quit_tick=svcGetSystemTick();
                ui_swap_hold();
                continue;
			}
			room_pace_restart();
			continue;
		case E_SETTINGS: {
			room_plain(true);   // the room (and your hands) behind Settings
			ui_destination scene={.userdata=&room,.enter=settings_room_enter,.leave=settings_room_leave,
				.tick=settings_room_tick,.settled=settings_room_settled,.draw_top=settings_room_top};
			bb_settings_room_run(&scene);
			room_plain(false);
			if (!aptMainLoop()) { leave(); return HOME_QUIT; }   // HOME > Close inside Settings or About / Credits: nothing more to draw
			if (settings_ui_take_folders()) {   // Settings > Games folder
				leave();
				return HOME_FOLDERS;
			}
			// Reset all settings keeps the last game; the library may have changed
            cont_request_refresh();
            layout();
			room_tv();
			room_view_back(&room);
			continue;
		}
		}

		if (bb_platform_saves(BB_SAVES_MENU) == BB_SAVES_CLOSE)   // closed inside its dialog: no more drawing
			break;
		music_update(); HT("music");
		const float dt = UI_METRIC_1_0F / UI_METRIC_60_0F * (float)video_ui_steps();   // 2 frames long on the Old's 30 fps room
		clock_s += dt;
		{
			u64 tv0=svcGetSystemTick();
			room_tv_poll((int)(clock_s * 60)); HT("tv poll");
			room_pace_add(ROOM_PACE_TV,svcGetSystemTick()-tv0);
			bool arrived = room_view_arrived(&room);
			room.nofocus = room_quit_on;   // Quit: nothing picked out, the plain home view
			room_view_step(&room, dt);
			if (!arrived && room_view_arrived(&room)) room_sfx("arrive");
		}
		int s = sel_index();
		if (title_for != s) {
			title_for = s;
			title_t = 0;
			pop = UI_METRIC_0_9F;
			pop_v = 0;
		}
		ui_motion_settle(&pop, &pop_v, UI_METRIC_1_0F, UI_MOTION_SETTLE_RATE, dt);
		if (title_t < 1) title_t = fminf(1, title_t + dt * 5);
		if (fade > 0) fade = fmaxf(0, fade - dt * 3);
		for (int i = 0; i < 5 && i < library_count(); i++)
			covers_get(i, NULL);
		for (int i = 0; i < npicks; i++)
			covers_get(picks[i], NULL);
		HT("covers get done"); covers_pump(); HT("covers pump done");
		present();
		if (first_frame) {
			first_frame = false;
			boot_mark("first frame");
			music_stream();   // the later thread reads the music now (the card was the room's until here)
		}
		// A published snapshot is usable during background validation. Never hold
		// the room dark waiting for a long walk; shelves can fill after reveal.
		room_intro_wait += dt;
		if (room_intro < UI_METRIC_1 && ((bb_index_complete() && bb_system_total()>0) || room_intro_wait >= UI_MOTION_BOOT_WAIT_S)) {
			if (room_intro == UI_METRIC_0) room_scene_library_follow(true);
			if (!room_intro_tick) room_intro_tick=svcGetSystemTick();
            room_intro = fminf(UI_METRIC_1, (float)(svcGetSystemTick()-room_intro_tick)/SYSCLOCK_ARM11/ROOM_INTRO_SECONDS);
		}

	}
	leave();
	return HOME_QUIT;
}

// A game started here: the main menu continues it next time.
void home_note_game(const char *path, const char *save_name, const char *title, const char *state_name)
{
	// A game launched from the library arrives without a title ("" = the name from the path): the room's
	// Last played and Continue then name it as the library does, not nothing
	char derived[sizeof(g_cfg.last_game)];
	if ((!title || !title[0]) && state_name && state_name[0]) {
		ui_clean_title(state_name, derived, sizeof(derived));
		title = derived;
	}
	if (!strcmp(g_cfg.last_game_path, path) && !strcmp(g_cfg.last_game_state, state_name) &&
	    !strcmp(g_cfg.last_game, title ? title : "") && !strcmp(g_cfg.last_game_save, save_name ? save_name : ""))
		return;
	snprintf(g_cfg.last_game_path, sizeof(g_cfg.last_game_path), "%s", path);
	snprintf(g_cfg.last_game_state, sizeof(g_cfg.last_game_state), "%s", state_name);
	snprintf(g_cfg.last_game, sizeof(g_cfg.last_game), "%s", title ? title : "");
	snprintf(g_cfg.last_game_save, sizeof(g_cfg.last_game_save), "%s", save_name ? save_name : "");
	config_save();
    cont_request_refresh(); /* Auto-import can change Continue while the room is open. */
}

// ---------------------------------------------------------------- empty library

#define DEFAULT_GAMES_DIR "sdmc:/roms/psx"
static char empty_msg[PATH_LEN / 2 + 128];   // a path (cut at 80) and the keys that help, wrapped by the shared text
static int empty_msg_frames;
// "<what> <end of the folder path><then>": the path keeps its last folder names (whole UTF-8 characters), cut by
// RENDERED width so a long WWWW path and a CJK one both stay two lines (the text wraps at 304 px, from y 170,
// and must end above the button bar); wide glyphs just keep fewer characters
static void empty_say(const char *what, const char *dir, const char *then)
{
	enum { LINE_PX = 304, MESSAGE_PX = 2 * LINE_PX - 120, PATH_PX = LINE_PX - 16 };
	char tail[PATH_LEN];
	for (int chars = 80; ; chars -= chars > 12 ? 4 : 1) {
		ui_path_tail(tail, sizeof(tail), dir, chars);
		snprintf(empty_msg, sizeof(empty_msg), "%s %.200s%s", what, tail, then);
		if (chars <= 1 || (ui_text_width(NULL, UI_FONT_SMALL, empty_msg) <= MESSAGE_PX && ui_text_width(NULL, UI_FONT_SMALL, tail) <= PATH_PX))
			break;
	}
}
#define EMPTY_RETRY ". Rescan tries again; Folders picks another folder."

static void empty_top(void *ud); /* shared presentation */

static const ui_button_t empty_bar[3] = {
	{ KEY_B, "Menu", 0, false }, { KEY_Y, "Folders", UI_ICON_FOLDER, false }, { KEY_SELECT, "Rescan", UI_ICON_RESCAN, false },
};

static void empty_bottom(void *ud); /* shared presentation */

int home_empty_library_run(void)
{
	have_games = false;
	fade = 1;
	empty_msg_frames = 0;
	if (g_cfg.games_dir[0]) {   // a folder was chosen but holds no games: say so, don't just look unchanged
		if (bb_games_dir_gone) {   // the saved choice is kept: the card may only be unreadable for now
			empty_say("Can't read", g_cfg.games_dir, EMPTY_RETRY);
			empty_msg_frames = 1 << 30;   // stays until the folder reads or another is chosen
		} else
		{
		empty_say("No games found in", g_cfg.games_dir, ".");
		empty_msg_frames = 300;
		}
	}
	music_play(true);
	ui_wants_3d = true;
	int r = 0;
	while (aptMainLoop()) {
 HT("loop");
		if(bb_index_poll(true))shelf_library_changed();
		if(bb_system_total()>0){r=3;break;}
		HT("preload/dev begin");
        if (dev_tick(false)) { r = 3; break; }
		HT("dev done");
        int screen = dev_ui_take();
		if (screen == DEV_UI_SETTINGS) { settings_ui_run(false, empty_top); continue; }
		input_poll(); HT("input");
		u32 k = ui_header_back(ui_bar_input(empty_bar, 3, input_keys_down()));
        k=ui_swap_keys(k,UI_SWAP_MENU);
		if (k & KEY_B) { sfx_play(SFX_BACK); r = 1; break; }
		if (k & KEY_Y) { sfx_play(SFX_SELECT); r = 2; break; }
		if (k & KEY_X) { sfx_play(SFX_SELECT); settings_ui_run(false, empty_top); continue; }
		if (k & KEY_SELECT) {
			// the start's cheap look: the saved folder, else the usual roots and folders named like a system
			char found[PATH_LEN];
			sfx_play(SFX_SELECT);
			bb_games_dir_retry();   // the saved folder reads again: that one
			if (!bb_index_discover(g_cfg.games_dir, found, sizeof(found))) {
				if (g_cfg.games_dir[0]) empty_say("Can't read", g_cfg.games_dir, EMPTY_RETRY);
				else snprintf(empty_msg, sizeof(empty_msg), "No games folder found. Folders picks one.");
				empty_msg_frames = g_cfg.games_dir[0] ? 1 << 30 : 240;
			} else {
				bb_games_dir_found(found);   // the saved folder, or a fallback for this session only
				if (library_load(g_cfg.games_dir, true)) {
					r = 3;
					break;
				}
				if (!aptMainLoop()) { r = 0; break; }
				if(bb_index_status()==BB_INDEX_SCANNING)empty_say("Finding games in",g_cfg.games_dir,"...");
				else empty_say("Still no games in",g_cfg.games_dir,".");
				empty_msg_frames = 180;
			}
		}
		music_update(); HT("music");
		clock_s += UI_METRIC_1_0F / UI_METRIC_60_0F;
		if (fade > 0) fade = fmaxf(0, fade - UI_METRIC_3_0F / UI_METRIC_60_0F);
		if (empty_msg_frames > 0) empty_msg_frames--;
		// HOME > Close can end a library_load above (it polls aptMainLoop): nothing may be drawn after it
		if (!aptMainLoop() || !gspHasGpuRight()) { r = 0; break; }
		ui_begin();
		video_present_vsync(false, empty_top, empty_bottom, NULL);
	}
	music_play(false);
	ui_wants_3d = false;
	return r;
}

// The place cards' lines before A (place_cards.h): what the room's Collection, Settings and Quit cards say.
// The text lives here until the next frame builds it again (a few counts and one sum: no file, no allocation).
static place_lines place_text;
static PSX3D_COLD float place_measure(const char *s, void *ud) { (void)ud; return shelf_scene_line_width(s); }
static void place_rows(shelf_card *c, int place)
{
	const place_fit fit = { place_measure, NULL, UI_ROOM_TEXT_R - UI_ROOM_TEXT_X, UI_ROOM_GAP };
	place_text.n = 0;
	if (place == ROOM_LIBRARY) {   // the library's counts as published so far (the scan and its re-check update them)
		static place_lines lines;   // made again only when what it shows changes (bb_index_counts_key and the counts)
		static bool empty;
		typedef struct { bool made, scanning; u32 key; int favorites; unsigned n; int games[BB_MAX_SYSTEMS]; } shown_t;
		static shown_t was;
		shown_t now;
		memset(&now, 0, sizeof(now));   // compared whole (memcmp): no stray padding
		now.made = true;
		now.scanning = !bb_index_complete() && bb_index_busy();   // no usable snapshot yet: nothing known (not 0)
		now.key = bb_index_counts_key();
		now.favorites = playlog_favourite_count();
		const bb_system_t *order[BB_MAX_SYSTEMS];
		place_system sys[BB_MAX_SYSTEMS];
		now.n = bb_system_display_order(order, BB_MAX_SYSTEMS, false);
		for (unsigned i = 0; i < now.n; i++) {   // each system's own list follows the index some frames later
			sys[i] = (place_system){ order[i]->name, order[i]->short_name, bb_system_library_count(order[i]) };
			now.games[i] = sys[i].games;
		}
		if (memcmp(&now, &was, sizeof(now))) {
			place_collection(&lines, &fit, sys, (int)now.n, now.favorites, !now.scanning, &empty);
			was = now;
		}
		place_text = lines;
		if (empty) c->hint = empty_help(false);   // where games go: the one sentence the empty library says everywhere
	} else if (place == ROOM_SETTINGS) {
		place_settings(&place_text, BB_VERSION, bb_update_glance(), theme()->name);   // Room style is on Settings' first page
	} else if (place == ROOM_QUIT_PLACE) {
		char clean[PATH_LEN];
		ui_clean_title(cont_title(), clean, sizeof(clean));
		place_quit(&place_text, &fit, playlog_today((u32)time(NULL)), clean);
	}
	for (int i = 0; i < place_text.n && c->nrows < SHELF_CARD_ROWS; i++)
		c->row[c->nrows++] = (shelf_card_row){ place_text.line[i].label, place_text.line[i].value[0] ? place_text.line[i].value : NULL };
}

#include "components/home.inc"
