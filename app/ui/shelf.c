#include "ui_trace_views.h"
/* BreadBox bookshelf controller (room themes): content, navigation, motion and
 * launch. Painting is shelf_scene*.c; the screens are components/shelf.inc.
 * Motion follows the approved mock (js/breadbox-r8.js) step for step: a
 * spring-damped camera along the shelf, a box that comes out by itself when
 * the shelf settles, a shared, eased dolly between all shelves and one shelf. */
#include "app_policy.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "theme.h"
#include "app_paths.h"
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
#include "bb_settings.h"
#include "bb_update.h"
#include "home_presentation.h"
#include "bb_registry.h"
#include "bb_index.h"
#include "devtools.h"
#include "shelf.h"
#include "library_model.h"
#include "cold_code.h"
#include "list_look.h"
#include "shelf_geometry.h"
#include "shelf_input.h"
#include "presetupdate.h"
#include "ui_motion.h"


#define DT (1.0f / 120)
#define SLOT_X0 40.0f       // first spine edge on every shelf, mm
#define ROW_GAP_MIN 180.0f  // board to board, mm (the mock's three shelves)
#define CASE_W_MIN 640.0f
#define MAX_BOXES 1024
#define MAX_ROWS 8

static shelf_view view;
static shelf_row rows[MAX_ROWS];
static shelf_box *boxes;
static int nboxes;
static unsigned generation;
static bool favorite_rebuild_pending;
// what the shelves were built from: the shared index and every system's published count
// (the PlayStation library is published after the index settles)
static unsigned library_key(void) { return bb_index_generation() * 2654435761u + (unsigned)bb_system_total(); }

const void *shelf_picked(void) { return NULL; }

static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float ease(float x) { return ui_motion_ease(x); }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float rnd(int i) { float x = sinf(i * 127.1f + 311.7f) * 43758.5453f; return x - floorf(x); }

static void lay_out(void)
{
	float maxh = 0, end = CASE_W_MIN - 60;
	for (int r = 0; r < view.nrows; r++) {
		shelf_row *row = &rows[r];
		bb_library_sort(row, view.sort);
		row->hmax = 0;
		for (int i = 0; i < row->n; i++) row->hmax = fmaxf(row->hmax, row->box[i].h);
		float s = row->s = row->hmax > 0 ? fminf(SHELF_CLOSE_MAX, SHELF_CLOSE_PX / row->hmax) : SHELF_CLOSE_MAX;
		float x = SLOT_X0;
		for (int i = 0; i < row->n; i++) {
			shelf_box *b = &row->box[i];
			float gap = 1.2f + rnd(r * 50 + i) * 1.6f;
			// whole close-up pixels: left edge, thickness and height
			float left = roundf((x + gap) * s) / s;
			b->t = fmaxf(1, roundf(b->t * s)) / s;
			b->h = roundf(b->h * s) / s;
			b->x = left + b->t / 2;
			x = left + b->t;
			if (b->h > maxh) maxh = b->h;
		}
		row->hmax = 0;   // now on the grid too
		for (int i = 0; i < row->n; i++) row->hmax = fmaxf(row->hmax, row->box[i].h);
		row->end = x;
		if (x > end) end = x;
	}
	view.row_gap = maxh + 43 > ROW_GAP_MIN ? maxh + 43 : ROW_GAP_MIN;
	view.case_w = end + 60;
	view.case_top = view.nrows * view.row_gap;
	for (int r = 0; r < view.nrows; r++)
		rows[r].base = (view.nrows - 1 - r) * view.row_gap;
	view.layout++;
}

// Shelves from the registry: Favorites on top, then every system with games.
static int row_of(const bb_system_t *s, int fallback)
{
	for (int r = 0; r < view.nrows; r++) if (rows[r].system == s) return r;
	return fallback < view.nrows ? fallback : view.nrows - 1;
}
// Each shelf's chosen box, by system, also while that shelf is missing from a build.
static const bb_system_t *sel_sys[MAX_ROWS];
static int sel_box[MAX_ROWS], nsel;
static bool sel_clamped[MAX_ROWS];
static char sel_key[MAX_ROWS][sizeof(((shelf_box *)0)->key)];
static void sel_remember(const bb_system_t *s, int box, const char *key)
{
 int i;
 for (i = 0; i < nsel; i++) if (sel_sys[i] == s) break;
 if (i == nsel) { if (nsel == MAX_ROWS) return; sel_sys[nsel++] = s; }
 sel_box[i] = box;
 snprintf(sel_key[i], sizeof(sel_key[i]), "%s", key);
}
static int sel_recall(const bb_system_t *s)
{
	for (int i = 0; i < nsel; i++) if (sel_sys[i] == s) {
  for (int r = 0; r < view.nrows; r++) if (rows[r].system == s)
   for (int k = 0; k < rows[r].n; k++)
    if (sel_key[i][0] && !strcmp(rows[r].box[k].key, sel_key[i])) return k;
  return sel_box[i];
 }
	return 0;
}
static void sel_capture(int r)
{
	// A partial catalog's temporary clamp is not a new player choice.
	int edge = rows[r].n ? rows[r].n - 1 : 0;
	if (!sel_clamped[r] || view.sel[r] != edge)
		sel_remember(rows[r].system, view.sel[r], rows[r].n ? rows[r].box[view.sel[r]].key : "");
}
static void build(void)
{
	favorite_rebuild_pending=false; // this build satisfies any deferred Favorite geometry
	// The set of shelves can change between builds: after a game the other systems' libraries are
	// still loading. Keep the current shelf, each shelf's chosen box (also of a shelf missing for a
	// while) and the pulled box by system, never by position (a PlayStation game used to come back
	// on the Game Boy Advance shelf).
	int nkeep = view.nrows;
	for (int r = 0; r < nkeep; r++) sel_capture(r);
	const bb_system_t *row_sys = nkeep ? rows[view.row].system : NULL, *hsel_sys = nkeep ? rows[view.hsel].system : NULL,
		*prow_sys = nkeep && view.prow < nkeep ? rows[view.prow].system : NULL;
	int keep_row = nkeep ? view.row : -1, keep_hsel = view.hsel, keep_prow = view.prow;
 nboxes=bb_library_build(rows,MAX_ROWS,boxes,MAX_BOXES,&view.nrows,&view.total,
  view.last_title,sizeof(view.last_title),shelf_geometry_fill);
	lay_out();
	if (nkeep) {
		for (int r = 0; r < view.nrows; r++) view.sel[r] = sel_recall(rows[r].system);
		view.row = row_of(row_sys, keep_row);
		view.hsel = row_of(hsel_sys, keep_hsel);
		if (view.hsel != keep_hsel) view.hcur = view.hsel;   // same shelf, new position: no glide
		view.prow = row_of(prow_sys, keep_prow);
	}
	for (int r = 0; r < view.nrows; r++) {
		sel_clamped[r] = view.sel[r] >= rows[r].n;
		if (view.sel[r] >= rows[r].n) view.sel[r] = rows[r].n ? rows[r].n - 1 : 0;
		if (view.sel[r] < 0) view.sel[r] = 0;
	}
	view.rows = rows;
	if (view.pull > 0 && (view.prow >= view.nrows || view.pidx >= rows[view.prow].n)) view.pull = view.pto = 0;
	generation = library_key();
}

