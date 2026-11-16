#include "ui_motion.h"
// Save state browser. Every state keeps a screenshot of the moment it was
// saved (<game>.stN.thm next to <game>.stN). The bottom screen shows the ten
// slots as a grid of thumbnails; the top screen shows the selected one large
// with the time it was saved. Loading has two more pages (L/R): the save
// backups' ring of automatic saves (same grid) and the memory card copies
// (a list), see history.h.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>
#include "app_policy.h"
#include "bb_save.h"
#include <sys/types.h>
#include "theme.h"
#include "app_paths.h"
#include "app_log.h"
#include "platform_video.h"
#include "memory_cards.h"
#include "emu.h"
#include "savestates.h"
#include "bb_pause.h"
#include "app_input.h"
#include "ui_list.h"
#include "ui_pause.h"
#include "ui.h"
#include "uisound.h"
#include "libui.h"
#include "scene.h"
#include "devtools.h"
#include "safefile.h"
#include "history.h"
#include "statefile.h"
#include "postcards.h"
#include "bb_slots.h"
#include <unistd.h>

#define SLOTS SLOT_COUNT   // BreadBox: twelve slots, shown 1..12 (app_policy.h, bb_slots.h); files .st0 .. .st11
_Static_assert(SLOTS == PC_SLOTS, "the board and the browser share the slot count");
#define RING 10         // the ring page: the newest automatic saves (the history keeps ten)
#define TW 256          // thumbnail size, 4:3
#define TH 192
#define TEX 256         // texture is TEX x TEX
#define THM_MAGIC 0x48543350   // "P3TH"

typedef struct {
	u32 magic;
	u16 w, h;
	u32 saved;          // seconds since 1970, 3DS clock
} thm_hdr_t;

typedef struct {
	bool used;          // a state file exists
	bool thumb;         // ... and has a screenshot
	u32 saved;
	C3D_Tex tex;
	bool tex_ok;
	float lift, lift_v;
} slot_t;

#define CARD_ROWS 6
#define CARD_COPIES (2 * (1 + HIST_CARD_DAYS + HIST_CARD_PINS))

typedef struct {
	hist_entry_t e;
	int card;            // cards_backup_group() index
	const char *label;
} card_copy_t;

typedef struct {
	bool saving;
	const char *title;
	int page;            // STATES_PAGE_*
	slot_t slot[SLOTS];
	slot_t ring[SLOTS];  // the ring page: the newest automatic saves
	hist_entry_t ring_e[SLOTS];
	int ring_n, ring_sel;
	card_copy_t *cards;  // CARD_COPIES, on the heap while the browser is open
	int cards_n, card_sel, card_top, card_saves;   // saves on the selected copy
	C3D_Tex now;        // the paused frame: preview for saving to an empty slot
	bool now_ok;
	int sel;
	bool confirm;       // waiting for a second A (overwrite) or X (delete)
	bool confirm_delete;
	float t;
	u64 motion_tick;
} browser_t;

#define accent (theme()->accent)

// ---------------------------------------------------------------- files

// slot STATE_AUTO is the auto-save made on exit (".sta")
static void slot_path(char *out, size_t len, const char *prefix, int slot, bool thm)
{
	if (slot == STATE_AUTO)
		snprintf(out, len, "%s.sta%s", prefix, thm ? ".thm" : "");
	else
		snprintf(out, len, "%s.st%d%s", prefix, slot, thm ? ".thm" : "");
}

// screenshot of the paused game (needs the GPU), NULL without a picture
u16 *state_thumb_take(void)
{
	u16 *px = malloc(TW * TH * 2);
	if (px && !video_snapshot(px, TW, TH)) {
		free(px);
		px = NULL;
	}
	return px;
}

#define THM_SIZE (sizeof(thm_hdr_t) + TW * TH * 2)

static bool thm_head_ok(const void *head)
{
	thm_hdr_t h;
	memcpy(&h, head, sizeof(h));
	return h.magic == THM_MAGIC && h.w == TW && h.h == TH;
}
static const safe_format_t thm_fmt = { THM_SIZE, sizeof(thm_hdr_t), thm_head_ok };

