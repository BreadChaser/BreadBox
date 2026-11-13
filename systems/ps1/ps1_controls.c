#include "app_policy.h"
#include "input_transport.h"
#include "ps1_controls.h"
#include "ui_play_controls.h"
// Input: 3DS buttons sit in the same physical places as the PS1 face buttons
// (A=Circle, B=Cross, X=Triangle, Y=Square), so the mapping is 1:1.
// The circle pad doubles as the d-pad for digital games and is also the
// left analog stick; the New 3DS C-stick is the right stick.
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "theme.h"
#include "ui_style.h"
#include "app_config.h"
#include "core_options.h"
#include "gamecfg.h"
#include "app_input.h"
#include "presets.h"
#include "ui.h"
#include "app_main.h"
#include "game_palette.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

static void ps1_controls_poll(void);
static void touch_update(void);
static int touch_default_binding(int slot);
static bool touch_stick_active(void);
static void actions_update(void);
static bool touch_slot_held(int i);
static bool touch_map_enabled(int id);

static int stick_x, stick_y;
static unsigned touch_revision;
static unsigned turbo_phase;
static bool pad_analog;   // the game has the pad in analog mode
int padAnalogMode(unsigned int index);
int padToggleAnalog(unsigned int index);

static ui_play_controls panel;
static void ps1_panel_prepare(void);
static unsigned cached_parts;
static game_preset_options_t cached_caps;
static unsigned prepared_revision = ~0u;
static int prepared_fps = -1;
static unsigned prepared_policy=~0u;

static u32 held, down;
static u16 touch_mask(void);
static circlePosition cpad, cstick;

#define CPAD_DPAD_THRESHOLD 60

// remappable buttons (input_map_*); -1 = does nothing in the game
static const u32 remap_keys[INPUT_PHYSICAL_N] = { KEY_A, KEY_B, KEY_X, KEY_Y, KEY_L, KEY_R, KEY_ZL, KEY_ZR, KEY_START, KEY_SELECT };
static const char *const remap_names[INPUT_REMAP_N] = { "A", "B", "X", "Y", "L", "R", "ZL", "ZR", "START", "SELECT", "TOUCH_L2", "TOUCH_R2", "TOUCH_L3", "TOUCH_R3", "TOUCH_X1", "TOUCH_X2" };
static const int remap_default[INPUT_REMAP_N] = {
	RETRO_DEVICE_ID_JOYPAD_A, RETRO_DEVICE_ID_JOYPAD_B, RETRO_DEVICE_ID_JOYPAD_X, RETRO_DEVICE_ID_JOYPAD_Y,
	RETRO_DEVICE_ID_JOYPAD_L, RETRO_DEVICE_ID_JOYPAD_R, RETRO_DEVICE_ID_JOYPAD_L2, RETRO_DEVICE_ID_JOYPAD_R2,
	RETRO_DEVICE_ID_JOYPAD_START, RETRO_DEVICE_ID_JOYPAD_SELECT,
	RETRO_DEVICE_ID_JOYPAD_L2, RETRO_DEVICE_ID_JOYPAD_R2, RETRO_DEVICE_ID_JOYPAD_L3, RETRO_DEVICE_ID_JOYPAD_R3,
	INPUT_FF_TOGGLE, INPUT_REWIND_HOLD,
};
static int remap[INPUT_REMAP_N];
static bool remap_init;

// the PS1 buttons a 3DS button can be set to, in menu order
static const int ps1_ids[] = {
	RETRO_DEVICE_ID_JOYPAD_A, RETRO_DEVICE_ID_JOYPAD_B, RETRO_DEVICE_ID_JOYPAD_X, RETRO_DEVICE_ID_JOYPAD_Y,
	RETRO_DEVICE_ID_JOYPAD_L, RETRO_DEVICE_ID_JOYPAD_R, RETRO_DEVICE_ID_JOYPAD_L2, RETRO_DEVICE_ID_JOYPAD_R2,
	RETRO_DEVICE_ID_JOYPAD_L3, RETRO_DEVICE_ID_JOYPAD_R3, RETRO_DEVICE_ID_JOYPAD_START, RETRO_DEVICE_ID_JOYPAD_SELECT,
	RETRO_DEVICE_ID_JOYPAD_UP, RETRO_DEVICE_ID_JOYPAD_DOWN, RETRO_DEVICE_ID_JOYPAD_LEFT, RETRO_DEVICE_ID_JOYPAD_RIGHT,
	INPUT_FF_TOGGLE, INPUT_FF_HOLD, INPUT_REWIND_HOLD, INPUT_TURBO_A, INPUT_TURBO_B, -1,
};
#define N_PS1 (int)(sizeof(ps1_ids) / sizeof(ps1_ids[0]))

