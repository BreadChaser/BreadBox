#!/usr/bin/env python3
"""Run production Share loop, QR cache and camera with fake input/APT/service.
Does not emulate SOC/AM threads or certify GPU/QR decoding (exact CIA smoke does).
"""
from pathlib import Path
import subprocess,tempfile,re,os
r=Path(__file__).resolve().parents[2]
s=(r/'app/services/share.c').read_text()
def fn(name,src=s):
 m=re.search(r'(?:static )?(?:bool|void|const char \*|int)\s*'+name+r'\([^;{}]*\)\s*\{',src);assert m,name
 a=src.index('{',m.start());end=a+1;depth=1
 while depth:
  depth+=(src[end]=='{')-(src[end]=='}');end+=1
 return src[m.start():end]
loop=fn('share_about_run');loop=loop[loop.index('{')+1:loop.index('\n\treturn;\n')+len('\n\treturn;\n')]
state=s[s.index('static uint8_t qr['):s.index('static void draw_top')]
items=s[s.index('enum { SHARE_RELEASE'):s.index('// Runs until B')]
# Test the actual find_cia function against AM/FS stubs, not a rewritten policy.
find=s[s.index('// Is the file the build running here?'):s.index('static bool lan_ok')]
code=r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "room.h"
#include "home_view.h"
#include "share.h"
#include "ui_motion.h"
#include "qrcode.h"
#include "title_version.h"
bool theme_is_room(void){return true;}   /* the Room: Share glides there and back */
typedef unsigned u32;typedef unsigned short u16;typedef unsigned long long u64;typedef int Handle;
#define KEY_A 1u
#define KEY_B 2u
#define KEY_X 4u
#define KEY_TOUCH 8u
#define KEY_DOWN 16u
#define SYSCLOCK_ARM11 1000000ULL
#define APP_NAME "BreadBox"
#define BB_VERSION "0.9.1"
#define SELF_TITLE 17
#define RELEASE_URL "https://github.com/BreadChaser/BreadBox/releases/latest/download/BreadBox.cia"
#define MEDIATYPE_SD 0
#define ARCHIVE_SDMC 0
#define PATH_EMPTY 0
#define PATH_ASCII 0
#define FS_OPEN_READ 0
#define R_FAILED(r) ((r)<0)
#define R_SUCCEEDED(r) ((r)>=0)
typedef struct {u64 titleID;u16 version;} AM_TitleEntry;
typedef struct {u64 titleID;u64 size;u16 version;} AM_TitleInfo;
static int files,invalid,opens,closes,homebrew;static u16 cia_version=3,running_version=25631;
static const char *const cia_paths[]={"sdmc:/cias/BreadBox.cia"};
static char cia_path[64],cia_note[160],url[64]="http://localhost:8080/BreadBox.cia";static u64 cia_size;
static int amInit(void){return 0;}static void amExit(void){}
static int fsMakePath(int t,const char *s){(void)t;(void)s;return 0;}
static int FSUSER_OpenFileDirectly(Handle *h,int a,int b,int c,int d,int e){(void)a;(void)b;(void)c;(void)d;(void)e;opens++;*h=1;return files?0:-1;}
static int AM_GetCiaFileInfo(int a,AM_TitleEntry *i,Handle h){(void)a;(void)h;i->titleID=invalid?99:SELF_TITLE;i->version=cia_version;return 0;}
static bool envIsHomebrew(void){return homebrew;}
static int AM_GetTitleInfo(int m,unsigned n,u64 *ids,AM_TitleInfo *t){assert(m==MEDIATYPE_SD && n==1 && *ids==SELF_TITLE);t->titleID=*ids;t->version=running_version;return 0;}
static int FSFILE_GetSize(Handle h,u64 *n){(void)h;*n=123;return 0;}
static void FSFILE_Close(Handle h){(void)h;closes++;}
typedef struct {bool init;} flist_t;
static bool serving,alive;static int frame,scenario,starts,stops,paints,dialogs;static u64 clock_tick;
static room_view view;static int enters,leaves;static float leave_time;
static void draw_top(void *p){(void)p;}static void draw_bottom(void *p){(void)p;}
static bool share_start(void);
typedef void *Thread;static Thread server_thread;static int listen_fd=-1;static bool server_stop;
#define U64_MAX (~0ULL)
static void threadJoin(Thread t,u64 timeout){assert(t && timeout==U64_MAX && server_stop);stops++;}
static void threadFree(Thread t){assert(t);}
static void close(int fd){assert(fd==19);}