// a thumbnail file, crash-safe with the previous one as backup
bool state_thumb_save_file(const char *path, const u16 *px)
{
	u8 *file = malloc(THM_SIZE);
	if (!file)
		return false;
	thm_hdr_t h = { THM_MAGIC, TW, TH, (u32)time(NULL) };
	memcpy(file, &h, sizeof(h));
	memcpy(file + sizeof(h), px, TW * TH * 2);
	bool ok = safe_write(path, &thm_fmt, file, true);
	free(file);
	return ok;
}

// ... saved next to state <slot>
// a picture saved later than it was taken (the Continue point after a game, bb_later): its
// time is the moment of the save, not of the write
bool state_thumb_save_at(const char *prefix, int slot, const u16 *px, u32 saved)
{
	char path[PATH_LEN];
	slot_path(path, sizeof(path), prefix, slot, true);
	u8 *file = malloc(THM_SIZE);
	if (!file)
		return false;
	thm_hdr_t h = { THM_MAGIC, TW, TH, saved };
	memcpy(file, &h, sizeof(h));
	memcpy(file + sizeof(h), px, TW * TH * 2);
	bool ok = safe_write(path, &thm_fmt, file, true);
	free(file);
	return ok;
}

bool state_thumb_save(const char *prefix, int slot, const u16 *px)
{
	char path[PATH_LEN];
	slot_path(path, sizeof(path), prefix, slot, true);
	return state_thumb_save_file(path, px);
}

// thumbnail of <slot> into px (TW x TH); skip_current: only its backup (or
// an unfinished copy)
static bool thumb_read(const char *prefix, int slot, u16 *px, thm_hdr_t *h, bool skip_current)
{
	char path[PATH_LEN];
	slot_path(path, sizeof(path), prefix, slot, true);
	u8 *file = safe_load(path, &thm_fmt, skip_current ? 1u << SAFE_CURRENT : 0, NULL);
	if (!file)
		return false;
	memcpy(h, file, sizeof(*h));
	memcpy(px, file + sizeof(*h), TW * TH * 2);
	free(file);
	return true;
}

// the state was loaded from its backup: bring the matching thumbnail back too
void state_thumb_restore(const char *prefix, int slot)
{
	char path[PATH_LEN];
	safe_info_t info;
	slot_path(path, sizeof(path), prefix, slot, true);
	u8 *file = safe_load(path, &thm_fmt, 1u << SAFE_CURRENT, &info);
	if (file)
		safe_repair(path, &thm_fmt, file, info.from);
	free(file);
}

// saved time of a state: its file, else its backup, else a good unfinished
// copy (a first save cut off before it was put in place)
static bool state_stat(const char *prefix, int slot, u32 *saved)
{
	char path[PATH_LEN];
	safe_format_t fmt = emu_state_format();
	time_t t;
	slot_path(path, sizeof(path), prefix, slot, false);
	if (!safe_stat(path, &fmt, &t))
		return false;
	*saved = (u32)t;
	return true;
}

bool state_thumb_write(const char *prefix, int slot)
{
	u16 *px = state_thumb_take();
	bool ok = px && state_thumb_save(prefix, slot, px);
	free(px);
	return ok;
}

// RGB565 image (TW x TH) into a TEX x TEX texture: 8x8 tiles, Morton order
// inside each tile
static void upload(C3D_Tex *t, const u16 *img)
{
	u16 *dst = t->data;
	memset(dst, 0, t->size);
	for (u32 y = 0; y < TH; y++) {
		u32 ty = y;
		for (u32 x = 0; x < TW; x++) {
			u32 i = (((ty >> 3) * (TEX >> 3) + (x >> 3)) << 6) |
				(x & 1) | ((ty & 1) << 1) | ((x & 2) << 1) | ((ty & 2) << 2) | ((x & 4) << 2) | ((ty & 4) << 3);
			dst[i] = img[y * TW + x];
		}
	}
	C3D_TexFlush(t);
	C3D_TexSetFilter(t, GPU_LINEAR, GPU_LINEAR);
}

static bool make_tex(C3D_Tex *t, const u16 *img)
{
	if (!C3D_TexInit(t, TEX, TEX, GPU_RGB565))
		return false;
	upload(t, img);
	return true;
}