const char *input_ps1_name(int id)
{
	switch (id) {
	case RETRO_DEVICE_ID_JOYPAD_A: return "Circle";
	case RETRO_DEVICE_ID_JOYPAD_B: return "Cross";
	case RETRO_DEVICE_ID_JOYPAD_X: return "Triangle";
	case RETRO_DEVICE_ID_JOYPAD_Y: return "Square";
	case RETRO_DEVICE_ID_JOYPAD_L: return "L1";
	case RETRO_DEVICE_ID_JOYPAD_R: return "R1";
	case RETRO_DEVICE_ID_JOYPAD_L2: return "L2";
	case RETRO_DEVICE_ID_JOYPAD_R2: return "R2";
	case RETRO_DEVICE_ID_JOYPAD_L3: return "L3";
	case RETRO_DEVICE_ID_JOYPAD_R3: return "R3";
	case RETRO_DEVICE_ID_JOYPAD_START: return "Start";
	case RETRO_DEVICE_ID_JOYPAD_SELECT: return "Select";
	case RETRO_DEVICE_ID_JOYPAD_UP: return "Up";
	case RETRO_DEVICE_ID_JOYPAD_DOWN: return "Down";
	case RETRO_DEVICE_ID_JOYPAD_LEFT: return "Left";
	case RETRO_DEVICE_ID_JOYPAD_RIGHT: return "Right";
	case INPUT_FF_TOGGLE: return "Fast forward (toggle)";
	case INPUT_FF_HOLD: return "Fast forward (hold)";
	case INPUT_REWIND_HOLD: return "Rewind (hold)";
 case INPUT_TURBO_A: return "Turbo Circle";
 case INPUT_TURBO_B: return "Turbo Cross";
	}
	return "None";
}

static void remap_defaults(void)
{
	for (int i = 0; i < INPUT_REMAP_N; i++)
		remap[i] = i < INPUT_PHYSICAL_N ? remap_default[i] : INPUT_BIND_PRESET;
	remap_init = true;
	touch_revision++;
}

int input_map_factory(int i) { return i>=0 && i<INPUT_REMAP_N ? remap_default[i] : -1; }
const char *input_map_key_name(int i) { return i >= 0 && i < INPUT_REMAP_N ? remap_names[i] : ""; }
int input_map_raw(int i)
{
	if (!remap_init) remap_defaults();
	return i >= 0 && i < INPUT_REMAP_N ? remap[i] : -1;
}
int input_map_get(int i)
{
	int binding = input_map_raw(i);
	return i >= INPUT_PHYSICAL_N && binding == INPUT_BIND_PRESET ? touch_default_binding(i - INPUT_PHYSICAL_N)
																 : binding;
}

bool input_map_is_default(int i) { return i >= 0 && i < INPUT_REMAP_N && input_map_raw(i) == (i < INPUT_PHYSICAL_N ? remap_default[i] : INPUT_BIND_PRESET); }

void input_map_set(int i, int id)
{
	if (!remap_init) remap_defaults();
	if (i < 0 || i >= INPUT_REMAP_N)
		return;
	if (i >= INPUT_PHYSICAL_N && id == INPUT_BIND_PRESET) {
		remap[i] = id;
		touch_revision++;
		return;
	}
	for (int k = 0; k < N_PS1; k++)
		if (ps1_ids[k] == id) {
			remap[i] = id;
			touch_revision++;
			return;
		}
}

// step a button's PS1 assignment forward/back through the list
void input_map_cycle(int i, int dir)
{
	int cur = 0;
	for (int k = 0; k < N_PS1; k++)
		if (ps1_ids[k] == input_map_get(i))
			cur = k;
	do { cur = (cur + dir + N_PS1) % N_PS1; }
	while (ps1_ids[cur] >= INPUT_FF_TOGGLE && !touch_map_enabled(ps1_ids[cur]));
	input_map_set(i, ps1_ids[cur]);
}

void input_map_reset(void)
{
	remap_defaults();
	input_hotkey_set(0, 39); input_hotkey_set(1, 35);
}

