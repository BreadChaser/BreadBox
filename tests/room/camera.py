#!/usr/bin/env python3
"""Project the production shelf camera at both ends through the real stereo camera."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'app/ui/shelf_scene.c').read_text()
h=(root/'app/ui/shelf_scene_internal.h').read_text()
body=h[h.index('// The boxes of one shelf flowed'):h.index('extern shelf_scene_flow shelf_scene_flowed;')]
a=h.index('static inline float row_cur(');body+=h[a:h.index('\n',a)+1]
a=s.index('static float row_total(');b=s.index('// once per frame:',a)
body+=s[a:b]
a=s.index('static bool room_project(');b=s.index('// The wall',a)
body+=s[a:b]
body+='\nstatic room_pose current_camera; static float ui_parallax=1.5f;\nstatic room_pose room_cam(void){return current_camera;}\n'
a=s.index('static void room_spine_xrange(');b=s.index('int shelf_scene_visible_spines(int r, int *indices, int cap)\n{',a)
body+=s[a:b]
prefix=r'''
#include "room.h"
#include "ui_motion.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PSX3D_COLD
#define F 600.0f
#define X0 0
#define SHELF_ONE 1
typedef struct { float x,t,h,w; } shelf_box;
typedef struct { int n;float end,hmax;shelf_box *box; } shelf_row;
static struct {int nrows,level,row,hsel;unsigned layout;float cur,zoom,pull;int sel[8];shelf_row *rows;} view,*shelf_scene_view=&view;
static room_file file;static room_case_plan plan;
static const room_file *room_scene_file(void){return &file;}
static const room_case_plan *room_scene_case_plan(void){return &plan;}
static float clampf(float x,float lo,float hi){return x<lo?lo:x>hi?hi:x;}
static float lerpf(float a,float b,float k){return a+(b-a)*k;}
'''
main=r'''
// Frozen pre-culling collector (b4bc10b1): intentionally projects every box.
static int all_box_oracle(int r, int *indices, int cap)
{
 int cubby; room_case_geo g; float off;
 if (!row_place(r,row_cur(r),&cubby,&g,&off)) return 0;
 const shelf_row *row=&shelf_scene_view->rows[r];
 room_pose p=room_cam();
 int n=0, cnt=row->n<FLOW_MAX?row->n:FLOW_MAX;
 float distances[64];
 if(cap>64)cap=64;
 for(int i=0;i<cnt;i++) {
  if(shelf_scene_flowed.cub[i]!=cubby-shelf_scene_flowed.first)continue;
  const shelf_box *b=&row->box[i];
  float x=g.xin0+shelf_scene_flowed.lx[i]-off;
  if(x-b->t/2<g.xl || x+b->t/2>g.xr)continue;
  bool visible=false;float distance=1e9f;
  for(int eye=-1;eye<=1;eye+=2) {
   float a,y,z,y2;
   float e=eye*(ui_parallax/1.5f)*p.eye*.5f;
   if(!room_project(&p,e,x-b->t/2,g.base+b->h/2,5,&a,&y) ||
      !room_project(&p,e,x+b->t/2,g.base+b->h/2,5,&z,&y2))continue;
   if(fmaxf(a,z)>=-12 && fminf(a,z)<=412 && y>=-24 && y<=264)visible=true;
   distance=fminf(distance,fabsf((a+z)/2-200));
  }
  if(!visible)continue;
  int at=n;
  while(at>0 && distance<distances[at-1])at--;
  if(at>=cap)continue;
  if(n<cap)n++;
  for(int j=n-1;j>at;j--){indices[j]=indices[j-1];distances[j]=distances[j-1];}
  indices[at]=i;distances[at]=distance;
 }
 return n;
}

static float project(const room_pose *p,float eye,float x,float y,float z){
 float m[16];room_camera(p,eye,m,NULL,NULL,NULL);
 float sy=m[4]*x+m[5]*y+m[6]*z+m[7],w=m[12]*x+m[13]*y+m[14]*z+m[15];
 return 200-200*sy/w;
}
int main(int argc,char **argv){
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
 void *data=malloc(size);assert(data && fread(data,1,size,f)==(size_t)size);fclose(f);char err[80];assert(room_parse(&file,data,size,err,sizeof(err)));
 shelf_box boxes[1024];shelf_row rows[3]={{0}};shelf_scene_view->rows=rows;shelf_scene_view->nrows=3;shelf_scene_view->level=SHELF_ONE;
 const int counts[]={0,1,2,3,8,15,30,31,100,1024};
 for(int style=0;style<2;style++)for(int mix=0;mix<4;mix++)for(unsigned n=0;n<sizeof(counts)/sizeof(*counts);n++){
  int count=counts[n];float x=0,t=style?23:10.5f;
  for(int i=0;i<count;i++){float thick=mix && i%3==0?(mix==1?25:mix==2?80:300):t;boxes[i]=(shelf_box){x+2+thick/2,thick,125,style?125:142};x+=2+thick;}
  for(int row=0;row<3;row++){
   shelf_scene_view->layout++;shelf_scene_view->row=row;rows[row]=(shelf_row){count,x,125,boxes};
   room_section sec[3]={{0,count,t},{0,count,t},{0,count,t}};room_case_plan_make(&file,sec,3,&plan);
   for(int end=0;end<2;end++){
    shelf_scene_view->cur=end && count?count-1:0;float tx,base;assert(cam_target(row,&tx,&base));assert(isfinite(tx));
    int cub;room_case_geo g;float off;assert(row_place(row,shelf_scene_view->cur,&cub,&g,&off));
    room_pose p=room_section_pose(&file,tx,base,125,0,0);
    current_camera=p;
    // Compare identical poses to the old all-box projection, independent of
    // the new clip-plane interval and binary searches. Test sets, not order.
    room_pose wall=room_wall_pose(&file,plan.used,base);
    for(int stereo=0;stereo<2;stereo++)for(int step=0;step<=10;step++) {
     ui_parallax=stereo?1.5f:0;
     float k=step/10.0f;current_camera=room_mix(&wall,&p,k);
     int fast[64],slow[64];int nf=room_collect_spines(row,fast,64),ns=all_box_oracle(row,slow,64);
     assert(nf==ns);
     for(int i=0;i<ns;i++){bool found=false;for(int j=0;j<nf;j++)if(slow[i]==fast[j])found=true;assert(found);}
    }
    ui_parallax=1.5f;current_camera=p;
    int visible[64],nv=room_collect_spines(row,visible,64);
    int expected=0;
    for(int k=0;k<count;k++){
     float x=g.xin0+shelf_scene_flowed.lx[k]-off;
     if(shelf_scene_flowed.cub[k]!=cub-shelf_scene_flowed.first || x-boxes[k].t/2<g.xl || x+boxes[k].t/2>g.xr)continue;
     bool on=false;
     for(int eye=-1;eye<=1;eye+=2){float l=project(&p,eye*p.eye/2,x-boxes[k].t/2,base+62.5f,5),r=project(&p,eye*p.eye/2,x+boxes[k].t/2,base+62.5f,5);if(fmaxf(l,r)>=-12 && fminf(l,r)<=412)on=true;}
     if(on){expected++;bool found=false;for(int v=0;v<nv;v++)if(visible[v]==k)found=true;assert(found);}
    }
    assert(nv==expected && nv<=31);
    if(count>=100 && mix<2)assert(nv>1 && nv<count); // clamped ends exclude hidden cubby boxes

    if(count && mix<2){
     int i=(int)shelf_scene_view->cur;float bx=g.xin0+shelf_scene_flowed.lx[i]-off;
     for(int eye=-1;eye<=1;eye++){
      float e=eye*p.eye/2;
      for(int side=-1;side<=1;side+=2){float sx=project(&p,e,bx+side*boxes[i].t/2,base+62.5f,5);assert(sx>=10 && sx<=390);}
      // First sees the outer upright; last sees its upright or short-row bookend.
      float upr=g.xin0-g.xl,edge=!end?g.xl-upr:fminf(g.xr+upr,g.xin0+shelf_scene_flowed.cw[0]+8);
      float sx=project(&p,e,edge,base+62.5f,0);assert(sx>=10 && sx<=390);
     }
    }
    for(int hz=30;hz<=60;hz+=30){float px=g.cx,v=0,last=px;
     for(int k=0;k<hz*3;k++){ui_motion_settle(&px,&v,tx,UI_MOTION_SETTLE_RATE,1.0f/hz);assert(tx>=g.cx?px>=last-.0001f && px<=tx+.0001f:px<=last+.0001f && px>=tx-.0001f);last=px;}
     assert(fabsf(px-tx)<.001f);
    }
   }
  }
 }
 free(data);puts("Shelf camera: empty/short/overflow/max rows, mixed package widths, both ends, three cubbies, mono/stereo, all-box culling oracle (wall/mid-glide/close, 80/300 mm), monotonic 30/60 Hz settle PASS");return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'test.c').write_text(prefix+body+main)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined','-I'+str(root/'include/breadbox'),'-I'+str(root/'platform/3ds'),str(p/'test.c'),str(root/'app/ui/room_bbr.c'),'-lm','-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test'),str(root/'assets/romfs/room/room.bbr')],check=True)
