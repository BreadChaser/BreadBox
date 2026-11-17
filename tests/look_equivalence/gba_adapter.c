/* Cartridge metadata is fixed; storage and the Settings adapter are production code. */
#include <string.h>
#include "gba_saveid.h"
#include "bb_feature_ui.h"
#include "app_main.h"
#include "app_input.h"
bool gba_saveid_copy(const char *a,const char *b){(void)a;(void)b;return true;}
bool gba_saveid_path(const char *path,char id[GBA_ID_LEN]){(void)path;strcpy(id,"gbs-U3IE-01");return true;}
bool gba_cart_header(const char *path,unsigned char head[192]){(void)path;memset(head,0,192);memcpy(head+0xac,"U3IE",4);return true;}
bb_controls bb_active_controls;
void bb_controls_device_open(bb_controls *c,const char *sys,const char *key,bool shoulders)
{
 const uint32_t keys[]={KEY_A,KEY_B,KEY_X,KEY_Y,KEY_L,KEY_R,g_new3ds?KEY_ZL:0,g_new3ds?KEY_ZR:0,KEY_START,KEY_SELECT,KEY_UP|KEY_CPAD_UP,KEY_DOWN|KEY_CPAD_DOWN,KEY_LEFT|KEY_CPAD_LEFT,KEY_RIGHT|KEY_CPAD_RIGHT};
 bb_controls_open(c,sys,key,g_new3ds?BB_NEW:BB_OLD,keys,shoulders);
}
bool bb_controls_ui(bb_controls *c,bool game){(void)c;(void)game;return false;}
const char *bb_cheats_keyboard(bb_cheats *c){(void)c;return NULL;}
const bb_cheat_ops bb_gba_cheats={0};
#include "../../systems/gba/gba_settings_model.c"