static const struct { u32 key; unsigned id; } map[] = {
	{ KEY_DUP,    RETRO_DEVICE_ID_JOYPAD_UP },
	{ KEY_DDOWN,  RETRO_DEVICE_ID_JOYPAD_DOWN },
	{ KEY_DLEFT,  RETRO_DEVICE_ID_JOYPAD_LEFT },
	{ KEY_DRIGHT, RETRO_DEVICE_ID_JOYPAD_RIGHT },
};

// Config stores readable names; combo indices are private to the Controls UI.
static const u32 hotkey_combos[] = {
	0,
	KEY_A | KEY_B,
	KEY_A | KEY_X,
	KEY_A | KEY_Y,
	KEY_A | KEY_L,
	KEY_A | KEY_R,
	KEY_A | KEY_ZL,
	KEY_A | KEY_ZR,
	KEY_A | KEY_START,
	KEY_A | KEY_SELECT,
	KEY_B | KEY_X,
	KEY_B | KEY_Y,
	KEY_B | KEY_L,
	KEY_B | KEY_R,
	KEY_B | KEY_ZL,
	KEY_B | KEY_ZR,
	KEY_B | KEY_START,
	KEY_B | KEY_SELECT,
	KEY_X | KEY_Y,
	KEY_X | KEY_L,
	KEY_X | KEY_R,
	KEY_X | KEY_ZL,
	KEY_X | KEY_ZR,
	KEY_X | KEY_START,
	KEY_X | KEY_SELECT,
	KEY_Y | KEY_L,
	KEY_Y | KEY_R,
	KEY_Y | KEY_ZL,
	KEY_Y | KEY_ZR,
	KEY_Y | KEY_START,
	KEY_Y | KEY_SELECT,
	KEY_L | KEY_R,
	KEY_L | KEY_ZL,
	KEY_L | KEY_ZR,
	KEY_L | KEY_START,
	KEY_L | KEY_SELECT,
	KEY_R | KEY_ZL,
	KEY_R | KEY_ZR,
	KEY_R | KEY_START,
	KEY_R | KEY_SELECT,
	KEY_ZL | KEY_ZR,
	KEY_ZL | KEY_START,
	KEY_ZL | KEY_SELECT,
	KEY_ZR | KEY_START,
	KEY_ZR | KEY_SELECT,
	KEY_START | KEY_SELECT,
};
#define HOTKEY_COMBOS (int)(sizeof(hotkey_combos)/sizeof(hotkey_combos[0]))
static int hotkeys[INPUT_HOTKEY_N] = { 39, 35 }; // SELECT+R / SELECT+L
int input_hotkey_factory(int i) { return i ? 35 : 39; }
bool input_hotkey_is_default(int i) { return i >= 0 && i < INPUT_HOTKEY_N && hotkeys[i] == (i ? 35 : 39); }
static bool gameplay, ff_on, ff_hold, rw_hold;
static u32 action_blocked, previous_toggles, consumed_keys;

int input_hotkey_get(int i) { return i >= 0 && i < INPUT_HOTKEY_N ? hotkeys[i] : 0; }
void input_hotkey_set(int i, int combo)
{
	if (i >= 0 && i < INPUT_HOTKEY_N && combo >= 0 && combo < HOTKEY_COMBOS)
		hotkeys[i] = combo;
}
const char *input_hotkey_key_name(int i) { return i == 0 ? "hotkey_ff" : "hotkey_rewind"; }
const char *input_hotkey_combo_name(int combo)
{
	static char name[32];
	if (combo <= 0 || combo >= HOTKEY_COMBOS) return "off";
	int a = -1, b = -1;
	for (int i = 0; i < INPUT_PHYSICAL_N; i++)
		if (hotkey_combos[combo] & remap_keys[i]) { if (a < 0) a = i; else b = i; }
	// Put SELECT/START first, matching the customary default spelling.
	if (b >= 8) { int t = a; a = b; b = t; }
	snprintf(name, sizeof(name), "%s+%s", remap_names[a], remap_names[b]);
	return name;
}
void input_hotkey_load(int i, const char *name)
{
	char wanted[32];
	snprintf(wanted, sizeof(wanted), "%s", name);
	for (int c = 0; c < HOTKEY_COMBOS; c++)
		if (!strcmp(wanted, input_hotkey_combo_name(c))) { input_hotkey_set(i, c); return; }
}

void input_gameplay(bool enabled)
{
	gameplay = enabled;
 input_transport_handler(enabled ? ps1_controls_poll : NULL);
	ff_on = ff_hold = rw_hold = false;
	previous_toggles = consumed_keys = 0;
	action_blocked = input_keys_held(); // menu/HOME gestures must release before firing actions
}

