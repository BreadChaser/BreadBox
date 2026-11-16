// Shared savestates presentation; content/state are provided by its controller.
static void draw_tex(C3D_Tex *t, float x, float y, float w, float h, float z)
{
	static Tex3DS_SubTexture sub = { TW, TH, 0.0f, 1.0f, TW / (float)TEX, 1.0f - TH / (float)TEX };
	C2D_Image img = { t, &sub };
	C2D_DrawImageAt(img, x, y, z, NULL, w / TW, h / TH);
}

static void draw_preview(C3D_Tex *t, int sel, const char *empty, const char *line)
{
	C2D_Font font = libui_font();
	u32 white = theme()->help_title, dim = theme()->text_muted;
	const float pw = 264, ph = 198, px = (UI_TOP_SCREEN_WIDTH - pw) / 2, py = 10;
	float dx = ui_depth(-0.6f);
	C2D_DrawRectSolid(px - 3 + dx, py - 3, 0.62f, pw + 6, ph + 6, accent[sel & 3]);
	C2D_DrawRectSolid(px + dx, py, 0.63f, pw, ph, theme()->card_bottom);
	if (t)
		draw_tex(t, px + dx, py, pw, ph, 0.64f);
	else
		ui_text_ex(font, 200 + dx, py + ph / 2 - 10, 0.6f, dim, C2D_AlignCenter, empty);
	ui_text_ex(NULL, 200 + dx, 215, UI_FONT_BODY, white, C2D_AlignCenter, line);
}

static void draw_top(void *ud)
{
	browser_t *b = ud;
	if (b->page == STATES_PAGE_SLOTS) {
		postcards_top(&slot_view);
		return;
	}
	C2D_DrawRectSolid(0, 0, 0.6f, UI_TOP_SCREEN_WIDTH, UI_SCREEN_HEIGHT, theme()->help_bg);
	char line[128], when[40];
	if (b->page == STATES_PAGE_RING) {
		slot_t *s = &b->ring[b->ring_sel];
		if (b->ring_sel >= b->ring_n) {
			draw_preview(NULL, b->ring_sel, "Nothing here yet", "Automatic saves appear here");
			return;
		}
		char why[64];
		ring_why(why, sizeof(why), &b->ring_e[b->ring_sel]);
		age_text(when, sizeof(when), s->saved);
		snprintf(line, sizeof(line), "%s  -  %s", why, when);
		draw_preview(s->tex_ok ? &s->tex : NULL, b->ring_sel, "No preview", ui_fit(line, UI_FONT_BODY, 390));
		return;
	}
	if (b->page == STATES_PAGE_CARDS) {
		C2D_Font font = libui_font();
		u32 white = theme()->help_title, dim = theme()->text_muted;
		float dx = ui_depth(-0.6f);
		if (b->card_sel >= b->cards_n) {
			ui_text_ex(font, 200 + dx, 100, 0.6f, dim, C2D_AlignCenter, "No memory card copies yet");
			return;
		}
		card_copy_t *c = &b->cards[b->card_sel];
		age_text(when, sizeof(when), (u32)c->e.when);
		ui_text_ex(font, 200 + dx, 50, UI_TEXT_FLOOR, white, C2D_AlignCenter, c->label);
		snprintf(line, sizeof(line), "%s  -  %s", card_kind(c->e.kind), when);
		ui_text_ex(NULL, 200 + dx, 90, UI_FONT_SUBTITLE, white, C2D_AlignCenter, line);
		int now = cards_saves_now(c->card);
		if (b->card_saves >= 0)
			snprintf(line, sizeof(line), "%d save%s on this copy, %d on the card now", b->card_saves,
				b->card_saves == 1 ? "" : "s", now);
		else
			snprintf(line, sizeof(line), "This copy could not be read");
		ui_text_ex(NULL, 200 + dx, 120, UI_FONT_BODY, dim, C2D_AlignCenter, line);
		ui_text_ex(NULL, 200 + dx, 170, UI_FONT_VALUE, dim, C2D_AlignCenter,
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
 ui_list_draw(&state_list,n,sel,state_label,state_value,b,accent[sel&3],NULL);
 ui_list_context(b->saving?"Save postcard":"Load postcard",b->confirm?(b->confirm_delete?"X: Delete?":"A: Replace?"):
  b->page==STATES_PAGE_CARDS?"Memory cards":b->page==STATES_PAGE_RING?"Auto-saves":"Postcards");
 if(!n)ui_text_ex(NULL,160,90,UI_FONT_BODY,theme()->text_muted,C2D_AlignCenter,"No memory card copies yet");
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
