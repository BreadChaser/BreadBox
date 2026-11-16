#ifndef BB_CONTROLS_H
#define BB_CONTROLS_H
#include "bb_profile.h"
/* Physical slots use libctru key masks supplied by the device adapter. */
#define BB_CONTROL_BUTTONS 14
#define BB_CONTROL_ROWS 17
#define BB_CONTROL_PAUSE 14
#define BB_CONTROL_SAVE 15
#define BB_CONTROL_LOAD 16
typedef struct { bb_setting_desc schema[BB_CONTROL_ROWS]; bb_profile profile; uint32_t keys[BB_CONTROL_BUTTONS]; bool speed_reserved; } bb_controls;
void bb_controls_open(bb_controls *,const char *,const char *,unsigned,const uint32_t *,bool);
const char *bb_controls_warning(const bb_controls *,unsigned row);
bool bb_controls_change(bb_controls *,unsigned row,int direction);
void bb_controls_map(const bb_controls *,uint32_t out[16],uint32_t *turbo_a,uint32_t *turbo_b);
unsigned bb_controls_actions(const bb_controls *,uint32_t held,uint32_t down,uint32_t *gameheld);
uint32_t bb_controls_hotkey(const bb_controls *,unsigned);
const char *bb_controls_value(const bb_controls *,unsigned);
bool bb_controls_row_shown(const bb_controls *,unsigned row);   /* ZL / ZR rows exist on the New 3DS only */
unsigned bb_controls_rows_shown(const bb_controls *);
unsigned bb_controls_row_at(const bb_controls *,unsigned shown);      /* the shown list position -> the schema row */
#endif
