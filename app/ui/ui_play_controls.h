#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "input_transport.h"
#include "control_model.h"
#define UI_PLAY_SLOTS 6
typedef struct { int x,y,w,h; const char *slot; } ui_play_rect;
typedef struct {
 ui_play_rect rect;
 const char *label;
 int symbol; /* Opaque mark, interpreted only by the supplying adapter. */
 uint32_t color;
 bool visible;
 play_action_t action;
} ui_play_slot;
typedef void (*ui_play_mark_fn)(int symbol,float x,float y,float pixels,float spacing);
typedef struct {
 ui_play_slot slots[UI_PLAY_SLOTS];
 bool new_model,left,stick;
 int fps_mode;
 ui_play_mark_fn mark;
} ui_play_layout;
typedef struct {
 ui_play_layout layout;
 int owner,active,stick_x,stick_y;
 bool blocked,dirty,menu,stats,ff_lit,pressed;
 unsigned draws;
} ui_play_controls;
void ui_play_geometry(ui_play_layout *layout); /* session/config-change only */
void ui_play_begin(ui_play_controls *panel,const ui_play_layout *layout);
void ui_play_poll(ui_play_controls *panel,input_contact_t contact);
void ui_play_reset(ui_play_controls *panel);
bool ui_play_take_menu(ui_play_controls *panel);
bool ui_play_take_stats(ui_play_controls *panel);
bool ui_play_redraw(ui_play_controls *panel);
void ui_play_speed(ui_play_controls *panel,bool fast_forward);
void ui_play_draw(ui_play_controls *panel);
bool ui_play_visible(const ui_play_controls *panel);