static void ps1_controls_poll(void)
{
 held = input_keys_held(); down = input_keys_down();
 hidCircleRead(&cpad); irrstCstickRead(&cstick);
 if (gameplay && pad_analog != (bool)padAnalogMode(0)) {
  pad_analog = !pad_analog; touch_revision++;
 }
 touch_update(); actions_update();
 turbo_phase = (turbo_phase + 1) % 6;
}

// Controller setting: Analog puts the pad in analog mode (like its ANALOG
// button); Digital leaves the mode to the game. changed: the player just
// switched the row, so an analog pad goes back to digital too.
void input_controller_apply(bool changed)
{
	bool analog = padAnalogMode(0);
	if (config_controller() ? !analog : changed && analog)
		padToggleAnalog(0);
}



static void actions_update(void)
{
	if (!gameplay) return;
	game_preset_options_t caps = cached_caps;
	if (!caps.fast_forward) ff_on = false;
	action_blocked &= held;
	consumed_keys = 0;
	u32 toggles = 0;
	ff_hold = rw_hold = false;
	for (int i = 0; i < INPUT_HOTKEY_N; i++) {
		u32 combo = (i ? caps.rewind : caps.fast_forward) ? hotkey_combos[hotkeys[i]] : 0;
		if (combo && !(combo & action_blocked) && (held & combo) == combo) {
			consumed_keys |= combo;
			if (i == 0) toggles |= 1u << INPUT_REMAP_N;
			else rw_hold = true;
		}
	}
	for (int i = 0; i < INPUT_REMAP_N; i++) {
		bool pressed = i < INPUT_PHYSICAL_N ?
			(held & ~action_blocked & ~consumed_keys & remap_keys[i]) != 0 :
			!(action_blocked & KEY_TOUCH) && touch_slot_held(i-INPUT_PHYSICAL_N);
		if (!pressed || !touch_map_enabled(input_map_get(i))) continue;
		switch (input_map_get(i)) {
		case INPUT_FF_TOGGLE: toggles |= 1u << i; break;
		case INPUT_FF_HOLD: ff_hold = true; break;
		case INPUT_REWIND_HOLD: rw_hold = true; break;
		}
	}
	if (toggles & ~previous_toggles) ff_on = !ff_on;
	previous_toggles = toggles;
	if (rw_hold) ff_on = false; // rewind takes priority over either FF mode
}

bool input_fast_forward(void) { return cached_caps.fast_forward && gameplay && !rw_hold && (ff_on || ff_hold); }
void input_fast_forward_off(void) { ff_on = ff_hold = false; }
bool input_rewind(void) { return cached_caps.rewind && gameplay && rw_hold; }

static u16 joypad_mask(void)
{
	u16 mask = 0;
	u32 game_held = held & ~consumed_keys;
	if (!remap_init)
		remap_defaults();
	for (int i = 0; i < INPUT_PHYSICAL_N; i++)
		if ((game_held & remap_keys[i]) && input_map_get(i) >= 0 && input_map_get(i) < 16)
			mask |= 1 << input_map_get(i);
 for(int i=0;i<INPUT_PHYSICAL_N;i++)if((game_held&remap_keys[i]) && turbo_phase<3) {
  if(input_map_get(i)==INPUT_TURBO_A)mask|=1<<RETRO_DEVICE_ID_JOYPAD_A;
  if(input_map_get(i)==INPUT_TURBO_B)mask|=1<<RETRO_DEVICE_ID_JOYPAD_B;
 }
	for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
		if (game_held & map[i].key)
			mask |= 1 << map[i].id;

 mask |= touch_mask();
 for(int i=INPUT_PHYSICAL_N;i<INPUT_REMAP_N;i++)if(turbo_phase<3 && touch_slot_held(i-INPUT_PHYSICAL_N)) {
  if(input_map_get(i)==INPUT_TURBO_A)mask|=1<<RETRO_DEVICE_ID_JOYPAD_A;
  if(input_map_get(i)==INPUT_TURBO_B)mask|=1<<RETRO_DEVICE_ID_JOYPAD_B;
 }

	// circle pad as d-pad (libctru's KEY_CPAD_* use a small threshold, use our own)
	if (cpad.dy >  CPAD_DPAD_THRESHOLD) mask |= 1 << RETRO_DEVICE_ID_JOYPAD_UP;
	if (cpad.dy < -CPAD_DPAD_THRESHOLD) mask |= 1 << RETRO_DEVICE_ID_JOYPAD_DOWN;
	if (cpad.dx < -CPAD_DPAD_THRESHOLD) mask |= 1 << RETRO_DEVICE_ID_JOYPAD_LEFT;
	if (cpad.dx >  CPAD_DPAD_THRESHOLD) mask |= 1 << RETRO_DEVICE_ID_JOYPAD_RIGHT;
	return mask;
}