static void pull(float to, float dur) { view.pfrom = view.pull; view.pto = to; view.pt0 = view.t; view.pdur = dur > .05f ? dur : .05f; view.rev = 0; view.sped = false; }
// The take-out and the put-back run at the hands' clip lengths (squeezed they snap); any key
// during one acts at once without a cut: it reverses from where it is, or hurries the rest.
static float progress(void) { return view.pdur > 0 ? clampf((view.t - view.pt0) / view.pdur, 0, 1) : 1; }
static bool taking_out(void) { return view.pto == 1 && progress() < 1 && view.rev != 2; }
static bool putting_back(void) { return view.pto == 0 && view.pull > .002f && progress() < 1; }
static void take_out(void)
{
	view.prow = view.row; view.pidx = view.sel[view.row];
	view.side = view.turn_from = view.turn_steps = 0;
	view.turn_queued = 0; view.turn_home = false;
	pull(1, shelf_scene_clip_seconds(&view, "pick", 1.05f)); view.arm = false;
}
// the box in the hand is turning (shelf_input.h shelf_turn_side): the next turn waits for it
static bool turning(void) { return view.turn_steps && view.t - view.turn_t0 < UI_MOTION_TURN_S; }
// turn the box in the hand `steps` quarters (+ R, - L) from where it is
static void turn(int steps)
{
	if (!steps) return;
	view.turn_from = view.side;
	view.side = shelf_turn_side(view.side, steps);
	view.turn_steps = steps;
	view.turn_t0 = view.t;
	sfx_play(SFX_MOVE);
}
// go on toward `to` with a clip of `len` s already `at` s in: the clip and the pull both carry on
static void resume(float to, float len, float at)
{
	float was = view.pull;
	pull(to, len);
	float k = clampf(at / view.pdur, 0, 1), e = ease(k);
	view.pt0 = view.t - k * view.pdur;
	if (e < .999f) view.pfrom = (was - to * e) / (1 - e);   // lerp(pfrom, to, e) == was
}
static void put_back(float speed)
{
	view.turn_queued = false;
	if (taking_out()) {   // half way out: the same way back, from where it is
		float back = progress() * shelf_scene_clip_seconds(&view, "pick", 1.05f);
		pull(0, back / speed);
		view.rev = 1;
		view.rev_t = back;
		return;
	}
	float len = shelf_scene_clip_seconds(&view, "putback", .7f);
	if (view.pto == 1 && view.rev == 2 && progress() < 1) {   // a put-back taken back: on with it from there
		resume(0, len, view.rev_t * (1 - progress()));
		view.pdur /= speed; view.pt0 = view.t - (view.t - view.pt0) / speed;
		return;
	}
	pull(0, len / speed);
}
static void hurry(void)   // the rest of this take-out / put-back at 1.5x, from where it is
{
	if (view.sped) return;
	float k = progress();
	view.pdur /= 1.5f;
	view.pt0 = view.t - k * view.pdur;
	view.sped = true;
}
static void take_back_out(void)   // A during a put-back: it comes back out the way it went
{
	if (view.rev == 1) {   // a take-out going back: forward again from where it is
		resume(1, shelf_scene_clip_seconds(&view, "pick", 1.05f), view.rev_t * (1 - progress()));
		return;
	}
	float back = progress() * shelf_scene_clip_seconds(&view, "putback", .7f);
	pull(1, back);
	view.rev = 2;
	view.rev_t = back;
}

static void step(float dt)
{
	shelf_view *w = &view;
	w->t += dt;
	ui_motion_settle(&w->cur, &w->cv, w->sel[w->row], UI_MOTION_SETTLE_RATE, dt);
	ui_motion_settle(&w->hcur, &w->hcv, w->hsel, UI_MOTION_SETTLE_RATE, dt);
	w->zoom = lerpf(w->zfrom, w->zto, ease((w->t - w->zt0) / UI_MOTION_CAMERA_S));   // the UI mock's walk up to a shelf
	w->pull = lerpf(w->pfrom, w->pto, ease((w->t - w->pt0) / w->pdur));
	// stop and it comes out by itself after UI_SHELF_AUTO_TAKEOUT_S (disabled: the case would come out
	// too quickly; the selected box only tips out, A takes it)
	if (UI_SHELF_AUTO_TAKEOUT_S > 0 && w->arm && w->level == SHELF_ONE && w->zoom > .99f && w->pto == 0 && w->pull < .002f &&
	    w->t - w->last_move > UI_SHELF_AUTO_TAKEOUT_S && fabsf(w->cur - w->sel[w->row]) < .03f && rows[w->row].n)
		take_out();
}

// The Room walks its camera up to a shelf and back (the keys wait for it); the list themes have no camera,
// so there the walk is over at once and the next key counts right away.
static void walk(float to)
{
	if (!theme_is_room()) view.zoom = to;
	view.zfrom = view.zoom; view.zto = to; view.zt0 = view.t;
}
static void open_shelf(int r)
{
	view.level = SHELF_ONE; view.row = r; view.cur = view.sel[r]; view.cv = 0;
	walk(1); view.arm = true; view.last_move = view.t + UI_MOTION_CAMERA_S;
}
static void to_all(void)
{
	view.level = SHELF_ALL; view.hsel = view.row; view.hcur = view.row; view.hcv = 0;
	walk(0);
	log_printf("shelf: walk back row=%d selected=%d games=%d\n", view.row, view.sel[view.row], rows[view.row].n);
}

static float want_out = -1;   // when A asked for the selected box (-1: not)
static int want_row, want_idx;   // which box it was
static const bb_system_t *played_system;   // shelf_played: the game just played (its box in front)
// Navigation always belongs to the chosen shelf. B returns to the bookcase;
// choosing another shelf there is the deliberate way to change systems.
static void move(int d, int sound)
{
	shelf_row *r = &rows[view.row];
	if (!r->n) return;
	int i = view.sel[view.row] + d;
	i = i < 0 ? 0 : i >= r->n ? r->n - 1 : i;
	if (i == view.sel[view.row]) { view.cv = d < 0 ? -.7f : .7f; return; }   // the end: the shelf leans, silent
	if (view.pto == 1) put_back(1.5f);
	view.sel[view.row] = i; view.last_move = view.t; view.arm = true;
	sfx_play(sound);
}

