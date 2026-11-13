// Postcards on a corkboard: shared save slot presentation (postcards.h).
// Every metric is a UI_PC_* token (ui_style.h); every colour a theme role.

typedef struct {
	u32 paper, edge, frame, ink, soft, stamp, cont, cork, board, wall, wall_low, mark, light, glow, shade;
	u32 pin[UI_METRIC_4];
} pc_palette;

// Room themes: paper, cork and pins of the room; other themes: their own roles.
static void pc_palette_get(pc_palette *p)
{
	const theme_t *th = theme();
	const theme_room_t *r = theme_room(th);
	if (r && r->cork) {
		*p = (pc_palette){ r->paper, r->paper_edge, r->wax, r->ink, r->ink_soft, r->pin[UI_METRIC_2], theme_alpha(r->stamp, UI_METRIC_255),
			r->cork, r->wood, r->wall, r->skirting, r->postmark, r->paper, theme_alpha(r->light, UI_PC_GLOW_ALPHA), th->shadow,
			{ r->pin[UI_METRIC_0], r->pin[UI_METRIC_1], r->pin[UI_METRIC_2], r->pin[UI_METRIC_3] } };
		return;
	}
	*p = (pc_palette){ th->card_top, th->card_bottom, th->light, th->heading, th->text_muted, th->accent[UI_METRIC_2], th->danger,
		th->surface, th->section_line, th->help_bg, th->surface, th->value, th->text_bright, th->select_glow, th->shadow,
		{ th->accent[UI_METRIC_0], th->accent[UI_METRIC_1], th->accent[UI_METRIC_2], th->accent[UI_METRIC_3] } };
}

// Card i's centre, size and resting tilt on the board.
static void pc_layout(const pc_view *v, int i, float *x, float *y, float *w, float *h, float *tilt)
{
	*tilt = (float)((i * UI_PC_TILT_MUL) % UI_PC_TILT_MOD - UI_PC_TILT_MID) * UI_PC_TILT_STEP;
	if (!v->has_continue) {
		*x = UI_PC_FLAT_X + (i % UI_METRIC_6) * UI_PC_FLAT_DX;
		*y = UI_PC_FLAT_Y + (i / UI_METRIC_6) * UI_PC_FLAT_DY;
		*w = UI_PC_FLAT_W;
		*h = UI_PC_FLAT_H;
		return;
	}
	if (i == PC_CONTINUE) {
		*x = UI_PC_CONT_X;
		*y = UI_PC_CONT_Y;
		*w = UI_PC_CONT_W;
		*h = UI_PC_CONT_H;
		*tilt = -UI_PC_CONT_TILT;
		return;
	}
	*x = UI_PC_GRID_X + (i % UI_METRIC_4) * UI_PC_GRID_DX;
	*y = UI_PC_GRID_Y + (i / UI_METRIC_4) * UI_PC_GRID_DY;
	*w = UI_PC_CARD_W;
	*h = UI_PC_CARD_H;
}

// a picture fitted inside a box, centred; aspect from its own size, less `band` rows above and below it
static void pc_image_band(C3D_Tex *t, float tw, float th, float band, float x, float y, float bw, float bh, float z)
{
	if (!t || tw <= UI_METRIC_0 || th <= band * UI_METRIC_2)
		return;
	float ph = th - band * UI_METRIC_2, s = fminf(bw / tw, bh / ph), w = tw * s, h = ph * s;
	Tex3DS_SubTexture sub = { (u16)tw, (u16)ph, UI_METRIC_0_0F, UI_METRIC_1_0F - band / t->height, tw / t->width,
		UI_METRIC_1_0F - (th - band) / t->height };
	C2D_Image img = { t, &sub };
	C2D_DrawImageAt(img, x + (bw - w) / UI_METRIC_2, y + (bh - h) / UI_METRIC_2, z, NULL, s, s);
}
static void pc_image(C3D_Tex *t, float tw, float th, float x, float y, float bw, float bh, float z)
{
	pc_image_band(t, tw, th, UI_METRIC_0, x, y, bw, bh, z);
}

// handwriting: the body font, slanted, in ink. base is the baseline: the slant turns about it, so the writing
// stands where it is put (it used to turn about the cell's top, which moved the baseline left).
static void pc_hand(float x, float base, float size, u32 color, u32 align, const char *text)
{
	C3D_Mtx saved;
	C2D_ViewSave(&saved);
	C2D_ViewTranslate(x, base);
	C2D_ViewShear(-UI_PC_HAND_SLANT, UI_METRIC_0_0F);
	ui_text_ex(NULL, UI_METRIC_0, UI_PAPER_TOP(UI_METRIC_0, size), size, color, align, text);
	C2D_ViewRestore(&saved);
}

