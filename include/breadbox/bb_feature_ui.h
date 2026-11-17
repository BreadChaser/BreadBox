#ifndef BB_FEATURE_UI_H
#define BB_FEATURE_UI_H
#include "bb_controls.h"
#include "bb_cheats.h"
extern bb_controls bb_active_controls;
void bb_controls_device_open(bb_controls *,const char *,const char *,bool);
bool bb_controls_ui(bb_controls *,bool game);
bool bb_cheats_ui(const bb_cheat_ops *,bool game);
const char *bb_cheats_keyboard(bb_cheats *);
#endif
