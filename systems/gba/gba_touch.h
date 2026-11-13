#pragma once
#include "ui_play_controls.h"
/* The GBA on-screen touch buttons (Touch A, Touch B, Fast forward, Rewind) are off in 1.0 (owner 2026-11-12: they need
 * another mapping; to be relooked). Off: every slot is hidden, their settings rows are gone, saved bindings are neither
 * read nor written and the touch-controls intro is not shown; touch-to-pause and the FPS counter work as before (the
 * play panel's no-buttons path). Building with -DGBA_TOUCH_BUTTONS=1 brings the buttons back unchanged. */
#ifndef GBA_TOUCH_BUTTONS
#define GBA_TOUCH_BUTTONS 0
#endif
/* Adapter bindings are stable on disk; guest ids are local to GBA. */
enum { GBA_TOUCH_HIDE=-1, GBA_TOUCH_TURBO_A=16,GBA_TOUCH_TURBO_B,
 GBA_TOUCH_FF_TOGGLE,GBA_TOUCH_FF_HOLD,GBA_TOUCH_REWIND };
typedef struct { int binding[4],left,fps; } gba_touch_config;
#define GBA_TOUCH_DEFAULT ((gba_touch_config){{8,0,GBA_TOUCH_FF_TOGGLE,GBA_TOUCH_REWIND},0,0})
extern gba_touch_config gba_touch_settings;
#define GBA_TOUCH_BITS ((15u<<16)|(3u<<21))
static inline uint32_t gba_touch_bit(unsigned slot) {return 1u<<(16+slot+(slot>=4));}
void gba_touch_begin(uint32_t keys[16],uint32_t *turbo_a,uint32_t *turbo_b,bool new_model,bool rewind);
void gba_touch_reset(void);
enum { GBA_TOUCH_MENU_EVENT=1, GBA_TOUCH_STATS_EVENT=2, GBA_TOUCH_DIRTY_EVENT=4,
 GBA_TOUCH_TOGGLE_EVENT=8, GBA_TOUCH_HOLD_EVENT=16, GBA_TOUCH_REWIND_EVENT=32,
 GBA_TOUCH_SPEED_EVENT=64 };
extern unsigned gba_touch_pending;
unsigned gba_touch_dispatch(input_contact_t touch);
static inline unsigned gba_touch_step(input_contact_t touch)
{ return !touch.held && !gba_touch_pending?0:gba_touch_dispatch(touch); }
uint32_t gba_touch_keys(void);
void gba_touch_draw(void);
void gba_touch_lit(bool ff);
bool gba_touch_descriptor(unsigned field,bool new_model,control_descriptor_t *out);
void gba_touch_fps_save_global(void);
bool gba_touch_intro(void);
void gba_touch_intro_shown(void);
void gba_touch_setting_set(unsigned field,int value);
void gba_touch_setting_clear(unsigned field);
static inline int gba_touch_validate(unsigned field,int value)
{
 if(field==4)return !!value;
 if(field==5)return value>=0 && value<=2?value:0;
 if(value==-1 || value==0 || (value>=2 && value<=8) || value==10 || value==11 || (value>=16 && value<=20))return value;
 return field<4?GBA_TOUCH_DEFAULT.binding[field]:-1;
}
