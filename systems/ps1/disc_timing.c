#ifdef BB_DISC_TIMING
#include "disc_timing_front.h"
#include "cold_code.h"
#include "app_log.h"
#include "app_paths.h"
#include "libretro.h"
#include <3ds.h>
#include <stdio.h>
#include <string.h>

int cdt_enabled;
unsigned cdt_reader;
volatile struct cdt_read_counts cdt_reads[2];
static struct cdt_read_counts previous[2];
static unsigned frame, frames, over33, over50, lines;
static u64 last_tick, report_ms, maxframe;
static unsigned underruns, drops;
static double audio_gap;
static struct { unsigned from, to, period, hold, mask; } script[32];
#define TICKS_MS 268112.0

/* Check the syscall's scratch registers with all outputs declared. */
#ifdef __arm__
PSX3D_COLD static void clock_abi(void)
{
    register unsigned low __asm__("r0");
    register unsigned high __asm__("r1");
    register unsigned scratch2 __asm__("r2") = 0x12345678;
    register unsigned scratch3 __asm__("r3") = 0x23456789;
    register unsigned scratch12 __asm__("r12") = 0x3456789a;
    __asm__ volatile("svc 0x28" : "=r"(low), "=r"(high), "+r"(scratch2),
                     "+r"(scratch3), "+r"(scratch12) : : "cc", "memory");
    log_printf("disc timing: clock scratch r2=%08x r3=%08x r12=%08x\n",
               scratch2, scratch3, scratch12);
}
#endif

/* Called before retro_init: neither disc thread nor game frame exists yet. */
PSX3D_COLD int cdt_prepare(int enabled)
{
    cdt_enabled = enabled;
    cdt_reader = 0;
    memset((void *)cdt_reads, 0, sizeof(cdt_reads));
    memset(previous, 0, sizeof(previous));
    frame = frames = over33 = over50 = maxframe = lines = 0;
    last_tick = report_ms = 0;
    underruns = drops = 0; audio_gap = 0;
    if (!enabled) return 0;
#ifdef __arm__
    clock_abi();
#endif
    FILE *f = fopen(PSX3D_DIR "/cdtime.txt", "r");
    char line[128];
    unsigned scanned = 0;
    while (f && lines < 32 && scanned++ < 128 && fgets(line, sizeof(line), f)) {
        if (sscanf(line, "%u %u %u %u %x", &script[lines].from, &script[lines].to,
                   &script[lines].period, &script[lines].hold, &script[lines].mask) == 5 &&
            script[lines].period) lines++;
    }
    if (f) fclose(f);
    log_printf("disc timing: enabled, %u input ranges\n", lines);
    return 0;
}

PSX3D_COLD unsigned cdt_pad(void)
{
    unsigned mask = 0;
    for (unsigned i = 0; i < lines; i++)
        if (frame >= script[i].from && frame < script[i].to &&
            (frame - script[i].from) % script[i].period < script[i].hold)
            mask |= script[i].mask;
    return mask;
}

PSX3D_COLD int cdt_input(unsigned port, unsigned device, unsigned index, unsigned id, int live)
{
    if (!cdt_enabled || port || device != RETRO_DEVICE_JOYPAD || index) return live;
    unsigned mask = cdt_pad();
    return id == RETRO_DEVICE_ID_JOYPAD_MASK ? live | (short)mask : live || (id < 16 && (mask >> id & 1));
}

PSX3D_COLD void cdt_frame(void)
{
    u64 now = svcGetSystemTick();
    if (!report_ms) report_ms = osGetTime();
    if (last_tick) {
        u64 elapsed = now - last_tick;
        frames++;
        if (elapsed > 33 * TICKS_MS) over33++;
        if (elapsed > 50 * TICKS_MS) over50++;
        if (elapsed > maxframe) maxframe = elapsed;
    }
    last_tick = now;
    frame++;
}

PSX3D_COLD void cdt_audio(const char *stats)
{
    const char *p = strstr(stats, "underruns=");
    unsigned u, d;
    if (p && sscanf(p, "underruns=%u drop=%u", &u, &d) == 2) {
        underruns += u; drops += d;
    }
    p = strstr(stats, "maxgap=");
    double gap;
    if (p && sscanf(p, "maxgap=%lf", &gap) == 1 && gap > audio_gap)
        audio_gap = gap;
}

PSX3D_COLD void cdt_report(unsigned long long now, double target)
{
    if (!report_ms || now - report_ms < 10000) return;
    struct cdt_read_counts counts[2];
    for (unsigned i = 0; i < 2; i++) {
        struct cdt_read_counts current = cdt_reads[i];
        counts[i] = current;
#define DELTA(field) counts[i].field -= previous[i].field
        DELTA(reads); DELTA(ticks); DELTA(misses); DELTA(wait_ticks);
        DELTA(hunks); DELTA(hunk_ticks);
#undef DELTA
        previous[i] = current;
    }
    log_printf("disc timing: frame %u frames %u speed %.2f over33 %u over50 %u maxframe %.2f | "
               "main %u miss %u ms %.2f max %.2f wait %.2f callmax %.2f hunks %u decode %.2f hmax %.2f | "
               "ahead %u ms %.2f max %.2f hunks %u decode %.2f hmax %.2f | audio underruns %u drop %u maxgap %.1f\n",
               frame, frames, frames * 100000.0 / (now - report_ms) / target,
               over33, over50, maxframe / TICKS_MS,
               counts[0].reads, counts[0].misses, counts[0].ticks / TICKS_MS,
               counts[0].maximum / TICKS_MS, counts[0].wait_ticks / TICKS_MS,
               counts[0].call_max / TICKS_MS, counts[0].hunks,
               counts[0].hunk_ticks / TICKS_MS, counts[0].hunk_max / TICKS_MS,
               counts[1].reads, counts[1].ticks / TICKS_MS, counts[1].maximum / TICKS_MS,
               counts[1].hunks, counts[1].hunk_ticks / TICKS_MS, counts[1].hunk_max / TICKS_MS,
               underruns, drops, audio_gap);
    frames = over33 = over50 = maxframe = 0;
    underruns = drops = 0; audio_gap = 0;
    report_ms = now;
}
#endif
