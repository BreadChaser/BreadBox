// Execute the actual bookshelf controller (app/ui/shelf.c) under sanitizers
// with two fake systems: shelves from the registry, layout, sort, favorites,
// the settle-then-take-out motion, put back, and the launch path. Painting
// (shelf_scene.c) is replaced by checks on the view it is given.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "theme.h"
#include "app_log.h"
#include "app_config.h"
#include "settings_ui.h"
#include "bb_platform.h"
#include "platform_video.h"
#include "app_input.h"
#include "ui_pause.h"
#include "ui.h"
#include "library.h"
#include "covers.h"
#include "uisound.h"
#include "playlog.h"
#include "games_dir.h"
#include "app_main.h"
#include "devtools.h"
#include "bb_index.h"
#include "bb_registry.h"
#include "shelf.h"
#define SYSCLOCK_ARM11 268123480ULL
psx3d_config g_cfg;
bool ui_wants_3d, ui_wants_plain, ui_room_eyes, g_new3ds = true;
static u64 clock_ticks;
static u64 svcGetSystemTick(void) { return clock_ticks; }

// ---- two systems with catalogs (titles out of order on purpose)
static const char *const ps_titles[] = { "Tekken 3", "Ace Combat 2", "Silent Hill", "Crash Bandicoot" };
static const char *const gb_titles[] = { "Metroid Fusion", "Advance Wars" };
static int ps_fail;   // the next launches fail
static bool ps_catalog(int row, bb_catalog_item *o)
{
	if (row < 0 || row >= 4) return false;
	snprintf(o->title, sizeof(o->title), "%s", ps_titles[row]);
	snprintf(o->key, sizeof(o->key), "%s", ps_titles[row]);
	snprintf(o->path, sizeof(o->path), "ps/%s.cue", ps_titles[row]);
	o->cover_id = row;
	o->discs = ps_fail == 1 ? 2 : 0;   // the second failing launch: a two-disc game (its disc list can't be written)
	return true;
}
static int ps_launched = -1;
static bool ps_launch(int row, char *p, size_t n) { if (ps_fail) { ps_fail--; return false; } ps_launched = row; snprintf(p, n, "ps-launch/%d", row); return true; }
static bool gb_catalog(int row, bb_catalog_item *o)
{
	if (row < 0 || row >= 2) return false;
	snprintf(o->title, sizeof(o->title), "%s", gb_titles[row]);
	snprintf(o->key, sizeof(o->key), "gba:%s", gb_titles[row]);
	snprintf(o->path, sizeof(o->path), "gb/%s.gba", gb_titles[row]);
	o->cover_id = 65536 * 2 + row;
	return true;
}
static const bb_shell_ops ps_ops = { .catalog = ps_catalog, .launch = ps_launch }, gb_ops = { .catalog = gb_catalog };
static const bb_package jewel = { .width = 142, .height = 125, .depth = 10.5f, .style = BB_PACKAGE_CASE };
static bb_system_t sys_ps = { .id = "ps1", .name = "PlayStation", .short_name = "PS1", .release_year = 2002, .release_month = 1, .shell = &ps_ops, .package = &jewel },
	sys_gb = { .id = "gba", .name = "Game Boy Advance", .short_name = "GBA", .release_year = 2001, .release_month = 3, .shell = &gb_ops };   // no package: a plain carton
// (fake dates: the PlayStation stand-in is the newer system here, so its shelf is row 1; real order is in order.c)
const bb_system_t *const bb_systems[] = { &sys_ps, &sys_gb };
const unsigned bb_system_count = 2;
static int gb_count = 2, reload_at;   // the Game Boy Advance library may still be loading after a game
static unsigned index_gen = 1;
static bool real_order;   // PlayStation older than Game Boy Advance, as on the real registry
int bb_system_library_count(const bb_system_t *s) { return s == &sys_ps ? 4 : gb_count; }
int bb_system_cover_id(const bb_system_t *s, int i) { (void)s; return i; }
bool bb_index_get(const bb_system_t *s, int i, char *p, size_t n, bb_entry *e) { (void)s; (void)i; (void)p; (void)n; (void)e; return false; }
unsigned bb_index_generation(void) { return index_gen; }
bool bb_index_busy(void) { return false; }
bool bb_index_complete(void) { return true; }
static bool closing, close_in_settings, close_in_load, close_in_system, close_load_success;
static int system_settings;
static void system_settings_run(const bb_system_t *s) { (void)s; system_settings++; if(close_in_system)closing=true; }
void bb_settings_game(const bb_system_t *s,const char *path,const char *key,const char *title,void (*top)(void *),void *ud)
{
 assert(s && path && *path && key && *key && title && *title && top);
 (void)ud;system_settings_run(s);
}

