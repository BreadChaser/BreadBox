from pathlib import Path
import os, shutil, subprocess, tempfile
r=Path(__file__).resolve().parents[2];s=(r/'gba-core/arm/arm_emit.h').read_text()
macros=['write32','arm_relative_offset','reg_a0','reg_a1','reg_a2','reg_base','reg_flags','reg_rv','mem_reg','generate_save_flags','generate_restore_flags','generate_load_memreg','arm_generate_store_reg','arm_generate_load_reg','generate_add_imm','generate_sub_imm','generate_branch_filler','generate_branch_patch_conditional','arm_imm_lsl_to_rot','word_bit_count','generate_function_far_call','arm_load_imm_32bit']
pre='''#define _GNU_SOURCE
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/mman.h>
typedef uint32_t u32;typedef uint8_t u8;
#define REG_SAVE 26
#define REG_USERDEF 32
#define armfn_block_load_address 16
#define armfn_block_store_address 17
void arm_block_load_address(void),arm_block_store_address(void);
#include "arm/arm_codegen.h"
#include "arm/arm_dpimacros.h"
static u32 bit_count[256];
static u32 arm_register_allocation[64];
'''
for m in macros:
 a=s.index('#define '+m);b=s.index('\n',a)
 while s[b-1]=='\\':b=s.index('\n',b+1)
 pre+=s[a:b]+'\n'
for name,end in [('u32 arm_prepare_store_reg(u32 scratch_reg','u32 thumb_prepare_store_reg'),('u32 arm_prepare_load_reg(u8 **tptr','u32 arm_prepare_load_reg_pc')]:
 a=s.index(name,s.index('u32 arm_register_allocation[]'));b=s.index(end,a+1);pre+=s[a:b]+'\n'
