"""Run the production PS1 message callbacks with fake OSD and log sinks."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
emu = (root / 'systems/ps1/emu.c').read_text()
start = emu.index('\tcase RETRO_ENVIRONMENT_SET_MESSAGE:')
end = emu.index('\n\tdefault:', start)
dispatch = emu[start:end]

source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "libretro.h"
#include "ps1_messages.h"

static unsigned shown, logged;
static char visible[512], log_line[512];
void emu_osd(const char *text)
{
    shown++;
    snprintf(visible, sizeof(visible), "%s", text);
}
void log_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(log_line, sizeof(log_line), fmt, ap);
    va_end(ap);
    logged++;
}
/* The old callbacks used this formatter before sending directly to OSD. */
void osd(const char *fmt, ...)
{
    char text[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    emu_osd(text);
}
static bool environment(unsigned cmd, void *data)
{
    switch (cmd) {
''' + dispatch + r'''
    default: return false;
    }
}
static void send(const char *text, bool extended, enum retro_message_target target)
{
    struct retro_message legacy = {.msg = text, .frames = 180};
    struct retro_message_ext ext = {
        .msg = text, .duration = 3000, .priority = 3,
        .level = RETRO_LOG_WARN, .target = target,
        .type = RETRO_MESSAGE_TYPE_NOTIFICATION, .progress = -1
    };
    assert(environment(extended ? RETRO_ENVIRONMENT_SET_MESSAGE_EXT :
           RETRO_ENVIRONMENT_SET_MESSAGE, extended ? (void *)&ext : (void *)&legacy));
}
int main(void)
{
    const char *quiet[] = {
        "No PlayStation BIOS file found - add for better compatibility",
        "BIOS set to 'hle'",
        "Enabled hacks: PSX CPU Clock57, DRC Hacks",
        "Enabled hacks: TurboCD"
    };
    const char *actionable[] = {
        "LibCrypt protected game with missing SBI detected",
        "Physical CD-ROM support is not compiled in.",
        "Could not open BIOS: scph1001.bin",
        "Save state failed",
        "ANALOG ON",
        "Disc 2 inserted"
    };
    for (unsigned launch = 0; launch < 3; launch++) {
        for (unsigned extended = 0; extended < 2; extended++) {
            for (unsigned target = RETRO_MESSAGE_TARGET_ALL;
                 target <= RETRO_MESSAGE_TARGET_OSD; target++) {
                for (unsigned i = 0; i < sizeof(quiet)/sizeof(*quiet); i++) {
                    shown = logged = 0;
                    send(quiet[i], extended, target);
                    assert(shown == 0 && logged == 1);
                    assert(strstr(log_line, quiet[i]));
                }
                for (unsigned i = 0; i < sizeof(actionable)/sizeof(*actionable); i++) {
                    shown = logged = 0;
                    send(actionable[i], extended, target);
                    assert(shown == 1 && logged == 0);
                    assert(!strcmp(visible, actionable[i]));
                }
            }
        }
    }
    shown = 0;
    send("Save state failed", true, RETRO_MESSAGE_TARGET_LOG);
    assert(shown == 0);
    puts("PASS: repeated PS1 launches keep BIOS/hacks in logs; useful notices still display (legacy + extended)");
}
'''

with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
    tmp = Path(tmp)
    (tmp / 'check.c').write_text(source)
    includes = ['tests/app/host', 'app/ui', 'include/breadbox', 'platform/3ds',
                'systems/ps1', 'core/deps/libretro-common/include']
    command = ['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
               '-fsanitize=address,undefined', '-g', '-DAPP_BREADBOX',
               *[f'-I{root / path}' for path in includes],
               str(tmp / 'check.c'), str(root / 'systems/ps1/ps1_messages.c'),
               '-o', str(tmp / 'check')]
    subprocess.run(command, check=True)
    subprocess.run([str(tmp / 'check')], check=True)

    # Prove the test catches the old callbacks bypassing the message policy.
    (tmp / 'check.c').write_text(source.replace('ps1_core_message("%s", m->msg);',
                                              'osd("%s", m->msg);'))
    subprocess.run(command, check=True)
    failed = subprocess.run([str(tmp / 'check')], stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    assert failed.returncode, 'old unfiltered callbacks escaped the regression test'
    print('PASS: original startup-popup callbacks fail the regression test')