static void pc_outline(float x, float y, float w, float h, float line, u32 c, float z)
{
	C2D_DrawRectSolid(x, y, z, w, line, c);
	C2D_DrawRectSolid(x, y + h - line, z, w, line, c);
	C2D_DrawRectSolid(x, y, z, line, h, c);
	C2D_DrawRectSolid(x + w - line, y, z, line, h, c);
}

// a postage stamp: perforated paper edge around a coloured field; the label at `size`, down to `min`, `side` in
// from the field's sides
static void pc_stamp_ex(const pc_palette *p, float x, float y, float w, float h, u32 field, const char *label, float size, float min, float side)
{
	C2D_DrawRectSolid(x, y, UI_PC_Z_STAMP, w, h, p->frame);
	for (float d = UI_PC_PERF_STEP / UI_METRIC_2; d < w; d += UI_PC_PERF_STEP) {
		C2D_DrawCircleSolid(x + d, y, UI_PC_Z_PERF, UI_PC_PERF_R, p->paper);
		C2D_DrawCircleSolid(x + d, y + h, UI_PC_Z_PERF, UI_PC_PERF_R, p->paper);
	}
	for (float d = UI_PC_PERF_STEP / UI_METRIC_2; d < h; d += UI_PC_PERF_STEP) {
		C2D_DrawCircleSolid(x, y + d, UI_PC_Z_PERF, UI_PC_PERF_R, p->paper);
		C2D_DrawCircleSolid(x + w, y + d, UI_PC_Z_PERF, UI_PC_PERF_R, p->paper);
	}
	C2D_DrawRectSolid(x + UI_PC_STAMP_INSET, y + UI_PC_STAMP_INSET, UI_PC_Z_FIELD, w - UI_PC_STAMP_INSET * UI_METRIC_2, h - UI_PC_STAMP_INSET * UI_METRIC_2, field);
	if (label) {   // on the field's foot, the same pad below and beside it; a long label ("CONTINUE") gets smaller
		const char *fit = ui_fit_ex(NULL, label, size, min, w - (UI_PC_STAMP_INSET + side) * UI_METRIC_2, &size);
		ui_text_ex(NULL, x + w / UI_METRIC_2, UI_PAPER_TOP(y + h - UI_PC_STAMP_INSET - UI_PC_STAMP_PAD, size), size, p->frame, C2D_AlignCenter, fit);
	}
}
static void pc_stamp(const pc_palette *p, float x, float y, float w, float h, u32 field, const char *label)
{
	pc_stamp_ex(p, x, y, w, h, field, label, UI_FONT_CONTEXT, UI_PC_STAMP_LABEL_MIN, UI_PC_STAMP_PAD);
}

// the postmark's double ring and the wavy cancel lines to its right
static void pc_postmark_rings(const pc_palette *p, float x, float y, float r)
{
	for (int k = UI_METRIC_0; k < UI_METRIC_2; k++) {
		float rr = r - k * UI_PC_MARK_GAP;
		for (int i = UI_METRIC_0; i < UI_PC_MARK_SEGS; i++) {
			float a0 = i * UI_PC_TAU / UI_PC_MARK_SEGS, a1 = (i + UI_METRIC_1) * UI_PC_TAU / UI_PC_MARK_SEGS;
			C2D_DrawLine(x + cosf(a0) * rr, y + sinf(a0) * rr, p->mark, x + cosf(a1) * rr, y + sinf(a1) * rr, p->mark, UI_PC_MARK_LINE, UI_PC_Z_MARK);
		}
	}
	for (int k = UI_METRIC_0; k < UI_PC_WAVES; k++)
		for (int i = UI_METRIC_0; i < UI_PC_WAVE_SEGS; i++) {
			float x0 = x + r + UI_PC_WAVE_GAP + i * UI_PC_WAVE_DX, y0 = y - UI_PC_WAVE_SPAN + k * UI_PC_WAVE_DY;
			C2D_DrawLine(x0, y0 + sinf(i * UI_PC_WAVE_FREQ) * UI_PC_WAVE_AMP, p->mark,
				x0 + UI_PC_WAVE_DX, y0 + sinf((i + UI_METRIC_1) * UI_PC_WAVE_FREQ) * UI_PC_WAVE_AMP, p->mark, UI_PC_MARK_LINE, UI_PC_Z_MARK);
		}
}
// the postmark: the rings with the date over the time in them
static void pc_postmark(const pc_palette *p, float x, float y, float r, u32 saved)
{
	pc_postmark_rings(p, x, y, r);
	char day[UI_METRIC_16], hm[UI_METRIC_16];
	time_t tt = saved;
	struct tm *tm = gmtime(&tt);   // the 3DS clock is local time already
	if (!tm)
		return;
	strftime(day, sizeof(day), "%b %d", tm);
	strftime(hm, sizeof(hm), "%H:%M", tm);
	for (char *c = day; *c; c++)
		if (*c >= 'a' && *c <= 'z')
			*c -= 'a' - 'A';
	// the date over the time, centred in the ring; each fitted to the inner ring's chord at its outer edge
	float cap = UI_PC_MARK_TEXT * UI_FONT_CAP, half = UI_PC_MARK_TEXT_GAP / UI_METRIC_2_0F;
	float inner = r - UI_PC_MARK_GAP - UI_PC_MARK_LINE, edge = half + cap;
	float room = UI_METRIC_2 * sqrtf(inner * inner - edge * edge) - UI_METRIC_2;
	ui_text_ex(NULL, x, UI_PAPER_TOP(y - half, UI_PC_MARK_TEXT), UI_PC_MARK_TEXT, p->mark, C2D_AlignCenter, ui_fit(day, UI_PC_MARK_TEXT, room));
	ui_text_ex(NULL, x, UI_PAPER_TOP(y + edge, UI_PC_MARK_TEXT), UI_PC_MARK_TEXT, p->mark, C2D_AlignCenter, ui_fit(hm, UI_PC_MARK_TEXT, room));
}