static void scan(browser_t *b, const char *prefix)
{
	u16 *px = malloc(TW * TH * 2);

	for (int i = 0; i < SLOTS; i++) {
		slot_t *s = &b->slot[i];
		s->used = state_stat(prefix, i, &s->saved);
		if (!s->used || !px)
			continue;
		thm_hdr_t h;
		if (thumb_read(prefix, i, px, &h, false) && make_tex(&s->tex, px)) {
			s->tex_ok = s->thumb = true;
			s->saved = h.saved;
		}
	}
	if (px && video_snapshot(px, TW, TH))
		b->now_ok = make_tex(&b->now, px);
	if (!b->saving) {
		// the ring: <backups>/<game>/, the game being the prefix's file name
		const char *game = strrchr(prefix, '/');
		game = game ? game + 1 : prefix;
		b->ring_n = history_list(game, false, b->ring_e, RING);
		for (int i = 0; i < b->ring_n; i++) {
			slot_t *s = &b->ring[i];
			char thm[620];
			s->used = true;
			s->saved = (u32)b->ring_e[i].when;
			snprintf(thm, sizeof(thm), "%s.thm", b->ring_e[i].path);
			u8 *file = px && b->ring_e[i].thumb ? safe_load(thm, &thm_fmt, 0, NULL) : NULL;
			if (file) {
				memcpy(px, file + sizeof(thm_hdr_t), TW * TH * 2);
				s->tex_ok = s->thumb = make_tex(&s->tex, px);
				free(file);
			}
		}
		// the memory cards' copies, newest first per card
		char group[PATH_LEN], ext[16];
		const char *label;
		hist_entry_t *list = malloc(CARD_COPIES * sizeof(*list));
		b->cards = malloc(CARD_COPIES * sizeof(*b->cards));
		for (int c = 0; list && b->cards && cards_backup_group(c, group, sizeof(group), ext, sizeof(ext), &label); c++) {
			int n = history_list(group, true, list, CARD_COPIES);
			for (int i = 0; i < n && b->cards_n < CARD_COPIES; i++)
				if (!strcmp(list[i].ext, ext))
					b->cards[b->cards_n++] = (card_copy_t){ list[i], c, label };
		}
		free(list);
	}
	free(px);
}

static void release(browser_t *b)
{
	for (int i = 0; i < SLOTS; i++) {
		if (b->slot[i].tex_ok)
			C3D_TexDelete(&b->slot[i].tex);
		if (b->ring[i].tex_ok)
			C3D_TexDelete(&b->ring[i].tex);
	}
	if (b->now_ok)
		C3D_TexDelete(&b->now);
	free(b->cards);
	b->cards = NULL;
}

// ---------------------------------------------------------------- backups

static const char *ring_tag(hist_kind_t k)
{
	static const char *const tag[] = { [HIST_TIMER] = "Timer", [HIST_EXIT] = "Exit", [HIST_LOAD] = "Load",
		[HIST_SAVE] = "Save", [HIST_RESET] = "Reset", [HIST_START] = "Start", [HIST_DELETE] = "Delete",
		[HIST_RESTORE] = "Undo" };
	return k <= HIST_RESTORE ? tag[k] : "";
}

// why an automatic save exists, in words
static void ring_why(char *out, size_t len, const hist_entry_t *e)
{
	int slot = e->ext[0] && e->ext[1] == 's' && e->ext[2] == 't' && e->ext[3] >= '0' && e->ext[3] <= '9' ? e->ext[3] - '0' : -1;
	switch (e->kind) {
	case HIST_TIMER: snprintf(out, len, "Auto-saved while you played"); break;
	case HIST_EXIT: snprintf(out, len, "Taken when you left the game"); break;
	case HIST_LOAD: snprintf(out, len, "Before you loaded a save"); break;
	case HIST_SAVE: snprintf(out, len, "Postcard %d before you saved over it", SLOT_NO(slot)); break;
	case HIST_RESET: snprintf(out, len, "Before the game was reset"); break;
	case HIST_START: snprintf(out, len, "Your old spot when you chose Start game"); break;
	case HIST_DELETE: snprintf(out, len, "Postcard %d before you tossed it", SLOT_NO(slot)); break;
	case HIST_RESTORE: snprintf(out, len, "Before you went back to an older save"); break;
	default: snprintf(out, len, "-"); break;
	}
}

static const char *card_kind(hist_kind_t k)
{
	return k == HIST_CARD_SESSION ? "When you started" : k == HIST_CARD_DAY ? "First of the day" : "Kept for you";
}

