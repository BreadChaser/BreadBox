#define _GNU_SOURCE
#include <3ds.h>
#include "bb_sd_bench.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#define TEMP "sdmc:/3ds/BreadBox/sd-throughput.tmp"
static void (*queued)(void *);
static void *context;
static bool cancelled,refuse,fail_write,fail_size,corrupt;
static unsigned writers,success,failure,reports,handles,preallocated;
static char mapped[1024];
static const char *path(FS_Path p){snprintf(mapped,sizeof(mapped),"sdmc:%s",p.data);return mapped;}
u64 svcGetSystemTick(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000000ULL+t.tv_nsec/1000;}
bool bb_index_cancelled(void){return cancelled;}
bool bb_index_defer_low(void (*fn)(void *),void *ctx){if(refuse)return false;assert(!queued);queued=fn;context=ctx;return true;}
bool bb_writer_begin(void){writers++;return true;}
void bb_writer_end(void){assert(writers==1);writers--;}
void log_printf(const char *fmt,...){char text[512];va_list a;va_start(a,fmt);vsnprintf(text,sizeof(text),fmt,a);va_end(a);
 if(strstr(text,"sd-bench: DONE"))success++;
 if(strstr(text,"sd-bench: FAILED"))failure++;
 if(strstr(text," bytes=")){assert(strstr(text,"ok=1"));reports++;}}
Result FSUSER_OpenArchive(FS_Archive *a,int id,FS_Path p){(void)id;(void)p;*a=1;return 0;}
Result FSUSER_CloseArchive(FS_Archive a){(void)a;return 0;}
Result FSUSER_CreateFile(FS_Archive a,FS_Path p,int attr,u64 size){(void)a;(void)attr;assert(!size);int fd=open(path(p),O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0)return -1;return close(fd);}
Result FSUSER_OpenFileDirectly(Handle *h,int id,FS_Path archive,FS_Path p,int flags,int attr){(void)id;(void)archive;(void)attr;
 *h=open(path(p),flags==FS_OPEN_WRITE?O_WRONLY:O_RDONLY);if(*h<0){*h=0;return -1;}handles++;return 0;}
Result FSFILE_SetSize(Handle h,u64 n){if(fail_size){errno=ENOSPC;return -1;}preallocated++;return ftruncate(h,n);}
Result FSFILE_Read(Handle h,u32 *got,u64 at,void *p,u32 n){ssize_t k=pread(h,p,n,at);if(k<0)return -1;*got=k;if(corrupt && k){((unsigned char *)p)[0]^=1;corrupt=false;}return 0;}
Result FSFILE_Write(Handle h,u32 *got,u64 at,const void *p,u32 n,u32 flags){assert(flags==0);if(fail_write){errno=ENOSPC;return -1;}ssize_t k=pwrite(h,p,n,at);if(k<0)return -1;*got=k;return 0;}
Result FSFILE_Flush(Handle h){return fsync(h);}
Result FSFILE_Close(Handle h){assert(handles);handles--;return close(h);}
static void once(void){assert(queued);void (*fn)(void *)=queued;void *ctx=context;queued=NULL;fn(ctx);}
static void drain(void){unsigned n=0;while(queued){assert(++n<50000);once();}assert(!writers && !handles);}
static void reset(void){cancelled=refuse=fail_write=fail_size=corrupt=false;assert(!queued && !writers && !handles);assert(access(TEMP,F_OK));}
int main(void){assert(!mkdir("sdmc:",0700));assert(!mkdir("sdmc:/3ds",0700));assert(!mkdir("sdmc:/3ds/BreadBox",0700));
 assert(bb_sd_bench_start());assert(!bb_sd_bench_start());drain();assert(success==1 && failure==0 && reports==32 && preallocated==4);reset();
 FILE *f=fopen(TEMP,"wb");assert(f);assert(fputs("owned by someone else",f)>=0);assert(!fclose(f));assert(bb_sd_bench_start());drain();
 char text[40]={0};f=fopen(TEMP,"rb");assert(fread(text,1,sizeof(text),f)==21);assert(!strcmp(text,"owned by someone else"));assert(!fclose(f));assert(!unlink(TEMP));reset();
 refuse=true;assert(!bb_sd_bench_start());reset();
 assert(bb_sd_bench_start());once();assert(!access(TEMP,F_OK));cancelled=true;drain();reset();
 // Inject native write failure, preallocation failure and a corrupt readback.
 for(unsigned test=0;test<3;test++){
  fail_write=test==0;fail_size=test==1;corrupt=test==2;
  assert(bb_sd_bench_start());drain();reset();
 }
 assert(success==1 && failure==6);puts("SD diagnostic: data equality, exclusive ownership, cancellation, queue/SD-full and corrupt readback PASS");
}
