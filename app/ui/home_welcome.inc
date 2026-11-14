// The first-launch welcome, variant A "Tour" (welcome.h, devtools ONBOARDING-CARDS.md), inside home.c: notebook
// cards on the bottom screen (the room's shared card) while this room's camera visits what each one talks about:
// the room, the bookcase, the room again for what came from PSX3D (only when something did). A next, B back, START skips, X on
// the games card opens the games-folder picker (the tour comes back on that card after it). Shown once at the
// first start, after the PSX3D import and the room's fade-up; again from Settings > Help (bb_welcome_open).
enum { WELCOME_DONE, WELCOME_FOLDERS, WELCOME_CLOSED };
#define WELCOME_IMPORT_WAIT_S 12   // the first start waits this long for the PSX3D import, then tours without its card
static int welcome_page, welcome_resume = -1;   // resume: the card to open on at the next room (after the picker)
static unsigned welcome_kinds;                  // PSX_IMPORT_* kinds that came over: the third card
static u64 welcome_since;
static int welcome_focus;                        // the place picked out when the tour began

static PSX3D_COLD int welcome_pages(void) { return bb_welcome_pages(welcome_kinds); }

static PSX3D_COLD void welcome_glide(int page)
{
	// -1: the room's own view. The PSX3D card stays on it: the corkboard close up is a glowing blur until it holds
	// postcards, and a player arriving from PSX3D has none yet.
	static const int place[3] = { -1, ROOM_LIBRARY, -1 };
	if (place[page] >= 0) room_view_go(&room, place[page]);
	else { room_view_back(&room); room.focus = welcome_focus; }   // the same home view as card 1 (not leaning to the bookcase)
}

static const ui_button_t welcome_bar_first[] = { { KEY_START, "Skip", UI_ICON_FF, false }, { KEY_A, "Next", UI_ICON_OPEN, false } };
static const ui_button_t welcome_bar_games[] = { { KEY_B, "Back", UI_ICON_BACK, false }, { KEY_X, "Games folder", UI_ICON_FOLDER, false },
	{ KEY_A, "Next", UI_ICON_OPEN, false } };
static const ui_button_t welcome_bar_games_done[] = { { KEY_B, "Back", UI_ICON_BACK, false }, { KEY_X, "Games folder", UI_ICON_FOLDER, false },
	{ KEY_A, "Done", UI_ICON_CHECK, false } };
static const ui_button_t welcome_bar_last[] = { { KEY_B, "Back", UI_ICON_BACK, false }, { KEY_A, "Done", UI_ICON_CHECK, false } };
static PSX3D_COLD const ui_button_t *welcome_bar(int *n)
{
	if (welcome_page == 0) { *n = 2; return welcome_bar_first; }
	if (welcome_page == 1) { *n = 3; return welcome_pages() > 2 ? welcome_bar_games : welcome_bar_games_done; }
	*n = 2;
	return welcome_bar_last;
}

static PSX3D_COLD float welcome_measure(const char *s, void *ud) { (void)ud; return shelf_scene_line_width(s); }

static PSX3D_COLD void welcome_bottom(void *ud)
{
	(void)ud;
	static char title[48], line[2][96];
	shelf_card c = { 0 };
	c.page = (u8)(welcome_page + 1);
	c.pages = (u8)welcome_pages();
	if (welcome_page == 0) {
		c.title = BB_WELCOME_TITLE;
		c.row[c.nrows++] = (shelf_card_row){ BB_WELCOME_SYSTEMS_1 };
		c.row[c.nrows++] = (shelf_card_row){ BB_WELCOME_SYSTEMS_2 };
	} else if (welcome_page == 1) {   // while the library is still read: the "Add games" form, never a wrong count
		int lines = bb_welcome_games(bb_index_complete() ? (int)bb_system_total() : -1, g_cfg.games_dir, title, sizeof(title),
			line[0], line[1], sizeof(line[0]), welcome_measure, NULL, UI_ROOM_TEXT_R - UI_ROOM_TEXT_X);
		c.title = title;
		for (int i = 0; i < lines; i++) c.row[c.nrows++] = (shelf_card_row){ line[i], NULL, false, false, i ? 0 : UI_ICON_FOLDER };
		c.row[c.nrows++] = (shelf_card_row){ BB_WELCOME_RESCAN, NULL, false, false, UI_ICON_RESCAN };
	} else {
		int lines = bb_welcome_brought(line[0], line[1], sizeof(line[0]), welcome_kinds, welcome_measure, NULL, UI_ROOM_TEXT_R - UI_ROOM_TEXT_X);
		c.title = BB_WELCOME_PSX3D;
		for (int i = 0; i < lines; i++) c.row[c.nrows++] = (shelf_card_row){ line[i] };
		c.nrows++;   // a blank ruled line: the list and the promise stay two sentences
		c.row[c.nrows++] = (shelf_card_row){ BB_WELCOME_UNTOUCHED };
	}
	shelf_scene_card(&c);
	int n;
	const ui_button_t *bar = welcome_bar(&n);
	ui_bar_draw(bar, n);
}

