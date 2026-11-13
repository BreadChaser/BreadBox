from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[2];s=(r/'gba-core/cpu_threaded.c').read_text()
def extract(start,end):return s[s.index(start):s.index(end,s.index(start))]
lookup=extract('u8 function_cc *block_lookup_address_arm(u32 pc)\n{','u8 function_cc *block_lookup_address_thumb(u32 pc)\n{')
flush=extract('void flush_translation_cache_ram(void)\n{','void flush_translation_cache_rom(void)\n{')
init=extract('void init_dynarec_caches(void)\n{','void flush_dynarec_caches(void)\n{')
init=init[:init.index('\n}')+2]
code='''#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef unsigned char u8;typedef uint32_t u32;typedef uint16_t u16;
#define function_cc
#define ARM_ARCH
#define MIN(a,b) ((a)<(b)?(a):(b))
#define INITIAL_TOP_TAG 65535
#define REG_PC 15
u32 arm_iwram_dispatch[8192],reg[64],rom_branch_hash[16];
u8 iwram[65536],ewram[524288],cache[4096],*ram_translation_cache=cache,*rom_translation_cache=cache;
u8 *ram_translation_ptr,*last_ram_translation_ptr,*rom_translation_ptr,*last_rom_translation_ptr;
u32 iwram_code_min,iwram_code_max,ewram_code_min,ewram_code_max,ram_block_tag,flush_ram_count;
unsigned long long gpsp_profile[20];
int attempt,failures,syncs;u8 *answer;
void jit_write_enable(void){} void jit_exec_enable(void){} void translate_icache_sync(void){syncs++;}
u8 *block_lookup_translate_arm(u32 pc){return attempt++<failures?NULL:answer;}
'''+lookup+flush+init+'''
int main(void){
 memset(arm_iwram_dispatch,1,sizeof(arm_iwram_dispatch));init_dynarec_caches();
 for(int i=0;i<8192;i++)assert(!arm_iwram_dispatch[i]);
 for(unsigned pc=0x03000000;pc<0x03008000;pc+=4){
 attempt=0;failures=2;answer=cache+64;syncs=0;
 assert(block_lookup_address_arm(pc|0x800000)==answer && syncs==1 && attempt==3);
 assert(arm_iwram_dispatch[(pc&0x7ffc)>>2]==(u32)(uintptr_t)answer);
 }
 flush_translation_cache_ram();for(int i=0;i<8192;i++)assert(!arm_iwram_dispatch[i]);
 attempt=0;failures=0;answer=cache+32;block_lookup_address_arm(0x02000100);assert(!arm_iwram_dispatch[64]);
 attempt=0;failures=4;answer=cache+32;assert(!block_lookup_address_arm(0x03000100));assert(!arm_iwram_dispatch[64]);
 puts("PASS production dispatch publication, mirrors, failed translation, init and full RAM invalidation");}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);subprocess.run(['cc','-O2',str(p/'test.c'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