// "12 min ago", "3 h ago", else the date
static void age_text(char *out, size_t len, u32 saved)
{
	time_t now = time(NULL);
	if (saved && (time_t)saved <= now && now - saved < 60)
		snprintf(out, len, "just now");
	else if (saved && (time_t)saved <= now && now - saved < 3600)
		snprintf(out, len, "%d min ago", (int)((now - saved) / 60));
	else if (saved && (time_t)saved <= now && now - saved < 12 * 3600)
		snprintf(out, len, "%d h ago", (int)((now - saved) / 3600));
	else {
		time_t tt = saved;
		struct tm *tm = gmtime(&tt);   // the 3DS clock is local time already
		if (!saved || !tm)
			snprintf(out, len, "-");
		else
			strftime(out, len, "%b %d  %H:%M", tm);
	}
}

static void card_select(browser_t *b)
{
	b->card_saves = -1;
	if (b->card_sel >= b->cards_n)
		return;
	FILE *f = fopen(b->cards[b->card_sel].e.path, "rb");
	u8 *buf = malloc(16 * 128);
	if (f && buf && fread(buf, 1, 16 * 128, f) == 16 * 128)
		b->card_saves = card_saves(buf, 16 * 128);
	if (f)
		fclose(f);
	free(buf);
}

// X X on a slot: it goes into the ring, so Undo in the pause menu brings it back
states_deleted_t states_deleted;
static bool slot_delete(const char *prefix, int slot)
{
	char path[PATH_LEN], thm[PATH_LEN], ext[8];
	const char *game = strrchr(prefix, '/');
	game = game ? game + 1 : prefix;
	slot_path(path, sizeof(path), prefix, slot, false);
	slot_path(thm, sizeof(thm), prefix, slot, true);
	snprintf(ext, sizeof(ext), ".st%d", slot);
	char bak[PATH_LEN + 8], tbak[PATH_LEN + 8];
	snprintf(bak, sizeof(bak), "%s.bak", path);
	snprintf(tbak, sizeof(tbak), "%s.bak", thm);
	// the slot's file, or its backup if that is all there is
	bool cur = access(path, F_OK) == 0, old = !cur && access(bak, F_OK) == 0;
	hist_entry_t e;
	unsigned reason, saved = 0, id;
	if (cur || old)
		state_file_info(cur ? path : bak, &reason, &saved, &id);
	if ((cur || old) && !history_adopt(game, HIST_DELETE, ext, cur ? path : bak, cur ? thm : tbak, saved, &e)) {
		emu_osd_alert("Could not delete: the save could not be moved to the backups");
		return false;
	}
	if (cur || old)
		states_deleted = (states_deleted_t){ true, slot, e };
	safe_remove(path);   // its backup and temp files
	safe_remove(thm);
	return true;
}

// ---------------------------------------------------------------- drawing

static void draw_tex(C3D_Tex *t, float x, float y, float w, float h, float z); /* rendered by the shared savestates component */

static void when_text(char *out, size_t len, u32 saved)
{
	time_t tt = saved;
	struct tm *tm = gmtime(&tt);   // the 3DS clock is local time already
	if (!saved || !tm)
		snprintf(out, len, "-");
	else
		strftime(out, len, "%b %d %Y  %H:%M", tm);
}

static void draw_preview(C3D_Tex *t, int sel, const char *empty, const char *line); /* rendered by the shared savestates component */

static void draw_top(void *ud); /* rendered by the shared savestates component */


#define STATES_BAR_MAX 5