static int16_t axis(int v)
{
	// 3DS pads report about +/-156; scale to the libretro +/-32767 range
	int r = v * 32767 / 150;
	if (r > 32767) r = 32767;
	if (r < -32768) r = -32768;
	return (int16_t)r;
}

int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
	if (port != 0)
		return 0;

	switch (device & RETRO_DEVICE_MASK) {
	case RETRO_DEVICE_JOYPAD: {
		u16 mask = joypad_mask();
		if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
			return (int16_t)mask;
		return id < 16 ? (mask >> id) & 1 : 0;
	}
	case RETRO_DEVICE_ANALOG:
		if (index == RETRO_DEVICE_INDEX_ANALOG_LEFT)
			return id == RETRO_DEVICE_ID_ANALOG_X ? axis(cpad.dx) : axis(-cpad.dy);
		if (index == RETRO_DEVICE_INDEX_ANALOG_RIGHT) {
			if (!g_new3ds)
				return touch_stick_active() ? (id == RETRO_DEVICE_ID_ANALOG_X ? stick_x : stick_y) * 32767 / 31 : 0;
			return id == RETRO_DEVICE_ID_ANALOG_X ? axis(cstick.dx) : axis(-cstick.dy);
		}
		return 0;
	}
	return 0;
}

// System policy supplies the immutable shared layout on revision changes.
#define TB_MENU INPUT_TOUCH_N
static bool needed(int need) { return need == NEEDS_ANALOG ? pad_analog : need; }
// Binding precedence: explicit global/game map, then preset/default bindings.
// Preset policy only supplies initial bindings; explicit slots always win.
static int touch_default_binding(int slot)
{
	game_preset_options_t c = game_capabilities();
	int mode = c.touch_controls;
	if (mode == TOUCH_CONTROLS_OFF)
		return INPUT_BIND_HIDE;
	if (mode == TOUCH_CONTROLS_ON)
		return remap_default[INPUT_PHYSICAL_N + slot];
	if (c.touch_bindings[slot] != INPUT_BIND_PRESET)
		return c.touch_bindings[slot];
	bool need = slot < 2 ? c.needs_l2r2 : slot < 4 ? needed(c.needs_l3r3) : slot == 4 ? c.fast_forward : c.rewind;
	return need ? remap_default[INPUT_PHYSICAL_N + slot] : INPUT_BIND_HIDE;
}
int input_stick_binding(void)
{
	if (g_new3ds)
		return 0;
	if (g_cfg.touch_stick >= 0)
		return g_cfg.touch_stick;
	game_preset_options_t c = game_capabilities();
	int mode = c.touch_controls;
	return mode == TOUCH_CONTROLS_ON || (mode != TOUCH_CONTROLS_OFF && needed(c.needs_right_stick));
}
void input_stick_set(int binding)
{
	if (binding < -1 || binding > 1)
		return;
	g_cfg.touch_stick = binding;
	input_touch_reset();
}
static unsigned touch_parts(void) { ps1_panel_prepare(); return cached_parts; }
static bool touch_stick_active(void) {ps1_panel_prepare();return panel.layout.stick && g_cfg.show_fps!=2;}
bool touch_buttons_active(void) {return touch_parts()!=0;}

// Called after per-game config is loaded, before the core loads the disc.
void input_touch_begin(void)
{
	game_presets_reset();
	pad_analog = false;
	int legacy = gamecfg_int("touch_controls", TOUCH_CONTROLS_AUTO);
	// Migrate the removed shortcut into explicit independent slot bindings.
	if (legacy == TOUCH_CONTROLS_OFF || legacy == TOUCH_CONTROLS_ON) {
		for (int i = INPUT_PHYSICAL_N; i < INPUT_REMAP_N; i++)
			if (legacy == TOUCH_CONTROLS_OFF || input_map_raw(i) == INPUT_BIND_PRESET)
				input_map_set(i, legacy == TOUCH_CONTROLS_OFF ? INPUT_BIND_HIDE : remap_default[i]);
		g_cfg.touch_stick = legacy == TOUCH_CONTROLS_ON;
	}
	input_touch_reset();
}