static const pc_card *pc_shown(const pc_view *v)
{
	const pc_card *c = &v->card[v->sel];
	return !c->used && v->saving && v->sel != PC_CONTINUE && v->now.tex ? &v->now : c;
}

// ---------------------------------------------------------------- top screen: the chosen card, large
void postcards_top(void *view)
{
	const pc_view *v = view;
	pc_palette p;
	pc_palette_get(&p);
	C2D_DrawRectangle(UI_METRIC_0, UI_METRIC_0, UI_PC_Z_BACK, UI_TOP_SCREEN_WIDTH, UI_SCREEN_HEIGHT, p.wall, p.wall, p.wall_low, p.wall_low);
	const pc_card *c = &v->card[v->sel], *pic = pc_shown(v);
	bool cont = v->sel == PC_CONTINUE;
	float dx = ui_depth(-UI_PC_TOP_DEPTH);
	C3D_Mtx saved;
	C2D_ViewSave(&saved);
	C2D_ViewTranslate(UI_METRIC_200 + dx, UI_PC_TOP_CY);
	C2D_ViewRotate(-UI_PC_TOP_TILT);
	C2D_ViewTranslate(-UI_PC_TOP_W / UI_METRIC_2, -UI_PC_TOP_H / UI_METRIC_2);
	C2D_DrawRectSolid(UI_PC_SHADOW_X, UI_PC_SHADOW_Y, UI_PC_Z_SHADOW, UI_PC_TOP_W, UI_PC_TOP_H, theme_alpha(p.shade, UI_PC_SHADOW_ALPHA));
	C2D_DrawRectangle(UI_METRIC_0, UI_METRIC_0, UI_PC_Z_PAPER, UI_PC_TOP_W, UI_PC_TOP_H, p.paper, p.paper, p.edge, p.paper);
	// the picture in its white border
	C2D_DrawRectSolid(UI_PC_TOP_PIC_X - UI_PC_TOP_BORDER, UI_PC_TOP_PIC_Y - UI_PC_TOP_BORDER, UI_PC_Z_BORDER,
		UI_PC_TOP_PIC_W + UI_PC_TOP_BORDER * UI_METRIC_2, UI_PC_TOP_PIC_H + UI_PC_TOP_BORDER * UI_METRIC_2, p.frame);
	if (pic->used || pic == &v->now) {
		C2D_DrawRectSolid(UI_PC_TOP_PIC_X, UI_PC_TOP_PIC_Y, UI_PC_Z_INK, UI_PC_TOP_PIC_W, UI_PC_TOP_PIC_H, p.ink);
		bool big = v->big_ok && v->big_of == (pic == &v->now ? PC_CARDS : v->sel);
		if (big)
			pc_image((C3D_Tex *)&v->big, v->big_w, v->big_h, UI_PC_TOP_PIC_X, UI_PC_TOP_PIC_Y, UI_PC_TOP_PIC_W, UI_PC_TOP_PIC_H, UI_PC_Z_PIC);
		else if (pic->tex)
			pc_image(pic->tex, pic->tw, pic->th, UI_PC_TOP_PIC_X, UI_PC_TOP_PIC_Y, UI_PC_TOP_PIC_W, UI_PC_TOP_PIC_H, UI_PC_Z_PIC);
		else
			ui_text_ex(NULL, UI_PC_TOP_PIC_X + UI_PC_TOP_PIC_W / UI_METRIC_2, UI_TEXT_CENTRED_TOP(UI_PC_TOP_PIC_Y + UI_PC_TOP_PIC_H / UI_METRIC_2, UI_FONT_BODY),
				UI_FONT_BODY, p.light, C2D_AlignCenter, "No picture");
	} else {
		C2D_DrawRectSolid(UI_PC_TOP_PIC_X, UI_PC_TOP_PIC_Y, UI_PC_Z_INK, UI_PC_TOP_PIC_W, UI_PC_TOP_PIC_H, p.edge);
		pc_hand(UI_PC_TOP_PIC_X + UI_PC_TOP_PIC_W / UI_METRIC_2, UI_PC_TOP_PIC_Y + (UI_PC_TOP_PIC_H + UI_PC_HAND_BIG * UI_FONT_CAP) / UI_METRIC_2,
			UI_PC_HAND_BIG, p.soft, C2D_AlignCenter, cont ? "Nothing to continue yet" : "A blank card");
	}
	// stamp, postmark, the writing
	pc_stamp(&p, UI_PC_TOP_STAMP_X, UI_PC_TOP_STAMP_Y, UI_PC_TOP_STAMP_W, UI_PC_TOP_STAMP_H, cont ? p.cont : p.stamp, cont ? "CONTINUE" : v->stamp);
	if (c->used && c->saved)
		pc_postmark(&p, UI_PC_TOP_MARK_X, UI_PC_TOP_MARK_Y, UI_PC_TOP_MARK_R, c->saved);
	for (int i = UI_METRIC_0; i < UI_PC_TOP_RULES; i++)
		C2D_DrawRectSolid(UI_PC_TOP_TEXT_X, UI_PC_TOP_RULE_Y + i * UI_PC_TOP_RULE_DY, UI_PC_Z_INK, UI_PC_TOP_TEXT_W, UI_METRIC_1, theme_alpha(p.soft, UI_PC_RULE_ALPHA));
	char line[UI_METRIC_96];
	if (cont)
		snprintf(line, sizeof(line), "Continue");
	else
		snprintf(line, sizeof(line), "Slot %d", v->sel + UI_METRIC_1);
	pc_hand(UI_PC_TOP_TEXT_X, UI_PC_TOP_RULE_Y, UI_PC_HAND_BIG, cont ? p.cont : p.ink, UI_METRIC_0,
		ui_fit(line, UI_PC_HAND_BIG, UI_PC_TOP_TEXT_W - UI_PC_SLANT_ROOM));   // on the first ruled line, within the rules
	// no play time and no game name (the save's own game): owner 2026-11-05, nothing without a purpose
	// printed footer
	char when[UI_METRIC_40];
	time_t tt = c->saved;
	struct tm *tm = c->saved ? gmtime(&tt) : NULL;
	if (tm)
		strftime(when, sizeof(when), "%b %d %Y, %H:%M", tm);
	if (c->used && tm)
		snprintf(line, sizeof(line), cont ? "Where you left off: %s" : "Saved %s", when);
	else
		line[UI_METRIC_0] = UI_METRIC_0;   // an empty card says nothing (it is blank); a used one its date
	if (line[UI_METRIC_0])
		ui_text_ex(NULL, UI_PC_TOP_PIC_X, UI_PAPER_TOP(UI_PC_TOP_FOOT_BASE, UI_FONT_VALUE), UI_FONT_VALUE, p.soft, UI_METRIC_0,
			ui_fit(line, UI_FONT_VALUE, UI_PC_TOP_FOOT_W));
	C2D_ViewRestore(&saved);
}

