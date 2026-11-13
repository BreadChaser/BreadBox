/* Remote SD throughput diagnostic; GPL-2.0-or-later. */
#include "app_policy.h"
#include "bb_sd_bench.h"
#include "bb_writers.h"
#include "bb_index.h"
#include <3ds.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define COLD __attribute__((noinline,section(".psx3d_cold.text")))
#define TEMP APP_SD_ROOT "/sd-throughput.tmp"
#define TOTAL (32u*1024*1024)
#define BUFFER (1024*1024)
extern void log_printf(const char *,...);
typedef struct {
 unsigned api,test,at,phase;bool owned,failed;
 FILE *file;Handle handle;
 unsigned char *buffer,*stdio_buffer;
 u64 ticks,max_ticks;
} sd_bench;
static unsigned busy;
static const unsigned chunks[]={16*1024,64*1024,256*1024,1024*1024};
static const char *names[]={"stdio-default","stdio-256k","FSFILE","FSFILE-preallocated"};
static COLD bool close_file(sd_bench *b)
{
 bool ok=true;
 if(b->file){ok=!fclose(b->file);b->file=NULL;}
 if(b->handle){ok=R_SUCCEEDED(FSFILE_Close(b->handle)) && ok;b->handle=0;}
 return ok;
}
static COLD bool open_file(sd_bench *b,bool write)
{
 if(b->api<2) {
  int fd=open(TEMP,write?O_WRONLY|O_CREAT|O_EXCL:O_RDONLY,0600);
  if(fd<0)return false;
  if(write)b->owned=true;
  b->file=fdopen(fd,write?"wb":"rb");if(!b->file){close(fd);return false;}
  if(b->api==1 && setvbuf(b->file,(char *)b->stdio_buffer,_IOFBF,256*1024))return false;
  return true;
 }
 FS_Path path=fsMakePath(PATH_ASCII,TEMP+5);
 if(write){FS_Archive archive;Result rc=FSUSER_OpenArchive(&archive,ARCHIVE_SDMC,fsMakePath(PATH_EMPTY,""));if(R_FAILED(rc))return false;
  rc=FSUSER_CreateFile(archive,path,0,0);FSUSER_CloseArchive(archive);if(R_FAILED(rc))return false;b->owned=true;}
 Result rc=FSUSER_OpenFileDirectly(&b->handle,ARCHIVE_SDMC,fsMakePath(PATH_EMPTY,""),path,write?FS_OPEN_WRITE:FS_OPEN_READ,0);
 if(R_FAILED(rc))return false;
 if(write && b->api==3){u64 start=svcGetSystemTick();rc=FSFILE_SetSize(b->handle,TOTAL);
  log_printf("sd-bench: preallocate chunk=%u us=%llu ok=%d\n",chunks[b->test],(unsigned long long)((svcGetSystemTick()-start)/(SYSCLOCK_ARM11/1000000)),R_SUCCEEDED(rc));}
 return R_SUCCEEDED(rc);
}
static COLD void finish(sd_bench *b)
{
 if(!close_file(b))b->failed=true;
 if(b->owned && unlink(TEMP))b->failed=true;
 log_printf("sd-bench: %s\n",b->failed?"FAILED/cancelled":"DONE");
 free(b->buffer);free(b->stdio_buffer);free(b);bb_writer_end();__atomic_store_n(&busy,0,__ATOMIC_RELEASE);
}
static COLD void step(void *ctx)
{
 sd_bench *b=ctx;unsigned n=chunks[b->test];
 if(bb_index_cancelled()){b->failed=true;finish(b);return;}
 if(b->phase==0){
  if(!open_file(b,true)){b->failed=true;finish(b);return;}
  b->phase=1;b->ticks=b->max_ticks=0;b->at=0;
 } else if(b->phase==1 || b->phase==3) {
  bool write=b->phase==1;u32 got=0;u64 start=svcGetSystemTick();bool ok;
  if(b->api<2)ok=(write?fwrite(b->buffer,1,n,b->file):fread(b->buffer,1,n,b->file))==n;
  else {Result rc=write?FSFILE_Write(b->handle,&got,b->at,b->buffer,n,0):FSFILE_Read(b->handle,&got,b->at,b->buffer,n);ok=R_SUCCEEDED(rc) && got==n;}
  u64 elapsed=svcGetSystemTick()-start;b->ticks+=elapsed;if(elapsed>b->max_ticks)b->max_ticks=elapsed;
  if(!ok){b->failed=true;finish(b);return;}
  if(!write){for(unsigned i=0;i<n;i++)if(b->buffer[i]!=(unsigned char)i){b->failed=true;finish(b);return;}}
  b->at+=n;
  if(b->at==TOTAL)b->phase++;
 } else if(b->phase==2 || b->phase==4) {
  bool write=b->phase==2;u64 start=svcGetSystemTick();bool ok=true;
  if(write)ok=b->file?(!fflush(b->file) && !fsync(fileno(b->file))):R_SUCCEEDED(FSFILE_Flush(b->handle));
  ok=close_file(b) && ok;u64 elapsed=svcGetSystemTick()-start;b->ticks+=elapsed;
  log_printf("sd-bench: api=%s chunk=%u %s bytes=%u us=%llu max_io_us=%llu finalize_us=%llu ok=%d\n",names[b->api],n,write?"write":"read",TOTAL,
   (unsigned long long)(b->ticks/(SYSCLOCK_ARM11/1000000)),(unsigned long long)(b->max_ticks/(SYSCLOCK_ARM11/1000000)),(unsigned long long)(elapsed/(SYSCLOCK_ARM11/1000000)),ok);
  if(!ok){b->failed=true;finish(b);return;}
  if(write){if(!open_file(b,false)){b->failed=true;finish(b);return;}b->phase=3;b->at=0;b->ticks=b->max_ticks=0;}
  else {if(unlink(TEMP)){b->failed=true;finish(b);return;}b->owned=false;
   if(++b->test==4){b->test=0;b->api++;}if(b->api==4){finish(b);return;}b->phase=0;}
 }
 if(!bb_index_defer_low(step,b)){b->failed=true;finish(b);}
}
COLD bool bb_sd_bench_start(void)
{
 unsigned expected=0;if(!__atomic_compare_exchange_n(&busy,&expected,1,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return false;
 if(!bb_writer_begin()){__atomic_store_n(&busy,0,__ATOMIC_RELEASE);return false;}
 sd_bench *b=calloc(1,sizeof(*b));
 if(b){b->buffer=memalign(4096,BUFFER);b->stdio_buffer=memalign(4096,256*1024);}
 if(!b || !b->buffer || !b->stdio_buffer){if(b){free(b->buffer);free(b->stdio_buffer);free(b);}bb_writer_end();__atomic_store_n(&busy,0,__ATOMIC_RELEASE);return false;}
 for(unsigned i=0;i<BUFFER;i++)b->buffer[i]=(unsigned char)i;
 if(!bb_index_defer_low(step,b)){b->failed=true;finish(b);return false;}
 return true;
}