// CdromId is private in the core's static archive. misc.c:CheckCdrom logs it
// through the existing libretro callback on load and disc change. Consume that
// exact message; no filename guesses, pad-read heuristics or per-frame lookup.
void input_touch_disc_log(const char *message)
{
#ifdef PSX3D_PS1_OVERCLOCK_PRESETS
	game_presets_core_log(message);
#endif
	if (strncmp(message, "CD-ROM ID: ", 11))
		return;
	game_preset_options_t before = game_capabilities();
	game_presets_disc_log(message);
	options_mark_updated(); // a curated profile can change the effective frameskip policy
	game_preset_options_t after = game_capabilities();
	if (memcmp(&before, &after, sizeof(before))) {
		input_gameplay(gameplay);
		input_touch_reset();
	}
}

void input_touch_reset(void)
{
 touch_revision++;ui_play_reset(&panel);stick_x=stick_y=0;
}
static void touch_update(void)
{
 if(!gameplay)return;
 ps1_panel_prepare();
 ui_play_poll(&panel,input_contact());
 stick_x=panel.stick_x;stick_y=panel.stick_y;
}

bool input_stats_touched(void) {return ui_play_take_stats(&panel);}
bool menu_touched(void) {return ui_play_take_menu(&panel);}
static bool touch_slot_held(int i)
{ return gameplay && g_cfg.show_fps!=2 && panel.active==i &&
  (input_map_get(INPUT_PHYSICAL_N+i)!=INPUT_FF_TOGGLE || panel.pressed); }
static bool touch_map_enabled(int id)
{
 if(!gameplay)cached_caps=game_capabilities();
 if(id<0)return false;
 if(id==INPUT_FF_TOGGLE || id==INPUT_FF_HOLD)return cached_caps.fast_forward;
 if(id==INPUT_REWIND_HOLD)return cached_caps.rewind;
 return id<16 || id==INPUT_TURBO_A || id==INPUT_TURBO_B;
}
static u16 touch_mask(void)
{
 if(g_cfg.show_fps==2 || panel.active<0 || panel.active>=INPUT_TOUCH_N)return 0;
 int id=input_map_get(INPUT_PHYSICAL_N+panel.active);
 return id>=0 && id<16?1u<<id:0;
}
bool input_touch_changed(void)
{
 ps1_panel_prepare();ui_play_speed(&panel,input_fast_forward());
 return ui_play_redraw(&panel);
}
static void ps1_mark(int id,float x,float y,float pixels,float spacing);
static void ps1_panel_prepare(void)
{
 unsigned policy=(unsigned)g_new3ds | ((unsigned)(g_cfg.touch_stick+1)<<1) | ((unsigned)g_cfg.touch_click_left<<3) | ((unsigned)pad_analog<<4);
 if(prepared_revision==touch_revision && prepared_fps==g_cfg.show_fps && prepared_policy==policy)return;
 prepared_revision=touch_revision;prepared_fps=g_cfg.show_fps;prepared_policy=policy;
 cached_caps=game_capabilities();
 ui_play_layout l={.new_model=g_new3ds,.left=g_new3ds && g_cfg.touch_click_left,
  .stick=input_stick_binding()!=0,.fps_mode=g_cfg.show_fps,.mark=ps1_mark};
 cached_parts=l.stick?1u<<INPUT_TOUCH_N:0;
 for(int i=0;i<INPUT_TOUCH_N;i++) {
  int id=input_map_get(INPUT_PHYSICAL_N+i);
  bool visible=!(g_new3ds && i<2) && touch_map_enabled(id);
  if(i==4 && !cached_caps.fast_forward)visible=false;
  if(i==5 && !cached_caps.rewind)visible=false;
  l.slots[i]=(ui_play_slot){.symbol=id,.color=ui_input_color(id),.visible=visible,
   .action=id==INPUT_FF_TOGGLE?PLAY_ACTION_FF_TOGGLE:id==INPUT_FF_HOLD?PLAY_ACTION_FF_HOLD:id==INPUT_REWIND_HOLD?PLAY_ACTION_REWIND_HOLD:PLAY_ACTION_NONE};
  if(visible)cached_parts|=1u<<i;
 }
 ui_play_geometry(&l);ui_play_begin(&panel,&l);
 if(!input_contact().held)ui_play_poll(&panel,input_contact());
}