static bool favorite_pending;
static void favorite(void)
{
	// Removing a held Favorite must finish its return before rebuilding the row.
	if (view.row == 0 && (view.pto == 1 || view.pull > .002f)) {
		if (view.pto == 1) put_back(1);
		favorite_pending = true; return;
	}
	shelf_row *r = &rows[view.row];
	if (!r->n) return;
	shelf_box *b = &r->box[view.sel[view.row]];
 int live_row;
 if(!bb_library_favorite(boxes,nboxes,b,&live_row))return;
	// A changed index also changes cover game indices. Defer geometry until the
	// normal drain invalidates cover slots; a metadata-only rebuild is safe only
	// while the exact published catalog is still current.
	if(generation != library_key()){favorite_rebuild_pending=true;sfx_play(SFX_SELECT);return;}
	// the Favorites shelf changes: keep the camera on the same game elsewhere
	int row = view.row, sel = view.sel[row];
	const bb_system_t *s = b->system; int srow = live_row;
	bool out = view.pto == 1;
	// Only shelf order changes here; the published catalog and cover slot indices do not.
	build();
	if (row == 0) {
		view.sel[0] = sel < rows[0].n ? sel : rows[0].n ? rows[0].n - 1 : 0;
		if (out) view.pull = view.pto = 0;
	} else {
		for (int i = 0; i < rows[row].n; i++)
			if (rows[row].box[i].system == s && rows[row].box[i].row == srow) view.sel[row] = i;
	}
	sfx_play(SFX_SELECT);
}

// the box a game was started from: it is the one in front when the game ends
static const bb_system_t *launched_sys;
static int launched_row;
static char launched_path[512];
// A game that can't start says why, once, through the shared dialog.
// The game the list is on (the list themes play and set it up from the list: no box is taken out there).
static const shelf_box *chosen_box(void)
{
	return view.level == SHELF_ONE && rows[view.row].n ? &rows[view.row].box[view.sel[view.row]] : NULL;
}
#define SHELF_LAUNCH_TITLE "Can't start this game"
static const char *launch_why;   // why the last launch() failed
static bool launch(char *path, size_t cap)
{
	const shelf_box *b = theme_is_room() ? shelf_pulled_box(&view) : chosen_box();
	launch_why = BB_LIBRARY_LAUNCH_GONE;
	if (!b) return false;
 int row=-1;
 bool ok=bb_library_launch(b,path,cap,&launch_why,&row);
	if (ok) {
		launched_sys = b->system;
		launched_row = row;
		snprintf(launched_path, sizeof(launched_path), "%s", path);
	}
	return ok;
}

static const ui_button_t *bar(void)
{
 static ui_button_t buttons[BB_LIBRARY_ACTIONS];
 int phase=view.level==SHELF_ALL ? BB_LIBRARY_SYSTEMS : !theme_is_room() ? BB_LIBRARY_LIST
  : view.pto==1 || view.pull>.002f ? BB_LIBRARY_HELD : BB_LIBRARY_GAMES;
 const char *primary=phase!=BB_LIBRARY_HELD ? NULL : favorite_pending ? "Returning..." : view.pto==0 ? "Take out"
  : progress()<1 ? (view.sped ? "Lifting..." : "Lift faster") : view.side ? "Front" : "Play";
 bool on=view.level!=SHELF_ALL && rows[view.row].n && rows[view.row].box[view.sel[view.row]].fav;
 bb_library_actions(phase,on,primary,buttons,BB_LIBRARY_ACTIONS);
 return buttons;
}

// A check that could not read part of the card says so once per check, through the shared dialog.
// Only a real read error: a slow or big card always finishes, and a memory limit keeps the games found before
// (bb_index.h). No counts: the player can't act on them.
#define SHELF_SCAN_TITLE "Some games may be missing"
#define SHELF_SCAN_ERROR "BreadBox couldn't read part of your games folder. Check the SD card, then hold Y in your collection to check it again."
static bool scan_stopped_early(void)
{
	return bb_index_status() == BB_INDEX_ERROR;
}
static bool scan_card(void)
{
	static unsigned told;
	unsigned scans = bb_index_scans();
	if (!scan_stopped_early() || scans == told) return false;
	told = scans;
	ui_confirm_action(SHELF_SCAN_TITLE, SHELF_SCAN_ERROR, "A  Back", false);
	return true;
}