// ---------------------------------------------------------------- bottom screen: the board
static void pc_card_draw(const pc_view *v, const pc_palette *p, int i)
{
	const pc_card *c = &v->card[i];
	float x, y, w, h, tilt;
	pc_layout(v, i, &x, &y, &w, &h, &tilt);
	bool sel = i == v->sel, cont = i == PC_CONTINUE;
	float lift = c->lift, s = UI_METRIC_1_0F + UI_PC_LIFT_SCALE * lift, a = UI_METRIC_1_0F;
	float rot = tilt * (UI_METRIC_1_0F - lift);
	if (v->anim == PC_ANIM_TOSS && v->anim_card == i) {
		float t = ui_motion_ease(v->anim_t);
		x += t * UI_PC_TOSS_DX;
		y += t * t * UI_PC_TOSS_DY;
		rot += t * UI_PC_TOSS_SPIN;
		a = UI_METRIC_1_0F - t;
	}
	int alpha = (int)(a * UI_METRIC_255);
	C3D_Mtx saved;
	C2D_ViewSave(&saved);
	C2D_ViewTranslate(x, y - UI_PC_LIFT_Y * lift);
	C2D_ViewRotate(rot);
	C2D_ViewScale(s, s);
	C2D_ViewTranslate(-w / UI_METRIC_2, -h / UI_METRIC_2);
	if (sel)
		C2D_DrawRectSolid(-UI_PC_GLOW, -UI_PC_GLOW, UI_PC_Z_GLOW, w + UI_PC_GLOW * UI_METRIC_2, h + UI_PC_GLOW * UI_METRIC_2, p->glow);
	C2D_DrawRectSolid(UI_PC_CARD_SHADOW_X, UI_PC_CARD_SHADOW_Y + UI_PC_LIFT_Y * lift, UI_PC_Z_SHADOW, w, h,
		theme_alpha(p->shade, alpha * UI_PC_SHADOW_ALPHA / UI_METRIC_255));
	if (c->used || (cont && v->has_continue)) {
		u32 paper = theme_alpha(c->used ? p->paper : p->edge, alpha);
		C2D_DrawRectangle(UI_METRIC_0, UI_METRIC_0, UI_PC_Z_PAPER, w, h, paper, paper, theme_alpha(p->edge, alpha), paper);
	} else {
		// an empty place: a faint outline where a card would go
		pc_outline(UI_METRIC_0, UI_METRIC_0, w, h, UI_METRIC_1, theme_alpha(p->edge, alpha * UI_PC_EMPTY_ALPHA / UI_METRIC_255), UI_PC_Z_INK);
	}
	float pw = w - UI_PC_INSET * UI_METRIC_2, ph = h - UI_PC_INSET - UI_PC_CAPTION;
	if (c->used && c->tex)
		pc_image(c->tex, c->tw, c->th, UI_PC_INSET, UI_PC_INSET, pw, ph, UI_PC_Z_PIC);
	else if (c->used)
		C2D_DrawRectSolid(UI_PC_INSET, UI_PC_INSET, UI_PC_Z_INK, pw, ph, theme_alpha(p->edge, alpha));
	else if (!cont)
		ui_text_ex(NULL, w / UI_METRIC_2, UI_TEXT_CENTRED_TOP(UI_PC_INSET + ph / UI_METRIC_2, UI_FONT_VALUE), UI_FONT_VALUE,
			theme_alpha(p->edge, alpha), C2D_AlignCenter, v->saving ? "+" : "-");
	if (c->used && !c->tex && !cont)
		ui_text_ex(NULL, w / UI_METRIC_2, UI_TEXT_CENTRED_TOP(UI_PC_INSET + ph / UI_METRIC_2, UI_FONT_VALUE), UI_FONT_VALUE, p->soft,
			C2D_AlignCenter, "?");
	char n[UI_METRIC_12];
	if (cont)
		snprintf(n, sizeof(n), "Continue");
	else
		snprintf(n, sizeof(n), "%d", i + UI_METRIC_1);
	float hand = cont ? UI_PC_HAND_SMALL : UI_PC_HAND_TINY;   // the caption strip: on its baseline, within the card
	pc_hand(UI_PC_CAPTION_X, h - UI_PC_CAPTION_PAD, hand, theme_alpha(cont ? p->cont : c->used ? p->ink : p->light, alpha), UI_METRIC_0,
		ui_fit(n, hand, w - UI_PC_CAPTION_X - UI_PC_CAPTION_PAD - UI_PC_SLANT_ROOM));
	if (cont) {
		C2D_DrawRectSolid(UI_METRIC_0, UI_METRIC_0, UI_PC_Z_INK, UI_PC_CONT_EDGE, h, theme_alpha(p->cont, alpha));
		pc_stamp(p, w - UI_PC_CONT_STAMP_W - UI_PC_INSET, -UI_PC_CONT_STAMP_UP, UI_PC_CONT_STAMP_W, UI_PC_CONT_STAMP_H, p->cont, NULL);
	}
	// the stamp a save leaves: drops in from above
	if (v->anim == PC_ANIM_STAMP && v->anim_card == i) {
		float t = ui_motion_ease(v->anim_t), k = UI_METRIC_1_0F + (UI_METRIC_1_0F - t) * UI_PC_STAMP_DROP;
		C3D_Mtx inner;
		C2D_ViewSave(&inner);
		C2D_ViewTranslate(w / UI_METRIC_2, h / UI_METRIC_2);
		C2D_ViewRotate(-UI_PC_STAMP_TILT);
		C2D_ViewScale(k, k);
		u32 ink = theme_alpha(p->cont, (int)(t * UI_METRIC_255));
		pc_outline(-UI_PC_SAVED_W / UI_METRIC_2, -UI_PC_SAVED_H / UI_METRIC_2, UI_PC_SAVED_W, UI_PC_SAVED_H, UI_PC_SAVED_LINE, ink, UI_PC_Z_SAVED);
		ui_text_ex(NULL, UI_METRIC_0, UI_TEXT_CENTRED_TOP(UI_METRIC_0, UI_FONT_SMALL), UI_FONT_SMALL, ink, C2D_AlignCenter, "SAVED");
		C2D_ViewRestore(&inner);
	}
	C2D_ViewRestore(&saved);
	if (c->used || cont)   // the pin
		C2D_DrawCircleSolid(x, y - h * s / UI_METRIC_2 + UI_PC_PIN_DOWN - UI_PC_LIFT_Y * lift, UI_PC_Z_PIN, UI_PC_PIN_R,
			theme_alpha(cont ? p->cont : p->pin[i & UI_METRIC_3], alpha));
}