// The first start's moment: the room is up and still, the PSX3D import has finished (or waited long enough).
static PSX3D_COLD bool welcome_ready(void)
{
	if (room_intro < 1 || room.mode != ROOM_HOME || quit_tick || room_quit_on) return false;
	if (welcome_resume >= 0) return true;
	if (!bb_welcome_due()) return false;
	if (!welcome_since) welcome_since = svcGetSystemTick();
	unsigned kinds;
	return breadbox_import_settled(&kinds) || svcGetSystemTick() - welcome_since >= SYSCLOCK_ARM11 * (u64)WELCOME_IMPORT_WAIT_S;
}

// The tour from card `page` over this room; the camera goes back where it was (home, or Settings' scope) after.
static PSX3D_COLD int welcome_run(int page)
{
	int back_to = room.mode == ROOM_AT || room.mode == ROOM_GO ? room.place : -1, focus = room.focus;
	welcome_focus = focus;
	bool plain = ui_wants_plain;
	unsigned kinds = 0;
	breadbox_import_settled(&kinds);
	welcome_kinds = kinds | bb_welcome_boot_kinds();
	welcome_page = page < welcome_pages() ? page : welcome_pages() - 1;
	welcome_resume = -1;
	welcome_glide(welcome_page);
	log_printf("welcome: card %d of %d (from PSX3D 0x%x)\n", welcome_page + 1, welcome_pages(), welcome_kinds);
	int result = -1;
	while (result < 0) {
		if (!aptMainLoop() || dev_tick(false)) { result = WELCOME_CLOSED; break; }
		input_poll();
		if (breadbox_import_settled(&kinds)) welcome_kinds = kinds | bb_welcome_boot_kinds();   // the PSX3D card may come late
		if (welcome_kinds) breadbox_import_notice_quiet();   // its card says what came: no second, shorter account on top
		int n;
		const ui_button_t *bar = welcome_bar(&n);
		u32 k = ui_swap_keys(ui_bar_input(bar, n, input_keys_down()), UI_SWAP_MENU);
		unsigned keys = (k & KEY_A ? BB_WELCOME_KEY_A : 0) | (k & KEY_B ? BB_WELCOME_KEY_B : 0) |
			(k & KEY_X ? BB_WELCOME_KEY_X : 0) | (k & KEY_START ? BB_WELCOME_KEY_START : 0);
		switch (bb_welcome_step(welcome_page, welcome_pages(), keys)) {
		case BB_WELCOME_NEXT: welcome_glide(++welcome_page); sfx_play(SFX_TAB); break;
		case BB_WELCOME_BACK: welcome_glide(--welcome_page); sfx_play(SFX_TAB); break;
		case BB_WELCOME_DONE:
		case BB_WELCOME_SKIP: sfx_play(SFX_SELECT); result = WELCOME_DONE; break;
		case BB_WELCOME_FOLDERS: sfx_play(SFX_SELECT); welcome_resume = 1; result = WELCOME_FOLDERS; break;
		}
		if (g_cfg.games_dir[0] && bb_index_poll(true)) shelf_library_changed();   // the games card follows the scan
		music_update();
		const float dt = UI_METRIC_1_0F / UI_METRIC_60_0F * (float)video_ui_steps();
		clock_s += dt;
		room_tv_poll((int)(clock_s * 60));
		room_view_step(&room, dt);
		covers_pump();
		if (result < 0 && gspHasGpuRight())   // HOME > Close / sleep: no GPU work without the GPU
			present_with(welcome_bottom);
	}
	room_plain(plain);   // Settings keeps the room plain behind it
	if (result == WELCOME_DONE) bb_welcome_seen();
	if (result != WELCOME_CLOSED) {
		if (back_to >= 0) room_view_go(&room, back_to);
		else room_view_back(&room);
		room.focus = focus;   // the place picked out before the tour (the bookcase visit moved it)
	}
	log_printf("welcome: %s on card %d\n", result == WELCOME_DONE ? "done" : result == WELCOME_FOLDERS ? "games folder" : "closed", welcome_page + 1);
	return result;
}

PSX3D_COLD bool bb_welcome_open(void)
{
	if (!welcome_live) return false;   // only over the room
	return welcome_run(0) == WELCOME_FOLDERS;
}
