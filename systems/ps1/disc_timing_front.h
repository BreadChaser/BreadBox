#pragma once
#ifdef BB_DISC_TIMING
#include "../../core/libpcsxcore/disc_timing.h"
int cdt_prepare(int enabled);
int cdt_input(unsigned port, unsigned device, unsigned index, unsigned id, int live);
void cdt_frame(void);
void cdt_audio(const char *stats);
void cdt_report(unsigned long long now, double target);
unsigned cdt_pad(void);
// Consume stats: disc summaries replace verbose per-second reports.
#define CDT_PREPARE(enabled) ((enabled) = cdt_prepare(enabled))
#define CDT_INPUT(port, device, index, id, live) cdt_input(port, device, index, id, live)
#define CDT_FRAME() do { if (cdt_enabled) cdt_frame(); } while (0)
#define CDT_AUDIO(stats) do { if (cdt_enabled) cdt_audio(stats); } while (0)
#define CDT_REPORT(now, target) do { if (cdt_enabled) cdt_report(now, target); } while (0)
#else
#define CDT_PREPARE(enabled) ((void)0)
#define CDT_INPUT(port, device, index, id, live) (live)
#define CDT_FRAME() ((void)0)
#define CDT_AUDIO(stats) ((void)0)
#define CDT_REPORT(now, target) ((void)0)
#endif
