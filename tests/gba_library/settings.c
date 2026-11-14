#define _GNU_SOURCE
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include "gba_settings.h"
#include "gba_touch.h"
#include "../common/test_tmpdir.h"
extern bool gba_touch_overridden(unsigned);
static int side;
int control_side(void){return side;}
void control_side_set(int v){side=!!v;}
int main(void) {
 char dir[4096];assert(test_mkdtemp(dir, sizeof(dir), "bb-gba-settings"));assert(!chdir(dir));
 mkdir("sdmc:",0700);mkdir("sdmc:/3ds",0700);mkdir("sdmc:/3ds/BreadBox",0700);
 gba_settings_load();assert(gba_settings.screen_size==1 && gba_settings.stereo && gba_settings.rewind);
 gba_settings_set(0,-1);assert(gba_settings.screen_size==0);
 gba_settings_set(1,1);assert(gba_settings.smoothing==1);
 gba_settings_for_game("roms/one.gba");assert(gba_settings.screen_size==0 && gba_settings.smoothing==1);
 assert(strstr(gba_settings_source(0),"defaults"));
 gba_settings_set(0,-1);assert(gba_settings.screen_size==2);
 assert(strstr(gba_settings_source(0),"This game"));
 gba_settings_for_game("roms/two.gba");assert(gba_settings.screen_size==0);
 gba_settings_set(2,1);assert(gba_settings.color_correction==1);
 gba_settings_for_game("roms/one.gba");assert(gba_settings.screen_size==2 && !gba_settings.color_correction);
 gba_settings_clear(0);assert(gba_settings.screen_size==0);assert(strstr(gba_settings_source(0),"defaults"));
 gba_settings_for_game("roms/one.gba");assert(gba_settings.screen_size==0);
 gba_settings_load();assert(gba_settings.screen_size==0 && gba_settings.smoothing==1 && !gba_settings.color_correction);
 /* Rewind: on by default, per system and per game like the other rows */
 gba_settings_set(GBA_SET_REWIND,1);assert(!gba_settings.rewind);
 gba_settings_for_game("roms/rewind.gba");assert(!gba_settings.rewind);
 gba_settings_set(GBA_SET_REWIND,1);assert(gba_settings.rewind);
 assert(strstr(gba_settings_source(GBA_SET_REWIND),"This game"));
 gba_settings_for_game("roms/other.gba");assert(!gba_settings.rewind);
 gba_settings_for_game("roms/rewind.gba");assert(gba_settings.rewind);
 gba_settings_clear(GBA_SET_REWIND);assert(!gba_settings.rewind);
 gba_settings_for_game("roms/rewind.gba");assert(!gba_settings.rewind);
 /* No 3D setting: stereo is never written, a legacy stereo=0 is ignored at launch */
 FILE *f=fopen("sdmc:/3ds/BreadBox/config/gba.ini","r");assert(f);char text[512];size_t n=fread(text,1,sizeof(text)-1,f);text[n]=0;fclose(f);
 assert(!strstr(text,"stereo") && strstr(text,"rewind=0"));
 #if GBA_TOUCH_BUTTONS   /* the on-screen buttons built in (run.py builds this test both ways) */
 /* Old files acquire the new defaults without borrowing PlayStation map keys. */
 assert(gba_touch_settings.binding[0]==8 && gba_touch_settings.binding[1]==0 && gba_touch_settings.fps==0);
 gba_settings_load();gba_touch_setting_set(0,10);gba_touch_setting_set(4,1);gba_touch_setting_set(5,2);
 gba_settings_load();assert(gba_touch_settings.binding[0]==10 && control_side()==1 && gba_touch_settings.fps==2);
 gba_settings_for_game("roms/touch.gba");assert(gba_touch_settings.binding[0]==10);
 gba_touch_setting_set(0,-1);gba_touch_setting_set(1,999);assert(gba_touch_settings.binding[1]==0);
 gba_touch_setting_set(5,1);gba_settings_for_game("roms/touch-other.gba");assert(gba_touch_settings.binding[0]==10 && gba_touch_settings.fps==2);
 gba_settings_for_game("roms/touch.gba");assert(gba_touch_settings.binding[0]==-1 && gba_touch_settings.fps==1);
 gba_touch_setting_clear(0);gba_touch_setting_clear(5);gba_settings_for_game("roms/touch.gba");
 assert(gba_touch_settings.binding[0]==10 && gba_touch_settings.fps==2);
 gba_touch_settings.fps=1;gba_touch_fps_save_global();
 assert(!gba_touch_overridden(5));
 gba_settings_for_game("roms/touch-other.gba");assert(gba_touch_settings.fps==1);
 gba_settings_load();gba_touch_setting_clear(0);gba_touch_setting_clear(5);
 f=fopen("sdmc:/3ds/BreadBox/config/gba.ini","r");assert(f);n=fread(text,1,sizeof(text)-1,f);text[n]=0;fclose(f);
 assert(!strstr(text,"touch_a=") && !strstr(text,"show_fps="));
 assert(gba_touch_intro() && gba_touch_intro()); /* loading does not mark unseen text */
 gba_touch_intro_shown();assert(!gba_touch_intro());
 #else   /* 0.9 release: the buttons are off (gba_touch.h); the FPS counter keeps its storage */
 for(int i=0;i<4;i++)assert(gba_touch_settings.binding[i]==GBA_TOUCH_HIDE);
 gba_settings_load();gba_touch_setting_set(0,10);gba_touch_setting_set(4,1);gba_touch_setting_set(5,2);
 gba_settings_load();assert(gba_touch_settings.binding[0]==GBA_TOUCH_HIDE && control_side()==1 && gba_touch_settings.fps==2);
 gba_settings_for_game("roms/touch.gba");gba_touch_setting_set(1,0);gba_touch_setting_set(5,1);
 gba_settings_for_game("roms/touch.gba");assert(gba_touch_settings.binding[1]==GBA_TOUCH_HIDE && gba_touch_settings.fps==1);
 gba_touch_setting_clear(5);gba_settings_load();gba_touch_setting_clear(5);
 f=fopen("sdmc:/3ds/BreadBox/config/gba.ini","r");assert(f);n=fread(text,1,sizeof(text)-1,f);text[n]=0;fclose(f);
 assert(!strstr(text,"touch_") && !strstr(text,"show_fps="));
 assert(!gba_touch_intro());
 #endif
 gba_settings_load();gba_settings_clear(GBA_SET_SCREEN);assert(gba_settings.screen_size==GBA_SETTINGS_DEFAULT.screen_size);
 gba_settings_load();assert(gba_settings.screen_size==GBA_SETTINGS_DEFAULT.screen_size);
 gba_settings_clear(GBA_SET_SMOOTH);assert(gba_settings.smoothing==GBA_SETTINGS_DEFAULT.smoothing);
 puts("GBA touch storage: old-file defaults, system/game scope, validated values, mirror/FPS persistence, inherit: PASS");
 puts("GBA system defaults, two independent game overrides, persisted source, rewind and clear-to-inherited: PASS");
 return 0;
}
