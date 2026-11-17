#!/usr/bin/env python3
"""Execute the kept-box handoff: queued reads, owned publication and cancellation."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'app/ui/shelf.c').read_text()
a=s.index('static struct { int done, count, at;')
b=s.index('// After a game:',a)
code=s[a:b]
e=s.index("static void ensure_built(void)");code+=s[e:s.index("static int preload_step",e)]
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX_BOXES 16
#define BOXES_FILE "boxes.dat"
typedef uint64_t u64;
#define SYSCLOCK_ARM11 1000000ull
static u64 ticks=1;
static u64 svcGetSystemTick(void){return ticks;}
typedef struct {char title[32];int color,accent;const void *system;int row;bool fav;unsigned last,seconds;int spine;} shelf_box;
static shelf_box *boxes;
static int nboxes;static unsigned generation;static bool built;
static struct {int nrows,layout,total;} view={.nrows=2};
static struct {bool saved;int nboxes,nrows,off[2],n[2];unsigned generation;} kept={true,3,2,{0,2},{2,1},9};
static bool worker,cancelled,unavailable,pending,complete;
static unsigned key=9;
static unsigned library_key(void){return key;}
static bool bb_index_complete(void){return complete;}
static bool room_pace_return_pending(void){return pending;}
static void first_view(void){}
static void build(void){nboxes=1;boxes[0].color=44;boxes[0].system=(void *)1;boxes[0].row=0;generation=key;}
static void (*queued)(void *);static void *queued_arg;
static bool bb_index_cancelled(void){assert(worker);return cancelled;}
static bool bb_index_defer_visible(void (*fn)(void *),void *arg){assert(!worker && !queued);if(unavailable)return false;queued=fn;queued_arg=arg;return true;}
static FILE *real_open(const char *p,const char *m){assert(worker);return fopen(p,m);}
static size_t real_read(void *p,size_t s,size_t n,FILE *f){assert(worker);return fread(p,s,n,f);}
#define fopen real_open
#define fread real_read
static void log_printf(const char *fmt,...){(void)fmt;}
static void run(void){assert(queued);worker=true;queued(queued_arg);queued=NULL;worker=false;}
'''
main=r'''
#undef fopen
#undef fread
int main(void){
 shelf_box input[3]={{.color=11,.system=(void *)1,.row=0},{.color=22,.system=(void *)1,.row=1},{.color=33,.system=(void *)2,.row=0}};
 FILE *f=fopen(BOXES_FILE,"wb");assert(f);assert(fwrite(input,sizeof(*input),3,f)==3);fclose(f);
 unavailable=true;
 for(int frame=0;frame<3;frame++){
  for(int call=0;call<120;call++){ensure_built();assert(boxes && built && boxes[0].color==44);}
  assert(restore.failures==(unsigned)frame+1);ticks+=100000;
 }
 assert(!kept.saved && restore.failures==3 && !queued);
 unavailable=false;kept.saved=true;
 ensure_built();assert(queued && boxes && boxes[0].color==44);
 for(int i=0;i<120;i++){ensure_built();assert(boxes && boxes[0].color==44);}
 run();assert(restore.data);pending=true;ensure_built();assert(boxes[0].color==44);
 pending=false;key=8;ensure_built();assert(restore.data && boxes[0].color==44);
 // The player changes metadata and the live geometry while the worker is pending.
 boxes[0].fav=true;boxes[0].last=999;boxes[0].seconds=1234;boxes[0].spine=9;
 shelf_box *live=boxes;int layout=view.layout;
 key=9;ensure_built();ensure_built();assert(!restore.data && built && generation==9 && nboxes==1);
 assert(boxes==live && view.layout==layout && boxes[0].color==44);
 assert(boxes[0].fav && boxes[0].last==999 && boxes[0].seconds==1234 && boxes[0].spine==9);
 // Empty live colors adopt the saved tint in place without strips/flow invalidation.
 kept.saved=true;boxes[0].color=0;ensure_built();run();ensure_built();
 assert(boxes==live && boxes[0].color==11 && boxes[0].fav && view.layout==layout);
 kept.saved=true;cancelled=true;ensure_built();run();ensure_built();assert(!kept.saved && boxes);
 // Same generation, incompatible row count on a complete index: drop, never retain forever.
 cancelled=false;kept.saved=true;kept.nrows=1;complete=true;key=9;
 ensure_built();run();ensure_built();assert(!kept.saved && !restore.data && boxes);
 free(boxes);remove(BOXES_FILE);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'test.c').write_text(prefix+code+main)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],cwd=td,check=True)
print('Kept boxes (live metadata/geometry preserved, no relayout, timed retries, complete mismatch drop): immediate catalog, later owned adoption, first-present gate, bounded enqueue fallback, cancellation and stale catalog PASS')
