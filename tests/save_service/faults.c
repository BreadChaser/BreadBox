/* Fail real production file operations, then destroy/replace the mutable core. */
#define main legacy_powercuts_main
#define safe_test_rename base_rename
#define safe_test_fwrite base_write
#define safe_test_fread base_read
#define safe_test_fclose base_close
#include "check.c"
#undef main
#undef safe_test_rename
#undef safe_test_fwrite
#undef safe_test_fread
#undef safe_test_fclose
#include "bb_final.h"
static const char *target;
static int fault;
static bool oom;static int calloc_fail_after=-1;
void *__real_malloc(size_t);void *__real_calloc(size_t,size_t);
void *__wrap_malloc(size_t n){return oom?NULL:__real_malloc(n);}
void *__wrap_calloc(size_t n,size_t z){if(calloc_fail_after==0){calloc_fail_after=-1;return NULL;}if(calloc_fail_after>0)calloc_fail_after--;return oom?NULL:__real_calloc(n,z);}
FILE *__real_fopen(const char *,const char *);
FILE *__wrap_fopen(const char *p,const char *m){return fault==1 && strstr(p,target) && strchr(m,'w')?NULL:__real_fopen(p,m);}
static bool selected(FILE *f)
{
 char link[64],path[1024];snprintf(link,sizeof(link),"/proc/self/fd/%d",fileno(f));
 ssize_t n=readlink(link,path,sizeof(path)-1);if(n<0)return false;path[n]=0;return strstr(path,target)!=NULL;
}
int safe_test_rename(const char *a,const char *b){return fault==4 && strstr(a,target)?-1:base_rename(a,b);}
size_t safe_test_fwrite(const void *p,size_t z,size_t n,FILE *f){return fault==2 && selected(f)?0:fwrite(p,z,n,f);}
size_t safe_test_fread(void *p,size_t z,size_t n,FILE *f){return fread(p,z,n,f);}
int safe_test_fclose(FILE *f){bool fail=fault==3 && selected(f);int rc=fclose(f);return fail?-1:rc;}
int main(int argc,char **argv)
{
 assert(argc==2);assert(!chdir(argv[1]));
 for(int end_continue=0;end_continue<2;end_continue++)for(int fallback=0;fallback<4;fallback++)for(int file=0;file<3;file++)for(int cut=1;cut<=4;cut++){
  char dir[32];snprintf(dir,sizeof(dir),"end%d-f%d-file%d-cut%d",end_continue,fallback,file,cut);assert(!mkdir(dir,0700));assert(!chdir(dir));history_set_root("history");
  target=file==0?"battery.sav":file==1?"battery.rtc":"game.state10";
  if(fallback==1)calloc_fail_after=1;
  begin();memset(ram,0x31,128);rtc[0]=3131;position=1;assert(bb_save_flush());assert(bb_save_state(10,true));
  memset(ram,0x42,128);rtc[0]=4242;position=2;fault=cut;
  if(file==2 && !end_continue){
   if(fallback==2)calloc_fail_after=0;   /* a failed allocation at the end: a point needs none for its metadata (r1c) */
   bool queued=bb_save_state_end(10,fallback!=3);
   assert(queued==(fallback!=3));   /* 3: written now on this thread (the one writer), failing */
  }
  if(file==2 && end_continue && fallback==2)calloc_fail_after=0;
  bool ended=bb_save_end(end_continue);if(file!=2 || end_continue)assert(!ended);
  if(fallback==2)calloc_fail_after=-1;   /* unused now: not a later begin's */
  bb_save_settle();assert(bb_save_final_failed());
  if(end_continue && file!=2){
   FILE *f=fopen("game.state10","rb");assert(f && !fseek(f,28,SEEK_SET) && fgetc(f)==1);fclose(f);
  }
  if(file==2){uint32_t attempted=0;assert(bb_final_status("game.state10",&attempted)==BB_FINAL_FAILED && attempted);}
  memset(ram,0,128);rtc[0]=0;position=0;char error[256];assert(!bb_save_begin(&config,error,sizeof(error)));
  assert(!bb_save_queue());assert(bb_save_end(false));fault=0;
  assert(bb_save_final_retry());disk(0x42);begin();assert(rtc[0]==4242);
  if(file==2)assert(bb_save_state(10,false) && position==2);
  assert(bb_save_end(false));assert(!chdir(".."));
 }
 /* Final snapshot storage is reserved at begin; OOM there prevents gameplay. */
 assert(!mkdir("oom",0700));assert(!chdir("oom"));history_set_root("history");
 char error[256];oom=true;assert(!bb_save_begin(&config,error,sizeof(error)));oom=false;
 begin();memset(ram,0x51,128);assert(bb_save_flush());position=3;assert(bb_save_state(10,true));
 oom=true;assert(!bb_save_state_end(10,true));oom=false;
 assert(bb_save_end(false));bb_save_settle();assert(bb_save_final_failed());
 /* A failed capture never masquerades as the newer Continue; acknowledge loss explicitly. */
 assert(!bb_save_final_retry());bb_save_final_discard();begin();assert(bb_save_state(10,false) && position==3);assert(bb_save_end(false));
 /* A failed latest capture cannot be certified by an older successful drain. */
 begin();memset(ram,0x61,128);assert(bb_save_flush());memset(ram,0x62,128);rtc[2]=999;
 assert(!bb_save_end(false));assert(bb_save_final_failed());disk(0x61);rtc[2]=30;
 assert(!bb_save_final_retry());bb_save_final_discard();
 puts("Final battery/RTC/Continue: open/write/close/rename faults, core destruction, owned retry, allocation refusal PASS");return 0;
}
