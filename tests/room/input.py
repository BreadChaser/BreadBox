#!/usr/bin/env python3
"""Execute the shipped shelf_run loop against fake clock, catalog, HID and renderer."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[2];s=(r/'app/ui/shelf.c').read_text();h=(r/'app/ui/shelf.h').read_text()
types=h[h.index('#define SHELF_TITLE'):h.index('int shelf_run(char *path, size_t cap);')]
helpers=s[s.index('static void pull('):s.index('// the box a game was started')]
bars=s[s.index('static const ui_button_t bar_all'):s.index('static bool built, stats_stale, return_catalog;')]
run=s[s.index('int shelf_run('):s.index('// Before a game:')]
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "3ds.h"
#include "ui_motion.h"
#include "shelf_input.h"
#define BB_TITLE_LEN 96
#define BB_KEY_LEN 32
#define MAX_ROWS 8
#define DT (1.0f/120)
#define SYSCLOCK_ARM11 1000000
#define UI_SWAP_REMOTE (1u<<20)
typedef struct {int id;const char *name;} bb_system_t;
typedef struct {char key[256],path[128],title[96];} bb_catalog_item;
typedef struct {unsigned key;const char *label;int icon;bool on;} ui_button_t;
typedef struct {int unused;} room_pose;
enum {SFX_MOVE,SFX_BACK,SFX_SELECT,SFX_LAUNCH,SFX_JUMP,SFX_ERROR,UI_ICON_SORT,UI_ICON_RESCAN,UI_ICON_TOGGLE,UI_ICON_OPEN,UI_ICON_BACK,UI_ICON_STAR,UI_ICON_PLAY};
enum {DEV_UI_SETTINGS=1,BB_SAVES_MENU,BB_SAVES_CLOSE,BB_SAVES_ASKED,BB_INDEX_SCANNING,LIBRARY_EMPTY,LIBRARY_CANCELLED,ROOM_PACE_PRELOAD,ROOM_PACE_LOGIC,BB_INDEX_LIMITED,BB_INDEX_ERROR};
typedef int library_result;
'''
fakes=r'''
static shelf_view view;static shelf_row rows[8];static shelf_box data[24],*boxes=data;static int nboxes=24;
static int frame,frames,scenario,polls,builds,toggles,options,rescans,edge_sounds,settings_open;static bool hands=true;
static u64 clock_us;static u32 keys[160],held_keys[160];static int touch_action;
static bool invalidated;
static void shelf_library_changed(void){invalidated=true;}
static bool paused,worker_busy,cover_drain_pending;static unsigned catalog_key=1;
static bool favorite_rebuild_pending;static unsigned generation=1;static bool return_catalog;static struct {bool saved;} kept;static struct {int done;} restore;
static struct {char games_dir[16];} g_cfg;
static bool g_new3ds,ui_wants_3d,ui_wants_plain,ui_room_eyes;
static const char *sort_names[]={"A","B","C"};
static float clampf(float v,float a,float b){return v<a?a:v>b?b:v;}
static float ease(float v){return ui_motion_ease(v);}
static float lerpf(float a,float b,float k){return a+(b-a)*k;}
static int sounds;
static void sfx_play(int s){sounds++;if(s==SFX_BACK)edge_sounds++;}
static void sfx_cancel(void){}
static bool item_of(const bb_system_t *s,int row,bb_catalog_item *i){(void)s;snprintf(i->key,sizeof(i->key),"%d",row);return true;}
static void key_of(const bb_system_t*s,bb_catalog_item*i){(void)s;(void)i;}
static bool playlog_toggle_favourite(const char*k){(void)k;toggles++;return false;}
static void build(void){assert(view.pull<.002f || view.row!=0);builds++;generation=catalog_key;if(scenario==34 || scenario==35)strcpy(rows[1].box[0].key,"different-game");}
static int box_item(const shelf_box*b,bb_catalog_item*i){return item_of(b->system,b->row,i)?b->row:-1;}
bool aptMainLoop(void){return frame<frames;}
static bool gspHasGpuRight(void){return true;}
static void shelf_preload_end(bool menu){(void)menu;}
static u64 svcGetSystemTick(void){return clock_us;}
static void check_frame(void);
static void video_present_vsync(bool b,void(*top)(void*),void(*bottom)(void*),void*p){(void)b;(void)top;(void)bottom;(void)p;check_frame();frame++;clock_us+=16667;}
static void input_poll(void){polls++;if(frame==0){
 if(scenario==27){view.pull=view.pto=1;view.pt0=-2;view.pdur=1;}
 if(scenario==1 || scenario==2 || scenario==3){view.pull=view.pto=1;view.side=scenario==3?1:2;view.pt0=-2;view.pdur=1;}
 if(scenario==4 || scenario==5 || scenario==6 || scenario==17 || scenario==31){view.row=0;view.prow=0;view.pto=0;view.pull=.8f;view.pfrom=1;view.pt0=-.1f;view.pdur=.7f;}
 if(scenario==7 || scenario==8 || scenario==16 || scenario==19 || scenario==33){view.pto=1;view.pull=.2f;view.pfrom=0;view.pt0=0;view.pdur=1;}
 }}
static u32 input_keys_down(void){return keys[frame];}
static u32 input_keys_held(void){return held_keys[frame];}
void hidTouchRead(touchPosition*p){*p=(touchPosition){80,225};}
static u32 ui_bar_input(const ui_button_t*b,int n,u32 k){(void)b;(void)n;return (k&KEY_TOUCH)&&touch_action?(k&~KEY_TOUCH)|touch_action:k;}
static const shelf_box *shelf_pulled_box(const shelf_view*v){return v->pull>.002f?&rows[v->prow].box[v->pidx]:NULL;}
static bool shelf_scene_hands(void){return hands;}
static void bb_settings_game(const bb_system_t*s,const char*p,const char*k,const char*t,void(*draw)(void*),void*ud){(void)s;(void)p;(void)k;(void)t;(void)draw;(void)ud;options++;}
static library_result library_load_result(const char*p,bool b){(void)p;(void)b;rescans++;if(scenario==29)frames=frame;return scenario==26?LIBRARY_EMPTY:scenario==30?LIBRARY_CANCELLED:0;}
static bool launch(char*p,size_t n){(void)p;(void)n;if(scenario==27){paused=true;cover_drain_pending=true;}return true;}
#define log_printf(...) ((void)0)
#define ensure_built() ((void)0)
#define ui_swap_enter() ((void)0)
#define show_played() ((void)0)
#define shelf_scene_in_room() true
#define presets_update_poll() false
#define covers_home_frame() false
#define aptSetHomeAllowed(x) ((void)0)
#define shelf_scene_wall_pose(...) true
#define room_pace_priority(...) false
#define shelf_scene_begin(...) ((void)0)
#define shelf_scene_prepare(...) ((void)0)
#define video_room_3d() false
#define music_play(...) ((void)0)
static void covers_resume(void){paused=false;}
#define theme_room(...) false
#define theme() 0
#define room_pace_restart() ((void)0)
static bool bb_index_poll(bool refresh){assert(refresh);return (scenario==37 || scenario==38) && frame==1;}
#define bb_index_status() 0
#define bb_index_scans() 0u
typedef int bb_index_status_t;
#define ui_confirm_action(...) true
#define dev_tick(...) false
#define dev_ui_take() 0
#define bb_platform_saves(...) 0
#define settle_input() ((void)0)
#define library_key() catalog_key
#define room_scene_library_follow(...) ((void)0)
#define restore_boxes() ((void)0)
#define refresh_played() ((void)0)
#define room_hands_prepare_ms(...) ((void)0)
#define room_hands_preload_step() true
#define room_pace_add(...) ((void)0)
static bool library_settled(int still);
static bool covers_stop(void){paused=true;return !worker_busy;}
#define covers_library_begin() ((void)0)
#define covers_library_end() ((void)0)
#define bb_system_total() (scenario==26?0:24)
#define bb_index_complete() true
#define shelf_scene_launch_seconds(...) .5f
#define ui_swap_keys(k,...) (k)
#define settings_ui_run(...) ((void)(settings_open++))
#define shelf_scene_strip_hit(...) -1
#define shelf_scene_card_hit(...) -1
#define covers_stop_wait() ((void)covers_stop())
#define covers_reset() ((void)0)
#define bb_games_dir_retry() ((void)0)
#define settings_ui_take_folders() false
#define covers_full(...) true
#define shelf_scene_case_ready(...) true
#define covers_focus(...) ((void)0)
#define covers_pump() ((void)0)
#define shelf_scene_frame(...) ((void)0)
#define music_update() ((void)0)
#define room_pace_moment(...) ((void)0)
#define room_pace_hook() ((void)0)
#define room_pace_frame(...) ((void)0)
#define video_room_plain(...) ((void)0)
#define linearSpaceFree() 0ul
#define shelf_scene_clip_seconds(v,n,fallback) (fallback)
static void draw_top(void*p){(void)p;}static void draw_bottom(void*p){(void)p;}
'''
main=r'''
static void check_frame(void){
 if(scenario==33 && frame==0){catalog_key=2;view.pull=view.pto=1;}
 if(scenario==34 && frame==0){catalog_key=2;view.pto=1;view.pull=0;view.pfrom=0;view.pt0=view.t;view.pdur=1;}
 if(scenario==35 && frame==0){catalog_key=2;want_out=0;worker_busy=true;}
 if(scenario==36 && frame<30)assert(builds==0);
 if((scenario==34 || scenario==35) && frame==1)assert(!strcmp(rows[1].box[0].key,"picked-game"));
 if(scenario==1 || scenario==2){assert(view.sel[1]==0 && view.pto==1 && view.side==(scenario==1?3:2));} // R from the back: on round to the edge (no hands: no turn)
 if(scenario==3)assert(view.pto==0 && view.side==1);
 if(scenario==4 && frame==0)assert(favorite_pending && builds==0 && toggles==0);
 if(scenario==4 && frame==1)assert(favorite_pending && builds==0 && toggles==0);
 if(scenario==5 && frame==1)assert(view.sped && favorite_pending && builds==0);
 if(scenario==7 && frame==0)assert(view.turn_queued==-1);
 if(scenario==23 && frame==0){assert(paused);generation=catalog_key;}
 if(scenario==23 && frame==1)assert(!paused && !cover_drain_pending);
 if(scenario==8 && frame==0)assert(view.sped && !strcmp(bar()[3].label,"Lifting..."));
}
static void reset(int id,int count){
 invalidated=false;scenario=id;frames=count;frame=polls=builds=toggles=options=rescans=edge_sounds=settings_open=sounds=0;clock_us=0;hands=true;touch_action=0;
 memset(keys,0,sizeof(keys));memset(held_keys,0,sizeof(held_keys));memset(&view,0,sizeof(view));
 view=(shelf_view){.rows=rows,.nrows=3,.total=24,.row=1,.level=SHELF_ONE,.zoom=1,.zto=1,.pdur=1};
 for(int r=0;r<3;r++){rows[r]=(shelf_row){.box=data+r*8,.n=8};for(int i=0;i<8;i++)rows[r].box[i].row=i;strcpy(rows[r].box[0].key,"picked-game");}
 favorite_rebuild_pending=false;favorite_pending=false;want_out=-1;generation=catalog_key=1;paused=worker_busy=false;cover_drain_pending=false;
}
static void go(void){char path[32];shelf_run(path,sizeof(path));}
int main(void){
 reset(1,1);keys[0]=KEY_R;go();assert(polls==1); // endpoint never pages
 reset(2,1);hands=false;keys[0]=KEY_R;go();assert(view.sel[1]==0 && view.pto==1); // fallback consumes
 reset(3,1);keys[0]=KEY_B;go(); // B from spine puts back, never turns first
 reset(4,2);keys[0]=KEY_Y;keys[1]=KEY_Y;go();assert(toggles==0 && builds==0); // second Y cannot rebuild returning Favorite
 reset(5,2);keys[0]=KEY_Y;keys[1]=KEY_B;go();assert(polls==2); // B polled and speeds pending return
 reset(6,3);keys[0]=KEY_Y;keys[1]=KEY_START;go();assert(polls==2 && toggles==1 && builds==1); // START polled, commits target and exits
 reset(7,80);keys[0]=KEY_L;go();assert(view.side==3 && view.turn_steps==-1 && view.turn_queued==0 && view.sel[1]==0); // L during the lift: after it, round the other way to the edge
 reset(8,1);keys[0]=KEY_A;go();
 reset(9,80);view.level=SHELF_ALL;view.zoom=view.zto=0;keys[0]=KEY_Y;held_keys[0]=KEY_Y;go();assert(rescans==0);
 reset(10,90);view.level=SHELF_ALL;view.zoom=view.zto=0;keys[0]=KEY_Y;for(int i=0;i<90;i++)held_keys[i]=KEY_Y;go();assert(rescans==1);
 reset(11,90);view.level=SHELF_ALL;view.zoom=view.zto=0;touch_action=KEY_Y;keys[0]=KEY_TOUCH;for(int i=0;i<90;i++)held_keys[i]=KEY_TOUCH;go();assert(rescans==1);
 reset(12,1);keys[0]=KEY_LEFT;go();assert(view.row==1 && view.sel[1]==0 && view.cv<0 && edge_sounds==0 && sounds==0); // the end of a shelf leans, silent
 reset(21,90);keys[0]=KEY_LEFT;for(int i=0;i<90;i++)held_keys[i]=KEY_LEFT;go();assert(edge_sounds==0 && sounds==0); // repeats nudge silently
 reset(22,3);keys[0]=KEY_L;keys[2]=KEY_L;go();assert(edge_sounds==0 && sounds==0); // a page press at the end is silent too
 reset(13,1);keys[0]=KEY_UP;go();assert(view.row==1);
 reset(14,1);keys[0]=KEY_DOWN;go();assert(view.row==1 && view.sel[1]==1);
 reset(16,1);keys[0]=KEY_X;go();assert(options==1 && builds==0); // early X opens options
 reset(17,90);keys[0]=KEY_Y;go();assert(toggles==1 && builds==1 && !favorite_pending);
 reset(19,80);keys[0]=KEY_R;keys[3]=KEY_L;go();assert(view.side==0 && view.turn_queued==0); // R then L during the lift: no turn
 reset(33,80);keys[0]=KEY_R;keys[2]=KEY_R;go();assert(view.side==2 && view.turn_steps==2 && view.turn_queued==0); // two presses during the lift: half round, once
 reset(20,2);view.sel[1]=4;keys[0]=KEY_LEFT;held_keys[0]=KEY_LEFT;keys[1]=KEY_CPAD_LEFT;held_keys[1]=KEY_LEFT|KEY_CPAD_LEFT;go();assert(view.sel[1]==3);
 reset(23,2);catalog_key=2;worker_busy=true;return_catalog=true;go();assert(!paused);return_catalog=false;
 reset(24,1);catalog_key=2;worker_busy=true;return_catalog=true;go();assert(paused && !cover_drain_pending);return_catalog=false; // APT exit transfers pause
 reset(25,2);catalog_key=2;worker_busy=true;return_catalog=true;keys[0]=KEY_START;go();assert(!paused);return_catalog=false; // room exit gives pause back
 reset(26,90);view.level=SHELF_ALL;view.zoom=view.zto=0;keys[0]=KEY_Y;for(int i=0;i<90;i++)held_keys[i]=KEY_Y;go();assert(rescans==1 && !paused);
 reset(27,90);keys[0]=KEY_A;go();assert(paused && !cover_drain_pending); // launch never wakes a draining worker
 reset(28,1);catalog_key=2;view.pull=0;worker_busy=true;keys[0]=KEY_Y;go();assert(toggles==1 && builds==0 && favorite_rebuild_pending); // changed catalog cannot bypass slot invalidation
 reset(29,90);view.level=SHELF_ALL;view.zoom=view.zto=0;keys[0]=KEY_Y;for(int i=0;i<90;i++)held_keys[i]=KEY_Y;go();assert(rescans==1 && paused); // close transfers the rescan pause
 reset(31,90);keys[0]=KEY_Y;keys[1]=KEY_SELECT;go();assert(settings_open==1 && toggles==1 && builds==1 && !favorite_pending); // Settings during Favorite put-back preserves and completes the target
 reset(32,90);catalog_key=2;go();assert(builds==1 && generation==catalog_key); // populated early visit follows later publication
 reset(37,3);go();assert(builds==1 && generation==catalog_key); // presentation completes after shared counts are already final
 reset(38,2);worker_busy=true;go();assert(builds==0 && invalidated); // exit before a consumed notice can rebuild
 reset(34,2);go();assert(builds==0 && view.pidx==0 && view.pto==1); // first lift frames retain the picked game
 reset(35,2);go();assert(builds==0); // pending take-out blocks geometry too
 reset(36,32);catalog_key=36;go();assert(builds==1); // real 30-frame settling
 reset(33,2);go();assert(builds==0); // defer geometry while a box is held
 reset(30,90);view.level=SHELF_ALL;view.zoom=view.zto=0;keys[0]=KEY_Y;for(int i=0;i<90;i++)held_keys[i]=KEY_Y;go();assert(rescans==1 && !paused && !view.notice); // cancel clears the hold hint
 shelf_repeat rep={0};assert(shelf_browse_step(&rep,KEY_LEFT,KEY_LEFT,0)==-1);
 assert(shelf_browse_step(&rep,KEY_CPAD_LEFT,KEY_LEFT|KEY_CPAD_LEFT,.01f)==0);
 assert(shelf_browse_step(&rep,0,KEY_LEFT,.41f)==-1);
 puts("Shipped shelf_run with fake HID/clock/catalog: endpoints, fallback, return identity, pending START/B, lift, hold/touch rescan and shelf bounds PASS");
}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td)
 settled=s[s.index('static bool library_settled(int still)'):s.index('static void first_view(void)')].replace('library_settled(', 'real_library_settled(')+ '\nstatic bool library_settled(int still){return scenario!=36 || real_library_settled(still); }\n'
 source=prefix+types+fakes+settled+s[s.index("static bool cover_drain_pending;"):s.index("// The shelves",s.index("static bool cover_drain_pending;"))].replace("static bool cover_drain_pending;", "")+helpers+bars+run+main
 (p/'test.c').write_text(source)
 subprocess.run(['cc','-std=gnu11','-Werror=implicit-function-declaration','-Wno-unused-variable','-fsanitize=address,undefined','-I'+str(r/'tests/app/host'),'-I'+str(r/'include/breadbox'),'-I'+str(r/'app/ui'),str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)

 # Restore the orphaned-pause call site in a private source copy: the real loop
 # must fail at the cancel-frame assertion, not at unrelated cache setup.
 bad=source.replace('cover_drain(view.pull < .002f && view.pto == 0 && want_out < 0 && !launching && (publication_changed || generation != library_key()) && library_settled(still))', 'view.pull < .002f && view.pto == 0 && want_out < 0 && !launching && (publication_changed || generation != library_key()) && library_settled(still) && covers_stop()')
 assert bad!=source
 (p/'negative.c').write_text(bad)
 subprocess.run(['cc','-std=gnu11','-Werror=implicit-function-declaration','-I'+str(r/'tests/app/host'),'-I'+str(r/'include/breadbox'),'-I'+str(r/'app/ui'),str(p/'negative.c'),'-lm','-o',str(p/'negative')],check=True)
 result=subprocess.run([str(p/'negative')],capture_output=True,text=True)
 assert result.returncode!=0 and '!paused && !cover_drain_pending' in result.stderr,result.stderr
 print('Negative control: restored && covers_stop() fails the production-loop drain ownership assertion PASS')

 # An unconditional exit give-back must be caught on launch/APT close too.
 bad=source.replace('if(result != 1 && aptMainLoop() && gspHasGpuRight())cover_drain(false);\n\telse cover_drain_pending=false;', 'cover_drain(false);')
 assert bad!=source, 'production exit guard changed; update the mutation'
 (p/'exit-negative.c').write_text(bad)
 subprocess.run(['cc','-std=gnu11','-Werror=implicit-function-declaration','-I'+str(r/'tests/app/host'),'-I'+str(r/'include/breadbox'),'-I'+str(r/'app/ui'),str(p/'exit-negative.c'),'-lm','-o',str(p/'exit-negative')],check=True)
 result=subprocess.run([str(p/'exit-negative')],capture_output=True,text=True)
 assert result.returncode!=0 and 'paused && !cover_drain_pending' in result.stderr,result.stderr
 print('Negative control: unconditional exit resume fails the production launch/close ownership assertion PASS')

 # A settled shared count is insufficient when an adapter's rows arrive later.
 bad=source.replace('(publication_changed || generation != library_key())', '(generation != library_key())')
 assert bad!=source
 (p/'publication-negative.c').write_text(bad)
 subprocess.run(['cc','-std=gnu11','-Werror=implicit-function-declaration','-I'+str(r/'tests/app/host'),'-I'+str(r/'include/breadbox'),'-I'+str(r/'app/ui'),str(p/'publication-negative.c'),'-lm','-o',str(p/'publication-negative')],check=True)
 result=subprocess.run([str(p/'publication-negative')],capture_output=True,text=True)
 assert result.returncode!=0 and 'builds==1 && generation==catalog_key' in result.stderr,result.stderr
 print('Negative control: shared counts without adapter notice strand shelf geometry PASS')