static bool aptMainLoop(void){return alive && frame<300;}
static bool dev_tick(bool g){(void)g;return false;}static bool dev_ui_pending(void){return false;}
static u64 svcGetSystemTick(void){return clock_tick;}
static void input_poll(void){}
static u32 input_keys_down(void){
 if(scenario==0 && frame==8)return KEY_B; // reverse while entering
 if(scenario==1 && frame==8)alive=false; // HOME-close transition, no present allowed
 if(frame==48)return KEY_DOWN;
 if(frame==50)return KEY_A;
 if(scenario==2 && frame==53)return KEY_B; // failed CIA remains list, exits
 if((scenario==3 || scenario==6) && frame==53)return KEY_B; // QR back must keep service
 if(scenario==3 && frame==55){assert(serving);return KEY_A;}
 if(scenario==3 && frame==57)return KEY_X;
 if(scenario==3 && frame==59){assert(!serving);return KEY_B;}
 if(scenario==4 && (frame==49 || frame==51))return KEY_DOWN;
 if(scenario==4 && frame==54)return KEY_A; // credits HOME-close
 if(frame==70)return KEY_B;
 return 0;
}
#define UI_SWAP_MENU 0u
static u32 ui_swap_keys(u32 k,u32 mask){assert(mask==UI_SWAP_MENU);return k;} // Share is menus only: no dim, no wait
static void ui_swap_enter(void){}
static u32 ui_card_input(flist_t *f,int n,int *sel,u32 k){(void)f;if(k&KEY_DOWN)*sel=(*sel+1)%n;return k;}
static void ui_card_update(flist_t *f,int a,int b){(void)f;(void)a;(void)b;}
static void ui_pause_motion(float p){assert(p>=0 && p<=1);}
static void ui_begin(void){}
static void bb_install_help_run(void){dialogs++;}
static void bb_credits_run(void){dialogs++;alive=false;}
static void video_present_vsync(bool g,void(*a)(void*),void(*b)(void*),void *p){(void)g;(void)a;(void)b;(void)p;assert(alive);paints++;frame++;clock_tick+=16667;}
static const ui_destination *dialog;
const ui_destination *ui_dialog_scene_set(const ui_destination *d){const ui_destination *old=dialog;dialog=d;return old;}
''' +fn('share_stop')+find+r'''
static bool share_start(void){starts++;serving=find_cia();if(serving){server_thread=(void*)1;listen_fd=19;server_stop=false;}return serving;}
'''+state+fn('make_qr')+items+'\nvoid share_about_run(bool game){'+loop+'\n}\n'+fn('share_destination_run')
home=(r/'app/ui/home.c').read_text()
# Use production camera tick, enter, leave and readiness wrappers.
code+='\nstatic void room_sfx(const char *s){(void)s;}\n'
for name in ['share_room_enter','share_room_leave','share_room_tick','share_room_settled']:
 code+=fn(name,home)+'\n'
code+=r'''
static void enter(void *ud){enters++;share_room_enter(ud);}
static void leave(void *ud){leaves++;leave_time=view.t;share_room_leave(ud);}
int main(void){
 FILE *f=fopen("assets/romfs/room/room.bbr","rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);void *data=malloc(n);assert(fread(data,1,n,f)==(size_t)n);fclose(f);
 room_file room;char err[80];assert(room_parse(&room,data,n,err,sizeof(err)));
 room_view_init(&view,&room,ROOM_SHARE,-1);room_pose start=room_view_pose(&view);share_room_enter(&view);
 share_room_tick(&view,.325f);assert(view.mode==ROOM_GO);room_pose halfway=room_view_pose(&view);
 room_pose target;assert(room_pose_of(&room,"share",&target));
 for(int i=0;i<3;i++)assert(fabsf(halfway.pos[i]-(start.pos[i]+target.pos[i])*.5f)<.01);
 room_pose before=room_view_pose(&view);share_room_leave(&view);room_pose after=room_view_pose(&view);
 assert(!memcmp(&before,&after,sizeof before));share_room_tick(&view,.649f);assert(view.mode==ROOM_BACK);share_room_tick(&view,.002f);assert(view.mode==ROOM_HOME);
 for(int hz=30;hz<=120;hz*=2){
  room_view_init(&view,&room,ROOM_SHARE,-1);share_room_enter(&view);int steps=0;
  while(!share_room_settled(&view)){share_room_tick(&view,1.0f/hz);steps++;assert(steps<=hz);}
  assert(steps/(float)hz>=UI_MOTION_CAMERA_S-.0001f && steps/(float)hz<=UI_MOTION_CAMERA_S+1.0f/hz+.0001f);
 }
 room_view_init(&view,&room,ROOM_SHARE,-1);share_room_enter(&view);share_room_tick(&view,.649f);assert(!share_room_settled(&view));share_room_tick(&view,.002f);assert(share_room_settled(&view));assert(view.invitation==1);
 make_qr(RELEASE_URL);assert(qr_size>0);unsigned char saved[sizeof qr];memcpy(saved,qr,sizeof qr);int size=qr_size;
 for(int i=0;i<600;i++){make_qr(RELEASE_URL);assert(qr_size==size && !memcmp(saved,qr,sizeof qr));}
 for(scenario=0;scenario<7;scenario++){
  frame=starts=stops=paints=dialogs=enters=leaves=0;clock_tick=1;alive=true;files=scenario==3 || scenario==6;invalid=0;serving=false;
  room_view_init(&view,&room,ROOM_SHARE,-1);
  ui_destination dest={&view,enter,leave,share_room_tick,share_room_settled,draw_top};share_destination_run(&dest);
  assert(!serving && !dialog && !share_scene && enters==1);
  if(scenario==0){assert(leaves==1 && leave_time<UI_MOTION_CAMERA_S && !starts && view.mode==ROOM_HOME);}
  if(scenario==1){assert(frame==8 && paints==8 && !starts);}
  if(scenario==2){assert(starts==1 && !stops && strstr(cia_note,"wasn't found"));}
  if(scenario==6){assert(starts==1 && stops==1 && view.mode==ROOM_HOME && server_stop);}
  if(scenario==3){assert(starts==1 && stops==1 && !server_thread && listen_fd==-1);share_stop();assert(stops==1);}
  if(scenario==4){assert(dialogs==2 && !alive);}
 }
 int probes=opens;scenario=5;frame=paints=0;clock_tick=1;alive=true;serving=false;starts=0;
 share_about_run(true);assert(share_n==3 && !starts && opens==probes && !serving);
 files=1;invalid=1;assert(!find_cia() && strstr(cia_note,"Invalid or mismatched"));assert(opens>=closes);
 // The CIA's title version as Share words it (tools/build/title-version.py; old decoder said 1.9.12).
 static const struct {u16 v;const char *note;} versions[]={
  {25631,"BreadBox 0.9.1"},
  {25615,"BreadBox 0.9.0. This file differs from the version running here"},
  {32783,"BreadBox 1.0.0. This file differs from the version running here"},
  {25616,"BreadBox test build 25616. This file differs from the version running here"},
  {25630,"BreadBox test build 25630. This file differs from the version running here"},
  {16642,"BreadBox test build 16642. This file differs from the version running here"}};
 // A test build compares counters: its own file is the same, the release file is not.
 static const struct {u16 running,v;int homebrew;const char *note;} test_builds[]={
  {25620,25620,0,"BreadBox test build 25620"},
  {25620,25631,0,"BreadBox 0.9.1. This file differs from the version running here"},
  {25620,25620,1,"BreadBox test build 25620. This file differs from the version running here"},
  {25620,25631,1,"BreadBox 0.9.1"}};
 invalid=0;
 for(unsigned k=0;k<sizeof versions/sizeof versions[0];k++){
  cia_version=versions[k].v;assert(find_cia());
  if(strcmp(cia_note,versions[k].note)){fprintf(stderr,"Share version %u: got '%s', want '%s'\n",versions[k].v,cia_note,versions[k].note);abort();}
 }
 for(unsigned k=0;k<sizeof test_builds/sizeof test_builds[0];k++){
  running_version=test_builds[k].running;cia_version=test_builds[k].v;homebrew=test_builds[k].homebrew;assert(find_cia());
  if(strcmp(cia_note,test_builds[k].note)){fprintf(stderr,"Share on test build %u, file %u, 3dsx %d: got '%s', want '%s'\n",test_builds[k].running,test_builds[k].v,test_builds[k].homebrew,cia_note,test_builds[k].note);abort();}
 }
 free(data);puts("Share production loop PASS: camera .65s/pose/reversal, 600 stable QR frames, missing/invalid CIA, CIA version text 0.9.0/0.9.1/1.0.0/test builds, QR back/X stop, HOME-close entry/dialog");
}
'''
with tempfile.TemporaryDirectory(prefix='share-room-',dir=os.environ.get('TMPDIR')) as tmp:
 p=Path(tmp);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Wno-unused-function','-Werror','-Iinclude/breadbox','-Iapp/ui',str(p/'test.c'),'app/ui/home_view.c','app/ui/room_bbr.c','app/ui/qrcode.c','-lm','-o',str(p/'check')],cwd=r,check=True)
 subprocess.run([str(p/'check')],cwd=r,check=True)
