"""Exercise the actual load-time scheduling policy with kernel calls stubbed."""
from pathlib import Path
import os
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as tmp:
    p = Path(tmp)
    (p / 'app_main.h').write_text('#include <stdbool.h>\nextern bool g_new3ds;\n')
    (p / 'app_log.h').write_text('void log_printf(const char *, ...);\n')
    (p / '3ds.h').write_text('''
#include <stdint.h>
typedef int32_t s32;
typedef int32_t Result;
typedef void *Thread;
typedef unsigned Handle;
#define CUR_THREAD_HANDLE 0xffff8000u
#define R_FAILED(r) ((r) < 0)
Result svcGetThreadPriority(s32 *, Handle);
Result svcSetThreadPriority(Handle, s32);
Handle threadGetHandle(Thread);
''')
    (p / 'test.c').write_text('''
#include <assert.h>
#include <stdbool.h>
#include <3ds.h>
#include "disc_thread.h"
bool g_new3ds;
struct sthread;
static int gets, sets, reader, priority = 48, requested;
static Result get_result, set_result;
struct sthread *cdra_read_thread(void) { gets++; return reader ? (void *)1 : 0; }
void *pcsxr_native_thread(struct sthread *p) { return p; }
Handle threadGetHandle(Thread t) { assert(t == (void *)1); return 123; }
Result svcGetThreadPriority(s32 *p, Handle h) { assert(h == CUR_THREAD_HANDLE); *p = priority; return get_result; }
Result svcSetThreadPriority(Handle h, s32 p) { assert(h == 123); sets++; requested = p; return set_result; }
void log_printf(const char *fmt, ...) { (void)fmt; }
int main(void) {
    ps1_disc_thread_prepare(); assert(gets == 0 && sets == 0);
    g_new3ds = true; ps1_disc_thread_prepare(); assert(sets == 0);
    reader = 1; get_result = -1; ps1_disc_thread_prepare(); assert(sets == 0);
    get_result = 0; priority = 63; ps1_disc_thread_prepare(); assert(sets == 0);
    priority = 48; ps1_disc_thread_prepare(); assert(sets == 1 && requested == 49);
    set_result = -1; ps1_disc_thread_prepare(); assert(sets == 2 && requested == 49);
    g_new3ds = false; int before = gets; ps1_disc_thread_prepare(); assert(gets == before && sets == 2);
    return 0;
}
''')
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-I' + tmp, '-I' + str(root / 'include/breadbox'),
                    '-I' + str(root / 'systems/ps1'), str(p / 'test.c'),
                    str(root / 'systems/ps1/disc_thread.c'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
print('disc thread scheduling: PASS')
