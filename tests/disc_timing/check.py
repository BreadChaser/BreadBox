"""Exercise the real optional counters and reporting with a deterministic clock."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as tmp:
    p = Path(tmp)
    (p / '3ds').mkdir()
    (p / '3ds/svc.h').write_text('unsigned long long svcGetSystemTick(void);\n')
    (p / '3ds.h').write_text('#include <3ds/svc.h>\ntypedef unsigned long long u64;\nu64 osGetTime(void);\n')
    (p / 'app_paths.h').write_text('#define PSX3D_DIR "."\n')
    (p / 'app_log.h').write_text('void log_printf(const char *, ...);\n')
    (p / 'test.c').write_text(r'''
#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "disc_timing_front.h"
static unsigned long long tick, millis = 1000;
static unsigned logged;
static char output[2048];
unsigned long long svcGetSystemTick(void) { return tick; }
unsigned long long osGetTime(void) { return millis; }
void log_printf(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vsnprintf(output, sizeof(output), fmt, ap); va_end(ap);
    logged++;
}
int main(void) {
    assert(_Alignof(struct cdt_read_counts) >= 64);
    cdt_prepare(0);
    unsigned n = logged;
    cdt_end(0, cdt_begin(0));
    CDT_HUNK(tick++);
    assert(tick == 1 && logged == n && cdt_reads[0].reads == 0);
    FILE *f = fopen("cdtime.txt", "w"); assert(f);
    fputs("2 8 3 1 100\n", f); fclose(f);
    cdt_prepare(1); n = logged;
    tick = UINT_MAX - 10u;
    unsigned start = cdt_begin(1);
    CDT_HUNK(tick += 20);
    cdt_end(1, start);
    assert(cdt_reads[1].reads == 1 && cdt_reads[1].ticks == 20);
    assert(cdt_reads[1].hunks == 1 && cdt_reads[1].hunk_ticks == 20);
    assert(cdt_reads[0].reads == 0 && logged == n);
    cdt_frame(); assert(cdt_pad() == 0);
    tick += 34 * 268112u; cdt_frame(); assert(cdt_pad() == 0x100);
    assert(cdt_input(0, 1, 0, 8, 0) == 1);
    assert(cdt_input(1, 1, 0, 8, 0) == 0);
    assert(cdt_input(0, 1, 0, 256, 0) == 0x100);
    tick += 51 * 268112u; cdt_frame(); assert(cdt_pad() == 0);
    cdt_report(10999, 60); assert(logged == n);
    cdt_audio("audio underruns=2 drop=7 maxgap=51.2ms");
    cdt_audio("audio underruns=3 drop=11 maxgap=34.0ms");
    cdt_audio("audio off");
    cdt_report(11000, 60); assert(logged == n + 1);
    assert(strstr(output, "over33 2 over50 1"));
    assert(strstr(output, "audio underruns 5 drop 18 maxgap 51.2"));
    tick += 20000ULL * 268112u; cdt_frame();
    cdt_reads[1].ticks = UINT_MAX - 4u;
    cdt_report(21000, 60);
    assert(strstr(output, "over33 1 over50 1 maxframe 20000.00"));
    assert(strstr(output, "audio underruns 0 drop 0 maxgap 0.0"));
    cdt_reads[1].ticks = 5;
    cdt_report(31000, 60);
    assert(strstr(output, "ahead 0 ms 0.00"));
    cdt_prepare(0); n = logged;
    cdt_end(1, cdt_begin(1)); assert(cdt_reads[1].reads == 0 && logged == n);
    return 0;
}
''')
    cc = os.environ.get('CC', 'cc')
    flags = [cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-DBB_DISC_TIMING', '-I' + tmp,
             '-I' + str(root / 'include/breadbox'), '-I' + str(root / 'systems/ps1'), '-I' + str(root / 'core/deps/libretro-common/include')]
    subprocess.run(flags + [str(p / 'test.c'), str(root / 'systems/ps1/disc_timing.c'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], cwd=p, check=True)
    subprocess.run([cc, '-c', str(root / 'systems/ps1/disc_timing.c'), '-o', str(p / 'off.o')], check=True)
    assert not subprocess.check_output(['nm', '--defined-only', str(p / 'off.o')], text=True).strip()
print('disc timing: PASS')