// B Back; loading: R next page. Slots: X Delete (a used slot), A Save / Load
// (nothing to load: no A). Auto-saves: A Load. Cards: A Restore.
static int states_bar(const browser_t *b, ui_button_t *out)
{
	int n = 0;
	out[n++] = (ui_button_t){KEY_B, b->confirm ? "Cancel" : "Back", b->confirm ? UI_ICON_CROSS : UI_ICON_BACK, false};
	if (!b->saving && !b->confirm) {   // the pages around this one, by name
		static const char *const page[] = { "Slots", "Auto-saves", "Cards" };
		out[n++] = (ui_button_t){KEY_L, page[(b->page + 2) % 3], UI_ICON_LEFT, false};
		out[n++] = (ui_button_t){KEY_R, page[(b->page + 1) % 3], UI_ICON_RIGHT, false};
	}
	if (b->page == STATES_PAGE_RING) {
		if (b->ring_sel < b->ring_n)
			out[n++] = (ui_button_t){KEY_A, "Load", UI_ICON_LOAD, false};
	} else if (b->page == STATES_PAGE_CARDS) {
		if (b->cards_n)
			out[n++] = (ui_button_t){KEY_A, "Restore", UI_ICON_LOAD, false};
	} else {
		const slot_t *s = &b->slot[b->sel];
		if (s->used)
			out[n++] = (ui_button_t){KEY_X, "Delete", UI_ICON_TRASH, false};
		if (b->saving || s->used)
			out[n++] = (ui_button_t){KEY_A, b->saving ? "Save" : "Load", b->saving ? UI_ICON_SAVE : UI_ICON_LOAD, false};
	}
	return n;
}

// The Slots page is the shared postcard board (postcards.h); this browser
// owns the slot textures, the view only points at them.
static pc_view slot_view;
static int slot_tossed = -1;
static void view_sync(browser_t *b)
{
 pc_view *v=&slot_view;
 for(int i=0;i<SLOTS;i++) {
  pc_card *c=&v->card[i];slot_t *s=&b->slot[i];
  if(i==slot_tossed)continue;   // keeps its picture until it has landed
  c->used=s->used;c->saved=s->saved;c->tex=s->tex_ok?&s->tex:NULL;c->tw=TW;c->th=TH;
 }
 v->now.tex=b->now_ok?&b->now:NULL;v->now.tw=TW;v->now.th=TH;
 v->sel=b->sel;v->confirm_replace=b->confirm && !b->confirm_delete;v->confirm_delete=b->confirm && b->confirm_delete;
 v->nbar=states_bar(b,v->bar);
}
static flist_t state_list;
static const char *state_label(int row,void *ud) {
 browser_t *b=ud;static char line[96];
 if(b->page==STATES_PAGE_CARDS) {
  card_copy_t *c=&b->cards[row];snprintf(line,sizeof(line),"Card %s - %s",c->label+12,card_kind(c->e.kind));
 } else if(b->page==STATES_PAGE_RING){if(row<b->ring_n)snprintf(line,sizeof(line),"Auto-save %d · %s",row+1,ring_tag(b->ring_e[row].kind));else snprintf(line,sizeof(line),"Auto-save %d",row+1);}   /* "Empty" once, as the value */
 else snprintf(line,sizeof(line),"Slot %d",SLOT_NO(row));
 return line;
}
static const char *state_value(int row,void *ud) {
 browser_t *b=ud;static char when[40];
 if(b->page==STATES_PAGE_CARDS){age_text(when,sizeof(when),(u32)b->cards[row].e.when);return when;}
 slot_t *slot=b->page==STATES_PAGE_RING?&b->ring[row]:&b->slot[row];
 if(!slot->used)return "Empty";
 if(slot->saved){age_text(when,sizeof(when),slot->saved);return when;}return "Saved";
}
static void draw_bottom(void *ud); /* rendered by the shared savestates component */

// ---------------------------------------------------------------- run

static void lift_update(browser_t *b, slot_t *slots, int sel)
{
 u64 now=svcGetSystemTick();
 float dt=b->motion_tick?(float)(now-b->motion_tick)/SYSCLOCK_ARM11:0;
 b->motion_tick=now;
 b->t+=dt;
 for(int i=0;i<SLOTS;i++)
  ui_motion_settle(&slots[i].lift,&slots[i].lift_v,i==sel?1.0f:0.0f,UI_MOTION_SETTLE_RATE,dt);
}

// grid navigation and touch; true: the selected cell was picked
static bool grid_input(u32 k, int *sel, bool slots)
{
 if(slots) {
  return (k&KEY_A)!=0;
 }
 k=ui_list_input(&state_list,SLOTS,sel,k,false);
 return (k&KEY_A)!=0;
}