void postcards_bottom(void *view)
{
	const pc_view *v = view;
	pc_palette p;
	pc_palette_get(&p);
	C2D_DrawRectSolid(UI_METRIC_0, UI_METRIC_0, UI_PC_Z_BACK, UI_BOTTOM_SCREEN_WIDTH, UI_SCREEN_HEIGHT, p.wall_low);
	// the board in its frame; cork flecks from a fixed pattern
	C2D_DrawRectSolid(UI_PC_BOARD_X - UI_PC_FRAME, UI_PC_BOARD_Y - UI_PC_FRAME, UI_PC_Z_FRAME, UI_PC_BOARD_W + UI_PC_FRAME * UI_METRIC_2,
		UI_PC_BOARD_H + UI_PC_FRAME * UI_METRIC_2, p.board);
	C2D_DrawRectangle(UI_PC_BOARD_X, UI_PC_BOARD_Y, UI_PC_Z_CORK, UI_PC_BOARD_W, UI_PC_BOARD_H, p.cork, p.cork, p.cork,
		theme_alpha(p.shade, UI_PC_CORK_SHADE));
	for (int i = UI_METRIC_0; i < UI_PC_FLECKS; i++) {
		float fx = (float)((i * UI_PC_FLECK_MX) % UI_PC_BOARD_W), fy = (float)((i * UI_PC_FLECK_MY) % UI_PC_BOARD_H);
		C2D_DrawRectSolid(UI_PC_BOARD_X + fx, UI_PC_BOARD_Y + fy, UI_PC_Z_FLECK, UI_PC_FLECK, UI_PC_FLECK,
			i & UI_METRIC_1 ? theme_alpha(p.shade, UI_PC_FLECK_ALPHA) : theme_alpha(p.light, UI_PC_FLECK_ALPHA));
	}
	// no note card with the game's name under Continue (it repeated the game; owner 2026-11-05)
	// the cards; the chosen one last, above its neighbours
	for (int i = UI_METRIC_0; i < PC_CARDS; i++)
		if (i != v->sel && (i < PC_SLOTS || v->has_continue))
			pc_card_draw(v, &p, i);
	pc_card_draw(v, &p, v->sel);
	// the line under the board
	char line[UI_METRIC_128];   // a notice or a confirmation only
	u32 ink = p.frame;
	if (v->notice && v->notice_t > UI_METRIC_0_0F) {
		snprintf(line, sizeof(line), "%s", v->notice);
		ink = p.light;
	} else if (v->confirm_replace)
		snprintf(line, sizeof(line), "Save over slot %d? Press A again", v->sel + UI_METRIC_1);
	else if (v->confirm_delete)
		snprintf(line, sizeof(line), "Toss slot %d? Press X again", v->sel + UI_METRIC_1);
	else
		line[UI_METRIC_0] = UI_METRIC_0;   // no slot / date line: the top screen shows the chosen card (owner 2026-11-05)
	if (v->confirm_replace || v->confirm_delete)
		ink = p.light;
	if (line[UI_METRIC_0])
		pc_hand(UI_METRIC_160, UI_PC_LINE_Y, UI_PC_HAND_SMALL, ink, C2D_AlignCenter, ui_fit(line, UI_PC_HAND_SMALL, UI_PC_LINE_W));
	ui_bar_draw(v->bar, v->nbar);
}

