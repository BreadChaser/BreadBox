// Buttons, touch, hotkeys and the PS1 controls (input.c and the PS1 controls adapter)
#pragma once
#include <3ds.h>
#include <stdbool.h>
#include <stdint.h>
#include <strings.h>

bool input_remote_key(const char *name);

// input.c
void input_poll(void);
int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id);
u32 input_keys_down(void);
u32 input_keys_held(void);
bool touch_buttons_active(void);
bool menu_touched(void);
bool input_stats_touched(void);
bool input_fast_forward(void);
void input_controller_apply(bool changed);
void input_fast_forward_off(void);
void input_fast_forward_start(void);   // the pause card's Fast forward row
bool input_rewind(void);
#define INPUT_PHYSICAL_N 10
#define INPUT_REMAP_N 16
#define INPUT_TOUCH_N (INPUT_REMAP_N - INPUT_PHYSICAL_N)
int input_controls_count(void);
int input_control_slot(int row);
const char *input_controls_help(void);
enum { INPUT_BIND_PRESET = -2, INPUT_BIND_HIDE = -1 };
enum { INPUT_FF_TOGGLE = 16, INPUT_FF_HOLD, INPUT_REWIND_HOLD, INPUT_TURBO_A, INPUT_TURBO_B };
#define INPUT_HOTKEY_N 2
int input_hotkey_get(int i);
bool input_hotkey_is_default(int i);
void input_hotkey_set(int i, int combo);
void input_hotkey_load(int i, const char *name);
const char *input_hotkey_key_name(int i);
const char *input_hotkey_combo_name(int combo);
// Systems with their own controls (GBA) use the app's hotkey pairs too.
u32 input_hotkey_mask(int i);             // the pair's buttons, 0 = off
void input_hotkey_cycle(int i, int dir);  // next pair (no ZL/ZR on Old 3DS)
void input_gameplay(bool enabled);
const char *input_map_key_name(int i);
const char *input_ps1_name(int id);
int input_map_get(int i);
int input_map_raw(int i);
int input_map_factory(int i);
int input_stick_binding(void);
void input_stick_set(int binding);
bool input_map_is_default(int i);
void input_map_set(int i, int id);
void input_map_cycle(int i, int dir);
void input_map_reset(void);
void touch_buttons_draw(void);
enum { TOUCH_CONTROLS_AUTO, TOUCH_CONTROLS_ON, TOUCH_CONTROLS_OFF };
void input_touch_begin(void);
void input_touch_disc_log(const char *message);
void input_touch_reset(void);
bool input_touch_changed(void);
const char *input_control_label(int i, void *ud);
const char *input_control_value(int i, void *ud);
u32 input_control_color(int row);
void input_control_cycle(int i, int dir);
