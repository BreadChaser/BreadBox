// App start and console capabilities (main.c, systems' 3D availability)
#pragma once
#include <stdbool.h>

void boot_mark(const char *what);   // main.c: "boot: <what> +N ms" since the app started (the cold-start table)

extern int __ctr_svchax;
// Why fast mode (the dynarec) is off; SLOW_NONE when it is on.
enum { SLOW_NONE, SLOW_EMULATOR, SLOW_NO_LUMA };
extern int g_slow_reason;
// Plain-language notice for the library/browser/game start, or NULL when fast mode is on.
const char *slow_mode_notice(bool shortform);

float slider_get(void);

extern bool g_new3ds;
extern bool g_3d_screen;
bool ps1_3d_available(void);
bool gba_3d_available(void);
extern int g_syscore_pct;