static bool built, stats_stale, return_catalog;
static int stats_at;
// A return rebuild can wait for an in-flight cover read. Favorite/sort may
// rebuild the same catalog meanwhile; cancelling that wait must give its
// pause back, even though the generation now matches and no rebuild runs.
static bool cover_drain_pending;
static bool cover_drain(bool needed)
{
 if (!needed) {
  if (cover_drain_pending) covers_resume();
  cover_drain_pending=false;
  return false;
 }
 cover_drain_pending=true;
 return covers_stop();
}
// The shelves' boxes go to the game (shelf_release); a copy waits on the SD card, so the
// shelf is there the moment the game ends, while the library is being read in again.
#define BOXES_FILE PSX3D_DIR "/cache/shelf-boxes.dat"
static struct { bool saved; int nboxes, off[MAX_ROWS], n[MAX_ROWS], nrows; unsigned generation; } kept;
static void keep_boxes(void)
{
	kept.saved = false;
	if (!boxes || !built || !nboxes) { log_printf("shelf: no boxes to keep (%d, built %d)\n", nboxes, built); return; }
	mkdir(PSX3D_DIR "/cache", 0777);
	FILE *f = fopen(BOXES_FILE, "wb");
	bool ok = f && fwrite(boxes, sizeof(*boxes), nboxes, f) == (size_t)nboxes;
	if (f) ok &= fclose(f) == 0;
	log_printf("shelf: %d boxes kept for after the game: %s\n", nboxes, ok ? "written" : "not written");
	if (!ok) return;
	for (int r = 0; r < view.nrows; r++) { kept.off[r] = rows[r].box ? (int)(rows[r].box - boxes) : 0; kept.n[r] = rows[r].n; }
	kept.nrows = view.nrows;
	kept.nboxes = nboxes;
	kept.generation = generation;
	kept.saved = true;
}
// Owned worker snapshot: only the main thread installs it into the shelf.
static struct { int done, count, at; unsigned failures; u64 retry_at; shelf_box *data; } restore;
static void restore_read(void *unused)
{
 (void)unused;
 shelf_box *data = bb_index_cancelled() ? NULL : calloc(MAX_BOXES, sizeof(*data));
 FILE *f = data ? fopen(BOXES_FILE, "rb") : NULL;
 bool ok = f && fread(data, sizeof(*data), restore.count, f) == (size_t)restore.count;
 if(f)fclose(f);
 if(!ok || bb_index_cancelled()){free(data);data=NULL;}
 restore.data=data;__atomic_store_n(&restore.done,2,__ATOMIC_RELEASE);
}
void shelf_restore_request(void)
{
 if(!kept.saved || __atomic_load_n(&restore.done,__ATOMIC_ACQUIRE))return;
 u64 now=svcGetSystemTick();
 if(restore.retry_at && now<restore.retry_at)return;
 restore.count=kept.nboxes;
 __atomic_store_n(&restore.done,1,__ATOMIC_RELEASE);
 if(!bb_index_defer_visible(restore_read,NULL)) {
  __atomic_store_n(&restore.done,0,__ATOMIC_RELEASE);
  restore.retry_at=now+SYSCLOCK_ARM11/10; // at most one failure per 100 ms, not per caller
  if(++restore.failures>=3)kept.saved=false;
 } else {restore.failures=0;restore.retry_at=0;restore.at=0;}
}
static bool restore_boxes(void)
{
 shelf_restore_request();
 if(__atomic_load_n(&restore.done,__ATOMIC_ACQUIRE)!=2 || room_pace_return_pending())return false;
 // A partially published catalog can be opened immediately. Keep the saved array
 // until the matching catalog is back; never replace a different library with it.
 bool match=kept.saved && kept.nrows==view.nrows && library_key()==kept.generation;
 if(!match && kept.saved && !bb_index_complete())return false;
 if(match && restore.data && (!built || generation!=library_key()))return false;
 if(match && restore.data) {
  // The live catalog owns geometry, favorites, history and navigation. Restore
  // only cached colors by identity, in bounded slices; never replace the array
  // or force strips/flow/case relayout while the player is gliding into it.
  int end=restore.at+16;if(end>nboxes)end=nboxes;
  for(;restore.at<end;restore.at++) {
   shelf_box *live=&boxes[restore.at];
   for(int i=0;i<restore.count;i++) {
    const shelf_box *saved=&restore.data[i];
    if(saved->system!=live->system || saved->row!=live->row)continue;
    if(!live->color)live->color=saved->color;
    if(!live->accent)live->accent=saved->accent;
    break;
   }
  }
  if(restore.at<nboxes)return false;
 }
 shelf_box *data=restore.data;restore.data=NULL;
 __atomic_store_n(&restore.done,0,__ATOMIC_RELEASE);
 kept.saved=false;
 bool adopted=data && match;
 free(data);
 if(adopted)log_printf("shelf: %d kept boxes published from worker (colors only, no relayout)\n",nboxes);
 return adopted;
}
// After a game: the played box's play time and date, once the index is back (no rebuild):
// found by its path (started from the shelf, the TV's Continue or remotely), 16 boxes a frame
static const bb_system_t *stats_sys;
static char stats_path[512];
static const char *bare(const char *p) { return strncmp(p, "sdmc:", 5) ? p : p + 5; }   // the same file either way
static void refresh_played(void)
{
	if (!stats_stale || !bb_index_complete()) return;
	for (int n = 0; n < 16 && stats_at < nboxes; n++, stats_at++) {
		shelf_box *b = &boxes[stats_at];
		bb_catalog_item item;
		if (!stats_sys || b->system != stats_sys || !bb_library_item(b->system, b->row, &item) || strcasecmp(bare(item.path), bare(stats_path))) continue;
		bb_library_key(b->system, &item);
		b->last = playlog_last(item.key);
		b->seconds = playlog_seconds(item.key);
		log_printf("shelf: %s played, last %lu, %lu s in all\n", b->title, (unsigned long)b->last, (unsigned long)b->seconds);
		for (int i = 0; i < nboxes; i++)   // its Favorites copy
			if (i != stats_at && boxes[i].system == b->system && boxes[i].row == b->row) { boxes[i].last = b->last; boxes[i].seconds = b->seconds; }
		stats_at = nboxes;
	}
	if (stats_at >= nboxes) stats_stale = false;
}
// the library has changed and settled: still a while, not still filling up after a game
static bool library_settled(int still)
{
	return still >= 30 && (bb_system_total() >= view.total || bb_index_complete());
}

static void first_view(void)
{
	static bool started;
	if (started) return;
	started = true;
	view.level = SHELF_ALL; view.hsel = view.nrows > 1 ? 1 : 0; view.hcur = view.hsel;
	view.zoom = view.zfrom = view.zto = 0; view.zt0 = -10; view.pt0 = -10; view.pdur = 1; view.play_t0 = -10;
}
static void ensure_built(void)
{
 if(restore_boxes()){first_view();return;}
	if (!boxes) boxes = calloc(MAX_BOXES, sizeof(*boxes));
	if (!boxes) return;
	if (!built || generation != library_key()) { build(); built = true; }
	first_view();
}
static int preload_step;   // 0 the shelves, 1 the spine atlases, 2 the hands, 3 all there
static bool preload_pause;
static u64 preload_waiting;
/* Quiet-frame preloading can span frames, but never owns a pause in a child menu. */
void shelf_preload_end(bool stay_in_menu)
{
 if(preload_pause && stay_in_menu)covers_resume();
 preload_pause=false;preload_waiting=0;
}
void shelf_library_changed(void){built=false;}
bool shelf_preload_step(void)
{
	static int still;
	int step = preload_step;
	static unsigned seen;
 if(room_pace_return_pending()){shelf_preload_end(true);return false;}

	// the library publishes system by system, game by game: build once it has been still a while
	unsigned key = library_key();
	still = key == seen ? still + 1 : 0;
	seen = key;
	if (restore_boxes()) {shelf_preload_end(true); if(step==0)preload_step = 1; return false; }   // back from a game: as they were
	refresh_played();
	if (step == 0 || (step == 3 && (!built || generation != key))) {   // the shelves, once the library index has settled
		if (!library_settled(still)) {shelf_preload_end(true);return false;}
		// Opening the GBA library drains the covers' worker (covers_library_begin), which on a cold card
		// sits in a download or a decode for seconds: 2621 froze the room 5.2 s on its first start. Ask it to stop
		// (never waits: the room goes on drawing); the shelves are built on the frame it has stopped.
		preload_pause=true;
		if (!covers_stop()) {
			if (!preload_waiting) preload_waiting = svcGetSystemTick();
			return false;
		}
		u64 t0 = svcGetSystemTick();
		ensure_built();
		u64 waited=preload_waiting ? t0-preload_waiting : 0;
		shelf_preload_end(true);
		log_printf("shelf: %d boxes on %d shelves built in %lu ms (%lu ms after the covers were asked to stop)\n", nboxes, view.nrows,
			(unsigned long)((svcGetSystemTick() - t0) / (SYSCLOCK_ARM11 / 1000)),
			(unsigned long)(waited / (SYSCLOCK_ARM11 / 1000)));
		if (step == 0) preload_step = boxes ? 1 : 0;
		return false;
	}
	if (step == 1) { if(theme_is_room())shelf_scene_prepare(); preload_step = 2; return false; }
	if (step == 2) { if (!theme_is_room() || room_hands_preload_step()) preload_step = 3; return false; }   // a part a frame
	return built && generation == library_key();
}
bool shelf_wall_pose(room_pose *out)
{
	if (!view.nrows) return false;   // the shelves' layout is kept while their boxes are given to a game
	return shelf_scene_wall_pose(&view, out);
}
void shelf_build_now(void)
{
	if (!boxes) boxes = calloc(MAX_BOXES, sizeof(*boxes));
	if (boxes && !view.rows) { build(); built = true; }
}

