#!/usr/bin/env python3
"""Exercise actual cartridge map macro and APT/menu functions against strict fakes."""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
def function(text,name):
 m=re.search(r'(?:static )?(?:const bb_pause_row |bool |void |int |unsigned )\*?'+name+r'\([^\n]*\)\s*\{',text);assert m,name
 a=m.start();n=text.index('{',a);depth=1;i=n+1
 while depth:
  if text[i]=='{':depth+=1
  if text[i]=='}':depth-=1
  i+=1
 return text[a:i]
with tempfile.TemporaryDirectory(prefix='breadbox-gba-check-') as td:
 td=Path(td)
 def run(name,code):
  f=td/(name+'.c');f.write_text(code)
  subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-unused-parameter','-fsanitize=address,undefined','-g','-I'+str(root/'systems/gba'),str(f),'-o',str(td/name)],check=True)
  subprocess.run([str(td/name)],check=True)
 s=(root/'gba-core/gba_memory.c').read_text();a=s.index('#define map_rom_entry');b=s.index('#define map_region',a)
 run('rom-map',r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static void *memory_map_read[8192];
'''+s[a:b]+r'''
int main(void) {
 for(unsigned pages=1;pages<=1024;pages++) {
  memset(memory_map_read,0,sizeof(memory_map_read));
  for(unsigned page=0;page<pages;page++) {
   void *p=(void *)(uintptr_t)(page+1);
   map_rom_entry(read,page,p,pages);
  }
  for(unsigned i=0;i<4096;i++) assert(!memory_map_read[i]);
  for(unsigned i=7168;i<8192;i++) assert(!memory_map_read[i]);
  for(unsigned i=0;i<1024;i++) {
   assert((uintptr_t)memory_map_read[4096+i]==i%pages+1);
   assert((uintptr_t)memory_map_read[5120+i]==i%pages+1);
  }
  for(unsigned i=0;i<512;i++) assert((uintptr_t)memory_map_read[6144+i]==i%pages+1);
 }
 puts("ROM map: all 1024 page counts (32 KiB through 32 MiB), no cross-window overwrite: PASS");
}
''')
 s=(root/'systems/gba/gba.c').read_text()
 pause=(root/'app/ui/bb_pause.c').read_text()
 ui=(root/'systems/gba/gba_settings_ui.c').read_text()
 run('lifecycle',r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
typedef unsigned u32;
typedef enum {APTHOOK_ONSUSPEND,APTHOOK_ONSLEEP,APTHOOK_ONEXIT,APTHOOK_ONRESTORE,APTHOOK_ONWAKEUP} APT_HookType;
static bool menu_paused,clock_reset,audio_paused,log_paused,game_active=true,run_stalled;
static bool apt_alive,remote_quit,gpu_right,g_new3ds;
static unsigned menu_row;
static unsigned state_slot,saves,gpu_calls,setting_saves,resumes,continues;
static unsigned test_input,present_count,close_after=1,last_settings_count;
typedef struct {unsigned key;const char *label;unsigned icon;bool disabled;} ui_button_t;
static const ui_button_t pause_bar[1];
#define PAUSE_BAR 1
#define PC_SLOTS 12
#define SYSCLOCK_ARM11 268111856ULL
enum {SFX_SELECT,SFX_BACK,SFX_MOVE,SFX_ERROR,SFX_VALUE,SFX_SAVE,SFX_LOAD,SFX_DIALOG,SFX_CONFIRM,SFX_DENY};
static void sfx_play(int s) {(void)s;}
static void sfx_now(int s) {(void)s;}
static void sfx_cancel(void) {}
static void sfx_flush(void) {}
static void sfx_card_closes(void) {}
static bool sw_framelog;
static unsigned long long svcGetSystemTick(void) {return 0;}
static unsigned file_of(int card) {return (unsigned)card;}
static bool bb_save_exists(unsigned file) {return file==1;}   /* slot 2 holds a save */
static unsigned bb_save_slots_present(void) {return 1u<<1;}
static bool bb_save_absent(unsigned file) {return file!=1;}
static char notice[192];
#define log_printf(...) ((void)0)
static unsigned long long linearSpaceFree(void) {return 0;}
static unsigned bb_heap_available(void) {return 0;}
#define UI_ICON_BACK 0
#define UI_ICON_LEFT 0
#define UI_ICON_RIGHT 0
#define DEV_UI_NONE 0
#define DEV_UI_RESUME 1
#define KEY_START 512
#define KEY_Y 256
#define KEY_X 1
#define KEY_TOUCH 2
#define KEY_A 4
#define KEY_B 8
#define KEY_UP 16
#define KEY_DOWN 32
#define KEY_LEFT 64
#define KEY_RIGHT 128
#define KEY_DUP KEY_UP
#define KEY_DDOWN KEY_DOWN
#define KEY_CPAD_UP KEY_UP
#define KEY_CPAD_DOWN KEY_DOWN
#include "gba_settings.h"
GbaSettings gba_settings;
const char *gba_color_name(int c){return "Original (vivid)";}
bool gba_settings_solar_game;
static unsigned changed_field=99,cleared;
static bool undo_ready;
typedef struct {bool init;} flist_t;
static flist_t menu_list,settings_list;
#define UI_LIST_ROWS 8
static u32 ui_list_input(const flist_t *l,int n,int *sel,u32 k,bool horizontal) {
 if(l==&settings_list) last_settings_count=n;
 if(n>0){if((k&KEY_UP)&&*sel>0)--*sel;if((k&KEY_DOWN)&&*sel+1<n)++*sel;}return k;
}
typedef struct {int px,py;} touchPosition;
static void hidTouchRead(touchPosition *p) {p->px=p->py=0;}
static int flist_row_at(flist_t *l,int y,int h,int p) {return -1;}
static void flist_update(flist_t *l,int s,int n,int rows) {}
static u32 ui_card_input(const flist_t *l,int n,int *sel,u32 k) {return ui_list_input(l,n,sel,k,false);}
static void ui_card_update(flist_t *l,int s,int n) {}
static bool ui_confirm(const char *a,const char *b,bool game) {return false;}
static bool gba_confirm(const char *a,const char *b,const char *action) {return false;}
static void message(const char *s) {}
static int slots_run(bool saving) {return -1;}
void gba_settings_clear(int row) {cleared++;}
void gba_settings_set(int row,int dir) {changed_field=row;}
static unsigned hotkey_cycles,hotkey_resets;
static int input_hotkey_factory(int i){return i+7;}
static void input_hotkey_set(int i,int combo){assert(combo==i+7);hotkey_resets++;}
static void input_hotkey_cycle(int i,int dir) {hotkey_cycles++;}
static void config_save(void) {}
const char *gba_settings_source(int row) {return "";}
static int setting_row;
static int texture;
static bool vars_dirty;
static bool aptMainLoop(void) {return apt_alive;}
static bool dev_tick(bool b) {return remote_quit;}
static void input_poll(void) {}
static void gba_touch_reset(void) {}
static int dev_ui_pending(void) {return 0;}
static void dev_ui_take(void) {}
static u32 input_keys_down(void) {u32 edge=test_input;test_input=0;return edge;}
static u32 input_keys_held(void) {return 0;}
static u32 ui_bar_input(const ui_button_t *bar,int n,u32 k) {return k;}
static void audio_pause(bool b) {audio_paused=b;if(!b)resumes++;}
static void playlog_pause(bool b) {log_paused=b;}
static bool save_battery(void) {saves++;return true;}
static bool state_save(unsigned slot) {if(slot==10)continues++;return true;}
static unsigned loaded_slot;
static bool state_load(unsigned slot) {loaded_slot=slot;return true;}
void gba_settings_save(void) {setting_saves++;}
static void ui_begin(void) {assert(gpu_right);gpu_calls++;}
static void draw_top(void *p) {}
static void draw_pause(void *p) {}
static void draw_paused_top(void *p) {}
static void draw_settings_help(void *p) {}
static void draw_gba_settings(void *p) {}
static void gpsp_retro_reset(void) {}
static int gpsp_retro_get_rtc(u32 *p) {(void)p;return 0;}
static void gpsp_retro_set_rtc(const u32 *p) {(void)p;}
static u32 ui_header_back(u32 k) {return k;}
static bool gspHasGpuRight(void) {return gpu_right;}
/* Presentation is covered by the exact-CIA smoke; this fixture tests lifecycle. */
static u32 ui_swap_keys(u32 keys,u32 mask) {return keys;}
static u32 ui_swap_confirm_keys(u32 keys,u32 mask) {return keys;}
static void ui_swap_enter(void) {}
static void ui_pause_frame(void) {}
static bool ui_pause_open(void) {return apt_alive && gpu_right;}
static bool ui_pause_close(void) {return apt_alive && gpu_right;}
#define UI_SWAP_REMOTE (1u<<23)
#define UI_SWAP_MENU 0u
static u32 ui_swap_pause_keys(u32 keys) {return keys & ~(KEY_START|UI_SWAP_REMOTE);}
static void ui_pause_motion(float progress) {}
static void video_gpu_idle(void) {assert(gpu_right);}
#define GPU_LINEAR 1
#define GPU_NEAREST 0
static void C3D_TexSetFilter(void *t,int a,int b) {assert(gpu_right);}
static void video_present_vsync(bool b,void(*t)(void*),void(*d)(void*),void*p) {assert(gpu_right);if(++present_count>=close_after)apt_alive=gpu_right=false;gpu_calls++;}

#define bb_pause_confirm gba_confirm
#define bb_pause_active() menu_paused
#define paused menu_paused
#define bb_save_flush save_battery
#define save_state state_save
#define load_state state_load
#define pause_menu bb_pause_run
#define settings_run settings_menu
static void session_pause(bool p){audio_pause(p);playlog_pause(p);}
static void session_rebase(void){clock_reset=true;}
typedef struct {const char *label;const char *(*value)(void);bool (*activate)(int);bool (*visible)(void);bool (*enabled)(void);unsigned char icon,tone;bool arrows;} bb_pause_row;
static struct {bool controls;const void *cheats;unsigned npause_rows;const bb_pause_row *pause_rows;void (*picture_box)(float *);} test_system;
static bool row_hidden(void){return false;}
static int bb_active_controls;static unsigned controls_calls,cheats_calls;
static bool bb_controls_ui(void *c,bool game){assert(c==&bb_active_controls && game);controls_calls++;return false;}
static bool bb_cheats_ui(const void *c,bool game){assert(c==test_system.cheats && game);cheats_calls++;return false;}
static int extra_dir;
static bool extra_activate(int dir){extra_dir+=dir;return true;}
static const bb_pause_row extra_row={.activate=extra_activate,.arrows=true};
static const bb_pause_row hidden_row={.activate=extra_activate,.visible=row_hidden};
enum {BB_SLOT_CONTINUE=10};
static int bb_slot_shown(int c) {return c+1;}
static struct {void (*pause)(bool);void (*rebase)(void);bool (*settings)(void);void (*reset)(void);unsigned (*rows)(void);typeof(test_system) *system;void (*draw_game)(void *);} config;
static void ui_game_blank(void *p){(void)p;}
static bool bb_save_checkpoint(bool make_continue){bool ok=save_battery();if(make_continue)ok=state_save(10)&&ok;return ok;}
/* the pause's Continue point (bb_save.c, tests/save_service/continue.c): taken before the card, handed on after a frame */
static unsigned pause_points,pause_puts,puts_before_frame;
static bool bb_save_pause_take(void){pause_points++;return true;}
static void bb_save_pause_put(void){pause_puts++;if(!present_count)puts_before_frame++;}
static void bb_save_pause_over(void){}
'''+function(ui,'visible_fields')+'\n'+function(ui,'gba_setting_rows')+'\n'+function(ui,'gba_setting_field')+'\n'+function(ui,'gba_setting_change')+'\n'+function(ui,'gba_setting_clear')+'\n'+function(s,'apply_filter')+'\n'+function(s,'settings_menu')+'\n'+re.search(r'enum \{ BB_PAUSE_RESUME[^;]*;',(root/'include/breadbox/bb_pause.h').read_text())[0]+'\n'+re.search(r'enum \{K_RESUME[^;]*;',pause)[0]+'\ntypedef struct {unsigned char kind,index;} pause_item;static pause_item items[24];static int nitems;typedef struct {int free_card,used,saved_card;unsigned long long saved_until;} bb_pause_slots;static bb_pause_slots slots_now={-1,0,-1,0};\n#define used_count slots_now.used\n#define free_slot slots_now.free_card\nstatic unsigned long long notice_until;static void bb_pause_slots_saved(bb_pause_slots *s,int c){s->saved_card=c;}static bool aptShouldClose(void){return !apt_alive;}\n#define UI_PC_NOTHING_SAVED "Nothing saved yet."\nstatic void bb_pause_scene_set(const char *t,void (*b)(float *),void (*g)(void *)){(void)t;(void)b;(void)g;}\nstatic void quit_picture_take(void){}\n#define menu_title "test"\nstatic void game_in_glass(void *p){(void)p;}\n#define PAUSE_ITEMS 24\n#define SAVED_SECONDS 3\n'+'\n'.join(function(pause,n) for n in ('system_row_shown','rows_build','system_row','pause_count','slots_scan','save_now'))+'\n'+function(pause,'bb_pause_run')+'\n'+function(pause,'bb_pause_suspend')+'\n'+function(pause,'bb_pause_restore')+'\n'+function(s,'apt_event')+r'''
int main(void) {
 config.pause=session_pause;config.rebase=session_rebase;config.settings=settings_menu;config.reset=gpsp_retro_reset;config.system=&test_system;
 /* HOME > Close wakes with no GPU right. Must exit without unpausing/rendering. */
 apt_alive=false;gpu_right=false;
 assert(pause_menu());assert(!gpu_calls && !resumes && audio_paused && log_paused && saves==1);
 assert(pause_points==1 && pause_puts==1);   /* left before a frame: handed on at the end, not lost */
 /* HOME > Resume while paused stays paused; wake in gameplay resumes. */
 menu_paused=true;apt_event(APTHOOK_ONRESTORE,0);
 assert(clock_reset && audio_paused && log_paused);
 menu_paused=false;clock_reset=false;apt_event(APTHOOK_ONWAKEUP,0);
 assert(clock_reset && !audio_paused && !log_paused);
 apt_event(APTHOOK_ONSLEEP,0);assert(audio_paused && saves==2);
 apt_event(APTHOOK_ONSUSPEND,0);assert(audio_paused && saves==3);
 apt_event(APTHOOK_ONEXIT,0);assert(audio_paused && saves==4);
 assert(continues==3); /* sleep + suspend + exit, including without GPU rights; opening the pause card writes no
                        Continue point before it shows (it held the card back ~3 s on the Old: deep UI test Old F2) */
 /* Normal pause, then aptMainLoop false: no frame after the exit signal. */
 apt_alive=gpu_right=true;resumes=0;
 assert(pause_menu());assert(gpu_calls==2 && !resumes && audio_paused);
 assert(pause_points==2 && pause_puts==2 && puts_before_frame==1);   /* the pause's point after its first frame */
 present_count=0;
 /* Settings itself propagates Close without any GPU call. */
 gpu_calls=0;gpu_right=apt_alive=false;assert(settings_menu());assert(!gpu_calls);
 /* Close after a settings frame propagates before the parent pause redraw. */
 apt_alive=gpu_right=true;test_input=0;assert(settings_menu());assert(gpu_calls==2);
 menu_row=3;test_input=KEY_A;gpu_calls=present_count=0;close_after=2;   /* Resume Save Load Settings */
 apt_alive=gpu_right=true;resumes=0;
 assert(pause_menu());assert(gpu_calls==4 && !resumes && audio_paused);
 /* Rows: 4 display rows + fast-forward hotkey; Rewind and its hotkey on the New
  * only; Sunlight only for a solar-sensor game; never a 3D row. */
 close_after=1;test_input=KEY_UP;present_count=0;g_new3ds=true;
 apt_alive=gpu_right=true;assert(settings_menu());assert(last_settings_count==7 && setting_row==0);
 present_count=0;g_new3ds=false;apt_alive=gpu_right=true;
 assert(settings_menu());assert(last_settings_count==5 && setting_row==0);
 gba_settings_solar_game=true;present_count=0;apt_alive=gpu_right=true;
 assert(settings_menu());assert(last_settings_count==6 && gba_setting_field(4)==GBA_SET_SOLAR && gba_setting_field(5)==GBA_SET_HOTKEY_FF);
 g_new3ds=true;assert(gba_setting_rows()==8 && gba_setting_field(5)==GBA_SET_REWIND && gba_setting_field(7)==GBA_SET_HOTKEY_REWIND);
 for(int r=0;r<gba_setting_rows();r++)assert(gba_setting_field(r)!=GBA_SET_STEREO);
 gba_setting_change(5,1);assert(changed_field==GBA_SET_REWIND);
 gba_setting_change(6,1);assert(hotkey_cycles==1 && changed_field==GBA_SET_REWIND);
 cleared=0;gba_setting_clear(6);gba_setting_clear(0);assert(cleared==1 && hotkey_resets==1);   /* X restores a shared hotkey factory value */
 gba_settings_solar_game=false;assert(gba_setting_rows()==7 && gba_setting_field(4)==GBA_SET_REWIND);
 /* Shared Undo and descriptor extension rows execute inside the same pause shell. */
 close_after=100;present_count=0;apt_alive=gpu_right=true;test_input=KEY_A;menu_row=1;undo_ready=true;
 assert(!pause_menu() && loaded_slot==11 && !undo_ready && !audio_paused);
 test_system.npause_rows=1;test_system.pause_rows=&extra_row;
 menu_row=4;test_input=KEY_LEFT;present_count=0;apt_alive=gpu_right=true;   /* Resume Save Load Settings extra Reset Quit */
 assert(!pause_menu() && extra_dir==-1 && !audio_paused);
 /* Optional Controls/Cheats shift descriptor rows without replacing them. */
 test_system.controls=true;test_system.cheats=&extra_row;close_after=2;
 menu_row=4;test_input=KEY_A;present_count=0;apt_alive=gpu_right=true;
 assert(pause_menu() && controls_calls==1 && !cheats_calls);
 menu_row=5;test_input=KEY_A;present_count=0;apt_alive=gpu_right=true;
 assert(pause_menu() && controls_calls==1 && cheats_calls==1);
 menu_row=6;test_input=KEY_LEFT;present_count=0;apt_alive=gpu_right=true;
 assert(!pause_menu() && extra_dir==-2);
 puts("GBA/shared HOME Close, nested settings Close, pause/sleep/wake, Undo, descriptor pause rows, no GPU after exit: PASS");
 /* A game can hide the system's rows (GBA Sunlight outside Boktai): its row is gone. */
 test_system.controls=false;test_system.cheats=0;undo_ready=false;test_system.pause_rows=&hidden_row;rows_build();assert(nitems==6);   /* Resume Save Load Settings Reset Quit */
 test_system.pause_rows=&extra_row;rows_build();assert(nitems==7);undo_ready=true;rows_build();assert(nitems==8 && items[1].kind==K_UNDO);
 /* One tap: the first empty slot (slot 1; slot 2 holds a save); all used: the board. */
 slots_scan();assert(used_count==1 && free_slot==0);assert(save_now()==0 && slots_now.saved_card==0 && state_slot==0);
 puts("GBA/shared HOME Close, nested settings Close, pause/sleep/wake, Undo, descriptor pause rows, Controls/Cheats rows, hidden rows, Undo only after a load, one-tap save, visible settings rows, no GPU after exit: PASS");
}
''')
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(root/'tests/gba/timing.c'),'-o',str(td/'timing')],check=True)
 subprocess.run([str(td/'timing')],check=True)

