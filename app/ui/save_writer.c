#include "save_writer.h"
#ifdef SAVE_WRITER_TEST
#include "writer_sdk.h"
#else
#include <3ds.h>
#endif
#include <stdlib.h>
#include <string.h>

struct save_writer {
    LightLock lock;
    LightEvent wake, done;
    Thread thread;
    size_t bytes;
    save_write_fn write;
    bool pending, busy, stopping, valid, ok;
    unsigned char *next, *active;
};

static void writer_main(void *arg)
{
    save_writer *w=arg;
    for(;;) {
        LightLock_Lock(&w->lock);
        if(!w->pending) {
            bool stop=w->stopping;
            LightLock_Unlock(&w->lock);
            if(stop)break;
            LightEvent_Wait(&w->wake);
            continue;
        }
        memcpy(w->active,w->next,w->bytes);
        w->pending=false;w->busy=true;w->valid=true;
        LightLock_Unlock(&w->lock);
        bool ok=w->write(w->active);
        LightLock_Lock(&w->lock);
        w->ok=ok;w->busy=false;
        LightEvent_Signal(&w->done);
        LightLock_Unlock(&w->lock);
    }
}

save_writer *save_writer_start(size_t bytes,save_write_fn write,int core)
{
    save_writer *w=calloc(1,sizeof(*w));
    if(!w)return NULL;
    w->next=malloc(bytes);w->active=malloc(bytes);
    if(!w->next || !w->active)goto fail;
    w->bytes=bytes;w->write=write;w->ok=true;
    LightLock_Init(&w->lock);
    LightEvent_Init(&w->wake,RESET_ONESHOT);
    LightEvent_Init(&w->done,RESET_ONESHOT);
    /* Priority 0x30 is below the main thread. Core 1 gets the normal syscore
     * share on Old; core 2 is an application core on New. Never raise MaxCpu. */
    w->thread=threadCreate(writer_main,w,32*1024,0x30,core,false);
    if(w->thread)return w;
fail:
    free(w->next);free(w->active);free(w);return NULL;
}

void save_writer_submit(save_writer *w,const void *snapshot)
{
    LightLock_Lock(&w->lock);
    /* Compare only immutable snapshots, never worker-owned durable metadata.
     * A failed attempt is retried even when its bytes have not changed. */
    const void *last=w->pending?w->next:w->active;
    if((w->pending || (w->valid && (w->busy || w->ok))) && !memcmp(last,snapshot,w->bytes)) {
        LightLock_Unlock(&w->lock);return;
    }
    memcpy(w->next,snapshot,w->bytes);w->pending=true;
    LightEvent_Signal(&w->wake);
    LightLock_Unlock(&w->lock);
}

bool save_writer_flush(save_writer *w)
{
    for(;;) {
        LightLock_Lock(&w->lock);
        if(!w->pending && !w->busy) {
            bool ok=w->ok;LightLock_Unlock(&w->lock);return ok;
        }
        LightEvent_Clear(&w->done);
        LightLock_Unlock(&w->lock);
        LightEvent_Wait(&w->done);
    }
}
bool save_writer_failed(save_writer *w)
{
    LightLock_Lock(&w->lock);bool failed=!w->ok;LightLock_Unlock(&w->lock);return failed;
}
void save_writer_stop(save_writer *w)
{
    if(!w)return;
    save_writer_flush(w);
    LightLock_Lock(&w->lock);w->stopping=true;LightEvent_Signal(&w->wake);LightLock_Unlock(&w->lock);
    threadJoin(w->thread,U64_MAX);threadFree(w->thread);
    free(w->next);free(w->active);free(w);
}
