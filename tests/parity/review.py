"""Run production review regression paths with small host devices."""
from pathlib import Path
import subprocess,tempfile,sys
r=Path(__file__).resolve().parents[2]
exec((r/'tests/gba/run.py').read_text().split('with tempfile.TemporaryDirectory')[0])
failed=[]
def check(name,source):
 with tempfile.TemporaryDirectory() as td:
  p=Path(td);(p/'test.c').write_text(source)
  result=subprocess.run(['cc','-std=gnu11','-I'+str(r/'systems/gba'),'-I'+str(r/'app/ui'),'-I'+str(r/'include/breadbox'),'-I'+str(r/'platform/3ds'),str(p/'test.c'),'-o',str(p/'test')],capture_output=True,text=True)
  if not result.returncode:result=subprocess.run([str(p/'test')],capture_output=True,text=True)
  print(name, 'FAIL' if result.returncode else 'PASS',flush=True)
  if result.returncode:print(result.stderr[-2000:]);failed.append(name)
pause=(r/'app/ui/bb_pause.c').read_text()
quick=function(pause,'bb_pause_quick') if 'bool bb_pause_quick(' in pause else ''
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
typedef uint64_t u64;
#define SYSCLOCK_ARM11 1000
#define BB_SLOT_QUICK 12
#define BB_SLOT_UNDO 11
#include "gba_timing.h"
static u64 tick=1000,paused_ticks,report,emu_ticks,present_ticks,max_emu,last_video,latch_ticks,frame_previous;
static unsigned latch_waits,report_frames,rendered,held=255,dev_drawn;
static bool clock_reset,audio_paused,undo_ready;static gba_clock clock;
static unsigned slots[8],writes,actions;static bool fail_undo;
static bool bb_save_exists(unsigned n){return n==12;}
static bool bb_save_state(unsigned n,bool save){slots[writes++]=n;if(n==11 && fail_undo)return false;tick+=250;if(!save)audio_paused=true;return true;}
static bool save_state(unsigned n){return bb_save_state(n,true);}
static bool load_state(unsigned n){return bb_save_state(n,false);}
static void pause_audio(bool b){audio_paused=b;}
static void message(const char *s){(void)s;}
static void rebase(void){clock_reset=true;}
static struct {void(*pause)(bool);void(*rebase)(void);} config={pause_audio,rebase};
static u64 svcGetSystemTick(void){return tick;}
static u64 ticks(void){return tick;}
'''+quick
check('Quick-load refuses without Undo; audio always resumes',prefix+r'''
int main(void){assert(bb_pause_quick(true));assert(writes==1 && slots[0]==12 && !audio_paused);writes=0;assert(bb_pause_quick(false));assert(undo_ready && slots[0]==11 && slots[1]==12 && !audio_paused);writes=0;fail_undo=true;assert(!bb_pause_quick(false));assert(writes==1 && slots[0]==11 && !audio_paused);}
''')
s=(r/'systems/gba/gba.c').read_text();a=s.index('        if(actions&');b=s.index('        if((actions&1)',a)
check('GBA quick-load restores audio, clock, and input',prefix+'\nstatic void run(void){'+s[a:b]+r'''}
int main(void){gba_clock_reset(&clock,tick,1000);actions=4;run();assert(!audio_paused);assert(clock.next==tick && report==tick && !held && last_video==tick);assert(writes==2 && slots[0]==11 && slots[1]==12 && undo_ready);}
''')
s=(r/'systems/common/retro_ctr.c').read_text();a=s.index('  if(actions&');b=s.index('  if((actions&1)',a)
check('NES quick-save avoids postcards and rebases',prefix+r'''
static struct {uint64_t frames;} host={90};
static unsigned report_drawn,report_shown,drawn,shown,injected_frames;
static void run(void){'''+s[a:b]+r'''}
int main(void){actions=2;run();assert(writes==1 && slots[0]==12);assert(clock_reset && !held && report==tick && report_frames==90);}
''')
s=(r/'systems/nes/core/libretro/libretro.cpp').read_text()
# C-shaped RAM helper uses the same two memory devices as the real core.
ram=s[s.index('static void bb_apply_ram_cheats()'):s.index('// Cached result')]
ram=ram.replace('emu->low_mem()', 'low').replace('emu->high_mem()', 'high')
check('NES freeze cannot mutate battery SRAM',r'''
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
static int *emu=(int*)1;static uint8_t low[2048],high[8192];
static struct {bool enabled;unsigned address,value;} bb_cheats[20][64];
static unsigned bb_cheat_lengths[20];
'''+ram+r'''
int main(void){high[32]=17;bb_cheat_lengths[0]=1;bb_cheats[0][0].enabled=true;bb_cheats[0][0].address=0x6020;bb_cheats[0][0].value=255;bb_apply_ram_cheats();assert(high[32]==17);}
''')
a=s.index('   bb_original_prg=',s.index('bool retro_load_game')) if '   bb_original_prg=' in s[s.index('bool retro_load_game'):] else -1
allocation=s[a:s.index('   struct retro_memory_descriptor',a)] if a>=0 else ''
allocation=allocation.replace('emu->cart()->prg_size()', 'sizeof(prg)').replace('emu->cart()->prg()', 'prg')
check('NES load keeps PRG backup unallocated',r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static uint8_t *bb_original_prg,prg[524288];
int main(void){'''+allocation+r'''assert(!bb_original_prg);}
''')
if failed:sys.exit(1)