static void draw_top(void *unused);
static void draw_bottom(void *unused);

// Back from a screen of its own: the key that closed it must not act here too.
static void settle_input(void)
{
	input_poll();
}

static const char *sort_names[SHELF_SORTS] = { "Sorted A to Z", "Recently played first", "Most played first" };

static char played_path[512];
void shelf_played(const bb_system_t *s, const char *path)
{
	played_system = s;
	snprintf(played_path, sizeof(played_path), "%s", path ? path : "");
	stats_sys = s;   // its box's play time, wherever it was started from (refresh_played)
	snprintf(stats_path, sizeof(stats_path), "%s", path ? path : "");
	stats_at = 0;
	stats_stale = true;
}
// After a game: that game's shelf, close up, the game selected (by its path, else the system's
// most recently played box), put back in its place.
// Stays pending until the system's shelf exists (after a game its index may still be loading) or a key is pressed.
static void show_played(void)
{
	const bb_system_t *s = played_system;
	if (!s) return;
	for (int r = 1; r < view.nrows; r++) {
		if (rows[r].system != s || !rows[r].n) continue;
		int at = -1;
		u32 newest = 0;
		if (s == launched_sys && played_path[0] && !strcmp(played_path, launched_path))   // started from this shelf
			for (int i = 0; i < rows[r].n && at < 0; i++) if (rows[r].box[i].row == launched_row) at = i;
		bool known = at >= 0;
		for (int i = 0; i < rows[r].n && at < 0; i++) {
			bb_catalog_item item;
			if (!played_path[0] || !bb_library_item(s, rows[r].box[i].row, &item)) continue;
			known = true;
			if (!strcmp(item.path, played_path)) at = i;
		}
		if (!known && played_path[0] && !bb_index_complete()) return;   // the library is being read in: not yet
		for (int i = 0; i < rows[r].n && at < 0; i++)
			if (rows[r].box[i].last > newest) newest = rows[r].box[i].last;
		for (int i = 0; i < rows[r].n && at < 0 && newest; i++)
			if (rows[r].box[i].last == newest) at = i;
		view.row = view.hsel = view.hcur = r;
		if (at >= 0) view.sel[r] = at;
		view.hcv = 0;
		view.level = SHELF_ONE;
		view.cur = view.sel[r];
		view.cv = 0;
		view.zoom = view.zfrom = view.zto = 1;
		view.zt0 = -10;
		view.arm = false;
		played_system = NULL;
		return;
	}
}

static PSX3D_COLD int shelf_publications(bool launching,bool *publication_changed,bool refresh_requested,
 bool *hands_ready,bool *remote_settings,u64 *last,u64 logic0)
{
		// Every visit follows publication, including a scan started in the room.
		// Rebuild only after the held box is back and the catalog has settled.
		(*publication_changed) |= bb_index_poll(true); // Adapter presentation can finish after shared counts stop changing.
		if(refresh_requested && bb_index_status()!=BB_INDEX_SCANNING && view.notice && !strcmp(view.notice,"Checking collection...")) {
			view.notice=scan_stopped_early() ? NULL : "Collection checked";view.notice_t=view.t;   // an early stop has its card
		}
		if (!launching && view.pull < .002f && scan_card()) {
			if (!aptMainLoop() || !gspHasGpuRight()) return 0;   // closed inside the card: no more drawing
			settle_input(); *last = svcGetSystemTick();
		}
		if (covers_home_frame()) { *last = svcGetSystemTick(); return -2; }   // HOME: held back while the covers use the network (as at Home)
		u64 l1 = svcGetSystemTick();
		if (dev_tick(false)) return 3;
		if (dev_ui_take() == DEV_UI_SETTINGS) *remote_settings = true;
		int saves = bb_platform_saves(BB_SAVES_MENU);   // the last game's save could not be written: ask
		if (saves == BB_SAVES_CLOSE) return 0;            // closed inside its dialog: no more drawing
		if (saves == BB_SAVES_ASKED) { settle_input(); *last = svcGetSystemTick(); }
		// the library changed: the shelves again, once the covers' worker has paused (never
		// waited for here: it may be in a download, a stalled frame of half a second)
		static unsigned seen_key, still;
		still = library_key() == seen_key ? still + 1 : 0;
		seen_key = library_key();
		u64 l2 = svcGetSystemTick();
		if(theme_is_room())room_scene_library_follow(false);   // the bookcase follows the library here too
		u64 l3 = svcGetSystemTick();
		if(kept.saved || __atomic_load_n(&restore.done,__ATOMIC_ACQUIRE))restore_boxes();
		refresh_played();
		u64 l4 = svcGetSystemTick();
		if (l4 - logic0 > SYSCLOCK_ARM11 / 125)   // over 8 ms: which part (the pace line only says "logic")
			log_printf("shelf: logic %.1f ms: library %.1f, remote %.1f, bookcase %.1f, played %.1f\n", (l4 - logic0) * 1000.0 / SYSCLOCK_ARM11,
				(l1 - logic0) * 1000.0 / SYSCLOCK_ARM11, (l2 - l1) * 1000.0 / SYSCLOCK_ARM11, (l3 - l2) * 1000.0 / SYSCLOCK_ARM11, (l4 - l3) * 1000.0 / SYSCLOCK_ARM11);
		if (theme_is_room() && !(*hands_ready)) {
			u64 h0 = svcGetSystemTick();
			bool still_shelf = view.pull < .002f && view.pto == 0 && fabsf(view.cv) < .3f && fabsf(view.zoom - view.zto) < .002f;
			// a still shelf has time to spare; on the Old 3DS 1 ms: with the chosen box's case being
			// made (warm_case, ~1.5 ms) 3 ms took its 4 ms drawing + 7.5 ms GPU past the frame (33 ms)
			room_hands_prepare_ms(still_shelf && g_new3ds ? 3 : 1);
			(*hands_ready) = room_hands_preload_step();
			room_pace_add(ROOM_PACE_PRELOAD, svcGetSystemTick() - h0);
		}
		if (cover_drain(view.pull < .002f && view.pto == 0 && want_out < 0 && !launching && ((*publication_changed) || generation != library_key()) && library_settled(still))) {
			// the covers' slots point at the old list: let them go. Not covers_reset(): that is the
			// user's rescan (Y), it forgets the database's misses and writes them to the SD card on
			// this thread (174 ms in Azahar, once after every game)
			u64 t0 = svcGetSystemTick();
			if (!covers_library_begin()) {covers_resume();return -2;} covers_library_end();
			build(); (*publication_changed)=false; favorite_rebuild_pending=false; show_played(); cover_drain(false);
			if(refresh_requested){view.notice="Collection updated";view.notice_t=view.t;}
			log_printf("shelf: the library changed (%u games), shelves made again in %.1f ms\n", (unsigned)bb_system_total(),
				(svcGetSystemTick() - t0) * 1000.0 / SYSCLOCK_ARM11);
		}
		if(return_catalog && bb_index_complete() && generation==library_key() && !kept.saved && !__atomic_load_n(&restore.done,__ATOMIC_ACQUIRE))return_catalog=false;
 return -1;
}