// A here goes back into the game (a load, a save, an older moment): that one dims; the rest are menus.
static bool pick_plays(const browser_t *b)
{
	if (b->page == STATES_PAGE_RING) return b->ring_sel < b->ring_n;
	if (b->page != STATES_PAGE_SLOTS) return false;   // the memory cards ask first
	const slot_t *s = &b->slot[b->sel];
	return b->saving ? !s->used || (b->confirm && !b->confirm_delete) : s->used;
}
int states_run(bool saving, const char *prefix, const char *title, int *slot, states_pick_t *pick)
{
	static browser_t b;
 memset(&state_list,0,sizeof(state_list));
	memset(&b, 0, sizeof(b));
	b.saving = saving;
	b.title = title;
	b.page = saving ? STATES_PAGE_SLOTS : pick->page;
	b.sel = (*slot >= 0 && *slot < SLOTS) ? *slot : 0;
	unsigned linear_before = (unsigned)(linearSpaceFree() / 1024);
	scan(&b, prefix);
 pc_open(&slot_view,saving,false,title,"PS1");slot_tossed=-1;
 u64 postcard_tick=svcGetSystemTick();
 {int used=0;for(int i=0;i<SLOTS;i++)used+=b.slot[i].used;
  log_printf("pause: PS1 %s board, %d of %d slots used, linear free %uK -> %uK (the slot pictures took %uK)\n",saving?"save":"load",used,SLOTS,
   linear_before,(unsigned)(linearSpaceFree()/1024),linear_before-(unsigned)(linearSpaceFree()/1024));}
	if (!saving && !b.slot[b.sel].used)   // loading: start on the newest save
		for (int i = 0, best = -1; i < SLOTS; i++)
			if (b.slot[i].used && (best < 0 || b.slot[i].saved > (u32)best)) {
				best = (int)b.slot[i].saved;
				b.sel = i;
			}
	card_select(&b);

	int result = -1;
	while (aptMainLoop()) {
		if (dev_tick(true) || dev_ui_pending()) break;
		input_poll();
		ui_button_t bar[STATES_BAR_MAX];
		u32 k = ui_bar_input(bar, states_bar(&b, bar), input_keys_down());
        if(b.page==STATES_PAGE_SLOTS) {
            int before=b.sel;slot_view.sel=b.sel;
            bool hit=!pc_animating(&slot_view) && pc_input(&slot_view,&k);
            b.sel=slot_view.sel;
            if(hit)k|=KEY_A;
            if(before!=b.sel)b.confirm=false;   /* pc_input made the move sound */
        }
        k=ui_swap_keys(k,pick_plays(&b)?KEY_A:UI_SWAP_MENU);

		if (!saving && (k & (KEY_L | KEY_R))) {
			b.page = (b.page + (k & KEY_R ? 1 : 2)) % 3;
			b.confirm = false;
			sfx_play(SFX_TAB);
			k = 0;
		}
		if (k & KEY_B) {
			if (!b.confirm)
				break;
			b.confirm = false;
			k = 0;
		}
		if (b.page == STATES_PAGE_RING) {
			if (grid_input(k, &b.ring_sel, false) && b.ring_sel < b.ring_n) {
				pick->page = STATES_PAGE_RING;
				pick->entry = b.ring_e[b.ring_sel];
				result = STATES_PAGE_RING;
				sfx_now(SFX_LOAD);sfx_card_closes();
				break;
			}
		} else if (b.page == STATES_PAGE_CARDS) {
			int before = b.card_sel;
 k=ui_list_input(&state_list,b.cards_n,&b.card_sel,k,false);
			if (b.card_sel < b.card_top) b.card_top = b.card_sel;
			if (b.card_sel >= b.card_top + CARD_ROWS) b.card_top = b.card_sel - CARD_ROWS + 1;
			if (b.card_sel != before)
				card_select(&b);
			if ((k & KEY_A) && b.card_sel < b.cards_n &&
			    ui_confirm_action("Put this memory card back?",
					"The game restarts with this card. The card as it is now and the game as it is now are kept in the backups.",
					"A  Put it back", true)) {
				pick->page = STATES_PAGE_CARDS;
				pick->entry = b.cards[b.card_sel].e;
				result = STATES_PAGE_CARDS;
				break;
			}
		} else {
			bool picked = grid_input(k, &b.sel, true);
			slot_t *s = &b.slot[b.sel];
			if (picked && saving) {
				if (s->used && !(b.confirm && !b.confirm_delete)) {
					b.confirm = true;
					b.confirm_delete = false;
					sfx_play(SFX_MOVE);
				} else {
					result = STATES_PAGE_SLOTS;
					sfx_now(SFX_SAVE);sfx_card_closes();
					break;
				}
			} else if (picked && s->used) {
				result = STATES_PAGE_SLOTS;
				sfx_now(SFX_LOAD);sfx_card_closes();
				break;
			} else if (picked)
				sfx_cancel();   // an empty card when loading: nothing to take
			if ((k & KEY_X) && s->used) {
				if (b.confirm && b.confirm_delete) {
					if (slot_delete(prefix, b.sel)) {
 slot_tossed=b.sel;pc_animate(&slot_view,PC_ANIM_TOSS,b.sel);   // cleared once it has landed
					}
					b.confirm = false;
				} else {
					b.confirm = true;
					b.confirm_delete = true;
					sfx_play(SFX_MOVE);
				}
			}
		}

 int list_sel=b.page==STATES_PAGE_CARDS?b.card_sel:b.page==STATES_PAGE_RING?b.ring_sel:b.sel;
 flist_update(&state_list,list_sel,b.page==STATES_PAGE_CARDS?b.cards_n:SLOTS,UI_LIST_ROWS);
		lift_update(&b, b.page == STATES_PAGE_RING ? b.ring : b.slot, b.page == STATES_PAGE_RING ? b.ring_sel : b.sel);
 u64 tick=svcGetSystemTick();
 view_sync(&b);pc_tick(&slot_view,(float)(tick-postcard_tick)/SYSCLOCK_ARM11);
 postcard_tick=tick;
 if(slot_tossed>=0 && !pc_animating(&slot_view)) {
  slot_t *gone=&b.slot[slot_tossed];
  if(gone->tex_ok){video_gpu_idle();C3D_TexDelete(&gone->tex);}
  memset(gone,0,sizeof(*gone));pc_card_clear(&slot_view.card[slot_tossed]);slot_tossed=-1;view_sync(&b);
 }

        ui_pause_frame();
		ui_begin();
		video_present_vsync(b.page != STATES_PAGE_SLOTS, draw_top, draw_bottom, &b);   // the board covers the game
	}
	*slot = b.sel;
 if(slot_tossed>=0){slot_t *gone=&b.slot[slot_tossed];if(gone->tex_ok && gspHasGpuRight()){video_gpu_idle();C3D_TexDelete(&gone->tex);}memset(gone,0,sizeof(*gone));slot_tossed=-1;}
 pc_close(&slot_view);
	release(&b);
	return result;
}

