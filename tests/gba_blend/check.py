#!/usr/bin/env python3
"""Execute production blend resolver with/without the derived palette cache."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[2];s=(r/'gba-core/video.cc').read_text()
a=s.index('#define BLND_MSK');b=s.index('// Applies brighten/darken effect to a bunch',a)
code=r'''
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <algorithm>
typedef uint32_t u32;typedef uint16_t u16;
#define MIN(a,b) std::min((unsigned)(a),(unsigned)(b))
#define CPU_TIME(category) ((void)0)
static u16 palette_ram_converted[512];static bool cpu_blend;static u32 gba_palette_generation;
enum {REG_BLDALPHA,REG_BLDY};static u16 regs[2];
static u16 read_ioreg(unsigned n){return regs[n];}
'''+s[a:b]+r'''
static unsigned rng=42;
static unsigned random32(){return rng=rng*1664525+1013904223;}
template<blendtype mode,bool obj>void check(unsigned start,unsigned end,u32 *src){
 u16 a[240],b[240];memset(a,0xa7,sizeof(a));memset(b,0xa7,sizeof(b));
 cpu_blend=false;merge_blend<mode,obj>(start,end,a,src);
 cpu_blend=true;merge_blend<mode,obj>(start,end,b,src);
 assert(!memcmp(a,b,sizeof(a)));
}
int main(){
 u32 src[240];
 for(unsigned iteration=0;iteration<4096;iteration++){
  if(iteration%3==0){for(unsigned i=0;i<512;i++)palette_ram_converted[i]=random32();gba_palette_generation++;}
  else if(iteration%3==1){palette_ram_converted[random32()%512]=random32();gba_palette_generation++;}
  regs[0]=(iteration&31)|(((iteration>>5)&31)<<8);regs[1]=iteration&31;
  for(unsigned x=0;x<240;x++)src[x]=random32();
  unsigned start=random32()%240,end=start+random32()%(241-start);
  check<OBJ_BLEND,false>(start,end,src);check<OBJ_BLEND,true>(start,end,src);
  check<BLEND_ONLY,false>(start,end,src);check<BLEND_ONLY,true>(start,end,src);
  check<BLEND_BRIGHT,false>(start,end,src);check<BLEND_BRIGHT,true>(start,end,src);
  check<BLEND_DARK,false>(start,end,src);check<BLEND_DARK,true>(start,end,src);
 }
 puts("PASS: all coefficients, saturation/brightness, semitransparency, palette edits, span edges");
}
'''
with tempfile.TemporaryDirectory(prefix='bb-blend-')as td:
 p=Path(td)/'check.cc';p.write_text(code)
 for flags in [[],['-DARM_ARCH'],['-DARM_ARCH','-DUSE_XBGR1555_FORMAT']]:
  subprocess.run(['c++','-O2','-Wall','-fsanitize=address,undefined',*flags,str(p),'-o',str(Path(td)/'check')],check=True)
  subprocess.run([str(Path(td)/'check')],check=True)
