"""Execute the real ARM indirect-branch stub with synthetic derived dispatch slots."""
from pathlib import Path
import os,shutil,subprocess,tempfile
r=Path(__file__).resolve().parents[2]
s=(r/'gba-core/arm/arm_stub.S').read_text()
a=s.index('defsymbl(arm_indirect_branch_arm)');b=s.index('defsymbl(arm_indirect_branch_thumb)',a)
dual=s[s.index('defsymbl(arm_indirect_branch_dual_arm)'):s.index('1:\n  store_registers_arm()',s.index('defsymbl(arm_indirect_branch_dual_arm)'))]
asm='''
.syntax unified
.arm
.text
#define RAM_TRANSLATION_CACHE_SIZE (384*1024)
#define reg_base r11
#define reg_flags r9
#define reg_dispatch r10
#define save_flags() mrs r9,cpsr
#define restore_flags() msr cpsr_f,r9
#define call_c_function(fn) push {r2,r3,r12,lr}; bl fn; pop {r2,r3,r12,lr}
#define defsymbl(x) .global x; x:
.global invoke
invoke:
 push {r4-r12,lr}
 mov r11,r0
 ldr r10,[r11,#(47*4)]
 mov r0,r1
 msr cpsr_f,r2
 cmp r3,#0
 beq 1f
 msr cpsr_f,r2
 b arm_indirect_branch_dual_arm
1:
 msr cpsr_f,r2
 b arm_indirect_branch_arm
'''+s[a:b]+dual+'''
1:
.global block_lookup_address_arm
block_lookup_address_arm:
 ldr r1,=slow_calls
 ldr r2,[r1]
 add r2,r2,#1
 str r2,[r1]
 ldr r0,=arrived
 bx lr
.global arrived
arrived:
 mrs r0,cpsr
 pop {r4-r12,pc}
.section .note.GNU-stack,"",%progbits
'''
code=r'''
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>
unsigned slow_calls;
extern unsigned invoke(void*,unsigned,unsigned,unsigned);
extern void arrived(void);
int main(void) {
 unsigned size=384*1024;
 unsigned char *m=calloc(1,0xc0000),*regs=m+0xb0000;
 unsigned char *cache=mmap(0,size,7,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(cache!=MAP_FAILED);
 uint32_t *dispatch=calloc(8192,4);
 *(uint32_t*)(regs+47*4)=(uint32_t)dispatch;
 *(uint32_t*)(cache+64)=0xe51ff004;*(uint32_t*)(cache+68)=(uint32_t)arrived;
 __builtin___clear_cache((char*)cache,(char*)cache+72);
 unsigned tags[]={0,0x101,0x1ff,0x201,0x7fff,0x8001,0xffff};unsigned cases=0;
 for(unsigned bx=0;bx<2;bx++)for(unsigned f=0;f<16;f++)for(unsigned i=0;i<7;i++)for(unsigned entry=0;entry<2;entry++)for(unsigned region=2;region<=3;region++)for(unsigned mirror=0;mirror<2;mirror++) {
  unsigned pc=(region<<24)|(bx?0x120:0x123)| (mirror?0x800000:0);
  *(uint16_t*)(regs-0xa8000+0x120)=tags[i];
  if(tags[i]>=0x8000) *(uint32_t*)(cache+size+((int16_t)tags[i]>>1)*8)=entry?64:0;
  dispatch[0x120/4]=tags[i]>=0x8000 && entry?(uint32_t)(cache+64):0;
  slow_calls=0;unsigned flags=invoke(regs,pc,f<<28,bx);
  assert((flags&0xf0000000)==f<<28);
  assert(slow_calls==!(region==3 && tags[i]>=0x8000 && entry));cases++;
 }
 printf("ARM dispatch: %u cases passed (hit/miss/empty slot/mirrors/mode/flags)\n",cases);
}
'''
cc=os.environ.get('ARM_TEST_CC') or shutil.which('arm-linux-gnueabihf-gcc')
qemu=os.environ.get('ARM_TEST_QEMU') or shutil.which('qemu-arm')
if not cc or not qemu:print('set ARM_TEST_CC and ARM_TEST_QEMU');raise SystemExit(77)
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'stub.S').write_text(asm);(p/'check.c').write_text(code)
 subprocess.run([cc,'-O2','-marm',str(p/'stub.S'),str(p/'check.c'),'-static','-o',str(p/'check')],check=True)
 subprocess.run([qemu,str(p/'check')],check=True)

# Exercise the production C publication/invalidation paths as well as the stub.
subprocess.run([__import__('sys').executable,str(Path(__file__).with_name('publication.py'))],check=True)
