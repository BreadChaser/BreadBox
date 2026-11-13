#!/usr/bin/env python3
"""Execute room pacing and refresh accounting against deterministic host fakes."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "app/ui/room_pace.c").read_text()

def function(name):
    start = source.index(name)
    brace = source.index("{", start)
    depth = 0
    for end in range(brace, len(source)):
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
            if depth == 0:
                return source[start:end + 1]
    raise AssertionError(name)

state = source[source.index("static struct {\n u64 last, window, returned;"):source.index("void room_pace_diagnostic")]
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint64_t u64;
#define SYSCLOCK_ARM11 1000000ull
#define ROOM_PACE_PARTS 11
static u64 clock_ticks;
static char logs[8192];
static unsigned freezes;
static u64 svcGetSystemTick(void) { return clock_ticks; }
static void log_printf(const char *fmt, ...) {
 va_list ap; va_start(ap,fmt); vsnprintf(logs+strlen(logs),sizeof(logs)-strlen(logs),fmt,ap); va_end(ap);
}
static void ht_freeze(void) { freezes++; }
static unsigned steps=1;
static unsigned video_ui_steps(void) { return steps; }
#include "room_refresh.h"
''' + state + "\n" + "\n".join(function(n) for n in ["void room_pace_diagnostic", "void room_pace_return_start", "bool room_pace_return_pending", "static void refresh_report", "void room_pace_presented"]) + r'''
int main(void) {
 R.label="room home";R.returned=123; R.last=456; R.armed=false; room_pace_presented();
 assert(!room_pace_return_pending() && R.last==0);
 clock_ticks=100000; room_pace_return_start(); assert(R.last==0);
 clock_ticks+=28000; R.armed=true; room_pace_presented();
 assert(strstr(logs,"+28 ms from menu restore"));
 assert(strstr(logs,"first present gap 2 lost 1"));
 assert(!strstr(logs,"refresh: lost 1 gap 2"));
 assert(R.frames==0 && R.lost==0 && freezes==0);
 room_pace_diagnostic(); clock_ticks+=7000; R.armed=true; room_pace_presented();
 assert(R.frames==0 && R.lost==0); /* diagnostic sample excluded */
 R.diagnostic=false; R.previous_diagnostic=false;
 clock_ticks+=65000; R.armed=true; room_pace_presented();
 assert(R.lost==3 && R.frames==1 && freezes==1);
 assert(!strstr(logs,"game exit"));
 // Old two-refresh cadence: synthetic post-vblank stamps for the observed
 // L'=28/7/65ms stalls. Work duration alone cannot determine vblank phase.
 memset(&R,0,sizeof(R));R.label="room home";steps=2;R.last=clock_ticks;
 const unsigned gaps[]={3,2,6},expected[]={1,0,4};
 for(unsigned i=0;i<3;i++) {
  unsigned before=R.lost;clock_ticks+=16715*gaps[i];R.armed=true;
  room_pace_presented();assert(R.lost-before==expected[i]);
 }
 assert(R.frames==3 && R.lost==5 && R.hist[3]==1 && R.hist[2]==1 && R.hist[6]==1);
 refresh_report();assert(strstr(logs,"three_plus 2 max 6 p99 6"));
 return 0;
}
'''
code = '#include <stdarg.h>\n' + code
with tempfile.TemporaryDirectory() as td:
    p = Path(td)
    (p / "test.c").write_text(code)
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-I", str(root / "app/ui"), str(p / "test.c"), "-o", str(p / "test")], check=True)
    subprocess.run([str(p / "test")], check=True)
print("Room pace: restore gate, restore latency/gap, diagnostic exclusion and lost refresh counts PASS")
