#!/usr/bin/env python3
"""Exercise production ARM VRAM fetches with the Classic NES SMC probe."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'gba-core/cpu.cc').read_text()
a=s.index('       if ((reg[REG_PC] >> 24) == 0x06)')
b=s.index('       condition = opcode >> 28;',a)
fetch=s[a:b]
a=s.index('       /* Taken control transfers refill')
b=s.index('       #ifdef TRACE_INSTRUCTIONS',a)
refill=s[a:b]
code=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
using u32=uint32_t;
using u8=uint8_t;
#define REG_PC 15
u32 reg[16],vram_prefetch_pc=~0u,vram_prefetch[2],opcode;
alignas(4) u8 vram[0x18000],rom[0x8000];
u8 *pc_address_block;
u32 readaddress32(u8 *base,u32 offset) {u32 v;memcpy(&v,base+offset,4);return v;}
void word(unsigned offset,u32 v) {memcpy(vram+offset,&v,4);}
u32 fetch(u32 pc) {reg[REG_PC]=pc;unsigned offset=pc&0x1ffff;if(offset>=0x18000)offset-=0x8000;pc_address_block=(pc>>24)==6?vram+(offset&~0x7fff):rom;
'''+fetch+r'''
return opcode;
}
void executed(u32 instruction) {opcode=instruction;
'''+refill+r'''
}
int main() {
 // Store at 0x260 overwrites 0x268, which is already in flight.
 word(0x260,0xe58e0000);word(0x264,0xe3a010ff);word(0x268,0xe3a010ff);
 assert(fetch(0x06000260)==0xe58e0000);word(0x268,0xe3a01000);
 assert(fetch(0x06000264)==0xe3a010ff);assert(fetch(0x06000268)==0xe3a010ff);
 // A later fetch after a branch sees the replacement.
 executed(0xeaffffff);assert(fetch(0x06000268)==0xe3a01000);
 // A taken branch to the sequential PC must also refill.
 word(0x270,1);word(0x274,2);fetch(0x06000270);word(0x274,3);
 executed(0xeaffffff);assert(fetch(0x06000274)==3);
 // Mirror and page boundary fetches use the correct VRAM backing words.
 vram_prefetch_pc=~0u;word(0x7ffc,4);word(0x8000,5);word(0x8004,6);
 assert(fetch(0x06007ffc)==4);word(0x8004,7);
 assert(fetch(0x06008000)==5);assert(fetch(0x06008004)==6);
 vram_prefetch_pc=~0u;word(0x17ffc,8);word(0x10000,9);word(0x10004,10);
 assert(fetch(0x06017ffc)==8);assert(fetch(0x06018000)==9);assert(fetch(0x06018004)==10);
 puts("ARM VRAM prefetch: SMC, branch refill, page and mirror boundaries passed");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.cc').write_text(code)
 subprocess.run(['g++','-std=c++11','-fsanitize=address,undefined','-g',str(p/'test.cc'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
