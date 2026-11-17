#!/usr/bin/env python3
"""Two gpSP core fixes, run from the production source:
1. update_timers(): an overflowed timer's counter register shows its reload value at once. It read 0 until the
   next tick; GBA Video carts read Timer 1 right after its overflow IRQ, took 0 as a full lap and decoded ~4 billion
   samples over their own state (Disney Vol. 1 crash), and Ms. Pac-Man - Maze Madness hung at frame 1024.
2. A branch into memory nothing can execute: every region either translates or gets the bridge entry (no -1 jump),
   and the bridge sleeps the CPU until the next interrupt when the target is not VRAM."""
from pathlib import Path
import re, subprocess, tempfile
r = Path(__file__).resolve().parents[2]
main = (r / 'gba-core/main.c').read_text()
a = main.index('static unsigned update_timers(')
timers = main[a:main.index('\nvoid init_main(void)', a)]
cpu = (r / 'gba-core/cpu.cc').read_text()
a = cpu.index('u32 function_cc execute_arm_vram(u32 cycles)')
bridge = cpu[a:cpu.index('\nvoid init_cpu(void)', a)]
threaded = (r / 'gba-core/cpu_threaded.c').read_text()
lookup = re.search(r'#ifdef HAVE_VRAM_INTERPRETER\n(.*?)#else', threaded, re.S)[1]
bridged = re.search(r'#define pc_region_bridged\(r\)\s*\\\n(.*?)\n\n', threaded, re.S)[1]
for name, body in (('update_timers', timers), ('execute_arm_vram', bridge)):
    assert body.count('{') == body.count('}'), name

code = r'''#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t u8; typedef uint16_t u16; typedef int32_t s32; typedef uint32_t u32; typedef u16 irq_type;
#define function_cc
#define TIMER_INACTIVE 0
#define TIMER_PRESCALE 1
#define TIMER_CASCADE 2
#define IRQ_TIMER0 0x0008
#define REG_TM0D 0x80
#define REG_TMXD(n) (REG_TM0D + (2 * (n)))
#define REG_PC 15
#define REG_CPSR 16
#define CPU_HALT_STATE 18
#define CPU_ACTIVE 0
#define CPU_HALT 1
#define PROFILE_EVENT(n) ((void)0)
#define cycles_to_run(c) ((c) & 0x7FFF)
#define completed_frame(c) ((c) & 0x80000000)
static u16 io_registers[0x200];
#define write_ioreg(regnum, val) io_registers[(regnum)] = (u16)(val)
typedef struct { s32 count; u32 reload, prescale, frequency_step, direct_sound_channels, irq, status; } timer_type;
static timer_type timer[4];
static unsigned sound_timer(u32 step, u32 channel) { (void)step; (void)channel; return 0; }
''' + timers + r'''
static u32 reg[64];
static bool vram_interpreter_active, vram_interpreter_smc;
static unsigned updates, interpreted;
static u32 update_gba(int cycles) { (void)cycles; updates++; return 0x80000000; }
static u32 execute_arm_internal(u32 cycles, bool vram_only) { (void)cycles; assert(vram_only); interpreted++; return 0; }
''' + bridge + r'''
#define pc_region_bridged(r) ''' + bridged.replace('\\\n', ' ') + r'''
static bool region_entered(unsigned pcregion)
{
  switch (pcregion) {
''' + lookup.split('#define vram_lookup_region', 1)[1].replace('\\', '').strip() + r'''
  case 0x0: case 0x2: case 0x3: case 0x8 ... 0xD:
    return true;
  }
  return false;
}
static void start(unsigned i, u32 reload, u32 prescale, u32 status)
{
  timer[i] = (timer_type){ .reload = reload, .prescale = prescale, .status = status, .irq = 1 };
  timer[i].count = (s32)(reload << prescale);
  write_ioreg(REG_TMXD(i), -(s32)reload);
}
int main(void)
{
  /* Timer 0 at sample rate, Timer 1 counting its overflows: a 0x800-sample ring (the Video carts' setup). */
  for (unsigned i = 0; i < 4; i++) timer[i].status = TIMER_INACTIVE;
  start(0, 512, 0, TIMER_PRESCALE);
  start(1, 0x800, 0, TIMER_CASCADE);
  assert(io_registers[REG_TMXD(1)] == 0xF800);
  irq_type irq = 0;
  for (unsigned lap = 0; lap < 3; lap++) {
    for (unsigned sample = 0; sample < 0x800; sample++) {
      update_timers(&irq, 512);
      u16 seen = io_registers[REG_TMXD(1)];
      assert(seen >= 0xF800);   /* never 0: the count the game reads stays inside its ring */
      assert(seen == (u16)(0xF800 + (sample + 1) % 0x800));
      assert(io_registers[REG_TMXD(0)] == (u16)(0x10000 - 512));
    }
    assert(irq & (IRQ_TIMER0 << 1));
    irq = 0;
  }
  /* A prescaled timer overflowing inside one step shows the reload plus the cycles past it. */
  start(2, 100, 6, TIMER_PRESCALE);
  update_timers(&irq, (100 << 6) + (3 << 6));
  assert(io_registers[REG_TMXD(2)] == (u16)(0x10000 - 100 + 3));

  /* Every region the dynarec can be asked to run is translated or bridged: none falls through to ~0. */
  for (unsigned region = 0; region <= 0xFF; region++) {
    assert(region_entered(region));
    bool bridged = pc_region_bridged(region);
    assert(bridged == !(region == 0 || region == 2 || region == 3 || (region >= 8 && region <= 0xD)));
  }
  /* The bridge: VRAM is interpreted; a wild target sleeps until the next interrupt (frames go on). */
  reg[REG_PC] = 0x06000100; reg[CPU_HALT_STATE] = CPU_ACTIVE;
  execute_arm_vram(100);
  assert(interpreted == 1 && reg[CPU_HALT_STATE] == CPU_ACTIVE);
  const u32 wild[] = { 0x1f411f40, 0x01fffdfe, 0xe0200002, 0x04000000, 0x05000000, 0x07000000, 0x0e000000 };
  for (unsigned i = 0; i < sizeof(wild) / sizeof(*wild); i++) {
    unsigned before = updates;
    reg[REG_PC] = wild[i]; reg[CPU_HALT_STATE] = CPU_ACTIVE;
    assert(completed_frame(execute_arm_vram(100)));
    assert(reg[CPU_HALT_STATE] == CPU_HALT && updates == before + 1 && interpreted == 1);
  }
  puts("gpSP core fixes: overflowed timers show their reload (cascade + prescaled), every PC region "
       "translates or bridges, a wild jump sleeps the CPU instead of branching to -1: PASS");
}
'''
with tempfile.TemporaryDirectory() as d:
    p = Path(d)
    (p / 'test.c').write_text(code)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wno-unused-function', '-fsanitize=address,undefined',
                    str(p / 'test.c'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
