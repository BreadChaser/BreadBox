"""Execute gpSP's actual parser/processor with RAM devices; check NES schema."""
from pathlib import Path
SPLIT_HEADERS=['app_paths.h', 'cold_code.h', 'app_log.h', 'app_config.h', 'settings_ui.h', 'last_picture.h', 'bb_platform.h', 'core_options.h', 'platform_video.h', 'core_hooks.h', 'dev_switches.h', 'pmu.h', 'csnd_out.h', 'rewind.h', 'gamecfg.h', 'cheat_codes.h', 'dsp_firmware.h', 'memory_cards.h', 'emu.h', 'saveid.h', 'savestates.h', 'stereo.h', 'audio.h', 'app_input.h', 'presets.h', 'ui_list.h', 'ui_pause.h', 'ui.h', 'library.h', 'covers.h', 'uisound.h', 'libui.h', 'share_about.h', 'play_together.h', 'playlog.h', 'hand_styles.h', 'home.h', 'games_dir.h', 'browser.h', 'app_main.h']
import re,subprocess,tempfile
r=Path(__file__).resolve().parents[2]
s=(r/'gba-core/cheats.c').read_text().replace('#include "common.h"',r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef uint8_t u8;typedef uint16_t u16;typedef int16_t s16;typedef uint32_t u32;
#include "cheats.h"
static uint8_t ram[256];
static u16 read_memory16(u32 a){return ram[a&255]|ram[(a+1)&255]<<8;}
static u32 read_memory32(u32 a){return read_memory16(a)|((u32)read_memory16(a+2)<<16);}
static void write_memory8(u32 a,u8 v){ram[a&255]=v;}
static void write_memory16(u32 a,u16 v){ram[a&255]=v;ram[(a+1)&255]=v>>8;}
static void write_memory32(u32 a,u32 v){write_memory16(a,v);write_memory16(a+2,v>>16);}
#define REG_P1 0
static u16 read_ioreg(unsigned a){(void)a;return 0;}
''')
s+=r'''
int main(void){
 cheat_clear();assert(cheat_parse(0,"32000010 0063")==CheatNoError);
 process_cheats();assert(ram[16]==99);
 cheat_disable(0);ram[16]=1;process_cheats();assert(ram[16]==1);
 assert(cheat_parse(0,"82000010 1234")==CheatNoError);process_cheats();assert(read_memory16(16)==0x1234);
 cheat_clear();ram[16]=3;process_cheats();assert(ram[16]==3);
 assert(cheat_parse(20,"32000010 0063")==CheatErrorTooMany);
 assert(cheat_parse(0,"92000010 0063")==CheatErrorEncrypted);
 puts("gpSP real CodeBreaker parser + RAM writes, disable/reset, invalid indices/encrypted codes PASS");
}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'check.c').write_text(s)
 subprocess.run(['cc','-std=gnu11','-g','-fsanitize=address,undefined','-I'+str(r/'gba-core'),str(p/'check.c'),'-o',str(p/'check')],check=True)
 subprocess.run([str(p/'check')],check=True)
