/* One presentation, supplied opaque marks and palette roles from each adapter. */
static ui_play_controls *drawing;
#define tbutton_t ui_play_rect
#define STICK_X UI_TOUCH_STICK_X
#define STICK_Y UI_TOUCH_STICK_Y
#define stick_x (drawing->stick_x)
#define stick_y (drawing->stick_y)
static bool play_render_touch_stick_active(void) {return drawing->layout.stick;}
static bool play_render_touch_buttons_active(void) {return ui_play_visible(drawing);}
static bool play_render_touch_button_visible(int i) {return drawing->layout.slots[i].visible;}
static tbutton_t play_render_touch_button(int i) {return drawing->layout.slots[i].rect;}
static unsigned play_render_touch_parts(void) {
 unsigned bits=0;for(int i=0;i<UI_PLAY_SLOTS;i++)if(play_render_touch_button_visible(i))bits|=UI_PLAY_BIT_ONE<<i;return bits;
}
static bool play_render_touch_button_lit(int i) {return drawing->active==i || (drawing->layout.slots[i].action==PLAY_ACTION_FF_TOGGLE && drawing->ff_lit);}
/* A slot's word mark (the GBA's A, B, L, R, Start, Select, Up...): the controller's own name at a drawn height of at
 * least 10 px on both consoles (UI_PLAY_LABEL_PX; the 13 / 16 px cell drew 6 px letters). A name wider than its slot is
 * condensed sideways, never made shorter. */
static void play_render_label(const ui_play_slot *s,float x,float y,float spacing) {
 const float px=UI_PLAY_LABEL_PX;size_t n=strlen(s->label);
 float natural=ui_text_width(NULL,px/30,s->label)+spacing*(n?n-1:0),room=s->rect.w-UI_PLAY_LABEL_PAD*2;
 ui_text_pixels(NULL,x,y+px*UI_PLAY_LABEL_DROP,px,spacing,natural>room?room:0,s->color,C2D_AlignCenter,s->label);
}
static void play_render_touch_mark(int i,float x,float y,float pixels,float spacing) {
 const ui_play_slot *s=&drawing->layout.slots[i];
 /* icons, not words, for the actions; A / B / L / R stay the controller's own marks */
 int icon=s->action==PLAY_ACTION_FF_TOGGLE || s->action==PLAY_ACTION_FF_HOLD?UI_ICON_FF:s->action==PLAY_ACTION_REWIND_HOLD?UI_ICON_REWIND:s->action==PLAY_ACTION_MENU?UI_ICON_PAUSE:UI_ICON_NONE;
 if(icon!=UI_ICON_NONE){ui_icon_draw(icon,x,y,pixels*UI_PLAY_ICON_SCALE,s->color,g_game_palette.panel);return;}
 if(drawing->layout.mark)drawing->layout.mark(s->symbol,x,y,pixels,spacing);
 else play_render_label(s,x,y,spacing);
}
static u32 play_render_button_ink(u32 color, bool enabled)
{
	if (enabled) return color;
	return C2D_Color32((g_game_palette.bg&255)+((int)(color&255)-(int)(g_game_palette.bg&255))*0.4f,
		((g_game_palette.bg>>8)&255)+((int)((color>>8)&255)-(int)((g_game_palette.bg>>8)&255))*0.4f, ((g_game_palette.bg>>16)&255)+((int)((color>>16)&255)-(int)((g_game_palette.bg>>16)&255))*0.4f, UI_ALPHA_OPAQUE);
}

static bool play_render_clip_border(float *x, float *y, float *x2, float *y2, tbutton_t b)
{
	float dx = *x2-*x, dy = *y2-*y, lo = 0, hi = 1;
	float p[] = { -dx, dx, -dy, dy };
	float q[] = { *x-b.x, b.x+b.w-*x, *y-b.y, b.y+b.h-*y };
	for (int j = 0; j < 4; j++) {
		if (fabsf(p[j]) < UI_PLAY_CLIP_EPSILON) { if (q[j] < 0) return false; }
		else if (p[j] < 0) lo = fmaxf(lo, q[j]/p[j]);
		else hi = fminf(hi, q[j]/p[j]);
	}
	if (lo > hi) return false;
	*x2 = *x+hi*dx; *y2 = *y+hi*dy;
	*x += lo*dx; *y += lo*dy;
	return true;
}
static void play_render_edge(float x, float y, float x2, float y2, float width, u32 color)
{
	C2D_DrawLine(x, y, color, x2, y2, color, width, 0.56f);
}

