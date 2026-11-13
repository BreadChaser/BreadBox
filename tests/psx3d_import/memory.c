/* One state-sized allocation must survive fragmented-heap menu activity.
 * In particular destination validation/history must displace, not duplicate,
 * the retained source body. Resource exhaustion must never reject a state. */
#define main fixture_main
#include "check.c"
#undef main
#include <pthread.h>
#include "safefile.h"
#include "history.h"
static bool constrained;
static unsigned large_allocations,metadata_operations,stack_allocations;
void import_metadata_operation(void){if(constrained)metadata_operations++;}
void *__real_malloc(size_t);
void *__wrap_malloc(size_t n)
{
 if(constrained && n==192*1024)stack_allocations++;
 if(constrained && n>=0x440000 && large_allocations++){errno=ENOMEM;return NULL;}
 return __real_malloc(n);
}
static bool refuse_checkpoint(void *ctx){(void)ctx;return false;}
static unsigned history_calls;
static bool refuse_history(void *ctx){(void)ctx;history_calls++;return false;}
static void *other_thread(void *ctx){(void)ctx;assert(safe_checkpoint());hist_entry_t entries[1];history_list("Absent",false,entries,1);return NULL;}
static bool error_task(void *ctx){(void)ctx;if(!safe_checkpoint() || !safe_checkpoint())return false;errno=ENOSPC;return false;}
static bool cancelled_task(void *ctx){bool *cancelled=ctx;*cancelled=!safe_checkpoint();return !*cancelled;}
static bool commit_playlog(void *ctx)
{const char *path=ctx;FILE *f=safe_begin(path);return f && fputs("new playlog\n",f)>=0 && safe_finish(f,path,true);}
static bool finish_task(void *ctx){return psx_import_finish(ctx);}
int main(int argc,char **argv)
{
 assert(argc==2 && !chdir(argv[1]));
 safe_set_checkpoint(refuse_checkpoint,NULL);history_set_checkpoint(refuse_history,NULL);pthread_t thread;assert(!pthread_create(&thread,NULL,other_thread,NULL));assert(!pthread_join(thread,NULL));assert(!safe_checkpoint());assert(!history_calls);hist_entry_t entries[1];history_list("Absent",false,entries,1);assert(history_calls);history_set_checkpoint(NULL,NULL);safe_set_checkpoint(NULL,NULL);strcpy(src,"source");strcpy(dst,"dest");setup(src);setup(dst);
 void *old=state(100),*newer=state(300);
 put(src,"states/G.sta",old,0x440000);put(dst,"states/G.sta",newer,0x440000);
 thumbnail_file(src,"states/G.sta.thm",100);thumbnail_file(dst,"states/G.sta.thm",300);
 memset((char *)old+0x440000-sizeof(state_trailer_t),0,sizeof(state_trailer_t));
 put(src,"states/Legacy [SCUS94900].st9",old,0x440000);free(old);free(newer);
 constrained=true;large_allocations=0;
 psx_import_cursor *c=psx_import_open(src,dst,false);assert(c);psx_import_result r;
 unsigned ticks=0;bool done=false;while(!done){metadata_operations=0;uint64_t meta=psx_import_io_counts(c).metadata_calls;done=psx_import_step(c,&r);assert(psx_import_io_counts(c).metadata_calls-meta<=1);assert(metadata_operations<=1);assert(safe_checkpoint());assert(++ticks<100000);}
 assert(!r.failed && !r.rejected && r.conflicts);
 metadata_operations=0;bool ok=false;while(!psx_import_task_step(c,finish_task,c,&ok)){assert(metadata_operations<=1);metadata_operations=0;}assert(ok && metadata_operations<=1);
 errno=0;while(!psx_import_task_step(c,error_task,NULL,&ok))assert(safe_checkpoint());assert(!ok && errno==ENOSPC);
 bool cancelled=false;assert(!psx_import_task_step(c,cancelled_task,&cancelled,&ok));psx_import_close(c);assert(cancelled && safe_checkpoint());
 assert(large_allocations==1 && stack_allocations==1);
 assert(crcfile(src,"states/Legacy [SCUS94900].st9")==crcfile(dst,"states/Legacy [SCUS94900].st9"));
 unsigned why,saved,id;char p[1024];at(p,dst,"states/G.sta");assert(state_file_info(p,&why,&saved,&id) && saved==300);
 /* The next reserve is refused; opening must fail explicitly, rather than
  * publish a completion manifest with false validation rejections. */
 assert(!psx_import_open(src,dst,true));
 constrained=false;c=psx_import_open(src,dst,true);assert(c);
 while(!psx_import_step(c,&r)){}assert(!r.failed);
 write_file("dest/saves/playlog.ini","old playlog\n",12);
 bool in_gap=false;for(unsigned i=0;i<1000;i++){
  bool done=psx_import_task_step(c,commit_playlog,"dest/saves/playlog.ini",&ok);
  if(access("dest/saves/playlog.ini",F_OK) && !access("dest/saves/playlog.ini.bak",F_OK)){in_gap=true;break;}
  assert(!done);
 }
 assert(in_gap);
 for(unsigned i=0;;i++){
  uint64_t meta=psx_import_io_counts(c).metadata_calls;bool cancelled=psx_import_cancel_step(c);
  assert(psx_import_io_counts(c).metadata_calls-meta<=1 && i<1000);if(cancelled)break;
 }
 psx_import_close(c); // game launch/HOME cancels at the FAT rename gap
 FILE *f=fopen("dest/saves/playlog.ini","rb");char text[32]={0};assert(f && fread(text,1,12,f)==12 && !fclose(f));assert(!strcmp(text,"old playlog\n"));
 assert(access("dest/saves/playlog.ini.tmp",F_OK));
 // A process/power cut at the same gap is recovered before playlog loading.
 assert(!rename("dest/saves/playlog.ini","dest/saves/playlog.ini.bak"));
 write_file("dest/saves/playlog.ini.tmp","new playlog\n",12);safe_recover("dest/saves/playlog.ini");
 assert(!access("dest/saves/playlog.ini",F_OK) && access("dest/saves/playlog.ini.tmp",F_OK));
 puts("Import one-state allocation limit and one metadata operation per permit: PASS");
}