// Rows filter individual capabilities; persistent map slots never change indices.
// Special items: INPUT_REMAP_N = stick, +1/+2 = FF/rewind hotkeys.
static int control_item(int row)
{
	int n = 0;
	for (int i = 0; i < INPUT_REMAP_N + 3; i++) {
		bool visible = true;
		if (i >= INPUT_PHYSICAL_N && i < INPUT_REMAP_N) {
			int t = i - INPUT_PHYSICAL_N;
			if (g_new3ds)
				visible = t >= 2;
			if (t == 4)
				visible = game_capabilities().fast_forward;
			else if (t == 5)
				visible = g_new3ds && (game_capabilities().rewind);
		} else if (i == INPUT_REMAP_N)
			visible = !g_new3ds;
		else if (i == INPUT_REMAP_N + 1)
			visible = game_capabilities().fast_forward;
		else if (i == INPUT_REMAP_N + 2)
			visible = g_new3ds && (game_capabilities().rewind);
		if (visible && n++ == row)
			return i;
	}
	return -1;
}
int input_controls_count(void)
{
	int n = 0;
	while (control_item(n) >= 0)
		n++;
	return n;
}
int input_control_slot(int row)
{
	int i = control_item(row);
	return i >= 0 && i < INPUT_REMAP_N ? i : -1;
}
const char *input_controls_help(void)
{
	game_preset_options_t c = game_capabilities();
	if (c.rewind && c.fast_forward)
		return "Map buttons to PS1 controls, turbo Circle/Cross, fast-forward toggle/hold, rewind hold or none. Change hotkey "
			   "pairs or choose off. Enable Rewind in Settings first. Hide removes a touch slot. Bind empty slots to show them.";
	if (c.rewind)
		return "Map buttons to PS1 controls, turbo Circle/Cross, rewind hold or none. Change its hotkey pair or choose off. "
			   "Enable Rewind in Settings first. Hide removes a touch slot. Bind empty slots to show them.";
	if (c.fast_forward)
		return "Map buttons to PS1 controls, turbo Circle/Cross, fast-forward toggle/hold or none. Change its hotkey pair or "
			   "choose off. Hide removes a touch slot. Bind empty slots to show them. The touch screen accepts one contact at a "
			   "time.";
	return "Map physical and touch buttons to PS1 controls, turbo Circle/Cross or none. Hide removes a touch slot. Bind empty slots to show them. The "
		   "touch screen accepts one contact at a time. Touch controls can be changed in Settings.";
}
const char *input_control_label(int row, void *ud)
{
	(void)ud;
	static const char *const labels[] = {
		"Touch top left", "Touch top right", "Touch bottom left", "Touch bottom right", "Touch side 1", "Touch side 2"
	};
	int i = control_item(row);
	if (i < 0)
		return "";
	if (i == INPUT_REMAP_N)
		return "Touch right stick";
	if (i > INPUT_REMAP_N)
		return i == INPUT_REMAP_N + 1 ? "FF keys" : "Rewind keys";
	if (g_new3ds && (i == INPUT_PHYSICAL_N+2 || i == INPUT_PHYSICAL_N+3))
		return i == INPUT_PHYSICAL_N+2 ? "Touch top" : "Touch bottom";
	return i >= INPUT_PHYSICAL_N ? labels[i - INPUT_PHYSICAL_N] : input_map_key_name(i);
}
const char *input_control_value(int row, void *ud)
{
	(void)ud;
	int i = control_item(row);
	if (i < 0)
		return "";
	if (i == INPUT_REMAP_N)
		return input_stick_binding() ? "Right stick" : "Hide";
	if (i > INPUT_REMAP_N) {
		const char *combo = input_hotkey_combo_name(input_hotkey_get(i - INPUT_REMAP_N - 1));
		return strcmp(combo, "off") ? combo : "Off";   // "off" is also the saved name
	}
	int id = input_map_get(i);
	if (i>=INPUT_PHYSICAL_N && !touch_map_enabled(id)) return "Hide";
	if (id >= INPUT_FF_TOGGLE && !touch_map_enabled(id)) return "None";
	if (id == INPUT_FF_TOGGLE)
		return "Fast fwd (toggle)";
	if (id == INPUT_FF_HOLD)
		return "Fast fwd (hold)";
	return input_ps1_name(id);
}
u32 input_control_color(int row)
{
	int i = control_item(row);
	if (i >= 0 && i < INPUT_REMAP_N) {
		int id = input_map_get(i);
		return ui_input_color(touch_map_enabled(id) ? id : INPUT_BIND_HIDE);
	}
	if (i == INPUT_REMAP_N && !input_stick_binding()) return ui_input_color(INPUT_BIND_HIDE);
	return ui_input_color(RETRO_DEVICE_ID_JOYPAD_L);
}

