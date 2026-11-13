// Shared savestates presentation; content/state are provided by its controller.
static void draw_tex(C3D_Tex *t, float x, float y, float w, float h, float z)
{
	static Tex3DS_SubTexture sub = { TW, TH, UI_METRIC_0_0F, UI_METRIC_1_0F, TW / (float)TEX, UI_METRIC_1_0F - TH / (float)TEX };
	C2D_Image img = { t, &sub };
	C2D_DrawImageAt(img, x, y, z, NULL, w / TW, h / TH);
}

static void draw_preview(C3D_Tex *t, int sel, const char *empty, const char *line)
{
	C2D_Font font = libui_font();
	u32 white = theme()->help_title, dim = theme()->text_muted;
	const float pw = UI_METRIC_264, ph = UI_METRIC_198, px = (UI_TOP_SCREEN_WIDTH - pw) / UI_METRIC_2, py = UI_METRIC_10;
	float dx = ui_depth(-UI_METRIC_0_6F);
	C2D_DrawRectSolid(px - UI_METRIC_3 + dx, py - UI_METRIC_3, UI_METRIC_0_62F, pw + UI_METRIC_6, ph + UI_METRIC_6, accent[sel & UI_METRIC_3]);
	C2D_DrawRectSolid(px + dx, py, UI_METRIC_0_63F, pw, ph, theme()->card_bottom);
	if (t)
		draw_tex(t, px + dx, py, pw, ph, UI_METRIC_0_64F);
	else
		ui_text_ex(font, UI_METRIC_200 + dx, py + ph / UI_METRIC_2 - UI_METRIC_10, UI_METRIC_0_6F, dim, C2D_AlignCenter, empty);
	ui_text_ex(NULL, UI_METRIC_200 + dx, UI_METRIC_215, UI_FONT_BODY, white, C2D_AlignCenter, line);
}

static void draw_top(void *ud)
{
	browser_t *b = ud;
	if (b->page == STATES_PAGE_SLOTS) {
		postcards_top(&slot_view);
		return;
	}
	C2D_DrawRectSolid(UI_METRIC_0, UI_METRIC_0, UI_METRIC_0_6F, UI_TOP_SCREEN_WIDTH, UI_SCREEN_HEIGHT, theme()->help_bg);
	char line[UI_METRIC_128], when[UI_METRIC_40];
	if (b->page == STATES_PAGE_RING) {
		slot_t *s = &b->ring[b->ring_sel];
		if (b->ring_sel >= b->ring_n) {
			draw_preview(NULL, b->ring_sel, "Nothing here yet", "Automatic saves appear here");
			return;
		}
		char why[UI_METRIC_64];
		ring_why(why, sizeof(why), &b->ring_e[b->ring_sel]);
		age_text(when, sizeof(when), s->saved);
		snprintf(line, sizeof(line), "%s  -  %s", why, when);
		draw_preview(s->tex_ok ? &s->tex : NULL, b->ring_sel, "No preview", ui_fit(line, UI_FONT_BODY, UI_METRIC_390));
		return;
	}
	if (b->page == STATES_PAGE_CARDS) {
		C2D_Font font = libui_font();
		u32 white = theme()->help_title, dim = theme()->text_muted;
		float dx = ui_depth(-UI_METRIC_0_6F);
		if (b->card_sel >= b->cards_n) {
			ui_text_ex(font, UI_METRIC_200 + dx, UI_METRIC_100, UI_METRIC_0_6F, dim, C2D_AlignCenter, "No memory card copies yet");
			return;
		}
		card_copy_t *c = &b->cards[b->card_sel];
		age_text(when, sizeof(when), (u32)c->e.when);
		ui_text_ex(font, UI_METRIC_200 + dx, UI_METRIC_50, UI_METRIC_0_7F, white, C2D_AlignCenter, c->label);
		snprintf(line, sizeof(line), "%s  -  %s", card_kind(c->e.kind), when);
		ui_text_ex(NULL, UI_METRIC_200 + dx, UI_METRIC_90, UI_FONT_SUBTITLE, white, C2D_AlignCenter, line);
		int now = cards_saves_now(c->card);
		if (b->card_saves >= UI_METRIC_0)
			snprintf(line, sizeof(line), "%d save%s on this copy, %d on the card now", b->card_saves,
				b->card_saves == UI_METRIC_1 ? "" : "s", now);
		else
			snprintf(line, sizeof(line), "This copy could not be read");
		ui_text_ex(NULL, UI_METRIC_200 + dx, UI_METRIC_120, UI_FONT_BODY, dim, C2D_AlignCenter, line);
		ui_text_ex(NULL, UI_METRIC_200 + dx, UI_METRIC_170, UI_FONT_VALUE, dim, C2D_AlignCenter,
			"Putting a card back restarts the game. The card as it is now is kept.");
		return;
	}
	slot_t *s = &b->slot[b->sel];
	when_text(when, sizeof(when), s->saved);
	if (b->saving && !s->used)
		snprintf(line, sizeof(line), "Slot %d  -  new save", SLOT_NO(b->sel));
	else if (s->used && !s->saved)   // copied in without a date
		snprintf(line, sizeof(line), "Slot %d", SLOT_NO(b->sel));
	else if (s->used)
		snprintf(line, sizeof(line), "Slot %d  -  %s", SLOT_NO(b->sel), when);
	else
		snprintf(line, sizeof(line), "Slot %d  -  empty", SLOT_NO(b->sel));
	// for saving to an empty slot: the frame that would be saved
	C3D_Tex *t = s->tex_ok ? &s->tex : (b->saving && b->now_ok ? &b->now : NULL);
	draw_preview(t, b->sel, s->used ? "No preview" : "Empty", line);
}

static void draw_bottom(void *ud)
{
	browser_t *b = ud;
 if(b->page==STATES_PAGE_SLOTS){postcards_bottom(&slot_view);return;}
 scene_draw(true);
 int n=b->page==STATES_PAGE_CARDS?b->cards_n:SLOTS;
 int sel=b->page==STATES_PAGE_CARDS?b->card_sel:b->page==STATES_PAGE_RING?b->ring_sel:b->sel;
 ui_list_draw(&state_list,n,sel,state_label,state_value,b,accent[sel&UI_METRIC_3],NULL);
 ui_list_context(b->saving?"Save postcard":"Load postcard",b->confirm?(b->confirm_delete?"X: Delete?":"A: Replace?"):
  b->page==STATES_PAGE_CARDS?"Memory cards":b->page==STATES_PAGE_RING?"Auto-saves":"Postcards");
 if(!n)ui_text_ex(NULL,UI_METRIC_160,UI_METRIC_90,UI_FONT_BODY,theme()->text_muted,C2D_AlignCenter,"No memory card copies yet");
	ui_button_t bar[STATES_BAR_MAX];
	ui_bar_draw(bar, states_bar(b, bar));
}

static void resume_top(void *ud)
{
	resume_t *r = ud;
	bb_continue_present_top(r->title, r->tex_ok ? &r->tex : NULL, TW, TH, r->saved);   // the shared Continue question
}

static void resume_bottom(void *ud)
{
	resume_t *r = ud;
	bb_continue_present_card(r->title, r->tex_ok ? &r->tex : NULL, TW, TH, r->saved, "PS1", r->older, r->over_failed);
}
