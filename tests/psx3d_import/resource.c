/* Inject destination comparison/validation allocation failures, then reopen
 * exactly the same destination: no receipt may permanently skip promotion. */
#define main fixture_main
#include "check.c"
#undef main
#include <malloc.h>
static char target[1024];
static unsigned fail_at,opens,injected;
static bool fail_buffer,fail_growth;
static unsigned source_opens;
static unsigned probe_failures;static int probe_errno;
int import_probe_error(const char *p)
{if(probe_failures && !strcmp(p,target)){probe_failures--;injected++;return probe_errno;}return 0;}
void import_opened(const char *p,const char *m)
{if(!strcmp(m,"rb") && strstr(p,"source/states/G.st5"))source_opens++;fail_buffer=fail_at && !strcmp(p,target) && !strcmp(m,"rb") && ++opens==fail_at;}
void *__real_memalign(size_t,size_t);
void *__wrap_memalign(size_t a,size_t n)
{if(fail_buffer){fail_buffer=false;injected++;errno=ENOMEM;return NULL;}return __real_memalign(a,n);}
void *__real_realloc(void *,size_t);
void *__wrap_realloc(void *p,size_t n)
{if(fail_growth && n>0x440000){fail_growth=false;injected++;errno=ENOMEM;return NULL;}return __real_realloc(p,n);}
static psx_import_result run(void)
{
 psx_import_cursor *c=psx_import_open(src,dst,false);assert(c);
 psx_import_result r;unsigned ticks=0;while(!psx_import_step(c,&r))assert(++ticks<100000);
 if(!r.failed)assert(psx_import_finish(c));psx_import_close(c);return r;
}
int main(int argc,char **argv)
{
 assert(argc==3 && !chdir(argv[1]));unsigned scenario=atoi(argv[2]);
 extern void psx_import_test_errno_trace(void);psx_import_test_errno_trace();
 strcpy(src,"source");strcpy(dst,"dest");setup(src);setup(dst);
 void *older=state(100),*newer=state(300);
 put(src,"states/G.st5",newer,0x440000);put(dst,"states/G.st5",older,0x440000);
 free(older);free(newer);at(target,dst,"states/G.st5");
 if(scenario==10){
  extern bool psx_import_test_probe(const char *,bool,bool *);bool error;
  extern bool psx_import_test_write_permit(bool);assert(!psx_import_test_write_permit(true) && psx_import_test_write_permit(false));
  for(unsigned mode=0;mode<2;mode++){
   probe_failures=1;probe_errno=EIO;
   assert(psx_import_test_probe(target,mode,&error)==!mode && error);
   probe_failures=1;probe_errno=ENOENT;
   assert(!psx_import_test_probe(target,mode,&error) && !error);
   probe_failures=1;probe_errno=ENOTDIR;
   assert(!psx_import_test_probe(target,mode,&error) && !error);
  }
  puts("Unknown metadata is never absence or a regular file: PASS");return 0;
 }
 if(scenario>=7){
  if(scenario==7)at(target,src,"states/G.st5"); /* Original source-side allocation bug. */
  else {probe_failures=1;probe_errno=scenario==8?EIO:ENOMEM;}
  fail_at=scenario==7?1:0;
  unsigned old_crc=crcfile(dst,"states/G.st5"),source_crc=crcfile(src,"states/G.st5");opens=0;source_opens=0;
  psx_import_result first=run();assert(injected==1 && first.failed && !first.rejected && !first.copied);
  if(scenario>=8)assert(!source_opens); /* Stop before expensive validation on unknown ownership. */
  assert(crcfile(dst,"states/G.st5")==old_crc && crcfile(src,"states/G.st5")==source_crc);
  fail_at=0;first=run();assert(first.copied==1 && !first.failed && !first.rejected);
  first=run();assert(!first.copied && !first.failed && !first.rejected);
  puts("Source allocation / metadata error preserves destination and retries: PASS");return 0;
 }
 if(scenario==6){
  put(src,"states/H.st5","bad",3);void *backup=state(400);put(src,"states/H.st5.bak",backup,0x440000);free(backup);
  fail_at=1;psx_import_result partial=psx_import_run(src,dst,NULL,NULL);
  assert(injected==1 && partial.failed && !partial.rejected && partial.copied==1);
  assert(crcfile(src,"states/H.st5.bak")==crcfile(dst,"states/H.st5"));
  fail_at=0;partial=psx_import_run(src,dst,NULL,NULL);assert(!partial.failed && !partial.rejected);
  puts("Resource flag does not poison later recovery candidates: PASS");return 0;
 }
 if(scenario==5){FILE *f=fopen(target,"ab");assert(f && fputc(0,f)!=EOF && !fclose(f));fail_growth=true;}
 else fail_at=scenario;
 unsigned original=crcfile(src,"states/G.st5");opens=0;
 psx_import_result r=run();assert(injected==1 && r.failed && !r.rejected && !r.copied);
 assert(crcfile(src,"states/G.st5")==original);
 fail_at=0;fail_growth=false;r=run();assert(!r.failed && !r.rejected);
 if(scenario!=5){assert(r.copied==1);unsigned why,saved,id;assert(state_file_info(target,&why,&saved,&id) && saved==300);}
 r=run();assert(!r.failed && !r.copied && !r.rejected);
 puts("Destination allocation failure retries without final receipt: PASS");
}