static void play_render_touch_panel_draw(tbutton_t b, bool enabled, bool hit)
{
	C2D_DrawRectSolid(b.x, b.y, 0.51f, b.w, b.h, play_render_button_ink(g_game_palette.border, enabled));
	if (hit)
		C2D_DrawRectangle(b.x+1, b.y+1, 0.52f, b.w-2, b.h-2,
			g_game_palette.gradient[0], g_game_palette.gradient[0], g_game_palette.gradient[1], g_game_palette.gradient[1]);
	else C2D_DrawRectSolid(b.x+1, b.y+1, 0.52f, b.w-2, b.h-2, play_render_button_ink(g_game_palette.face, enabled));
}

static void play_render_quadrant_draw(int i)
{
	int id = i;
	bool enabled = play_render_touch_button_visible(id), hit = play_render_touch_button_lit(i);
	bool curved = play_render_touch_stick_active();
	tbutton_t b = play_render_touch_button(i);
	float sx = i & 1 ? 1 : -1, sy = i & 2 ? 1 : -1;
	// Draw merged faces first; one shared cut-out is painted after all faces.
	// This keeps a full-ring button seamless without a per-frame tessellation.
	play_render_touch_panel_draw(b, enabled, hit);
	u32 accent = play_render_button_ink(drawing->layout.slots[id].color, enabled);
	if (hit) accent = C2D_Color32((accent&255)*0.65f+(g_game_palette.pressed_add&255), ((accent>>8)&255)*0.65f+((g_game_palette.pressed_add>>8)&255), ((accent>>16)&255)*0.65f+((g_game_palette.pressed_add>>16)&255), UI_ALPHA_OPAQUE);
	// Accent only the outer cluster edges touched by this face.
	int cluster_x = UI_PLAY_CLUSTER_X;
	if (b.y == UI_PLAY_TOP)
		C2D_DrawRectSolid(b.x, b.y, UI_PLAY_Z_ACCENT, b.w, 3, accent);
	if (b.y+b.h == UI_PLAY_BOTTOM)
		C2D_DrawRectSolid(b.x, b.y+b.h-3, UI_PLAY_Z_ACCENT, b.w, 3, accent);
	if (b.x == cluster_x)
		C2D_DrawRectSolid(b.x, b.y, UI_PLAY_Z_ACCENT, 3, b.h, accent);
	if (b.x+b.w == cluster_x+UI_PLAY_CLUSTER_SIZE)
		C2D_DrawRectSolid(b.x+b.w-3, b.y, UI_PLAY_Z_ACCENT, 3, b.h, accent);
	// Quarter centroids stay fixed. Merged halves use the middle of their
	// visible strip, keeping even the largest symbol clear of the stick ring.
	float lx = b.x+b.w/2.0f, ly = b.y+b.h/2.0f;
	if (curved) {
		if (b.w == UI_PLAY_CLUSTER_SIZE && b.h == UI_PLAY_CLUSTER_SIZE) {
			lx = STICK_X + sx*63.8176f; ly = STICK_Y + sy*63.8176f;
		} else if (b.w == UI_PLAY_CLUSTER_SIZE) ly = STICK_Y + sy*87;
		else if (b.h == UI_PLAY_CLUSTER_SIZE) lx = STICK_X + sx*87;
		else { lx = STICK_X + sx*63.8176f; ly = STICK_Y + sy*63.8176f; }
	}
	play_render_touch_mark(id, lx, ly, drawing->layout.new_model ? 16 : 13, drawing->layout.new_model ? 3 : 2);
}