static PSX3D_COLD u32 shelf_touch(u32 k)
{
		if (k & KEY_TOUCH) {
			touchPosition tp;
			hidTouchRead(&tp);
			if (tp.py < BAR_TOP) {
				int hit;
				if (view.level == SHELF_ONE) {   // a box, a letter (-2: one without games), else the card: A
					hit = theme_is_room() ? shelf_scene_strip_hit(&view, tp.px, tp.py) : list_look_library_hit(&view, tp.px, tp.py);
					if (hit == -1 && theme_is_room()) hit = shelf_room_letter_hit(&view, tp.px, tp.py);
					if (hit >= 0 && hit != view.sel[view.row]) {
						if (view.pto == 1) put_back(1.5f);
						view.sel[view.row] = hit; view.last_move = view.t; view.arm = true;
					} else if (theme_is_room() ? hit == -1 : hit >= 0 && list_look_library_in_rows(&view, tp.py)) k |= KEY_A;   // the Room's card; a list's chosen row again
				} else if (view.level == SHELF_ALL && (hit = (theme_is_room()?shelf_scene_card_hit(&view,tp.px,tp.py):list_look_library_hit(&view,tp.px,tp.py))) >= 0) {
					if (hit == view.hsel) k |= KEY_A; else view.hsel = hit;
				}
			}
		}
 return k;
}

static PSX3D_COLD int shelf_system_keys(u32 k,u32 held,shelf_repeat *repeat,shelf_hold *rescan_hold,bool *refresh_requested)
{
			shelf_browse_step(repeat, 0, 0, view.t);
			if (fabsf(view.zoom - view.zto) > .01f) return -1;
			if (k & (KEY_UP | KEY_LEFT | KEY_CPAD_UP | KEY_CPAD_LEFT)) {
				if (view.hsel > 0) { view.hsel--; sfx_play(SFX_MOVE); } else view.hcv -= .7f;
			}
			if (k & (KEY_DOWN | KEY_RIGHT | KEY_CPAD_DOWN | KEY_CPAD_RIGHT)) {
				if (view.hsel < view.nrows - 1) { view.hsel++; sfx_play(SFX_MOVE); } else view.hcv += .7f;
			}
			if (k & KEY_A) { open_shelf(view.hsel); sfx_play(SFX_SELECT); }
			if (k & KEY_B) {
				sfx_play(SFX_BACK);
				return 4;
			}
			if (k & KEY_X) {
				view.sort = (view.sort + 1) % SHELF_SORTS; build();
				view.notice = sort_names[view.sort]; view.notice_t = view.t; sfx_play(SFX_SELECT);
			}
			// Rescan is a deliberate hold, including a held touch on its existing tab.
			if(k & KEY_Y){view.notice="Hold Y to rescan";view.notice_t=view.t;}
			u32 rescan_keys = held | (k & KEY_Y);
			if (held & KEY_TOUCH) rescan_keys |= ui_bar_input(bar(), BB_LIBRARY_ACTIONS, KEY_TOUCH) & KEY_Y;
			if (shelf_hold_action(rescan_hold, (rescan_keys & KEY_Y) != 0, view.t)) {
				if(!covers_stop_wait()){covers_resume();return -1;} covers_reset();
				bb_games_dir_retry();
				library_result loaded=library_load_result(g_cfg.games_dir,true);
                if (!aptMainLoop()) return 0; // lifecycle takes ownership of the pause
                covers_resume(); // staying in the menu, including empty/cancel results
                if(loaded==LIBRARY_EMPTY && !bb_system_total()){return 5;}
                if(loaded==LIBRARY_CANCELLED){view.notice=NULL;return -1;}
				*refresh_requested=true;
				view.notice = "Checking collection..."; view.notice_t = view.t; sfx_play(SFX_SELECT);
			}

			return -1;
}