// ---- play log and favorites (keys as the shelf builds them: "<system>:<key>")
static char favs[8][64];
static int nfavs;
bool playlog_favourite(const char *k) { for (int i = 0; i < nfavs; i++) if (!strcmp(favs[i], k)) return true; return false; }
bool playlog_toggle_favourite(const char *k)
{
	for (int i = 0; i < nfavs; i++)
		if (!strcmp(favs[i], k)) { memmove(favs[i], favs[i + 1], (size_t)(nfavs - i - 1) * sizeof(favs[0])); nfavs--; return false; }
	snprintf(favs[nfavs++], sizeof(favs[0]), "%s", k);
	return true;
}
u32 playlog_last(const char *k) { return !strcmp(k, "ps1:Silent Hill") ? 2000 : !strcmp(k, "gba:Advance Wars") ? 1000 : 0; }
u32 playlog_seconds(const char *k) { return !strcmp(k, "ps1:Tekken 3") ? 9000 : 0; }

// ---- input script: one key set per frame; time advances 1/60 s per frame
static u32 script[512], held_script[512];
static int nscript, at, frames;
static void press(u32 k, int idle) { held_script[nscript] = 0; script[nscript++] = k; for (int i = 0; i < idle; i++) { held_script[nscript] = 0; script[nscript++] = 0; } }
static void hold(u32 k, int count) { int start = nscript; press(k, count - 1); for (int i = start; i < nscript; i++) held_script[i] = k; }
// HOME > Close: once `closing` is set aptMainLoop() is false and nothing may be drawn or presented
static int after_close;   // draw / present / cover calls made after the close
bool aptMainLoop(void) { if (closing) return false; assert(++frames < 4000); return true; }
bool gspHasGpuRight(void) { return !closing; }
bool dev_tick(bool g) { (void)g; return false; }
static int ui_request;
int dev_ui_take(void) { int r = ui_request; ui_request = 0; return r; }
bool bb_index_poll(bool refresh) {(void)refresh;return false;}
static bb_index_status_t scan_status = BB_INDEX_COMPLETE;
static unsigned scans;
bb_index_status_t bb_index_status(void){return scan_status;}
unsigned bb_index_scans(void){return scans;}
// the shared dialog (review R3): what the Library told the player
static int cards;
static char card_title[64], card_text[256], card_first[256];
bool ui_confirm_action(const char *t, const char *m, const char *a, bool g)
{ assert(!g && !strcmp(a, "A  Back")); if (!cards++) snprintf(card_first, sizeof card_first, "%s", m); snprintf(card_title, sizeof card_title, "%s", t); snprintf(card_text, sizeof card_text, "%s", m); return true; }
static library_result rescan_result=LIBRARY_LOADED;
bool bb_games_dir_retry(void) { return false; }
library_result library_load_result(const char *r,bool b){(void)r;(void)b;if(close_in_load){closing=true;return close_load_success?LIBRARY_LOADED:LIBRARY_CANCELLED;}return rescan_result;}
bool library_load(const char *r,bool b){return library_load_result(r,b)==LIBRARY_LOADED;}
static u32 down, held;
void input_poll(void) { held = at < nscript ? held_script[at] : 0; down = at < nscript ? script[at++] : KEY_START; }
u32 input_keys_down(void) { return down; }
u32 input_keys_held(void) { return held; }
u32 ui_bar_input(const ui_button_t *b, int n, u32 k) { assert(b && n == 4); return k; }
void ui_bar_draw(const ui_button_t *b, int n) { assert(b && n == 4); }
u32 ui_swap_keys(u32 keys,u32 mask) { (void)mask;return keys; }
void ui_begin(void) {}
static int swap_enters;
void ui_swap_enter(void) { swap_enters++; }
void ui_motion_shade(float alpha, int width) { (void)alpha; (void)width; }
void ui_menu_top(void *ud) { (void)ud; }
static void draw_top(void *);
static int settings_calls;
bool settings_ui_run(bool g, void (*t)(void *)) { assert(!g && t==draw_top);t(NULL); settings_calls++; if (close_in_settings) closing = true; return false; }
bool settings_ui_take_folders(void) { return false; }
void hidTouchRead(touchPosition *p) { p->px = p->py = 0; }
void sfx_play(int s) { (void)s; }
void sfx_cancel(void) {}
static int music_stops;
void music_play(bool on) { if (!on) music_stops++; }
size_t linearSpaceFree(void) { return 2 * 1024 * 1024; }
void music_update(void) {}
void covers_resume(void) {}
bool covers_stop_wait(void) {return true;}
bool covers_stop(void) { return true; }
void covers_reset(void) {}
bool covers_library_begin(void) {return true;}
bool covers_library_try_begin(void) { return true; }
void covers_library_end(void) {}
void covers_pump(void) { after_close += closing; }
static int focused = -1;
void covers_focus(int g) { focused = g; }
bool covers_full(int g) { (void)g; return true; }   // its full-size cover is there: A takes the box out at once
static const theme_room_t room;
const theme_t *theme(void) { static theme_t t; t.room = &room; return &t; }
// painting: the view must stay consistent every frame
static const shelf_view *seen;
void shelf_scene_frame(const shelf_view *v) { seen = v; after_close += closing; }
void shelf_scene_top(const shelf_view *v)
{
	// a library that changes is shown once it has settled (half a second): until then the shelves as they were
	if (real_order) assert(v->rows[0].system == NULL && ((v->nrows == 2 && v->rows[1].system == &sys_ps) || (v->nrows == 3 && v->rows[1].system == &sys_gb)));
	else assert(v->nrows == 3 && v->rows[0].system == NULL && v->rows[1].system == &sys_ps && v->rows[2].system == &sys_gb);
	assert(v->zoom >= -1e-3f && v->zoom <= 1.001f && v->pull >= -1e-3f && v->pull <= 1.001f && isfinite(v->cur));
	for (int r = 0; r < v->nrows; r++) {
		assert(v->sel[r] >= 0 && (v->rows[r].n == 0 || v->sel[r] < v->rows[r].n));
		for (int i = 1; i < v->rows[r].n; i++)   // spines in order, never overlapping
			assert(v->rows[r].box[i].x - v->rows[r].box[i].t / 2 > v->rows[r].box[i - 1].x + v->rows[r].box[i - 1].t / 2);
	}
}
void shelf_scene_bottom(const shelf_view *v) { (void)v; }
void shelf_scene_release(void) {}
bool shelf_scene_hands(void) { return false; }   // the 2D hand: no hands.bbh in the host test
// the room's bookcase (the Library); without it shelf_run leaves to the main menu at once
bool shelf_scene_prepare(void) { return true; }
static bool in_room = true;
static int preset_polls;
// HOME held back during cover downloads (covers_home_frame, tested in tests/app/covers): the Library runs it every
// frame, a frame that went HOME draws nothing more, and leaving never keeps HOME held
static int home_frames, home_go;
static bool home_allowed = true;
bool covers_home_frame(void) { home_frames++; if (home_go) { home_go--; home_allowed = false; return true; } return false; }
void aptSetHomeAllowed(bool on) { home_allowed = on; }
bool presets_update_poll(void) { preset_polls++; return false; }
bool shelf_scene_in_room(void) { return in_room; }
bool shelf_scene_wall_pose(const shelf_view *v, room_pose *out) { (void)v; (void)out; return in_room; }
void shelf_scene_begin(const shelf_view *v) { (void)v; }
void video_room_plain(bool on) { (void)on; }
// the shelves are kept between openings and built again when the library changes: here the
// test's library is its counts, its order and the release years
static bool empty_total;
int bb_system_total(void) { if(empty_total)return 0;return 4 + gb_count + (real_order ? 1000 : 0) + sys_ps.release_year * 7; }
void log_printf(const char *fmt, ...) { (void)fmt; }
bool room_hands_preload_step(void) { return true; }
void room_hands_prepare_ms(float ms) { (void)ms; }
void room_scene_library_follow(bool now) { (void)now; }
void room_pace_frame(const char *label) { (void)label; }
void room_pace_add(int part, uint64_t ticks) { (void)part; (void)ticks; }
void room_pace_hook(void) {}
void room_pace_restart(void) {}
bool room_pace_return_pending(void) {return false;}
bool bb_index_cancelled(void) {return false;}
bool bb_index_defer_visible(void (*fn)(void *),void *arg) {(void)fn;(void)arg;return false;}
int bb_platform_saves(int moment) { (void)moment; return 0; }
bool room_pace_priority(bool on) { (void)on; return false; }
float shelf_scene_launch_seconds(const shelf_view *v) { (void)v; return .9f; }
float shelf_scene_clip_seconds(const shelf_view *v, const char *action, float dflt) { (void)v; (void)action; return dflt; }
bool shelf_scene_case_ready(const shelf_view *v) { (void)v; return true; }
bool video_room_3d(void) { return g_new3ds; }
int shelf_scene_strip_hit(const shelf_view *v, float x, float y) { (void)v; (void)x; (void)y; return -1; }
int shelf_scene_card_hit(const shelf_view *v, float x, float y) { (void)v; (void)x; (void)y; return -1; }
const shelf_box *shelf_pulled_box(const shelf_view *v)
{
	if (v->pull <= .002f || v->prow >= v->nrows || v->pidx >= v->rows[v->prow].n) return NULL;
	return &v->rows[v->prow].box[v->pidx];
}
void video_present_vsync(bool g, void (*t)(void *), void (*b)(void *), void *u)
{
	(void)g;
	after_close += closing;
	t(u);
	b(u);
	clock_ticks += SYSCLOCK_ARM11 / 60;
	if (reload_at && frames >= reload_at) { gb_count = 2; index_gen++; reload_at = 0; }   // that library finished loading
}