static void play_render_stick_cutout_draw(void)
{
	static float px[97], py[97];
	static float segments[112][4]; // at most 96 arc edges + 8 tile splits
	static unsigned previous_parts = ~0u;
	static int nsegments;
	if (px[0] == 0) {
		for (int j = 0; j <= 96; j++) {
			float a = j*(2*(float)M_PI/96);
			px[j] = STICK_X + 60*cosf(a); py[j] = STICK_Y + 60*sinf(a);
		}
	}
	unsigned parts = play_render_touch_parts();
	if (parts != previous_parts) {
		previous_parts = parts;
		nsegments = 0;
		for (int i = 0; i < 4; i++) if (play_render_touch_button_visible(i)) {
			tbutton_t b = play_render_touch_button(i);
			for (int j = 0; j < 96; j++) {
				float x = px[j], y = py[j], x2 = px[j+1], y2 = py[j+1];
				if (play_render_clip_border(&x, &y, &x2, &y2, b)) {
					float *s = segments[nsegments++];
					s[0] = x; s[1] = y; s[2] = x2; s[3] = y2;
				}
			}
		}
	}
	C2D_DrawCircleSolid(STICK_X, STICK_Y, 0.53f, 60, g_game_palette.bg);
	for (int j = 0; j < nsegments; j++)
		play_render_edge(segments[j][0], segments[j][1], segments[j][2], segments[j][3], 1, g_game_palette.border);
}

static void play_render_stick_draw(void)
{
	C2D_DrawCircleSolid(STICK_X, STICK_Y, 0.60f, 50.75f, g_game_palette.stick_rim);
	C2D_DrawCircleSolid(STICK_X, STICK_Y, 0.61f, 49.25f, g_game_palette.stick_face);
	for (int d = -36; d <= 36; d += 18) {
		float extent = sqrtf(48.75f*48.75f-d*d);
		u32 c = g_game_palette.stick_grid;
		C2D_DrawLine(STICK_X+d, STICK_Y-extent, c, STICK_X+d, STICK_Y+extent, c, 1, 0.62f);
		C2D_DrawLine(STICK_X-extent, STICK_Y+d, c, STICK_X+extent, STICK_Y+d, c, 1, 0.62f);
	}
	float x = STICK_X+stick_x, y = STICK_Y+stick_y;
	C2D_DrawCircleSolid(x, y, 0.64f, 19.75f, g_game_palette.stick_thumb);
	C2D_DrawEllipse(x-18.25f, y-18.25f, 0.65f, 36.5f, 36.5f,
		g_game_palette.gradient[0], g_game_palette.gradient[0], g_game_palette.gradient[1], g_game_palette.gradient[1]);
}

static void play_render_touch_rect_draw(int i, bool left)
{
	tbutton_t b = play_render_touch_button(i);
	int id = i;
	bool enabled = play_render_touch_button_visible(id), hit = play_render_touch_button_lit(i);
	play_render_touch_panel_draw(b, enabled, hit);
	u32 c = play_render_button_ink(drawing->layout.slots[id].color, enabled);
	if (hit) c = C2D_Color32((c&255)*0.65f+(g_game_palette.pressed_add&255), ((c>>8)&255)*0.65f+((g_game_palette.pressed_add>>8)&255), ((c>>16)&255)*0.65f+((g_game_palette.pressed_add>>16)&255), UI_ALPHA_OPAQUE);
	C2D_DrawRectSolid(left ? b.x : b.x+b.w-3, b.y, UI_PLAY_Z_ACCENT, 3, b.h, c);
	play_render_touch_mark(id, b.x+b.w/2.0f, b.y+b.h/2.0f, drawing->layout.new_model ? 16 : 13, drawing->layout.new_model ? 3 : 2);
}