// ---------------------------------------------------------------- the Continue postcard (postcards.h pc_continue)
static u32 pc_mix(u32 a, u32 b, int t)   // a to b by t/255, every channel
{
	u32 out = UI_METRIC_0;
	for (int sh = UI_METRIC_0; sh < UI_METRIC_32; sh += UI_METRIC_8) {
		int ca = (int)((a >> sh) & UI_METRIC_255), cb = (int)((b >> sh) & UI_METRIC_255);
		out |= (u32)(ca + (cb - ca) * t / UI_METRIC_255) << sh;
	}
	return out;
}

// "Left off Nov 9, 18:30" (no leading zero on the day); empty when the time is unknown
static void pc_left_off(u32 saved, char *out, size_t cap)
{
	time_t tt = saved;
	struct tm *tm = saved ? gmtime(&tt) : NULL;   // the 3DS clock is local time already
	char month[UI_METRIC_8];
	out[UI_METRIC_0] = UI_METRIC_0;
	if (tm && strftime(month, sizeof(month), "%b", tm))
		snprintf(out, cap, "Left off %s %d, %02d:%02d", month, tm->tm_mday, tm->tm_hour, tm->tm_min);
}

// the ruled line a printed line stands on
static void pc_rule(const pc_palette *p, float x, float base, float w)
{
	C2D_DrawRectSolid(x, base, UI_PC_Z_INK, w, UI_METRIC_1, theme_alpha(p->soft, UI_PC_RULE_ALPHA));
}

