#include "disc_thread.h"
#include "cold_code.h"
#include "app_main.h"
#include "app_log.h"
#include <3ds.h>

struct sthread;
extern struct sthread *cdra_read_thread(void);
extern void *pcsxr_native_thread(struct sthread *thread);

/* Read-ahead can run between rendering jobs without delaying their deadlines. */
PSX3D_COLD void ps1_disc_thread_prepare(void)
{
    if (!g_new3ds) return;
    Thread thread = pcsxr_native_thread(cdra_read_thread());
    if (!thread) return;
    s32 priority;
    Result rc = svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    if (R_FAILED(rc) || priority >= 0x3f) return;
    rc = svcSetThreadPriority(threadGetHandle(thread), priority + 1);
    log_printf("disc reader: priority %ld result %08lx\n", priority + 1, rc);
}