void input_control_cycle(int row, int dir)
{
	int i = control_item(row);
	if (i < 0)
		return;
	if (i > INPUT_REMAP_N) {
		int hk = i - INPUT_REMAP_N - 1;
		int combo = input_hotkey_get(hk);
		do {
			combo = (combo + dir + HOTKEY_COMBOS) % HOTKEY_COMBOS;
		} while (!g_new3ds && (hotkey_combos[combo] & (KEY_ZL | KEY_ZR)));
		input_hotkey_set(hk, combo);
	} else if (i == INPUT_REMAP_N)
		input_stick_set(!input_stick_binding());
	else
		input_map_cycle(i, dir);
}

#include "components/controls.inc"

// Menu-only lookup: use the existing persistent names for individual resets.
const char *input_control_key(int row)
{
	static char key[32];
	int i = control_item(row);
	if (i < 0)
		return NULL;
	if (i == INPUT_REMAP_N)
		return "touch_stick";
	if (i > INPUT_REMAP_N)
		return input_hotkey_key_name(i - INPUT_REMAP_N - 1);
	snprintf(key, sizeof(key), "map_%s", input_map_key_name(i));
	return key;
}
bool config_control_overridden(int row)
{
	const char *key = input_control_key(row);
	return config_in_game() && key && gamecfg_get(key);
}

u32 input_hotkey_mask(int i)
{
	return i >= 0 && i < INPUT_HOTKEY_N ? hotkey_combos[hotkeys[i]] : 0;
}
void input_hotkey_cycle(int i, int dir)
{
	if (i < 0 || i >= INPUT_HOTKEY_N)
		return;
	int combo = hotkeys[i];
	do {
		combo = (combo + dir + HOTKEY_COMBOS) % HOTKEY_COMBOS;
	} while (!g_new3ds && (hotkey_combos[combo] & (KEY_ZL | KEY_ZR)));
	hotkeys[i] = combo;
}

// The pause card's Fast forward row: the game goes on fast-forwarding (the menu ended it).
void input_fast_forward_start(void)
{
	if (gameplay && game_capabilities().fast_forward)
		ff_on = true;
}

bool ps1_touch_descriptor(unsigned field,bool new_model,control_descriptor_t *out)
{
 static control_binding_t bindings[2][N_PS1+1];
 static unsigned counts[2];
 static const char *const keys[]={"map_TOUCH_L2","map_TOUCH_R2","map_TOUCH_L3","map_TOUCH_R3","map_TOUCH_X1","map_TOUCH_X2"};
 static const char *const labels[]={"Touch top left","Touch top right","Touch bottom left","Touch bottom right","Fast forward","Rewind"};
 if(field>=INPUT_TOUCH_N || !out)return false;
 unsigned model=new_model?1:0;
 if(!counts[model]) {
  bindings[model][counts[model]++]=(control_binding_t){INPUT_BIND_PRESET,"Recommended"};
  for(int i=0;i<N_PS1;i++) {
   int id=ps1_ids[i];
   if(id==INPUT_REWIND_HOLD && !new_model)continue;
   bindings[model][counts[model]++]=(control_binding_t){id,id==-1?"Hide":input_ps1_name(id)};
  }
 }
 unsigned slot=INPUT_PHYSICAL_N+field;
 game_preset_options_t caps=game_capabilities();
 *out=(control_descriptor_t){.key=keys[field],
  .label=new_model && field==2?"Touch top":new_model && field==3?"Touch bottom":labels[field],
  .value=input_map_raw(slot),.inherited=control_inherited_binding(slot),
  .visible=!(new_model && field<2) && (field!=4 || caps.fast_forward) && (field!=5 || (new_model && caps.rewind)),
  .overridden=config_in_game()?config_game_overridden(keys[field]):!input_map_is_default(slot),
  .bindings=bindings[model],.binding_count=counts[model]};
 return true;
}

static int inherited_maps[INPUT_REMAP_N];
void control_bindings_snapshot(void) {for(unsigned i=0;i<INPUT_REMAP_N;i++)inherited_maps[i]=input_map_raw(i);}
int control_inherited_binding(unsigned index)
{
 if(index>=INPUT_REMAP_N)return INPUT_BIND_HIDE;
 return config_in_game()?inherited_maps[index]:index<INPUT_PHYSICAL_N?input_map_factory(index):INPUT_BIND_PRESET;
}