static PSX3D_COLD bool shelf_game_keys(u32 k,u32 held,shelf_repeat *repeat,shelf_hold *rescan_hold,bool *launching)
{
		// one shelf
		{
			// All directions browse this shelf; bumpers page only while browsing.
			shelf_hold_action(rescan_hold, false, view.t);
			if (!theme_is_room()) { view.pull = view.pfrom = view.pto = 0; want_out = -1; }   // a box the Room left out or asked for (the theme changed in Settings)
			static shelf_repeat shoulder;
			bool out = view.pto == 1 || view.pull > .002f;
			// The list themes' A-Z list: Left / Right step letters as L / R do (the list itself runs up and down).
			const u32 sideways = KEY_LEFT | KEY_RIGHT | KEY_CPAD_LEFT | KEY_CPAD_RIGHT;
			u32 side = !theme_is_room() && !out && view.sort == SHELF_SORT_AZ ? sideways : 0;
			int delta = shelf_browse_step(repeat, k & ~side, held & ~side, view.t);
			if (delta) move(delta, SFX_MOVE);
			if (!out) {   // A to Z: the next / previous letter (stops at the ends); another order: five games
				u32 as_l = KEY_LEFT | KEY_CPAD_LEFT, as_r = KEY_RIGHT | KEY_CPAD_RIGHT;
				u32 kd = k | (k & side & as_l ? KEY_L : 0) | (k & side & as_r ? KEY_R : 0);
				u32 hd = held | (held & side & as_l ? KEY_L : 0) | (held & side & as_r ? KEY_R : 0);
				unsigned lr = shelf_shoulder_step(&shoulder, kd, hd, view.t);
				int d = lr & KEY_L ? -1 : 1;
				if (lr && view.sort == SHELF_SORT_AZ) move(bb_library_letter_jump(&rows[view.row], view.sel[view.row], d) - view.sel[view.row], SFX_JUMP);
				else if (lr) move(d * UI_SHELF_PAGE, SFX_JUMP);
				k &= ~(KEY_L | KEY_R);
			}
			if (k & (KEY_L | KEY_R)) {
				if (out) {
					if (theme_is_room() && !shelf_scene_hands()) {
						view.notice = "Turning is unavailable in this theme"; view.notice_t = view.t; sfx_play(SFX_ERROR);
					} else if (progress() < 1 || turning()) {   // after the lift / this turn, the same way
						int q = view.turn_queued + shelf_turn_steps(k);
						view.turn_queued = q > 2 ? 2 : q < -2 ? -2 : q;
						view.turn_home = false;
					} else turn(shelf_turn_steps(k));
				}
				// A rotation at either endpoint is still a rotation, never a page.
				k &= ~(KEY_L | KEY_R);
			}
		}
		if ((k & KEY_A) && !theme_is_room()) {   // the list themes: A plays the chosen game (PSX3D v0.5.2), no box to take out
			if (rows[view.row].n) { view.play_t0 = view.t; *launching = true; sfx_play(SFX_LAUNCH); k &= ~(KEY_B | KEY_Y | KEY_X); }   // this game: nothing else pressed with A may change the list under it
			else sfx_cancel();
		} else if (k & KEY_A) {
			if (view.pto == 1 && progress() < 1) hurry();                // already coming: sooner
			else if (putting_back()) { take_back_out(); sfx_play(SFX_SELECT); }
			else if (view.pto == 0 && view.pull < .02f && rows[view.row].n) { want_out = view.t; want_row = view.row; want_idx = view.sel[view.row]; sfx_play(SFX_SELECT); }   // its cover first
			else if (view.pto == 1 && view.pull > .98f && (view.side || view.turn_queued) && turning()) { view.turn_home = true; view.turn_queued = 0; }   // Front after this turn
			else if (view.pto == 1 && view.pull > .98f && view.side) turn(shelf_turn_home(view.side));
			else if (view.pto == 1 && view.pull > .98f) { view.play_t0 = view.t; *launching = true; sfx_play(SFX_LAUNCH); }
			else if (!rows[view.row].n) sfx_cancel();   // an empty shelf: nothing to take
		}
		// A on a box: out once its full-size cover, its back and its case texture are made (a moment, at most
		// UI_SHELF_COVER_WAIT_S), never the small cover in the hand; moved on meanwhile: not
		if (want_out >= 0) {
			const shelf_row *wr = &rows[view.row];
			if (view.row != want_row || view.sel[view.row] != want_idx || view.level != SHELF_ONE || !wr->n || view.pto != 0) want_out = -1;
			else {
				bool ready = covers_full(wr->box[view.sel[view.row]].cover) && (!theme_is_room() || shelf_scene_case_ready(&view));
				if (ready || view.t - want_out > UI_SHELF_COVER_WAIT_S) {
					log_printf("shelf: take-out after %.0f ms, case %s\n", (view.t - want_out) * 1000, ready ? "ready" : "late");
					want_out = -1; take_out();
				}
			}
		}
		if (k & KEY_B) {
			if (want_out >= 0) want_out = -1;
			else if (view.pto == 1) { put_back(1); view.arm = false; }   // half way out: back the same way
			else if (putting_back()) hurry();
			else if (view.pull < .02f) to_all();
			sfx_play(SFX_BACK);
		}
		if (k & KEY_Y) favorite();
		if (k & KEY_X) {
			const shelf_box *out = !theme_is_room() ? chosen_box() : (view.pto == 1 || view.pull > .002f) ? shelf_pulled_box(&view) : NULL;
			if (out) {
                bb_catalog_item item;
                if(bb_library_resolve(out,&item)>=0)
                    bb_settings_game(out->system,item.path,item.key,item.title,draw_top,NULL);
                settle_input(); return true;
            }
			else if (!theme_is_room()) sfx_cancel();   // a list without games: no game for Options (Sort is on the systems' bar)
			else {
				view.sort = (view.sort + 1) % SHELF_SORTS; build();
				view.notice = sort_names[view.sort]; view.notice_t = view.t; sfx_play(SFX_SELECT);
			}
		}
 return false;
}

