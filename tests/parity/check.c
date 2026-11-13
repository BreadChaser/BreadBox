#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "bb_profile.h"
#include "bb_controls.h"
#include "bb_cheats.h"
#include <libretro.h>
static unsigned resets,sets;static char active[1024];
static void reset(void){resets++;sets=0;active[0]=0;}
static void set(unsigned i,bool on,const char *code){assert(i==sets++ && on);snprintf(active,sizeof(active),"%s",code);}
static const char *const sizes[]={"Original","Fit","Stretch"};
static const bb_setting_desc schema[]={
 {.key="screen_size",.kind=BB_SET_CHOICE,.values=sizes,.nvalues=3,.def={1,1}},
 {.key="filter",.kind=BB_SET_TOGGLE}, {.key="audio",.kind=BB_SET_TOGGLE,.def={1,1}}
};
int main(void) {
 char dir[]="parity-test-XXXXXX";assert(mkdtemp(dir));assert(!chdir(dir));
 mkdir("sdmc:",0700);mkdir("sdmc:/3ds",0700);mkdir("sdmc:/3ds/BreadBox",0700);
 bb_profile p;bb_profile_open(&p,"nes","settings",NULL,schema,3,BB_OLD);
 assert(p.value[0]==1 && !p.value[1] && p.value[2]);assert(strstr(bb_profile_source(&p,0),"Built-in"));
 assert(bb_profile_change(&p,0,1));assert(bb_profile_change(&p,1,1));
 bb_profile_open(&p,"nes","settings","NES-ONE",schema,3,BB_NEW);
 assert(p.value[0]==2 && p.value[1]==1 && strstr(bb_profile_source(&p,1),"System defaults"));
 assert(bb_profile_change(&p,0,1));assert(p.value[0]==0 && strstr(bb_profile_source(&p,0),"This game"));
 assert(bb_profile_change(&p,2,1));assert(!p.value[2]);
 bb_profile_open(&p,"nes","settings","NES-TWO",schema,3,BB_OLD);assert(p.value[0]==2 && p.value[2]);
 bb_profile_open(&p,"nes","settings","NES-ONE",schema,3,BB_OLD);assert(!p.value[0] && !p.value[2]);
 assert(bb_profile_clear(&p,0));bb_profile_open(&p,"nes","settings","NES-ONE",schema,3,BB_OLD);assert(p.value[0]==2 && !(p.overrides&1));
 bb_profile_open(&p,"../bad","settings","ONE",schema,3,BB_OLD);assert(!bb_profile_change(&p,0,1));assert(p.value[0]==1);
 uint32_t keys[14];for(unsigned i=0;i<14;i++)keys[i]=1u<<i;
 bb_controls c;bb_controls_open(&c,"gba",NULL,BB_OLD,keys,true);
 uint32_t out[16],a,b;bb_controls_map(&c,out,&a,&b);assert(out[RETRO_DEVICE_ID_JOYPAD_A]==1 && !a && !b);
 for(int i=0;i<11;i++)assert(bb_profile_change(&c.profile,2,1));
 bb_controls_map(&c,out,&a,&b);assert(a==keys[2] && !b);
 bb_controls_open(&c,"gba","ONE",BB_OLD,keys,true);assert(c.profile.value[2]==11);
 assert(bb_profile_change(&c.profile,2,1));bb_controls_map(&c,out,&a,&b);assert(!a && b==keys[2]);
 uint32_t gameplay;assert(bb_controls_actions(&c,keys[2],keys[2],&gameplay)==1 && !gameplay);
 assert(bb_controls_actions(&c,keys[2],0,&gameplay)==0 && !gameplay);
 c.speed_reserved=false;
 assert(bb_profile_change(&c.profile,14,1));assert(bb_profile_change(&c.profile,14,1));
 assert(bb_controls_hotkey(&c,14)==(keys[9]|keys[5]));
 assert(bb_controls_actions(&c,keys[9],keys[9],&gameplay)==0 && gameplay==keys[9]);
 assert(bb_controls_actions(&c,keys[9]|keys[5]|keys[0],keys[5],&gameplay)==1 && gameplay==keys[0]);
 // NES allows Select+R; GBA reserves it for the forthcoming speed controls.
 c.speed_reserved=false;
 // Save/load actions consume their hotkey and resolve duplicate assignments once.
 assert(bb_profile_change(&c.profile,BB_CONTROL_SAVE,1));
 assert(bb_controls_actions(&c,keys[2],keys[2],&gameplay)==2 && !gameplay);
 assert(bb_profile_change(&c.profile,BB_CONTROL_LOAD,1));
 assert(bb_controls_actions(&c,keys[2],keys[2],&gameplay)==0 && !gameplay);
 assert(bb_profile_clear(&c.profile,BB_CONTROL_SAVE));
 assert(bb_controls_actions(&c,keys[2],keys[2],&gameplay)==4 && !gameplay);
 c.speed_reserved=true;
 // GBA FF/rewind own Select+R/L, even when a stale profile picks them.
 c.profile.value[BB_CONTROL_PAUSE]=3;
 assert(!bb_controls_actions(&c,keys[9]|keys[5],keys[5],&gameplay));
 assert(bb_controls_warning(&c,BB_CONTROL_PAUSE));
 assert(bb_controls_change(&c,BB_CONTROL_PAUSE,1));assert(c.profile.value[BB_CONTROL_PAUSE]==5);
 c.profile.value[BB_CONTROL_PAUSE]=4;
 assert(!bb_controls_actions(&c,keys[9]|keys[4],keys[4],&gameplay));
 bb_controls_open(&c,"nes",NULL,BB_OLD,keys,false);assert(!c.profile.value[4] && !c.profile.value[5]);
 assert(bb_profile_clear(&c.profile,4) && !c.profile.value[4]);
 assert(strstr(bb_profile_source(&c.profile,4),"Built-in"));
 char system_path[256];strcpy(system_path,c.profile.system_path);strcpy(c.profile.system_path,"missing/controls.ini");
 assert(!bb_profile_change(&c.profile,0,1) && c.profile.value[0]==1);strcpy(c.profile.system_path,system_path);
 bb_controls_open(&c,"gba","TWO",BB_NEW,keys,true);assert(c.profile.value[2]==11 && c.profile.value[4]==3);
 bb_cheats cheats={0};bb_cheats_open(&cheats,"gba","ONE",reset,set);
 assert(bb_cheats_add(&cheats,"Health","32000010 0063"));assert(!sets);
 assert(bb_cheats_toggle(&cheats,0));assert(sets==1 && !strcmp(active,"32000010 0063"));
 assert(!bb_cheats_add(&cheats,"Bad\nname","abc"));assert(!bb_cheats_add(&cheats,"Bad","a\"b"));
 bb_cheats_close(&cheats);bb_cheats_open(&cheats,"gba","ONE",reset,set);assert(cheats.count==1 && cheats.entries[0].enabled && sets==1);
 assert(bb_cheats_toggle(&cheats,0));assert(!sets);
 char saved_path[512];strcpy(saved_path,cheats.path);strcpy(cheats.path,"missing/cheats.cht");
 assert(!bb_cheats_toggle(&cheats,0) && !cheats.entries[0].enabled && !sets);strcpy(cheats.path,saved_path);
 bb_cheats_close(&cheats);
 bb_cheats_open(&cheats,"gba","ONE",reset,set);assert(!cheats.entries[0].enabled && !sets);bb_cheats_close(&cheats);
 bb_cheats_open(&cheats,"nes","ONE",reset,set);assert(!cheats.count);assert(bb_cheats_add(&cheats,"Lives","0010:09"));bb_cheats_close(&cheats);
 bb_cheats_open(&cheats,"gba","ONE",reset,set);assert(cheats.count==1 && !strcmp(cheats.entries[0].code,"32000010 0063"));
 for(unsigned i=1;i<BB_CHEATS_MAX;i++)assert(bb_cheats_add(&cheats,"More","32000010 0063"));
 assert(!bb_cheats_add(&cheats,"Full","32000010 0063"));bb_cheats_close(&cheats);
 puts("Shared profiles: scope, sources, clear, validation; controls: remap, turbo, hotkey consumption; cheats: persistence, isolation, limits PASS");
}