void ui_play_draw(ui_play_controls *panel)
{
 drawing=panel;panel->draws++;
 if (panel->layout.fps_mode==2) {
  ui_rect(UI_PLAY_DETAIL_MENU_X,UI_PLAY_DETAIL_MENU_Y,UI_PLAY_DETAIL_MENU_W,UI_PLAY_DETAIL_MENU_H,g_game_palette.panel);
  ui_icon_draw(UI_ICON_PAUSE,UI_PLAY_DETAIL_MENU_X+UI_PLAY_DETAIL_MENU_W/2,UI_PLAY_DETAIL_MENU_Y+UI_PLAY_DETAIL_MENU_H/2,UI_PLAY_MENU_ICON,g_game_palette.text,g_game_palette.panel);return;
 }
	if (!play_render_touch_buttons_active() && drawing->layout.fps_mode != 1) return;
	if (play_render_touch_buttons_active())
		C2D_DrawRectSolid(0, 0, UI_Z_BACKDROP, UI_BOTTOM_SCREEN_WIDTH, UI_SCREEN_HEIGHT, g_game_palette.bg);
	bool left = drawing->layout.new_model && drawing->layout.left;
 ui_play_rect menu={left?UI_PLAY_MENU_MIRROR_X:UI_PLAY_MENU_X,UI_PLAY_MENU_Y,UI_PLAY_MENU_W,UI_PLAY_MENU_H,NULL};
 play_render_touch_panel_draw(menu,true,false);
 /* the menu button: the pause mark (it pauses), no word; the stripes stay (the PS1 look) */
 ui_icon_draw(UI_ICON_PAUSE,left?menu.x+menu.w-UI_PLAY_MENU_LABEL_INSET-UI_PLAY_MENU_ICON/2:menu.x+UI_PLAY_MENU_LABEL_INSET+UI_PLAY_MENU_ICON/2,
  UI_PLAY_MENU_LABEL_Y-UI_PLAY_MENU_ICON/2,UI_PLAY_MENU_ICON,g_game_palette.title,g_game_palette.panel);
 for(int j=0;j<4;j++) {
  float stripe_x=left?menu.x+UI_PLAY_MENU_STRIPES_MIRROR_INSET:menu.x+UI_PLAY_MENU_LABEL_INSET;
  C2D_DrawRectSolid(stripe_x+UI_PLAY_MENU_STRIPE_STEP*j,UI_PLAY_MENU_STRIPE_Y,UI_PLAY_Z_ACCENT,
   UI_PLAY_MENU_STRIPE_W,UI_PLAY_MENU_STRIPE_H,g_game_palette.stripe[j]);
  if(drawing->layout.fps_mode!=1)
   C2D_DrawRectSolid((left?UI_PLAY_WORDMARK_MIRROR_X:UI_PLAY_WORDMARK_X)+UI_PLAY_WORDMARK_STRIPE_STEP*j,
    UI_PLAY_WORDMARK_STRIPE_Y,0.5f,UI_PLAY_WORDMARK_STRIPE_W,UI_PLAY_MENU_STRIPE_H,g_game_palette.stripe[j]);
 }
 if(drawing->layout.fps_mode!=1)ui_text_pixels(libui_font(),left?UI_PLAY_WORDMARK_MIRROR_X:UI_PLAY_WORDMARK_X,
  UI_PLAY_WORDMARK_Y,16,0,APP_WORDMARK_WIDTH,g_game_palette.ink,0,"" APP_NAME "");
	if (!drawing->layout.new_model) {
		for (int i = 0; i < 4; i++) if(play_render_touch_button_visible(i)) play_render_quadrant_draw(i);
		if (play_render_touch_stick_active()) { play_render_stick_cutout_draw(); play_render_stick_draw(); }
	} else {
		for (int i = 2; i < 4; i++) if(play_render_touch_button_visible(i)) play_render_touch_rect_draw(i, left);
	}
	if (play_render_touch_button_visible(4)) play_render_touch_rect_draw(4, !left);
	if (play_render_touch_button_visible(5)) play_render_touch_rect_draw(5, !left);
}

#undef tbutton_t
#undef STICK_X
#undef STICK_Y
#undef stick_x
#undef stick_y