// ---------------------------------------------------------------- resume

typedef struct { C3D_Tex tex; bool tex_ok, older, over_failed; u32 saved; float t; const char *title; } resume_t;

static void resume_top(void *ud); /* shared presentation */

// The question's bar is the shared one (bb_pause.h bb_continue_bar): B Back, X Start over, A Continue, and
// Y Older only when the backups have one.
static void resume_bottom(void *ud); /* shared presentation */

// The game was auto-saved when it was last closed: offer to continue.
// RESUME_CONTINUE: load the auto-save; RESUME_START: start the game;
// RESUME_OLDER: show the ring of automatic saves; RESUME_BACK: B, or the app closed while it asked (nothing
// chosen: the caller keeps the Continue point); RESUME_NONE: no prompt.
int states_resume_prompt(const char *prefix, const char *title, bool over_failed)
{
	static resume_t r;
	memset(&r, 0, sizeof(r));
	r.over_failed = over_failed;
	r.title = title;
	{
		const char *game = strrchr(prefix, '/');
		hist_entry_t e;
		r.older = history_list(game ? game + 1 : prefix, false, &e, 1) > 0;
	}
	if (!state_stat(prefix, STATE_AUTO, &r.saved))
		return RESUME_NONE;
	u16 *px = malloc(TW * TH * 2);
	thm_hdr_t h;
	if (px && thumb_read(prefix, STATE_AUTO, px, &h, false)) {
		r.tex_ok = make_tex(&r.tex, px);
		r.saved = h.saved;
	}
	free(px);
	int resume = RESUME_BACK;   // closed while asking: nothing chosen
	while (aptMainLoop()) {
		dev_tick_keys();
		input_poll();
		u32 k = ui_bar_input(bb_continue_bar, BB_CONTINUE_BUTTONS(r.older), input_keys_down());
		k = ui_swap_keys(k, KEY_A);
		int answer = bb_continue_input(k, r.older);   // B: back to the shelf, never a fresh start
		if (answer == BB_CONTINUE_YES) { resume = RESUME_CONTINUE; break; }
		if (answer == BB_CONTINUE_OVER) { resume = RESUME_START; break; }
		if (answer == BB_CONTINUE_OLDER) { resume = RESUME_OLDER; break; }
		if (answer == BB_CONTINUE_BACK) break;

        ui_pause_frame();
		ui_begin();
		video_present_vsync(false, resume_top, resume_bottom, &r);
	}
	if (r.tex_ok)
		C3D_TexDelete(&r.tex);
	return resume;
}