a=s.index('static u8 *arm_block_ram(');b=s.index('#define arm_block_sbit_no',a);pre+=s[a:b]
pre+='''
static unsigned tests;
static void check(unsigned region,unsigned off,u32 list,unsigned rn,bool load,int delta,bool tagged,u32 flags) {
 unsigned count=__builtin_popcount(list);size_t limit=region==3||region>=8?0x8000:region==6?0x18000:0x40000;
 u8 *mem=calloc(1,0xC0000);u32 *regs=(u32*)(mem+0xB0000);
 unsigned char *ram=region==3?mem+0x10000:mem+0x30000;
 unsigned char *tags=region==3?ram-0x8000:ram+0x40000;
 if(region==6)ram=mem+0x18000;
 for(unsigned j=0;j<limit;j++)ram[j]=(j*31+7)&255;
 for(unsigned i=0;i<16;i++)regs[i]=0x12340000+i;
 u32 initial[16];memcpy(initial,regs,sizeof(initial));
 regs[48]=(u32)arm_block_load_address;regs[49]=(u32)arm_block_store_address;
 regs[rn]=(region<<24)|off;initial[rn]=regs[rn];regs[REG_SAVE]=regs[rn]&~3u;
 size_t offset=(off&~3u)&(region==6?0x1ffff:limit-1);if(region==6&&offset>=0x18000)offset-=0x8000;
 if(region>=8) ((uint32_t*)((u8*)regs+0xd00))[regs[REG_SAVE]>>15]=tagged?0:(uint32_t)ram;
 if(region<6&&tagged)tags[offset+(tagged==1?0:tagged==2?count*2:count*4-1)]=1;
 u8 *code=mmap(NULL,4096,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_ANONYMOUS|MAP_PRIVATE,-1,0);assert(code!=MAP_FAILED);
 u8 *translation_ptr=code;
 write32(0xe92d4ff0); /* push r4-r11,lr */
 ARM_MOV_REG_REG(0,11,0);
 for(unsigned i=0;i<15;i++)if(arm_register_allocation[i]!=mem_reg) {ARM_LDR_IMM(0,arm_register_allocation[i],11,i*4);}
 ARM_MOV_REG_IMM(0,0,flags>>28,4); /* rotate4 -> bits31:28 */
 ARM_MSR_REG(0,ARM_PSR_F,0,ARM_CPSR);
 u8 *end=arm_block_ram(&translation_ptr,list,rn,load,delta,0x08001240);
 if(count<3 || rn==15 || (!load && (list&(1u<<rn)))) {assert(!end);tests++;munmap(code,4096);free(mem);return;}
 assert(end);
 ARM_MOV_REG_IMM(0,10,0,0);u8 *finish;generate_branch_filler(ARMCOND_AL,finish);
 generate_branch_patch_conditional(end,translation_ptr);ARM_STR_IMM(0,reg_a0,11,136);ARM_MOV_REG_IMM(0,10,1,0);
 generate_branch_patch_conditional(finish,translation_ptr);
 ARM_STR_IMM(0,10,11,128);
 ARM_MRS_CPSR(0,0);ARM_STR_IMM(0,0,11,132);
 for(unsigned i=0;i<15;i++)if(arm_register_allocation[i]!=mem_reg) {ARM_STR_IMM(0,arm_register_allocation[i],11,i*4);}
 write32(0xe8bd8ff0); /* pop r4-r11,pc */
 __builtin___clear_cache((char*)code,(char*)translation_ptr);
 ((void(*)(void*))code)(regs);
 bool fast=offset+count*4<=limit && (((region==2||region==3)&&(load||!tagged)) || (region==6&&!load) || (region>=8&&region<=12&&load&&!tagged));
 if(regs[32]!=fast || (regs[33]&0xF0000000)!=flags) {fprintf(stderr,"path/flags region=%u off=%x load=%u tag=%u got=%u flags=%x expected=%x\\n",region,off,load,tagged,regs[32],regs[33],flags);abort();}
 if(fast) {
  unsigned j=0;for(unsigned i=0;i<16;i++)if(list&(1u<<i)) {
   u32 word;memcpy(&word,ram+offset+j*4,4);
   if(load) {if((i==15?regs[34]:regs[i])!=word){fprintf(stderr,"data region=%u off=%x list=%x rn=%u load=%u i=%u got=%x expected=%x\\n",region,off,list,rn,load,i,i==15?regs[34]:regs[i],word);abort();}}else assert(word==(i==15?0x0800124c:initial[i]));j++;
  }
  if(!(list&(1u<<rn)))assert(regs[rn]==initial[rn]+(load?0:delta));
 }
 if(!fast)for(unsigned i=0;i<15;i++)assert(regs[i]==initial[i]);
 tests++;munmap(code,4096);free(mem);
}
int main(void) {
 for(unsigned i=0;i<256;i++)bit_count[i]=__builtin_popcount(i);
 for(unsigned i=0;i<64;i++)arm_register_allocation[i]=mem_reg;
 arm_register_allocation[15]=reg_a0;
 unsigned guest[]={0,1,6,9,12,14};for(unsigned i=0;i<6;i++)arm_register_allocation[guest[i]]=i+3;
 for(unsigned region=2;region<=13;region++)if(region==2||region==3||region==6||region==8||region==9||region==12||region==13)for(unsigned mode=0;mode<2;mode++)for(unsigned tagged=0;tagged<4;tagged++)for(unsigned f=0;f<16;f++)for(unsigned rnidx=0;rnidx<4;rnidx++)for(unsigned n=0;n<7;n++)for(int d=-32;d<=32;d+=32) {
  unsigned limit=region==3||region>=8?0x8000:region==6?0x18000:0x40000;unsigned offsets[]={0,1,0x100,0x800100,limit-32,limit-16,limit-4};
  unsigned bases[]={0,1,13,14};unsigned lists[]={0xff0,0x0007,0xd810,0x6810};
  for(unsigned l=0;l<4;l++)check(region,offsets[n],lists[l],bases[rnidx],mode,d,tagged,f<<28);
 }
 printf("ARM block RAM: %u cases passed (registers, bytes, mirrors, boundaries, tags, NZCV, writeback)\\n",tests);
}
'''
cc=os.environ.get('ARM_TEST_CC') or shutil.which('arm-linux-gnueabihf-gcc')
qemu=os.environ.get('ARM_TEST_QEMU') or shutil.which('qemu-arm')
if not cc or not qemu:
 print('ARM emitter execution needs ARM_TEST_CC and ARM_TEST_QEMU');raise SystemExit(77)
stub=(r/'gba-core/arm/arm_stub.S').read_text()
a=stub.index('defsymbl(arm_block_load_address)');b=stub.index('defsymbl(arm_indirect_branch_arm)',a)
asm='#define defsymbl(x) .global x; x:\n#define reg_base r11\n#define RDMAP_OFF 0xd00\n.arm\n.text\n'+stub[a:b]+'\n.section .note.GNU-stack,"",%progbits\n'
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'check.c').write_text(pre);(p/'check.S').write_text(asm)
 subprocess.run([cc,'-O2','-marm','-I',str(r/'gba-core'),str(p/'check.c'),str(p/'check.S'),'-static','-o',str(p/'check')],check=True)
 subprocess.run([qemu,str(p/'check')],check=True)

