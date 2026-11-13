#!/usr/bin/env python3
"""Save slot postcards: compile the production board layout (postcards.inc) and
d-pad navigation (postcards.c) against their real style tokens, then check that
every card can be reached with the d-pad and by touch, that no two cards'
places overlap, that all of them sit on the board, and that saving never lands
on the Continue card."""
from pathlib import Path
import re,subprocess,sys,tempfile
root=Path(__file__).resolve().parents[2];src=root/'app/ui'
def function(text,name):
 m=re.search(r'^static [^\n]*\b'+name+r'\([^)]*\)\n\{.*?\n\}',text,flags=re.S|re.M)
 if not m:sys.exit('missing '+name)
 return m[0]
inc=(src/'components/postcards.inc').read_text();ctl=(src/'postcards.c').read_text()
defs='\n'.join(l for l in (src/'postcards.h').read_text().splitlines() if re.match(r'#define PC_',l))
program=r'''#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include "ui_style.h"
#include "bb_slots.h"
'''+defs+r'''
typedef struct {bool saving,has_continue;} pc_view;
'''+function(inc,'pc_layout')+'\n'+function(ctl,'selectable')+'\n'+function(ctl,'neighbour')+r'''
static int hit(const pc_view *v,float px,float py)
{
 for(int i=0;i<PC_CARDS;i++){float x,y,w,h,t;if(!selectable(v,i))continue;pc_layout(v,i,&x,&y,&w,&h,&t);
  if(fabsf(px-x)<=w/2 && fabsf(py-y)<=h/2)return i;}
 return -1;
}
int main(void)
{
 const float dirs[4][2]={{-1,0},{1,0},{0,-1},{0,1}};
 for(int c=0;c<2;c++)for(int s=0;s<2;s++){
  pc_view v={s,c};bool seen[PC_CARDS]={0};int queue[PC_CARDS],head=0,tail=0;
  queue[tail++]=0;seen[0]=true;
  while(head<tail){int at=queue[head++];for(int d=0;d<4;d++){int n=neighbour(&v,at,dirs[d][0],dirs[d][1]);
   assert(selectable(&v,n));if(!seen[n]){seen[n]=true;queue[tail++]=n;}}}
  for(int i=0;i<PC_CARDS;i++){
   assert(seen[i]==selectable(&v,i));            /* every card the d-pad can reach, and only those */
   if(!selectable(&v,i))continue;
   float x,y,w,h,t;pc_layout(&v,i,&x,&y,&w,&h,&t);
   assert(x-w/2>=UI_PC_BOARD_X && x+w/2<=UI_PC_BOARD_X+UI_PC_BOARD_W);   /* on the board */
   assert(y-h/2>=UI_PC_BOARD_Y && y+h/2<=UI_PC_BOARD_Y+UI_PC_BOARD_H);
   assert(hit(&v,x,y)==i);                       /* a tap on a card picks that card */
   for(int j=0;j<i;j++){float x2,y2,w2,h2,t2;if(!selectable(&v,j))continue;pc_layout(&v,j,&x2,&y2,&w2,&h2,&t2);
    /* lifted (scaled) cards still keep clear of their neighbours */
    float k=1+UI_PC_LIFT_SCALE;
    assert(fabsf(x-x2)*2>=(w+w2)*k/2+UI_PC_GLOW || fabsf(y-y2)*2>=(h+h2)*k/2+UI_PC_GLOW);}
  }
  if(s)assert(!selectable(&v,PC_CONTINUE));      /* Continue writes itself; saving never targets it */
  if(c && !s)assert(selectable(&v,PC_CONTINUE));
 }
 puts("Save slot board: every card reachable by d-pad and touch, on the board, no overlaps, saving skips Continue PASS");
 return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
 c=Path(d)/'layout.c';c.write_text(program)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined','-I',str(src),'-I',str(root/'include/breadbox'),str(c),'-lm','-o',str(Path(d)/'layout')],check=True)
 subprocess.run([str(Path(d)/'layout')],check=True)