# Execute the real core guard cadence and frontend deadline against a fake clock.
with tempfile.TemporaryDirectory(prefix='breadbox-watchdog-') as td:
 td=Path(td)
 core=(root/'gba-core/main.c').read_text()
 front=(root/'systems/gba/gba.c').read_text()
 code=r'''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
typedef uint64_t u64;
static bool (*run_guard)(void);static unsigned run_guard_polls;
static u64 run_deadline,tick;static bool run_stalled;
static u64 svcGetSystemTick(void){return tick;}
''' + function(core,'retro_set_run_guard') + function(core,'run_guard_abort') + function(front,'frame_guard') + r'''
int main(void){
 run_deadline=100;tick=99;retro_set_run_guard(frame_guard);
 assert(!run_guard_abort() && !run_stalled);
 tick=100;for(int i=0;i<1023;i++)assert(!run_guard_abort());
 assert(run_guard_abort() && run_stalled);
 retro_set_run_guard(0);for(int i=0;i<2048;i++)assert(!run_guard_abort());
 puts("GBA cooperative frame deadline, poll cadence and guard removal: PASS");
}
'''
 (td/'guard.c').write_text('#include <stdio.h>\n'+code)
 subprocess.run(['cc','-Wall','-Werror','-fsanitize=address,undefined',str(td/'guard.c'),'-o',str(td/'guard')],check=True)
 subprocess.run([str(td/'guard')],check=True)

subprocess.run(['python3',str(root/'tests/gba/stereo.py')],check=True)