// ---------------------------------------------------------------- main menu

// The main menu's Continue tile: is there an auto-save, when was it made and
// its picture (tex is made only when *tex_ok comes back true).
bool states_auto_available(const char *prefix,u32 *saved)
{
 char path[PATH_LEN],bak[PATH_LEN+8];struct stat st;
 slot_path(path,sizeof(path),prefix,STATE_AUTO,false);
 snprintf(bak,sizeof(bak),"%s.bak",path);
 if(saved)*saved=0;
 if(stat(path,&st) && stat(bak,&st))return false;
 return true;
}

// Worker variant: do not drain all backup reads after cancellation. File I/O
// itself cannot be interrupted, so check before each read and between copies.
static bool thumb_read_cancel(const char *prefix,u16 *px,thm_hdr_t *h,bool (*cancel)(void *),void *arg)
{
 char base[PATH_LEN],path[PATH_LEN+8];
 static const char *const suffix[]={"",".bak",".tmp"};
 slot_path(base,sizeof(base),prefix,STATE_AUTO,true);
 for(unsigned i=0;i<3;i++) {
  if(cancel(arg))return false;
  snprintf(path,sizeof(path),"%s%s",base,suffix[i]);FILE *f=fopen(path,"rb");
  if(!f)continue;
  struct stat st;
  bool ok=!cancel(arg) && !fstat(fileno(f),&st) && st.st_size==THM_SIZE;
  if(ok)ok=!cancel(arg) && fread(h,sizeof(*h),1,f)==1 && thm_head_ok(h);
  for(size_t off=0;ok && off<TW*TH*2;off+=4096) {
   size_t n=TW*TH*2-off;if(n>4096)n=4096;
   ok=!cancel(arg) && fread((u8 *)px+off,1,n,f)==n;
  }
  fclose(f);
  if(ok)return true;
 }
 return false;
}
// CPU-only snapshot read. Cancellation is checked between SD operations; the
// caller owns pixels and publishes them before creating a texture on the UI.
bool states_auto_read(const char *prefix, u32 *saved, u16 *px, bool *picture,
                      bool (*cancel)(void *), void *arg)
{
 char path[PATH_LEN], bak[PATH_LEN + 8];struct stat st;
 *saved=0;*picture=false;
 if(cancel && cancel(arg))return false;
 slot_path(path,sizeof(path),prefix,STATE_AUTO,false);
 snprintf(bak,sizeof(bak),"%s.bak",path);
 if(stat(path,&st)) {
  if(cancel && cancel(arg))return false;
  if(stat(bak,&st))return false;
 }
 if(cancel && cancel(arg))return false;
 thm_hdr_t h;
 if(px && (cancel?thumb_read_cancel(prefix,px,&h,cancel,arg):thumb_read(prefix,STATE_AUTO,px,&h,false))) {*picture=true;*saved=h.saved;}
 return !(cancel && cancel(arg));
}
bool states_auto_texture(C3D_Tex *tex,const u16 *px) {return make_tex(tex,px);}

bool states_auto_peek(const char *prefix, u32 *saved, C3D_Tex *tex, bool *tex_ok)
{
 bb_save_settle();
 u16 *px=malloc(TW*TH*2);bool picture=false;
 bool ok=states_auto_read(prefix,saved,px,&picture,NULL,NULL);
 *tex_ok=picture && states_auto_texture(tex,px);free(px);return ok;
}

void states_thumb_draw(C3D_Tex *t, float x, float y, float w, float h, float z)
{
	draw_tex(t, x, y, w, h, z);
}


#include "components/savestates.inc"