int shelf_run(char *path, size_t cap)
{
	// The Library is the room's bookcase. No room (its file failed to load after a game, e.g. out of memory):
	// leave to the main menu (4), which shows the room's error card and closes BreadBox (home_run).
	if (!shelf_scene_in_room()) return 4;
	shelf_preload_end(false); // the Library takes over the worker pause
	u64 entered = svcGetSystemTick();
	bool first_frame = true;
	// built once and kept (shelf_preload_step); after a game the boxes as they were (the
	// played one in front), made again from the library once it has settled
	ensure_built();
	if (!boxes) return 0;
	view.pull = view.pfrom = view.pto = 0; view.play_t0 = -10;
	favorite_pending = false;
	// Texture preparation below completes the warm-up; reveal on the first ready frame.
	if(played_system)ui_swap_enter();
	if (played_system) show_played();
	bool raised = room_pace_priority(true);   // already raised by the room: it puts it back
	shelf_scene_begin(&view);
	shelf_scene_prepare();   // its textures before the first frame (~100 ms in that frame after a game)
	ui_wants_3d = theme_is_room() && video_room_3d();   // the room's bookcase in 3D also on the Old (30 fps)
	music_play(true);
	covers_resume();
	// the hands file (1.5 MB) in parts, a part a frame, not on the first frame after a game
	// (173 ms) nor on the first take-out (that one finishes it at once)
	bool hands_ready = false;
	u64 last = svcGetSystemTick();
	float acc = 0;
	int result = 0;
	bool launching = false, launch_out = false;
	bool remote_settings = false;
	bool refresh_requested=false, publication_changed=false;
	shelf_hold rescan_hold = {0};
	shelf_repeat repeat = {0};
	room_pace_restart();
	while (aptMainLoop()) {
		u64 logic0 = svcGetSystemTick();
        int publication=shelf_publications(launching,&publication_changed,refresh_requested,&hands_ready,&remote_settings,&last,logic0);
        if(publication==-2)continue;
        if(publication>=0){result=publication;break;}
		u64 now = svcGetSystemTick();
		float elapsed = (float)(now - last) / SYSCLOCK_ARM11;
		last = now;
		acc += elapsed > .1f ? .1f : elapsed;
		while (acc >= DT) { step(DT); acc -= DT; }
		// the L / R (and Front) pressed during the lift or a turn, once the box is still: one turn, the presses added up
		if ((view.turn_queued || view.turn_home) && view.pto == 1 && progress() >= 1 && !turning()) {
			int steps = view.turn_home ? shelf_turn_home(view.side) : view.turn_queued;
			view.turn_queued = 0; view.turn_home = false;
			if (steps) turn(steps);
		}
		if (launching) {
			if (view.t - view.play_t0 > (theme_is_room() ? shelf_scene_launch_seconds(&view) : 0)) {   // the hands open the case; a list has nothing to wait for
                input_poll();
                u32 edge=launch_out ? 0 : UI_SWAP_REMOTE;
                launch_out=true;
                if(!(ui_swap_keys(edge,UI_SWAP_REMOTE)&UI_SWAP_REMOTE))goto present;
                launch_out=false;
				result = launch(path, cap) ? 1 : 0;
				if (result) break;
				launching = false;
				view.play_t0 = -10;
				ui_confirm_action(SHELF_LAUNCH_TITLE, launch_why, "A  Back", false);
				if (!aptMainLoop() || !gspHasGpuRight()) break;   // closed inside the card: no more drawing
				settle_input(); last = svcGetSystemTick();
			}
			goto present;
		}
		input_poll();
		u32 k = input_keys_down(), held = input_keys_held();
			if (k) played_system = NULL;   // the player took over: no more jumping to the game just played
		const ui_button_t *b = bar();
		k = ui_bar_input(b, 4, k);
		if (remote_settings) { k |= UI_SWAP_REMOTE; remote_settings = false; }
		k = ui_swap_keys(k, UI_SWAP_MENU);
		if (k & (UI_SWAP_REMOTE | KEY_SELECT)) {
            settings_ui_run(false, draw_top);
            if (!aptMainLoop()) break;
            if (settings_ui_take_folders()) { result = 2; break; }
            settle_input(); last = svcGetSystemTick(); goto present;
        }
		if (k & KEY_START) {
			// Finish the requested metadata change before leaving; no returning box is drawn again.
			if (favorite_pending) { view.pull = 0; favorite_pending = false; favorite(); }
			result = 0; break;
		}
		if (favorite_pending) {
			if (view.pull < .002f) { favorite_pending = false; favorite(); }
			else if (k & KEY_B) hurry();
			goto present;
		}   // closes the shelf: home.c goes back to the room, never out of the app
        k=shelf_touch(k);
        if(view.level==SHELF_ALL) {
            int action=shelf_system_keys(k,held,&repeat,&rescan_hold,&refresh_requested);
            if(action>=0){result=action;break;}
            goto present;
        }
        if(shelf_game_keys(k,held,&repeat,&rescan_hold,&launching))last=svcGetSystemTick();
	present:
		// every path here may follow a dialog that HOME > Close ended: no covers, scene or present after it
		if (!aptMainLoop() || !gspHasGpuRight()) break;
		if (rows[view.row].n && view.level == SHELF_ONE) {
			const shelf_box *f = &rows[view.row].box[view.sel[view.row]];
			covers_focus(f->cover);
		}
		covers_pump();
		// a preset pack the covers' worker downloaded: checked and swapped in here, used from the next game
		// (the Library is its one main-thread step; one parse, ~0.2 s on the Old 3DS, once per new pack)
		if (!launching) presets_update_poll();
		if(theme_is_room())shelf_scene_frame(&view);
		music_update();
		{   // frame pacing by moment (the log's "pace:" lines)
			const char *moment = launching ? "library start" : view.pull > .002f ? "library hands"
				: fabsf(view.zoom - view.zto) > .002f ? "library walk" : fabsf(view.cv) > 1.5f ? "library scroll"
				: view.level == SHELF_ALL ? "library wall" : "library shelf";
			room_pace_add(ROOM_PACE_LOGIC, svcGetSystemTick() - logic0);
			room_pace_hook();
			room_pace_frame(moment);
		}
		// Old 3DS: the shelf at 1x; its spines are baked for exactly that. In the room's
		// bookcase both models draw it at 1x, as the room menu (video_room_plain)
		ui_wants_plain = true;
		video_room_plain(true);
		ui_room_eyes = theme_is_room();   // the room's bookcase: 3D also on the Old 3DS (video.c)
		video_present_vsync(false, draw_top, draw_bottom, NULL);
		ui_room_eyes = false;
		video_room_plain(false);
		if (first_frame) {   // opening the Library must not wait: the first frame right away
			first_frame = false;
			log_printf("shelf: first frame %lu ms after opening\n", (unsigned long)((svcGetSystemTick() - entered) / (SYSCLOCK_ARM11 / 1000)));
		}
		ui_wants_plain = false;
	}
	// A consumed adapter notice must survive leaving before the rebuild gate opens.
	if (publication_changed) shelf_library_changed();
	if (!raised) room_pace_priority(false);
	aptSetHomeAllowed(true);   // a hold from the last frame never outlives the Library
	ui_wants_3d = false;
	if(result != 1 && aptMainLoop() && gspHasGpuRight())cover_drain(false);
	else cover_drain_pending=false; // transfer the pause to gameplay / APT; do not wake a job
	// Returning to the same room keeps its music playing. Avoid clearing and
	// immediately re-enqueuing the DSP wave buffer at the shelf/home handoff.
	if (result != 1 && result != 4) music_play(false);
	log_printf("shelf: return result=%d row=%d wall=%d linear=%luK\n",
		result, view.row, view.hsel, (unsigned long)(linearSpaceFree() / 1024));
	return result;
}

// Before a game: everything the shelves hold goes to it (heap: the boxes; VRAM and linear:
// the spine atlases). Which shelves there are and each one's chosen box stay, so they are
// built again just as they were, in the background at home or when the shelf opens.
void shelf_release(void)
{
 // Index shutdown has drained deferred jobs before the menu releases memory.
 free(restore.data);restore.data=NULL;
 __atomic_store_n(&restore.done,0,__ATOMIC_RELEASE);restore.failures=0;restore.retry_at=0;
	shelf_scene_release();
	keep_boxes();
	return_catalog=true;
	for (int r = 0; r < view.nrows; r++) {
		sel_capture(r);
		rows[r].box = NULL;
		rows[r].n = 0;
	}
	free(boxes);
	boxes = NULL;
	nboxes = 0;
	built = false;
	view.pull = view.pfrom = view.pto = 0;
	preload_step = 0;preload_pause=false;preload_waiting=0;
}

#include "components/shelf.inc"