#include "../../app/ui/shelf.c"

static int run(char *path) { at = frames = 0; closing = false; after_close = 0; return shelf_run(path, 256); }

int main(void)
{
	char path[256];
	// review N1: a room that failed to load (after a game, out of memory) leaves the Library before its first frame,
	// to the main menu (4), which shows the room's error card; nothing is built, played or drawn
	in_room = false;
	nscript = 0;
	assert(run(path) == 4 && frames == 0 && !boxes && !view.nrows && music_stops == 0 && !seen && !preset_polls && !home_frames);
	in_room = true;
	nfavs = 0;
	snprintf(favs[nfavs++], sizeof(favs[0]), "ps1:Silent Hill");   // keys without a system get "ps1:" added
	// all shelves; B goes back to the main menu
	nscript = 0;
	press(KEY_B, 0);
	assert(run(path) == 4);
	const bb_system_t *order[BB_MAX_SYSTEMS];
	assert(bb_system_display_order(order, BB_MAX_SYSTEMS, true) == 2 && rows[0].system == NULL);
	assert(rows[1].system == order[0] && rows[2].system == order[1] && order[0] == &sys_ps);   // shelves follow the shared order
	assert(view.nrows == 3 && rows[0].n == 1 && rows[1].n == 4 && rows[2].n == 2 && view.total == 6);
	assert(!strcmp(rows[1].box[0].title, "Ace Combat 2") && !strcmp(rows[1].box[3].title, "Tekken 3"));   // A to Z
	assert(!strcmp(rows[0].box[0].title, "Silent Hill") && rows[0].box[0].system == &sys_ps);
	// package, plain carton: sizes on the close-up's pixel grid, within half a pixel
	assert(fabsf(rows[1].box[0].t - SHELF_CASE_SINGLE) <= .5f / rows[1].s && fabsf(rows[2].box[0].t - 16) <= .5f / rows[2].s);
	assert(fabsf(rows[2].box[0].h - 180) <= .5f / rows[2].s && rows[1].s == SHELF_CLOSE_MAX && fabsf(rows[2].hmax - rows[2].box[0].h) < 1e-3f);
	for (int r = 0; r < view.nrows; r++)
		for (int i = 0; i < rows[r].n; i++) {
			const shelf_box *b = &rows[r].box[i];
			float s = rows[r].s, e[3] = { (b->x - b->t / 2) * s, b->t * s, b->h * s };
			for (int k = 0; k < 3; k++) assert(fabsf(e[k] - roundf(e[k])) < 1e-3f);
			assert(b->h * s <= SHELF_CLOSE_PX + .5f);
		}
	assert(region_of("sdmc:/roms/ps/Tekken 3 (USA).bin") == SHELF_REGION_US && region_of("x/Ridge Racer (Europe) (En,Fr).cue") == SHELF_REGION_PAL);
	assert(region_of("x/Ridge Racer (Japan).cue") == SHELF_REGION_JP && region_of("x/Mario (E).gba") == SHELF_REGION_PAL && region_of("plain.bin") == SHELF_REGION_US);
	{   // round 8: a product code in the name, multi-disc sets, the jewel case's thickness, cartons as packaged
		char sn[12];
		assert(serial_of("x/Final Fantasy VII [SCUS-94163].cue", sn) && !strcmp(sn, "SCUS-94163"));
		assert(serial_of("x/SLPS_012.34.bin", sn) && !strcmp(sn, "SLPS-01234") && serial_of("SCES 00344 Wipeout.cue", sn) && !strcmp(sn, "SCES-00344"));
		assert(!serial_of("x/Tekken 3 (USA).cue", sn) && !serial_of("x/XSLUS-00594.cue", sn) && !serial_of("x/SLUS-005941.cue", sn));
		assert(serial_region("SLES-00001") == SHELF_REGION_PAL && serial_region("SLUS-00594") == SHELF_REGION_US && serial_region("SLPM-86001") == SHELF_REGION_JP);
		static const struct { const char *path; int discs; float t; u8 region; } cases[] = {
			{ "x/Tekken 3 (USA).cue", 1, SHELF_CASE_SINGLE, SHELF_REGION_US },
			{ "x/Ridge Racer (Japan).cue", 1, SHELF_CASE_SINGLE, SHELF_REGION_JP },
			{ "x/Ridge Racer (Europe).cue", 1, SHELF_CASE_PAL, SHELF_REGION_PAL },
			{ "x/Metal Gear Solid (USA) (Disc 1).cue", 2, SHELF_CASE_MULTI, SHELF_REGION_US },
			{ "x/Metal Gear Solid (Europe) (Disc 1).bin", 0, SHELF_CASE_MULTI, SHELF_REGION_PAL },
			{ "x/Final Fantasy VIII (Japan).m3u", 2, SHELF_CASE_MULTI, SHELF_REGION_JP },   // the PS1 catalog counts a playlist as 2+
			{ "x/Tomb Raider (Disk 2).cue", 1, SHELF_CASE_MULTI, SHELF_REGION_US },
			{ "x/Wipeout [SCES-00010] (USA).cue", 1, SHELF_CASE_PAL, SHELF_REGION_PAL },   // the code wins over the tag
		};
		for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
			bb_catalog_item it = { .discs = cases[i].discs };
			snprintf(it.title, sizeof(it.title), "Game");
			snprintf(it.path, sizeof(it.path), "%s", cases[i].path);
			shelf_box b;
			fill_box(&b, &sys_ps, 0, &it, 1);
			assert(b.t == cases[i].t && b.region == cases[i].region);
			b.system = &sys_gb;
			fill_box(&b, &sys_gb, 0, &it, 1);   // a carton keeps its package's depth
			assert(b.t == 16 && b.discs == (cases[i].discs ? cases[i].discs : 1));
		}
	}
	unsigned layout = view.layout;
	assert(view.row_gap >= 180 + 43 - .01f && view.case_top == 3 * view.row_gap && rows[0].base > rows[1].base);
	assert(!strcmp(view.last_title, "Silent Hill") && !strcmp(rows[2].box[0].tag, "GBA\xc2\xb7" "02"));
	// open the PlayStation shelf (row 1 is picked first), scroll right once, settle: the box only
	// tips out (owner: no take-out by itself); A takes it out, A again plays it through the
	// system's launch
	nscript = 0;
	press(KEY_A, 70);
	press(KEY_RIGHT, 120);
	assert(UI_SHELF_AUTO_TAKEOUT_S == 0);
	press(KEY_A, 100);
	press(KEY_A, 70);
	// the launch fails twice first (review R3): each time one card says why (the game is gone; then its disc list
	// can't be written), the box stays in the hands; A again plays it
	press(KEY_A, 70);
	press(KEY_A, 70);
	ps_fail = 2;
	assert(run(path) == 1 && cards == 2 && !strcmp(card_title, "Can't start this game"));
	assert(strstr(card_first, "can't find this game") && strstr(card_text, "disc list") && strstr(card_text, "SD card"));
	assert(view.level == SHELF_ONE && view.row == 1 && view.sel[1] == 1 && view.prow == 1 && view.pidx == 1);
	assert(!strcmp(rows[1].box[1].title, "Crash Bandicoot") && ps_launched == 3 && !strcmp(path, "ps-launch/3"));   // catalog row
	// the pulled box was focused for its full cover
	assert(focused == rows[1].box[1].cover);
	// B puts the box back, B again returns to all shelves, B leaves
	nscript = 0;
	press(KEY_A, 70);
	press(KEY_B, 60);
	press(KEY_B, 70);
	press(KEY_B, 0);
	assert(run(path) == 4 && view.level == SHELF_ALL && view.pull < .01f);
	// A while the shelf is still opening (its walk up): the box comes out all the same
	nscript = 0;
	press(KEY_A, 20);
	press(KEY_A, 100);
	press(KEY_START, 0);
	assert(run(path) == 0 && view.level == SHELF_ONE && view.pto == 1);
	nscript = 0;
	press(KEY_B, 80);
	press(KEY_B, 70);
	press(KEY_B, 0);
	assert(run(path) == 4 && view.level == SHELF_ALL && view.pull < .01f);
	// Y on a box adds it to Favorites; X sorts every shelf (recently played first)
	nscript = 0;
	press(KEY_A, 70);
	press(KEY_LEFT, 40);
	press(KEY_Y, 10);
	press(KEY_X, 10);
	press(KEY_START, 0);
	assert(run(path) == 0);
	// one X: recently played first (Silent Hill was played last), on every shelf
	assert(rows[0].n == 2 && !strcmp(rows[0].box[0].title, "Silent Hill") && !strcmp(rows[0].box[1].title, "Ace Combat 2"));
	assert(view.sort == SHELF_SORT_RECENT && !strcmp(rows[1].box[0].title, "Silent Hill") && rows[1].box[0].fav);
	assert(view.layout != layout && !ui_wants_plain);   // the far shelves are made again; 1x only while drawing
	// a carton system without its own launch starts the catalog path
	nscript = 0;
	press(KEY_B, 70);
	press(KEY_DOWN, 10);
	press(KEY_A, 70);
	press(KEY_A, 140);
	press(KEY_A, 70);
	int r = run(path);
	assert(r == 1 && !strncmp(path, "gb/", 3));
	assert(preset_polls > 0);   // a downloaded preset pack is installed in the Library (its only main-thread step)
	assert(home_frames > 0 && home_allowed);
	// the room does the glides: B from all shelves leaves at once
	nscript = 0;
	press(KEY_B, 70);   // from the GBA shelf to all shelves
	press(KEY_B, 0);    // leave
	r = run(path);
	assert(r == 4);
	{   // HOME pressed during a download: that frame went to the HOME Menu, the Library goes on after it
		int before = home_frames;
		home_go = 1; nscript = 0; press(KEY_B, 0);
		assert(run(path) == 4 && home_frames >= before + 2 && !home_go && home_allowed);
	}
	{   // only a read error says so, once per scan (review R3), no counts; a memory limit (the earlier games stay)
		// and a complete check say nothing (owner 2026-11-12: no warning for a big or slow card)
		int before = cards;
		scan_status = BB_INDEX_LIMITED; scans = 1;
		nscript = 0; press(0, 2); press(KEY_B, 0);
		assert(run(path) == 4 && cards == before);
		scan_status = BB_INDEX_ERROR; scans = 2;
		nscript = 0; press(0, 2); press(KEY_B, 0);
		assert(run(path) == 4 && cards == before + 1 && !strcmp(card_title, "Some games may be missing") && strstr(card_text, "couldn't read part"));
		nscript = 0; press(0, 2); press(KEY_B, 0);
		assert(run(path) == 4 && cards == before + 1);   // the same scan: told already
		for (const char *c = card_text; *c; c++) assert(!(*c >= '0' && *c <= '9'));
		scan_status = BB_INDEX_COMPLETE; scans = 3;
		nscript = 0; press(0, 2); press(KEY_B, 0);
		assert(run(path) == 4 && cards == before + 1);
	}
	shelf_build_now();   // built already: nothing changes
	assert(view.nrows == 3);
	// after a game (started from anywhere: Continue, the dev remote, another shelf) the shelf opens
	// THAT game's shelf with the game selected, put back in its place
	for (int pass = 0; pass < 3; pass++) {
		const bb_system_t *sys = pass == 1 ? &sys_ps : &sys_gb;
		const char *played = pass == 0 ? "gb/Metroid Fusion.gba" : pass == 1 ? "ps/Tekken 3.cue" : "";
		view.level = SHELF_ALL; view.row = view.hsel = pass == 1 ? 2 : 1; view.zoom = view.zfrom = view.zto = 0;
		view.pull = view.pfrom = view.pto = 0;
		shelf_played(sys, played);
		nscript = 0;
		int swaps = swap_enters;
		assert(run(path) == 0);   // START: leaves at once
		assert(swap_enters == swaps + 1); // every game return arms the shared ready-frame reveal
		const shelf_row *row = &view.rows[view.row];
		assert(view.level == SHELF_ONE && row->system == sys && view.hsel == view.row && view.zoom == 1 && view.pull < .01f);
		const shelf_box *b = &row->box[view.sel[view.row]];
		if (pass == 0) assert(b->row == 0 && !strcmp(b->title, "Metroid Fusion"));          // by its path
		if (pass == 1) assert(b->row == 0 && !strcmp(b->title, "Tekken 3"));
		if (pass == 2) assert(!strcmp(b->title, "Advance Wars"));                          // no path: the one played last
	}
	// A PlayStation game ends while the Game Boy Advance library (the shelf before it, as on the real
	// registry) is still loading: the shelf first has only PlayStation, then Game Boy Advance appears
	// in front of it. The shelf and the game stay PlayStation's (on the Old 3DS this came back on the
	// Game Boy Advance shelf at its first box).
	sys_ps.release_year = 2000; real_order = true;
	nscript = 0;
	assert(run(path) == 0 && view.rows[1].system == &sys_gb);   // the real order, every shelf loaded
	view.sel[1] = 1;   // the player's box on the Game Boy Advance shelf
	const char *gb_chosen = view.rows[1].box[1].title;
	gb_count = 0; reload_at = 20;
	view.level = SHELF_ALL; view.row = view.hsel = 1; view.zoom = view.zfrom = view.zto = 0;
	view.pull = view.pfrom = view.pto = 0;
	shelf_played(&sys_ps, "ps/Crash Bandicoot.cue");
	nscript = 0;
	for (int i = 0; i < 60; i++) script[nscript++] = 0;   // no key: START only after the reload
	assert(run(path) == 0 && index_gen == 2 && view.nrows == 3);
	assert(view.rows[view.row].system == &sys_ps); // later publication appears during this visit, preserving the chosen system
	reload_at = 0; nscript = 0;
	assert(run(path) == 0 && view.nrows == 3); // next visit retains the completed catalog
	{
		const shelf_row *row = &view.rows[view.row];
		assert(view.level == SHELF_ONE && row->system == &sys_ps && view.row == 2 && view.hsel == 2);
		assert(!strcmp(row->box[view.sel[view.row]].title, "Crash Bandicoot"));
		assert(view.rows[1].system == &sys_gb);
		// the Game Boy Advance shelf was missing for a while: its chosen box is still the player's
		assert(!strcmp(view.rows[1].box[view.sel[1]].title, gb_chosen));
	}
	// The room handoff keeps the music's existing DSP wave buffer; other exits stop it.
	view.level = SHELF_ALL; view.zoom = view.zfrom = view.zto = 0;
	nscript = 0; press(KEY_B, 0);
	int stops = music_stops;
	assert(run(path) == 4 && music_stops == stops);
	nscript = 0;
	assert(run(path) == 0 && music_stops == stops + 1);
	sys_ps.release_year = 2002; real_order = false; reload_at = 0;
	nscript = 0;
	assert(run(path) == 0 && view.rows[view.row].system == &sys_ps);   // back to the test order, still on that shelf
	// ... and only once: a later visit keeps the camera where the player left it
	view.level = SHELF_ALL; view.row = view.hsel = 1; view.zoom = view.zfrom = view.zto = 0;
	nscript = 0;
	assert(run(path) == 0 && view.level == SHELF_ALL && view.row == 1);
	// Explicit rescan returns the usable old list. A presentation arriving
	// much later (after raw validation completed) still refreshes this visit.
	real_order=true;sys_ps.release_year=2000;gb_count=0;index_gen++;reload_at=0;nscript=0;
	assert(run(path)==0 && view.nrows==2);
	view.level=SHELF_ALL;view.zoom=view.zfrom=view.zto=0;view.pull=view.pto=0;
	// Hold for 1.5 s; publish after the scan has returned (two apt polls per frame).
	nscript=0;hold(KEY_Y,90);press(0,90);reload_at=280;
	assert(run(path)==0 && view.nrows==3 && rows[1].system==&sys_gb);
	reload_at=0;
	// Standing Library Settings gate: wall, browsing and held box; SELECT and remote.
	for(int context=0;context<3;context++)for(int remote=0;remote<2;remote++) {
		view.level=context?SHELF_ONE:SHELF_ALL;view.row=view.hsel=1;
		view.pull=view.pto=context==2?1:0;view.pt0=-10;view.pdur=.1f;
		view.prow=1;view.pidx=0;settings_calls=0;ui_request=remote?DEV_UI_SETTINGS:0;
		nscript=0;press(remote?0:KEY_SELECT,3);
		assert(run(path)==0 && settings_calls==1);
	}
	view.level=SHELF_ALL;view.pull=view.pto=0;
	{   // HOME > Close inside Settings, its dialogs (About / Credits) or a rescan: out at once, nothing drawn after it
		nscript = 0; press(0, 3); settings_calls = 0; ui_request = DEV_UI_SETTINGS; close_in_settings = true;   // the remote's Settings
		assert(run(path) == 0 && settings_calls == 1 && after_close == 0);
		nscript = 0; press(KEY_SELECT, 3); settings_calls = 0; close_in_settings = true;   // SELECT: Settings on the bookcase
		view.level = SHELF_ALL;
		assert(run(path) == 0 && settings_calls == 1 && after_close == 0);
		close_in_settings = false; close_in_load = true;
		nscript = 0; hold(KEY_Y, 90);   // Hold Y: rescan, the close comes while it polls
		assert(run(path) == 0 && closing && after_close == 0);
		close_load_success = true;
		nscript = 0; hold(KEY_Y, 90);   // retained games: successful scan result while closing
		assert(run(path) == 0 && closing && after_close == 0);
		close_load_success = false;
		close_in_load = false;
		view.level = SHELF_ONE; view.row = view.hsel = 1;
		view.zoom = view.zfrom = view.zto = 1;
		view.pull = view.pfrom = view.pto = 1;
		close_in_system = true; system_settings = 0;
		nscript = 0; press(KEY_A, 120); press(KEY_X, 3);
		assert(run(path) == 0 && system_settings == 1 && after_close == 0);
		close_in_system = false;
		view.level = SHELF_ALL; view.zoom = view.zfrom = view.zto = 0;
		view.pull = view.pfrom = view.pto = 0;
		nscript = 0; press(KEY_B, 0);   // and the same shelf still works afterwards
		assert(run(path) == 4 && after_close == 0);
	}
	favorite_rebuild_pending=true;build();assert(!favorite_rebuild_pending); // a sort/rebuild consumes deferred Favorite work
	printf("shelf controller: rows, layout, sort, favorites, settle-and-take-out, put back, launch, leave at once, preset install: PASS\n");
	return 0;
}