# Compile the real NES schema/store hooks, not a duplicate schema.
s=(r/'systems/nes/sys_nes.c').read_text();s=s[s.index('static const char *const sizes'):s.index('static bb_run_result run')]
s='#include <assert.h>\n#include "bb_profile.h"\nstatic bool g_new3ds;\n'+s+r'''
int main(void){
 settings_open(NULL);assert(profile.count==3 && setting_value(0)==1 && !setting_value(1) && setting_value(2));
 assert(settings[0].category==BB_CAT_DISPLAY && settings[2].category==BB_CAT_AUDIO);
 assert(settings[0].nvalues==3 && settings[1].kind==BB_SET_TOGGLE);
}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'check.c').write_text(s)
 subprocess.run(['cc','-std=gnu11','-fsanitize=address,undefined','-I'+str(r/'app/ui'),'-I'+str(r/'include/breadbox'),'-I'+str(r/'platform/3ds'),str(p/'check.c'),str(r/'app/ui/bb_profile.c'),str(r/'app/ui/safefile.c'),'-o',str(p/'check')],check=True)
 subprocess.run([str(p/'check')],cwd=td,check=True)
print('NES production settings schema defaults/categories PASS')
# Run the production shared list controller with deterministic key events.
s=(r/'app/ui/bb_feature_ui.c').read_text();s=s[s.index('static bool run(bool game)'):s.index('bool bb_controls_ui(')]
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include "bb_controls.h"
#include "bb_system.h"
#include <sys/stat.h>
typedef uint32_t u32;typedef struct {bool init;} flist_t;
#define KEY_B 1
#define KEY_X 2
#define KEY_A 4
#define KEY_LEFT 8
#define KEY_RIGHT 16
#define UI_LIST_ROWS 8
#define UI_CARD_ROWS 8
static flist_t feature_list;static int feature_row;
static bb_controls *controls;static const bb_cheat_ops *cheat_ops;
static const char *feature_notice;typedef struct {const char *label;int icon;} ui_button_t;static ui_button_t feature_bar[3];static bool feature_game;enum {UI_ICON_CHANGE=1,UI_ICON_TOGGLE,UI_ICON_RESET,UI_ICON_PLUS};
static unsigned cursor,toggles,adds,draws;static u32 events[8];
static bool aptMainLoop(void){return cursor<8;}
static bool dev_tick(bool b){(void)b;return false;}
static int dev_ui_pending(void){return 0;}
static void input_poll(void){}
static u32 input_keys_down(void){return events[cursor++];}
static u32 ui_bar_input(ui_button_t *b,int n,u32 k){(void)b;(void)n;return k;}
static u32 ui_list_input(flist_t *f,int n,int *row,u32 k,bool b){(void)f;(void)row;(void)b;return n?k:k&~KEY_A;}
static u32 ui_card_input(flist_t *f,int n,int *r,u32 k){return ui_list_input(f,n,r,k,false);}
static void ui_swap_enter(void){}
static u32 ui_header_back(u32 k){return k;}
static bool gspHasGpuRight(void){return true;}
static void flist_update(flist_t *f,int row,int n,int count){(void)f;(void)row;(void)n;(void)count;}
static void music_update(void){}
static u32 ui_swap_keys(u32 keys,u32 mask){(void)mask;return keys;}
#define UI_SWAP_MENU 0u
enum {SFX_SELECT,SFX_VALUE,SFX_TOGGLE,SFX_ERROR};static void sfx_play(int e){(void)e;}
static void ui_begin(void){}
static void feature_top(void *p){(void)p;}
static void feature_bottom(void *p){(void)p;}
static void video_present_vsync(bool b,void(*top)(void*),void(*bottom)(void*),void *p){(void)b;(void)top;(void)bottom;(void)p;draws++;}
static int count(void){return adds?1:0;}
static void toggle(int i){assert(i==0);toggles++;}
static const char *add(void){adds++;return "Added";}
'''
s=prefix+s+r'''
int main(void){
 static const bb_cheat_ops ops={.count=count,.toggle=toggle,.add=add};cheat_ops=&ops;
 events[0]=KEY_A;events[1]=KEY_A;events[2]=KEY_B;
 assert(!run(true));assert(adds==1 && toggles==1 && draws==2 && cursor==3);
 mkdir("sdmc:",0700);mkdir("sdmc:/3ds",0700);mkdir("sdmc:/3ds/BreadBox",0700);
 bb_controls c;uint32_t keys[14]={0};bb_controls_open(&c,"nes","UI",BB_OLD,keys,false);controls=&c;
 cursor=0;draws=0;events[0]=KEY_RIGHT;events[1]=KEY_X;events[2]=KEY_B;
 assert(!run(true));assert(cursor==3 && draws==2 && c.profile.value[0]==1 && !c.profile.overrides);
}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'check.c').write_text(s)
 subprocess.run(['cc','-std=gnu11','-fsanitize=address,undefined','-I'+str(r/'app/ui'),'-I'+str(r/'include/breadbox'),'-I'+str(r/'platform/3ds'),str(p/'check.c'),str(r/'app/ui/bb_profile.c'),str(r/'app/ui/bb_controls.c'),str(r/'app/ui/safefile.c'),'-I'+str(r/'core/deps/libretro-common/include'),'-o',str(p/'check')],check=True)
 subprocess.run([str(p/'check')],cwd=td,check=True)
print('Shared cheat UI controller: empty list, add, toggle, Back PASS')
# PS1 compatibility: user files remain authoritative; .on/.txt stay readable by old builds.
s=(r/'systems/ps1/cheats.c').read_text()

s=s[:s.index("static const char *manager_value")].replace('#include "bb_pause.h"', "")
s=re.sub(r'(#include "(?:%s)"\n)+' % '|'.join(re.escape(h) for h in SPLIT_HEADERS), '@HOST_HEADERS@\n', s, count=1)
s=re.sub(r'#include "(?:%s)"\n' % '|'.join(re.escape(h) for h in SPLIT_HEADERS), '', s)
s=s.replace('@HOST_HEADERS@',r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <libretro.h>
#define PATH_LEN 512
#define PSX3D_DIR "sdmc:/3ds/BreadBox"
#ifndef LEGACY_DIR
#define LEGACY_DIR USER_DIR
#endif
typedef int SwkbdState;
#define SWKBD_TYPE_QWERTY 0
#define SWKBD_MULTILINE 0
#define SWKBD_BUTTON_CONFIRM 1
static unsigned keyboard;
static void swkbdInit(SwkbdState *s,int a,int b,int c){(void)s;(void)a;(void)b;(void)c;}
static void swkbdSetHintText(SwkbdState *s,const char *t){(void)s;(void)t;}
static void swkbdSetFeatures(SwkbdState *s,int f){(void)s;(void)f;}
static int swkbdInputText(SwkbdState *s,char *out,size_t n){(void)s;snprintf(out,n,"%s",keyboard++%2?"80000020 0007":"Added");return 1;}
void cheats_toggle(int);
static unsigned applied;
static unsigned char memory[16];
char CdromId[10]="SLUS12345";uint32_t CdromExeHash;
void (*psx3d_widescreen_hook)(void),(*psx3d_widescreen_state_hook)(void);
void *retro_get_memory_data(unsigned id){(void)id;return memory;}
void retro_cheat_reset(void){applied=0;}
void retro_cheat_set(unsigned i,bool on,const char *code){assert(i==applied++ && on && code[0]);}
static void log_printf(const char *fmt,...){(void)fmt;}
static void config_ws_sync(void){}
void psx3d_patch_clear(uint32_t a,unsigned n){(void)a;(void)n;}
typedef struct {int widescreen;} caps;
static caps game_capabilities(void){return (caps){0};}
''')
s+=r'''
bool ws_load(const char *a,const char *b,uint32_t c,int d){(void)a;(void)b;(void)c;(void)d;return false;}
void ws_unload(void){}
void ws_state_loaded(void){}
bool ws_enable(bool b){return b;}
bool ws_available(void){return false;}
void ws_frame(void){}
static bool sw_wsbundle;
int main(void){
 mkdir("sdmc:",0700);mkdir("sdmc:/3ds",0700);mkdir(PSX3D_DIR,0700);mkdir(LEGACY_DIR,0700);
 mkdir("romfs:",0700);mkdir("romfs:/cheats",0700);
 FILE *db=fopen("romfs:/cheats/Test.cht","w");assert(db);fputs("cheat0_desc = \"Health\"\ncheat0_code = \"80000010 0001\"\n",db);fclose(db);
 FILE *f=fopen(LEGACY_DIR "/Test.cht","w");assert(f);
 fputs("cheat0_desc = \"Health\"\ncheat0_code = \"80000010 0063\"\n",f);fclose(f);
 f=fopen(LEGACY_DIR "/Test.on","w");assert(f);fputs("Health\n",f);fclose(f);
 cheats_load("Test");assert(cheats_count()==1 && cheats_on(0) && applied==1);
 cheats_toggle(0);assert(!cheats_on(0) && !applied);
 // A downgrade reads only this file: Off must be visible immediately.
 f=fopen(LEGACY_DIR "/Test.on","r");assert(f);assert(fgetc(f)==EOF);fclose(f);
 cheats_load("Test");assert(cheats_count()==1 && !cheats_on(0) && !applied);
 cheats_toggle(0);cheats_load("Test");assert(cheats_on(0) && applied==1);
 assert(!strcmp(cheats[0].code,"80000010 0063"));
 struct stat st;assert(stat(LEGACY_DIR "/ps1",&st)); // no merged database snapshot
 CdromId[0]=0;CdromExeHash=0;cheats_disc_changed();cheats_load("Test");assert(cheats_on(0));
 f=fopen(LEGACY_DIR "/Test.on","r");assert(f);char flags[512];assert(fgets(flags,sizeof(flags),f) && !strcmp(flags,"Health\n"));fclose(f);
 // User edits must remain authoritative after a successful toggle.
 f=fopen(LEGACY_DIR "/Test.cht","w");assert(f);fputs("cheat0_desc = \"Health\"\ncheat0_code = \"80000010 0008\"\ncheat0_enable = true\ncheat1_desc = \"New\"\ncheat1_code = \"80000011 0009\"\ncheat1_enable = true\n",f);fclose(f);
 cheats_load("Test");assert(cheats_count()==2 && !strcmp(cheats[0].code,"80000010 0008") && cheats_on(0) && !cheats_on(1));
 assert(cheats_add_dialog());cheats_load("Test");assert(cheats_count()==3);int added=-1;for(int i=0;i<3;i++)if(!strcmp(cheats_desc(i),"Added"))added=i;assert(added>=0 && cheats_on(added));
 f=fopen(LEGACY_DIR "/Test.txt","r");assert(f);char text[2048];size_t len=fread(text,1,sizeof(text)-1,f);text[len]=0;fclose(f);assert(strstr(text,"Added") && strstr(text,"80000020 0007"));
 cheats_unload();unlink(LEGACY_DIR "/Test.on");cheats_load("Test");
 for(int i=0;i<cheats_count();i++)assert(!cheats_on(i));
 cheats_unload();puts("PS1 user edits, ignored exported enables, title .on toggles and .txt additions stay downgrade-safe PASS");
}
'''
# The widescreen feature flag is declared by the frontend header in production.
s=s.replace('static unsigned applied;','static bool sw_wsbundle;\nstatic unsigned applied;').replace('void ws_frame(void){}\nstatic bool sw_wsbundle;','void ws_frame(void){}')
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'check.c').write_text(s)
 subprocess.run(['cc','-std=gnu11','-fsanitize=address,undefined','-I'+str(r/'app/ui'),'-I'+str(r/'include/breadbox'),'-I'+str(r/'platform/3ds'),'-I'+str(r/'core/deps/libretro-common/include'),str(p/'check.c'),str(r/'app/ui/safefile.c'),'-o',str(p/'check')],check=True)
 subprocess.run([str(p/'check')],cwd=td,check=True)
