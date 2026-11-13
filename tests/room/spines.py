#!/usr/bin/env python3
"""Production projection, visibility cache, cell sizing/LRU and bake scheduler.
Only GPU paint, covers, clock and the current pose are host substitutes."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'app/ui/shelf_scene.c').read_text()
ns={"__file__":str(root/"tests/room/camera.py")}
exec((root/'tests/room/camera.py').read_text().split('with tempfile.TemporaryDirectory()')[0],ns)
prefix=ns['prefix'].replace('float x,t,h,w;','float x,t,h,w; const char *title; int cover,row; const void *system; unsigned color;')
prefix=prefix.replace('float end,hmax;','float end,hmax,s;').replace('float cur;','float cur,zoom;')
prefix+=r'''
#include <stdint.h>
#include <stdarg.h>
typedef uint64_t u64;typedef unsigned u32;
typedef struct {int unused;} C3D_Tex,Tex3DS_SubTexture;
#define ATLAS 512
#define MAX_CELLS 64
#define ROOM_BOXES 31
#define BAKES_PER_FRAME 1
#define K 1
#define SYSCLOCK_ARM11 1000000
#define GPU_ALWAYS 0
#define GPU_WRITE_COLOR 0
#define GPU_GEQUAL 0
#define GPU_WRITE_ALL 0
#define SHELF_ALL 0
static struct {
 unsigned frame;int cell_row,cell_key,cw,ch,cols,per_atlas,cells,atlases,atlas_rt[2];bool compact;
 struct {const void *system;int row;unsigned used;bool cover,tinted,logo;}cell[MAX_CELLS];
 struct {unsigned frame,layout;int row,n,indices[ROOM_BOXES],target_n,target[ROOM_BOXES],tested;bool valid;}visible;
} R;
static const shelf_box *held;
static unsigned bakes,analyses;static u64 clock_tick;
static bool available=true;
static u64 svcGetSystemTick(void){return ++clock_tick;}
static unsigned deficit_logs;
static void log_printf(const char *fmt,...){
 if(!strncmp(fmt,"shelf: cells=",13)) {
  va_list a;va_start(a,fmt);(void)va_arg(a,int);int visible=va_arg(a,int);(void)va_arg(a,int);va_end(a);
  assert(visible==(V->zoom>.3f?R.visible.n:0));
 }
 if(!strncmp(fmt,"shelf: missing",14))deficit_logs++;
}
static bool in_room(void){return true;}

static const shelf_box *shelf_pulled_box(const void *v){(void)v;return held;}
static C3D_Tex *covers_small(int c,Tex3DS_SubTexture *s){static C3D_Tex t;(void)c;(void)s;return available?&t:NULL;}
static void average_color(shelf_box *b,C3D_Tex *t,Tex3DS_SubTexture *s){(void)t;(void)s;analyses++;b->color=1;}
static void *logo_of(const void *s){(void)s;return NULL;}
static void bake(shelf_box *b,int c,C3D_Tex *t,Tex3DS_SubTexture *s){(void)b;(void)c;(void)t;(void)s;bakes++;}
static void C2D_SceneBegin(int n){(void)n;}
static void C3D_DepthTest(bool b,int a,int c){(void)b;(void)a;(void)c;}
static void C2D_Flush(void){}
'''
body=ns['body']
a=s.index('static int room_visible_spines(int r, int *indices, int cap)\n{');b=s.index('// the hands and the case',a)
body+=s[a:b]
a=s.index('static int close_row(void)');b=s.index('static C3D_Tex *cell_tex(',a)
body+=s[a:b]
a=s.index('static int cell_for(');b=s.index('// A colour with its HSL',a)
body+=s[a:b]
a=s.index('static void bake_pending(void)');b=s.index('// ---------------------------------------------------------------- the held case',a)
body+=s[a:b]
main=r'''
int main(int argc,char **argv){
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
 void *data=malloc(size);assert(data && fread(data,1,size,f)==(size_t)size);fclose(f);char err[80];assert(room_parse(&file,data,size,err,sizeof(err)));
 shelf_box boxes[1024];shelf_row rows[3]={{0}};V->rows=rows;V->nrows=3;V->level=SHELF_ONE;V->row=0;
 const int counts[]={8,64,1024};
 for(int style=0;style<2;style++)for(unsigned ci=0;ci<3;ci++)
 for(int thick=0;thick<3;thick++)for(int stereo=0;stereo<2;stereo++)for(int end=0;end<2;end++){
  int count=counts[ci];float x=0,t=style?23:10.5f;
  for(int i=0;i<count;i++){float w=thick && i==count/2?(thick==1?80:300):t;boxes[i]=(shelf_box){.x=x+2+w/2,.t=w,.h=125,.w=142,.cover=i,.row=i,.system=&file,.title="fixture"};x+=w+2;}
  V->layout++;rows[0]=(shelf_row){.n=count,.end=x,.hmax=125,.s=2,.box=boxes};
  room_section sec[3]={{0,count,t},{0,0,t},{0,0,t}};room_case_plan_make(&file,sec,3,&plan);
  V->cur=V->sel[0]=end?count-1:0;V->zoom=1;ui_parallax=stereo?1.5f:0;
  float tx,base;assert(cam_target(0,&tx,&base));
  room_pose close=room_section_pose(&file,tx,base,125,0,0),wall=room_wall_pose(&file,plan.used,base);
  memset(&R,0,sizeof(R));R.atlases=1;R.cell_row=-1;R.frame=1;cells_for(0);
  assert(R.cells>=(count<ROOM_BOXES+2?count:ROOM_BOXES+2) && R.cells<=MAX_CELLS);
  // A full atlas of hidden items must not prevent visible admissions.
  for(int c=0;c<R.cells;c++){R.cell[c].used=1;R.cell[c].system=&file;R.cell[c].row=end?c:count-1-c;}
  // Wall art is generic: only destination cells are warmed. The old
  // counter falsely compared the unpainted wall list against that target.
  V->zoom=0;current_camera=wall;
  for(int frame=0;frame<200;frame++){R.frame++;bake_pending();}
  for(int j=0;j<R.visible.target_n;j++)assert(cell_of(&boxes[R.visible.target[j]])>=0);
  assert(!deficit_logs);
  for(int frame=0;frame<150;frame++){
   float k=fminf(frame/60.0f,1);for(int j=0;j<9;j++)((float *)&current_camera)[j]=((float *)&wall)[j]*(1-k)+((float *)&close)[j]*k;
   V->zoom=k;R.frame++;analyses=0;bake_pending();assert(analyses<=1);
   assert(R.visible.tested<128);
   if(frame==0)assert(R.visible.target_n>0); // destination warm-up starts on wall // culled index range, never projects all 1024 records
   int a[31],b[31];u64 before=clock_tick;
   int na=room_visible_spines(0,a,31),nb=room_visible_spines(0,b,31);
   assert(clock_tick==before && na==nb && !memcmp(a,b,na*sizeof(*a))); // both eyes reuse scheduler list
  }
  int visible[31],n=room_visible_spines(0,visible,31);assert(n>0 && n<=31);
  for(int j=0;j<n;j++)assert(cell_of(&boxes[visible[j]])>=0);
  unsigned settled=bakes;
  shelf_box alien={0};held=&alien;R.frame++;bake_pending();held=NULL; // foreign pointer never subtracted
  for(int frame=0;frame<90;frame++){R.frame++;bake_pending();}
  assert(bakes==settled); // complete coverage, no eviction/repaint churn
  // A held case outside the on-screen interval keeps its existing cell.
  held=&boxes[(int)V->cur];int c=cell_of(held);assert(c>=0);
  for(int frame=0;frame<50;frame++){R.frame++;bake_pending();assert(cell_of(held)==c);}
  held=NULL;R.frame++;bake_pending();assert(cell_of(&boxes[(int)V->cur])>=0);
 }
 free(data);puts("Spines: real clamped stereo visibility/cache, single-atlas thick-box capacity, glide, short/1024 rows, held/put-back, eviction and no churn PASS");
}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'test.c').write_text(prefix+body+main)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-sign-compare','-fsanitize=address,undefined','-I'+str(root/'include/breadbox'),'-I'+str(root/'platform/3ds'),str(p/'test.c'),str(root/'app/ui/room_bbr.c'),'-lm','-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test'),str(root/'assets/romfs/room/room.bbr')],check=True)
