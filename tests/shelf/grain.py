#!/usr/bin/env python3
"""Production missing-grain job: deferred file I/O and texture lifetime handoff."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'app/ui/shelf_scene_wood.c').read_text();s=s[s.index('static struct {u8 *buf;'):s.index('// White radial light')]
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
typedef uint8_t u8;typedef uint64_t u64;typedef uintptr_t u32;
typedef struct {void *data;size_t size;} C3D_Tex;
static struct {C3D_Tex grain;} shelf_scene_state;
#define GRAIN_FILE "cache/grain"
#define PSX3D_DIR "."
#define G_COUNT 9
#define SYSCLOCK_ARM11 1000000
#define CUR_PROCESS_HANDLE 0
static bool cancelled,unavailable;static int regions;
static const short shelf_scene_grain_region[9][4]={[0 ... 8]={0,0,4,4}};
static bool bb_index_cancelled(void){return cancelled;}
static u64 svcGetSystemTick(void){return 0;}
static void log_printf(const char *fmt,...){(void)fmt;}
static void svcStoreProcessDataCache(int h,u32 p,size_t n){(void)h;(void)p;(void)n;}
typedef struct {float knot[6][3];} wood_state;
static void make_grain_part(u8 *g,int part,int first,int end,wood_state *state){(void)state;assert(end-first<=2);for(int row=first;row<end;row++)g[part*4+row]=(u8)(part+1);regions++;}
static void (*job)(void *);static void *arg;
static bool bb_index_defer_low(void (*fn)(void *),void *a){if(unavailable)return false;assert(!job);job=fn;arg=a;return true;}
static void run(void){assert(job);void (*fn)(void *)=job;job=NULL;fn(arg);}
'''
main=r'''
int main(void){
 shelf_scene_state.grain=(C3D_Tex){malloc(262144),262144};shelf_scene_grain_start(&shelf_scene_state.grain);
 assert(job && !regions);free(shelf_scene_state.grain.data);shelf_scene_state.grain.data=NULL;
 run();assert(regions==1 && grain_job.buf); /* worker never borrows a released texture */
 shelf_scene_state.grain.data=malloc(shelf_scene_state.grain.size);memset(shelf_scene_state.grain.data,128,shelf_scene_state.grain.size);
 for(int i=0;i<100 && grain_job.buf;i++){shelf_scene_grain_step();if(job)run();}
 assert(!grain_job.buf && regions==18);
 for(int i=0;i<9;i++)assert(((u8 *)shelf_scene_state.grain.data)[i*4]==i+1);
 assert(((u8 *)shelf_scene_state.grain.data)[100]==128);
 shelf_scene_grain_start(&shelf_scene_state.grain);assert(job);run();shelf_scene_grain_step();assert(regions==18 && !grain_job.buf);
 shelf_scene_grain_start(&shelf_scene_state.grain);cancelled=true;run();assert(grain_job.state==0 && grain_job.buf);
 free(grain_job.buf);grain_job.buf=NULL;cancelled=false;unavailable=true;
 shelf_scene_grain_start(&shelf_scene_state.grain);assert(!job);
 for(int i=0;i<100 && grain_job.buf;i++)shelf_scene_grain_step();
 assert(!grain_job.buf && regions==54);
 for(int i=0;i<9;i++)for(int row=0;row<4;row++)assert(((u8 *)shelf_scene_state.grain.data)[i*4+row]==i+1);
 free(shelf_scene_state.grain.data);
 puts("shelf grain: deferred cold generation/cache, released texture, warm cache, cancellation PASS");
}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'check.c').write_text(prefix+s+main)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'check.c'),'-o',str(p/'check')],check=True)
 subprocess.run([str(p/'check')],cwd=p,check=True)

# Exercise the real procedural renderer too: row slicing must preserve every
# texel (including nails), not merely the worker's handoff state above.
source=(root/'app/ui/shelf_scene_wood.c').read_text()
header=(root/'app/ui/shelf_scene_internal.h').read_text()
regions=header[header.index('enum { G_BOARD'):header.index('extern const short shelf_scene_grain_region')]+source[source.index('const short shelf_scene_grain_region'):source.index('// ----')]
texel=header[header.index('static inline u32 texel'):]
renderer=texel[:texel.index('\n}\n')+3]+source[source.index('static float ih('):source.index('/* Missing grain used')]
math_prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
typedef uint8_t u8;typedef uint32_t u32;
#define GRAIN_W 512
#define GRAIN_H 512
static bool bb_index_cancelled(void){return false;}
static float clampf(float x,float a,float b){return x<a?a:x>b?b:x;}
static float lerpf(float a,float b,float t){return a+(b-a)*t;}
static float rnd(int i){float x=sinf(i*127.1f+311.7f)*43758.5453f;return x-floorf(x);}
'''
render_main=r'''
int main(void){
 size_t n=GRAIN_W*GRAIN_H;u8 *whole=malloc(n),*chunked=malloc(n);
 memset(whole,128,n);
 wood_state whole_state={0};
 for(int p=0;p<G_COUNT;p++)make_grain_part(whole,p,0,shelf_scene_grain_region[p][3],&whole_state);
 const int chunks[]={1,2,17};
 for(unsigned c=0;c<sizeof(chunks)/sizeof(chunks[0]);c++){
  memset(chunked,128,n);
  wood_state states[G_COUNT]={0};
  // Deliberately interleave parts: knots belong to each caller, not last job.
  for(int row=0;row<512;row+=chunks[c])for(int p=G_COUNT-1;p>=0;p--)
   if(row<shelf_scene_grain_region[p][3])make_grain_part(chunked,p,row,row+chunks[c],&states[p]);
  assert(!memcmp(whole,chunked,n));
 }
 free(whole);free(chunked);puts("real shelf grain: whole == chunks of 1/2/17 rows, every atlas byte PASS");
}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'render.c').write_text(math_prefix+regions+renderer+render_main)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'render.c'),'-lm','-o',str(p/'render')],check=True)
 subprocess.run([str(p/'render')],check=True)
