#!/usr/bin/env python3
"""Run production RAM-tag detection for interpreted writes, including mirrors."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[2]
s=(r/'gba-core/gba_memory.c').read_text();a=s.index('bool vram_interpreter_active');b=s.index('/* Sound */',a);helper=s[a:b]
code='''#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
typedef uint8_t u8;typedef uint32_t u32;
u8 ewram[0x80000],iwram[0x10000];
'''+helper+'''
int main(void) {
 for(unsigned region=2;region<=3;region++) {
  u8 *tags=region==2?ewram+0x40000:iwram;u32 base=region<<24;
  for(unsigned bytes=1;bytes<=4;bytes*=2) {
   for(unsigned lane=0;lane<bytes;lane++) {
    tags[0x100+lane]=1;vram_interpreter_active=true;vram_interpreter_smc=false;
    vram_interpreter_mark_write(base+0x100,bytes);assert(vram_interpreter_smc);
    vram_interpreter_smc=false;vram_interpreter_mark_write(base+0x800100,bytes);assert(vram_interpreter_smc);
    vram_interpreter_smc=false;vram_interpreter_mark_write(base+0x200,bytes);assert(!vram_interpreter_smc);
    vram_interpreter_active=false;vram_interpreter_mark_write(base+0x100,bytes);assert(!vram_interpreter_smc);
    tags[0x100+lane]=0;
   }
  }
 }
 vram_interpreter_active=true;vram_interpreter_smc=false;vram_interpreter_mark_write(0x06000000,4);assert(!vram_interpreter_smc);
 puts("VRAM bridge SMC: byte/halfword/word writes, RAM mirrors, code/data and inactive gating passed");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(code)
 subprocess.run(['cc','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