void pc_continue_card(const pc_continue *c)
{
	pc_palette p;
	pc_palette_get(&p);
	shelf_scene_desk(!c->question);   // the question comes as a game starts, after the room let its textures go
	C3D_Mtx saved;
	C2D_ViewSave(&saved);
	const float w = UI_PC_CC_W, h = UI_PC_CC_H;
	C2D_ViewTranslate(UI_PC_CC_X + w / UI_METRIC_2, UI_PC_CC_Y + h / UI_METRIC_2);   // lying on the desk: turned a little about its middle
	C2D_ViewRotate(-UI_PC_CC_TILT);
	C2D_ViewTranslate(-w / UI_METRIC_2, -h / UI_METRIC_2);
	C2D_DrawRectSolid(UI_PC_SHADOW_X, UI_PC_SHADOW_Y, UI_PC_Z_SHADOW, w, h, theme_alpha(p.shade, UI_PC_SHADOW_ALPHA));
	u32 mid = pc_mix(p.paper, p.edge, UI_PC_CC_PAPER_MID);
	C2D_DrawRectangle(UI_METRIC_0, UI_METRIC_0, UI_PC_Z_PAPER, w, h, p.paper, mid, mid, p.edge);
	for (int i = UI_METRIC_0; i < UI_PC_FLECKS; i++)
		C2D_DrawRectSolid((float)((i * UI_PC_CC_FLECK_MX) % UI_PC_CC_W), (float)((i * UI_PC_CC_FLECK_MY) % UI_PC_CC_H), UI_PC_Z_BORDER,
			(float)(UI_METRIC_1 + i % UI_METRIC_3), UI_METRIC_1, theme_alpha(p.soft, UI_PC_CC_FLECK_ALPHA));
	const float sx = w - UI_PC_CC_PAD - UI_PC_CC_STAMP_W;
	if (c->empty) {   // a blank postcard: an empty stamp outline, the words on the middle of three lines
		C2D_DrawRectSolid(sx, UI_PC_CC_STAMP_Y, UI_PC_Z_STAMP, UI_PC_CC_STAMP_W, UI_PC_CC_STAMP_H, theme_alpha(p.edge, UI_PC_CC_EMPTY_ALPHA));
		C2D_DrawRectSolid(sx + UI_PC_STAMP_INSET, UI_PC_CC_STAMP_Y + UI_PC_STAMP_INSET, UI_PC_Z_FIELD,
			UI_PC_CC_STAMP_W - UI_PC_STAMP_INSET * UI_METRIC_2, UI_PC_CC_STAMP_H - UI_PC_STAMP_INSET * UI_METRIC_2, p.paper);
		for (int i = -UI_METRIC_1; i <= UI_METRIC_1; i++)
			pc_rule(&p, UI_PC_CC_PAD, UI_PC_CC_EMPTY_LINE + i * UI_PAPER_LINE, UI_PC_CC_NAME_W);
		ui_text_ex(NULL, w / UI_METRIC_2, UI_PAPER_TOP(UI_PC_CC_EMPTY_LINE, UI_TEXT_FLOOR), UI_TEXT_FLOOR, p.soft, C2D_AlignCenter,
			ui_fit("Nothing to continue", UI_TEXT_FLOOR, UI_PC_CC_NAME_W));
		C2D_ViewRestore(&saved);
		return;
	}
	// bottom up: the foot (red Start over notice, or "Left off"), the name's lines over it, the picture in the rest
	ui_fit_line foot[UI_PC_CC_ALERT_LINES], name[UI_PC_CC_NAME_LINES];
	float foot_size = UI_TEXT_FLOOR, name_size = UI_TEXT_FLOOR;
	int nf = UI_METRIC_0;
	if (c->alert && c->alert[UI_METRIC_0]) {
		nf = ui_fit_lines(NULL, c->alert, UI_TEXT_FLOOR, UI_TEXT_FLOOR, UI_PC_CC_NAME_W, UI_PC_CC_ALERT_LINES, foot, &foot_size);
	} else {
		pc_left_off(c->saved, foot[UI_METRIC_0], sizeof(foot[UI_METRIC_0]));
		nf = foot[UI_METRIC_0][UI_METRIC_0] ? UI_METRIC_1 : UI_METRIC_0;
	}
	int most = UI_PC_CC_NAME_LINES + UI_METRIC_1 - (nf ? nf : UI_METRIC_1);   // the name never reaches "Continue"
	int nn = ui_fit_lines(NULL, c->name, UI_TEXT_FLOOR, UI_TEXT_FLOOR, UI_PC_CC_NAME_W, most, name, &name_size);
	float first = UI_PC_CC_FOOT - (nf + nn - UI_METRIC_1) * UI_PAPER_LINE;   // the name's first baseline
	for (int i = UI_METRIC_0; i < nn; i++) {
		pc_rule(&p, UI_PC_CC_PAD, first + i * UI_PAPER_LINE, UI_PC_CC_NAME_W);
		ui_text_ex(NULL, UI_PC_CC_TEXT_X, UI_PAPER_TOP(first + i * UI_PAPER_LINE, name_size), name_size, p.ink, UI_METRIC_0, name[i]);
	}
	for (int i = UI_METRIC_0; i < nf; i++)
		ui_text_ex(NULL, UI_PC_CC_TEXT_X, UI_PAPER_TOP(UI_PC_CC_FOOT - (nf - UI_METRIC_1 - i) * UI_PAPER_LINE, foot_size), foot_size,
			c->alert ? p.cont : p.soft, UI_METRIC_0, ui_fit(foot[i], foot_size, UI_PC_CC_NAME_W));
	// the picture: the game's own shape, as big as the space over the name and left of the stamp's column allows
	bool pic = c->tex && c->tw > UI_METRIC_0 && c->th > c->band * UI_METRIC_2;
	float aspect = pic ? c->tw / (c->th - c->band * UI_METRIC_2) : UI_PC_CC_ASPECT;
	float bw = w - UI_PC_CC_PAD * UI_METRIC_2 - UI_PC_CC_COL_W - UI_PC_CC_COL_GAP, bh = first - UI_PAPER_LINE - UI_PC_CC_PAD;
	float ph = fminf(bh, roundf(bw / aspect)), pw = roundf(ph * aspect);
	C2D_DrawRectSolid(UI_PC_CC_PAD - UI_PC_CC_FRAME, UI_PC_CC_PAD - UI_PC_CC_FRAME, UI_PC_Z_BORDER, pw + UI_PC_CC_FRAME * UI_METRIC_2,
		ph + UI_PC_CC_FRAME * UI_METRIC_2, p.frame);
	if (pic)
		pc_image_band(c->tex, c->tw, c->th, c->band, UI_PC_CC_PAD, UI_PC_CC_PAD, pw, ph, UI_PC_Z_PIC);
	else
		C2D_DrawRectSolid(UI_PC_CC_PAD, UI_PC_CC_PAD, UI_PC_Z_PIC, pw, ph, p.edge);
	// the stamp, the postmark's rings over it (the date is printed at the foot), "Continue" in red under it
	pc_stamp_ex(&p, sx, UI_PC_CC_STAMP_Y, UI_PC_CC_STAMP_W, UI_PC_CC_STAMP_H, p.pin[UI_METRIC_2], c->stamp, UI_TEXT_FLOOR, UI_TEXT_FLOOR,
		UI_PC_CC_STAMP_SIDE);
	pc_postmark_rings(&p, sx - UI_PC_CC_MARK_DX, UI_PC_CC_MARK_Y, UI_PC_CC_MARK_R);
	float title_size, right = w - UI_PC_CC_TITLE_R;
	const char *title = ui_fit_ex(NULL, c->question ? "Continue?" : "Continue", UI_PC_CC_TITLE_SIZE, UI_TEXT_FLOOR,
		right - UI_PC_SLANT_ROOM - (UI_PC_CC_PAD + pw + UI_PC_CC_TITLE_GAP), &title_size);
	pc_hand(right - UI_PC_SLANT_ROOM, UI_PC_CC_TITLE_Y, title_size, p.cont, C2D_AlignRight, title);
	C2D_ViewRestore(&saved);
}
