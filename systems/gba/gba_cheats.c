#include "bb_feature_ui.h"
#include "bb_pause.h"
#include "retro_host.h"
extern const bb_retro_core bb_retro_gba;
static bb_cheats cheats;
void bb_gba_cheats_open(const char *key){bb_cheats_open(&cheats,"gba",key,bb_retro_gba.cheat_reset,bb_retro_gba.cheat_set);}
void bb_gba_cheats_close(void){bb_cheats_close(&cheats);}
static int count(void){return cheats.count;}
static const char *label(int i){return cheats.entries[i].name;}
static const char *value(int i){return cheats.entries[i].enabled?"On":"Off";}
static void toggle(int i){if(!bb_cheats_toggle(&cheats,i))bb_pause_notice("Could not save cheats");}
static const char *add(void){return bb_cheats_keyboard(&cheats);}
const bb_cheat_ops bb_gba_cheats={count,label,value,toggle,add};
