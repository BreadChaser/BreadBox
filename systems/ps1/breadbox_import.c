/* PSX3D import: immutable verified copies before publication, receipts last.
 * Kept outside the save ring as well: retention must never erase the only
 * remaining copy of a user's pre-import file. GPL-2.0-or-later. */

#include "breadbox_import.h"
#include "history.h"
#include "statefile.h"
#include "safefile.h"
#include "fs_path.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>
#ifdef __3DS__
#include <3ds.h>
#else
#include <ucontext.h>
#include <time.h>
#endif
#if defined(__3DS__) || defined(PSX_IMPORT_NATIVE_CRC)
/* The loader already supplies the native, four-byte CRC-32 implementation.
 * Use that same checksum for streaming/receipts instead of portlibs zlib. */
#define crc32(seed,data,n) state_crc32((data),(n),(seed))
#endif
#define COLD __attribute__((noinline,section(".psx3d_cold.text")))
#define PATH_CAP 1024
#define MAX_FILE (8u*1024*1024)
extern void log_printf(const char *,...);
typedef struct { size_t size; unsigned crc, saved, saves; bool weak; } file_info;
enum { OTHER, CARD, STATE, THUMB, SETTINGS, BLOB };   /* BLOB: copied as it is (cheats, BIOS) */
/* PSX3D folders copied into BreadBox under the same names (J-06: cheats and BIOS too). */
static const char *const import_folders[]={"saves","states","games","cheats","bios"};
#define IMPORT_FOLDERS (sizeof(import_folders)/sizeof(*import_folders))
/* Only immutable legacy STATE selections are cached, for the lifetime of a
 * cursor. Thumbnails probe their matching state repeatedly. Destination
 * validation is never cached; copy/readback still verifies current bytes. */
 typedef struct source_cache {struct source_cache *next;char base[PATH_CAP],selected[PATH_CAP];file_info info;} source_cache;
static source_cache **active_cache;
static const char *active_source;
static unsigned active_cached;

/* A private suspended stack preserves the recovery transaction while bounded
 * transfers and CRC slices yield back to the shared LOW worker. */
static bool resource_error;
static bool *no_space;
static psx_import_io *metrics;
static COLD void trace_io(const char *op,const char *path,bool metadata)
{if(metrics){snprintf(metrics->last_op,sizeof(metrics->last_op),"%s",op);snprintf(metrics->last_path,sizeof(metrics->last_path),"%s",path);if(metadata)metrics->metadata_calls++;}}
static COLD bool turn(void);
static COLD uint64_t microseconds(void);
/* A late probe failure may occur inside publication itself. Refuse subsequent
 * writes/renames as well as the high-level decision; read-only enumeration and
 * mandatory closes remain available so a later logical file can recover. */
static COLD bool io_permit(bool write){bool ok=turn();return ok && (!write || !resource_error);}
static COLD const char *file_path(FILE *f);
static COLD unsigned import_crc(unsigned seed,const void *p,size_t n)
{if(!turn())return seed;uint64_t began=microseconds();unsigned result=crc32(seed,p,n);if(metrics){metrics->crc_us+=microseconds()-began;metrics->crc_bytes+=n;}return result;}
#undef crc32
#define crc32(seed,p,n) import_crc(seed,p,n)

static COLD size_t import_read(void *p,size_t size,size_t n,FILE *f)
{if(!turn()){errno=ECANCELED;return 0;}trace_io("fread",file_path(f),false);uint64_t began=microseconds();size_t got=fread(p,size,n,f);uint64_t elapsed=microseconds()-began;
 if(elapsed>50000)log_printf("import: slow op=fread path=%s us=%llu\n",file_path(f),(unsigned long long)elapsed);
 if(metrics){metrics->read_bytes+=got*size;metrics->read_us+=elapsed;metrics->read_calls++;if(elapsed>metrics->max_read_us)metrics->max_read_us=elapsed;}return got;}
static COLD size_t import_write(const void *p,size_t size,size_t n,FILE *f)
{if(!io_permit(true)){errno=ECANCELED;return 0;}trace_io("fwrite",file_path(f),false);uint64_t began=microseconds();size_t put=fwrite(p,size,n,f);uint64_t elapsed=microseconds()-began;
 if(put<n && errno==ENOSPC && no_space)*no_space=true;
 if(elapsed>50000)log_printf("import: slow op=fwrite path=%s us=%llu\n",file_path(f),(unsigned long long)elapsed);
 if(metrics){metrics->written_bytes+=put*size;metrics->write_us+=elapsed;metrics->write_calls++;if(elapsed>metrics->max_write_us)metrics->max_write_us=elapsed;}return put;}
#define fread import_read
#define fwrite import_write
static bool (*io_turn)(void *);
static void *io_context;
static bool budget_cancelled,history_budget_active;
static COLD bool turn(void) {bool ok=!io_turn || io_turn(io_context);if(!ok)budget_cancelled=true;return ok;}
/* Count every filesystem call toward the permit, including metadata probes
 * between body slices. Cleanup closes still run after cancellation. A single
 * blocking SD call can exceed the deadline; no group of calls runs unchecked. */
#define IO_TIMED(call,field,p) ({const char *io_path=(p);trace_io(#call,io_path,strcmp(#field,"read")!=0);uint64_t began=microseconds();__auto_type result=(call);uint64_t elapsed=microseconds()-began;int error=errno;if(metrics){metrics->field##_us+=elapsed;metrics->field##_calls++;}if(elapsed>50000)log_printf("import: slow op=%s path=%s us=%llu errno=%d\n",#call,io_path,(unsigned long long)elapsed,error);errno=error;result;})
#define IO_REQUIRE() do {if(!turn()){errno=ECANCELED;return -1;}}while(0)
#define IO_MUTATE() do {if(!io_permit(true)){errno=resource_error?ENOMEM:ECANCELED;return -1;}}while(0)
static COLD int io_stat(const char *p,struct stat *s){IO_REQUIRE();return IO_TIMED(stat(p,s),stat,p);}
static COLD int io_lstat(const char *p,struct stat *s){IO_REQUIRE();return IO_TIMED(lstat(p,s),stat,p);}
static COLD int io_mkdir(const char *p,mode_t mode){IO_MUTATE();return IO_TIMED(mkdir(p,mode),mutate,p);}
static COLD int io_unlink(const char *p){IO_MUTATE();return IO_TIMED(unlink(p),mutate,p);}
static COLD int io_rename(const char *a,const char *b){IO_MUTATE();return IO_TIMED(rename(a,b),mutate,a);}
static __attribute__((unused)) COLD int io_fseek(FILE *f,long at,int whence){IO_REQUIRE();return IO_TIMED(fseek(f,at,whence),seek,file_path(f));}
static COLD int io_fgetc(FILE *f)
{IO_REQUIRE();int ch=IO_TIMED(fgetc(f),read,file_path(f));if(metrics && ch!=EOF)metrics->read_bytes++;return ch;}
static COLD char *io_fgets(char *p,int n,FILE *f)
{if(!turn()){errno=ECANCELED;return NULL;}char *got=IO_TIMED(fgets(p,n,f),read,file_path(f));if(metrics && got)metrics->read_bytes+=strlen(got);return got;}
static COLD int io_fflush(FILE *f){IO_MUTATE();return IO_TIMED(fflush(f),flush,file_path(f));}
static COLD int io_fsync(int fd,const char *p){IO_MUTATE();return IO_TIMED(fsync(fd),flush,p);}
/* Keep each body transfer within the 64 KiB permit, with an aligned buffer
 * owned until fclose, including cancellation. Avoid newlib's SD buffer setup. */
typedef struct import_buffer {struct import_buffer *next;FILE *file;void *data;char path[PATH_CAP];} import_buffer;
static import_buffer *buffers;
static COLD const char *file_path(FILE *f)
{for(import_buffer *b=buffers;b;b=b->next)if(b->file==f)return b->path;return "<stream>";}
static COLD int io_fclose(FILE *);
static COLD FILE *io_fopen(const char *p,const char *mode)
{
 if(!io_permit(mode[0]!='r' || strchr(mode,'+'))){errno=resource_error?ENOMEM:ECANCELED;return NULL;}
 FILE *f=IO_TIMED(fopen(p,mode),open,p);if(!f){if(errno==ENOMEM)resource_error=true;return NULL;}
 import_buffer *b=malloc(sizeof(*b));void *data=memalign(4096,65536);
 if(!b || !data || setvbuf(f,data,_IOFBF,65536)) {
  io_fclose(f);free(b);free(data);resource_error=true;errno=ENOMEM;log_printf("import: failure op=stdio-buffer path=%s errno=%d\n",p,errno);return NULL;
 }
 *b=(import_buffer){.next=buffers,.file=f,.data=data};snprintf(b->path,sizeof(b->path),"%s",p);buffers=b;return f;
}
/* At most two nested importer directories; keep names without extra allocation. */
static struct {DIR *dir;char path[PATH_CAP];} traced_dirs[4];
static COLD const char *directory_path(DIR *d)
{for(unsigned i=0;i<4;i++)if(traced_dirs[i].dir==d)return traced_dirs[i].path;return "<directory>";}
static COLD DIR *io_opendir(const char *p)
{
 if(!turn()){errno=ECANCELED;return NULL;}DIR *d=IO_TIMED(opendir(p),dir,p);
 if(d)for(unsigned i=0;i<4;i++)if(!traced_dirs[i].dir){traced_dirs[i].dir=d;snprintf(traced_dirs[i].path,sizeof(traced_dirs[i].path),"%s",p);break;}
 return d;
}
static COLD struct dirent *io_readdir(DIR *d){if(!turn()){errno=ECANCELED;return NULL;}return IO_TIMED(readdir(d),dir,directory_path(d));}
static COLD int io_fclose(FILE *f)
{
 import_buffer **p=&buffers;while(*p && (*p)->file!=f)p=&(*p)->next;
 import_buffer *b=*p;const char *label=b?b->path:"<stream>";if(b)*p=b->next;
 (void)turn();int rc=IO_TIMED(fclose(f),close,label);
 if(b){free(b->data);free(b);}return rc;
}
static COLD int io_closedir(DIR *d){(void)turn();int rc=IO_TIMED(closedir(d),dir,directory_path(d));for(unsigned i=0;i<4;i++)if(traced_dirs[i].dir==d)traced_dirs[i].dir=NULL;return rc;}
#undef IO_REQUIRE
#undef IO_MUTATE
#undef IO_TIMED
#define stat(...) io_stat(__VA_ARGS__)
#define lstat io_lstat
#define mkdir io_mkdir
#define unlink io_unlink
#define rename io_rename
#define fgetc io_fgetc
#define fgets io_fgets
#define fseek io_fseek
#define fflush io_fflush
#define fsync(fd) io_fsync(fd,file_path(f))
#define fopen io_fopen
#define opendir io_opendir
#define readdir io_readdir
#define fclose io_fclose
#define closedir io_closedir
#define IO_SLICE 65536u
static COLD unsigned sliced_crc(unsigned crc,const void *data,size_t n)
{
 const unsigned char *p=data;
 while(n) {size_t k=n>IO_SLICE?IO_SLICE:n;if(!turn())return crc;
  crc=crc32(crc,p,k);p+=k;n-=k;}
 return crc;
}
/* A source loaded for validation can feed its publication without another SD
 * read. Only one body's memory is retained, never the entire source set. */
static char loaded_path[PATH_CAP];
static void *loaded_data;
static bool loaded_owned;
/* Reserve the normal PS1 state body before import yields to menu jobs. Reuse
 * it for source/destination validation instead of relying on a fresh large
 * contiguous allocation after every file. A displaced source streams from SD. */
static void **workspace;
static size_t *workspace_capacity;
static file_info loaded_info;
static COLD void unload(void){if(loaded_owned)free(loaded_data);loaded_data=NULL;loaded_owned=false;loaded_path[0]=0;}
static const char *transaction_source;
static bool transaction_force,transaction_changed;
static COLD bool flush_file(FILE *);
#ifdef PSX_IMPORT_TEST
int psx_import_stop_after=-1;
static COLD bool step(void) { if(psx_import_stop_after<0)return true;if(!psx_import_stop_after)return false;psx_import_stop_after--;return true; }
#else
static COLD bool step(void) { return true; }
#endif
static COLD bool path(char *out,const char *a,const char *b)
{
 return fs_path_join(out,PATH_CAP,a,b);
}
/* Unknown is never absence: callers using !exists must fail closed before
 * any rename. The sticky resource flag also prevents receipts for this file. */
static COLD int probe(const char *p,struct stat *st)
{
 if(!lstat(p,st))return 1;
 if(errno==ENOENT || errno==ENOTDIR)return 0;
 resource_error=true;return -1;
}
static COLD bool exists(const char *p) { struct stat st;return probe(p,&st)!=0; }
static COLD bool regular(const char *p,struct stat *st)
{
 return probe(p,st)==1 && S_ISREG(st->st_mode);
}
#ifdef PSX_IMPORT_FAULT_TEST
#include <assert.h>
static bool refuse_test_turn(void *ctx){(void)ctx;return false;}
void psx_import_test_errno_trace(void)
{
 /* Refusal must override a preceding missing-file errno, without unlinking. */
 FILE *index=io_fopen("receipt-index","wb");assert(index && !io_fclose(index));
 io_turn=refuse_test_turn;errno=ENOENT;
 assert(io_unlink("receipt-index")==-1 && errno==ECANCELED);
 io_turn=NULL;resource_error=true;errno=ENOENT;
 assert(io_unlink("receipt-index")==-1 && errno==ENOMEM);
 resource_error=false;budget_cancelled=false;
 assert(exists("receipt-index"));assert(!io_unlink("receipt-index"));
 assert(!exists("receipt-index")); // next pass really invalidates the retained index
 psx_import_io io={0};metrics=&io;trace_io("slow-stat","receipt-index",true);
 (void)import_crc(0,"x",1);
 assert(!strcmp(io.last_op,"slow-stat") && !strcmp(io.last_path,"receipt-index"));
 assert(io.crc_bytes==1 && io.metadata_calls==1);metrics=NULL;
}
bool psx_import_test_write_permit(bool write){resource_error=true;bool ok=io_permit(write);resource_error=false;return ok;}
bool psx_import_test_probe(const char *p,bool use_regular,bool *error)
{struct stat st={.st_mode=S_IFREG};resource_error=false;bool value=use_regular?regular(p,&st):exists(p);*error=resource_error;return value;}
#endif
typedef struct directory_cache {struct directory_cache *next;char path[];} directory_cache;
static directory_cache **active_directories;
static COLD bool directory_known(const char *p)
{for(directory_cache *d=active_directories?*active_directories:NULL;d;d=d->next)if(!strcmp(d->path,p))return true;return false;}
static COLD void directory_remember(const char *p)
{if(!active_directories)return;unsigned n=0;for(directory_cache *d=*active_directories;d;d=d->next)if(++n>=1024)return;directory_cache *d=malloc(sizeof(*d)+strlen(p)+1);if(d){strcpy(d->path,p);d->next=*active_directories;*active_directories=d;}}
static COLD bool dirs(const char *file)
{
 char p[PATH_CAP];if(strlen(file)>=sizeof(p))return false;strcpy(p,file);
 /* A libctru device root is already mounted, not a directory to mkdir. */
 char *start=p+1,*device=strchr(p,':');
 if(device && device[1]=='/')start=device+2;
 for(char *s=start;*s;s++)if(*s=='/') {
  *s=0;struct stat st;
  if(!directory_known(p)){
   if(mkdir(p,0777) && (errno!=EEXIST || lstat(p,&st) || !S_ISDIR(st.st_mode)))return false;
   directory_remember(p);
  }
  *s='/';
 }
 return true;
}
static COLD int kind(const char *dir,const char *name)
{
 if(!strcmp(dir,"bios"))return BLOB;   /* every file, with or without an extension */
 const char *e=strrchr(name,'.');if(!e)return OTHER;
 if(!strcmp(dir,"games"))return !strcmp(e,".ini")?SETTINGS:OTHER;
 if(!strcmp(dir,"cheats"))return !strcmp(e,".on") || !strcmp(e,".txt") || !strcmp(e,".cht")?BLOB:OTHER;
 if(!strcmp(dir,"saves")) {
  if(!strcmp(e,".id"))return SETTINGS;
  return !strcmp(e,".srm") || !strcmp(e,".mcd") || !strcmp(e,".mcr")?CARD:OTHER;
 }
 if(!strcmp(e,".thm"))return THUMB;
 return !strcmp(e,".sta") || (strlen(e)==4 && e[1]=='s' && e[2]=='t' && e[3]>='0' && e[3]<='9')?STATE:OTHER;
}
/* Structural validation is the same check used before loading a PS1 state.
 * In particular an invalid sealed trailer never becomes a legacy state. */
static COLD bool inspect(const char *p,int type,file_info *info)
{
 struct stat st;if(!regular(p,&st) || (st.st_size<=0 && (st.st_size<0 || (type!=OTHER && type!=SETTINGS && type!=BLOB))) || st.st_size>MAX_FILE)return false;
 size_t n=(size_t)st.st_size;
 /* Opaque archive verification needs no whole-state heap allocation. */
 if(type==OTHER) {
  FILE *f=fopen(p,"rb");if(!f)return false;
  unsigned char buf[IO_SLICE];unsigned crc=0;size_t total=0,k;
  while(turn() && (k=fread(buf,1,IO_SLICE,f))) {total+=k;crc=crc32(crc,buf,k);}
  bool ok=!ferror(f) && total==n;if(fclose(f))ok=false;
  if(ok)*info=(file_info){.size=n,.crc=crc};return ok;
 }
 void *data;
 if(workspace) {
  unload();
  if(n>*workspace_capacity){void *more=realloc(*workspace,n);if(more){*workspace=more;*workspace_capacity=n;}else {resource_error=true;errno=ENOMEM;log_printf("import: failure op=validation-buffer-grow path=%s bytes=%lu errno=%d\n",p,(unsigned long)n,errno);return false;}}
  data=*workspace;
 } else data=malloc(n?n:1);
 if(!data){resource_error=true;errno=ENOMEM;log_printf("import: failure op=validation-buffer path=%s bytes=%lu errno=%d\n",p,(unsigned long)n,errno);return false;}
 FILE *f=fopen(p,"rb");bool ok=f!=NULL;size_t read=0;unsigned running_crc=0;
 while(ok && read<n) {size_t k=n-read>IO_SLICE?IO_SLICE:n-read;
  if(!turn() || fread((char *)data+read,1,k,f)!=k){ok=false;break;}
  running_crc=crc32(running_crc,(const unsigned char *)data+read,k);
  read+=k;}
 if(ok)ok=fgetc(f)==EOF && !ferror(f);
 if(f && fclose(f))ok=false;
 unsigned saved=0,saves=0;bool weak=false;
 if(ok && type==STATE) {
  /* state_check already verifies the sealed CRC. Read metadata without
   * asking state_trailer to hash the same multi-megabyte body again. */
  /* Validate the seal in yielding slices, then ask the loader for its
   * structural checks without repeating the payload CRC. This is private
   * memory; the source and serialized trailer are never altered. */
  unsigned char *seal=n>=sizeof(state_trailer_t)?(void *)((char *)data+n-sizeof(state_trailer_t)):NULL;
  state_trailer_t snapshot;
  const state_trailer_t *parsed=state_trailer(data,n,NULL);
  if(parsed)snapshot=*parsed;
  /* state_trailer returns shared static storage. A visible job may inspect
   * another state while this CRC yields, so retain our own trailer. */
  const state_trailer_t *meta=parsed?&snapshot:NULL;
  unsigned flags=meta?meta->flags:0;
  if(meta && (flags&STATE_SEALED)) {
   unsigned crc=sliced_crc(0,data,meta->data_len);
   crc=crc32(crc,(const void *)seal,offsetof(state_trailer_t,crc));
   ok=crc==meta->crc;unsigned unsealed=flags&~STATE_SEALED;memcpy(seal+offsetof(state_trailer_t,flags),&unsealed,sizeof(unsealed));
  }
  if(ok){state_result_t result=state_check(data,n);ok=result==STATE_OK;if(!ok)log_printf("import: validate path=%s op=state_check result=%d bytes=%lu\n",p,result,(unsigned long)n);}
  if(meta)memcpy(seal+offsetof(state_trailer_t,flags),&flags,sizeof(flags));
  const state_trailer_t *t=ok?state_trailer(data,n,NULL):NULL;
  if(t)saved=t->saved;
 }
 if(ok && type==CARD) {
  const unsigned char *c=data;ok=n==128*1024 && c[0]=='M' && c[1]=='C';
  for(unsigned frame=0;ok && frame<16;frame++) {
   unsigned char sum=0;for(unsigned i=0;i<127;i++)sum^=c[128*frame+i];
   if(sum!=c[128*frame+127])weak=true;
   if(frame && c[128*frame]==0x51)saves++;
  }
 }
 if(ok && type==THUMB) {
  const uint32_t *h=data;
  /* P3TH: u32 magic, u16 width/height, u32 timestamp. */
  ok=n==12+256*192*2 && h[0]==0x48543350 && h[1]==(256u | (192u<<16));
 }
 if(ok && type==SETTINGS)ok=n<=65536 && !memchr(data,0,n);
 if(ok)*info=(file_info){n,running_crc,saved,saves,weak};
 if(ok && transaction_source && !strncmp(p,transaction_source,strlen(transaction_source)) && p[strlen(transaction_source)]=='/') {
  unload();loaded_data=data;loaded_owned=!workspace;strcpy(loaded_path,p);loaded_info=*info;
 } else if(!workspace)free(data);return ok;
}
/* Recovery files are only READ in PSX3D. Use the same current/bak/tmp order
 * as the loader, and import them under the original region-qualified name. */
static COLD bool source_file(const char *base,int type,char *out,file_info *i)
{
 bool cacheable=active_cache && type==STATE && !strncmp(base,active_source,strlen(active_source)) && base[strlen(active_source)]=='/';
 if(cacheable)for(source_cache *e=*active_cache;e;e=e->next)if(!strcmp(e->base,base)){
  strcpy(out,e->selected);*i=e->info;return true;
 }
 static const char *const suffix[]={"",".bak",".tmp"};
 for(unsigned k=0;k<3;k++) {
  if(snprintf(out,PATH_CAP,"%s%s",base,suffix[k])>=PATH_CAP)return false;
  if(inspect(out,type,i) && !i->weak){
   if(cacheable && active_cached<128){source_cache *e=malloc(sizeof(*e));if(e){
    strcpy(e->base,base);strcpy(e->selected,out);e->info=*i;e->next=*active_cache;*active_cache=e;active_cached++;
   }}
   return true;
  }
  if(resource_error || budget_cancelled)return false;
 }
 /* Match the runtime: weak cards remain usable, but prefer any strong recovery copy. */
 if(type==CARD)for(unsigned k=0;k<3;k++) {
  if(snprintf(out,PATH_CAP,"%s%s",base,suffix[k])>=PATH_CAP)return false;
  if(inspect(out,type,i))return true;
 }
 return false;
}
static COLD bool source_picture(const char *base,char *out,file_info *i)
{
 char state[PATH_CAP],selected[PATH_CAP];file_info si;size_t n=strlen(base);
 if(n<4 || n>=sizeof(state))return false;
 memcpy(state,base,n-4);state[n-4]=0;
 if(!source_file(state,STATE,selected,&si))return false;
 /* A backup state must use its backup picture. Never pair the picture from
  * an interrupted newer save with the older state that actually survived. */
 if(snprintf(out,PATH_CAP,"%s%s",base,selected+strlen(state))>=PATH_CAP)return false;
 return inspect(out,THUMB,i);
}
static COLD bool equal(const char *a,const char *b)
{
 FILE *x=fopen(a,"rb"),*y=fopen(b,"rb");bool ok=x && y;
 unsigned char xb[4096],yb[4096];
 while(ok) {
  if(!turn()){ok=false;break;}
  size_t nx=fread(xb,1,sizeof(xb),x),ny=fread(yb,1,sizeof(yb),y);
  if(nx!=ny || memcmp(xb,yb,nx)) {ok=false;break;}
  if(nx<sizeof(xb)) {ok=!ferror(x) && !ferror(y);break;}
 }
 if(x)fclose(x);
 if(y)fclose(y);
 return ok;
}
/* Only importer-owned temporary names are removed. Never overwrite a final
 * archive, including on a CRC collision: compare the complete bytes. */
static COLD bool flush_file(FILE *f)
{
 if(fflush(f)){if(errno==ENOSPC && no_space)*no_space=true;return false;}
 /* The pinned libctru archive driver implements fsync as FSFILE_Flush.
  * Commit once per completed file, before verification and final rename. */
 if(fsync(fileno(f))){if(errno==ENOSPC && no_space)*no_space=true;return false;}
 return true;
}
/* Bulk publication uses explicit-offset FSFILE transfers. No preallocation:
 * firmware allocates at most the next 64 KiB write, and Flush is its own permit.
 * Host uses the same closed-output size/CRC contract through stdio. */
static COLD bool identity_path(const char *);
typedef struct {
 const char *path;uint64_t size;
#if defined(__3DS__) || defined(PSX_IMPORT_FSFILE_TEST)
 Handle handle;
#else
 FILE *file;
#endif
} transfer_file;
#if defined(__3DS__) || defined(PSX_IMPORT_FSFILE_TEST)
static COLD bool transfer_result(Result rc,const char *op,const char *p,uint64_t began)
{
 trace_io(op,p,!strstr(op,"read") && !strstr(op,"write"));
 uint64_t elapsed=microseconds()-began;
 if(elapsed>50000 || R_FAILED(rc))log_printf("import: fsfile op=%s path=%s us=%llu result=%08lx\n",op,p,(unsigned long long)elapsed,(unsigned long)rc);
 if(R_FAILED(rc)){errno=R_DESCRIPTION(rc)==RD_OUT_OF_MEMORY?ENOMEM:(R_SUMMARY(rc)==RS_OUTOFRESOURCE?ENOSPC:EIO);if(errno==ENOMEM)resource_error=true;if(errno==ENOSPC && no_space)*no_space=true;return false;}return true;
}
#endif
static COLD bool transfer_open(transfer_file *f,const char *p,bool write)
{
 f->path=p;f->size=0;
#if defined(__3DS__) || defined(PSX_IMPORT_FSFILE_TEST)
 if(!identity_path(p)){errno=EINVAL;return false;}
 uint16_t wide[PATH_CAP];ssize_t count=utf8_to_utf16(wide,(const uint8_t *)p+5,PATH_CAP-1);
 if(count<0 || count>=PATH_CAP-1){errno=EINVAL;return false;}wide[count]=0;
 if(!io_permit(write))return false;uint64_t began=microseconds();
 Result rc=FSUSER_OpenFileDirectly(&f->handle,ARCHIVE_SDMC,fsMakePath(PATH_EMPTY,""),fsMakePath(PATH_UTF16,wide),write?FS_OPEN_WRITE|FS_OPEN_CREATE:FS_OPEN_READ,0);
 if(metrics){metrics->open_us+=microseconds()-began;metrics->open_calls++;}
 return transfer_result(rc,"open",p,began);
#else
 f->file=fopen(p,write?"wb":"rb");return f->file!=NULL;
#endif
}
static COLD bool transfer_size(transfer_file *f)
{
#if defined(__3DS__) || defined(PSX_IMPORT_FSFILE_TEST)
 if(!turn())return false;uint64_t began=microseconds();Result rc=FSFILE_GetSize(f->handle,&f->size);
 if(metrics){metrics->stat_us+=microseconds()-began;metrics->stat_calls++;}return transfer_result(rc,"size",f->path,began);
#else
 struct stat st;if(!regular(f->path,&st))return false;f->size=st.st_size;return true;
#endif
}
static COLD bool transfer_close(transfer_file *f)
{
#if defined(__3DS__) || defined(PSX_IMPORT_FSFILE_TEST)
 (void)turn();uint64_t began=microseconds();Result rc=FSFILE_Close(f->handle);
 if(metrics){metrics->close_us+=microseconds()-began;metrics->close_calls++;}return transfer_result(rc,"close",f->path,began);
#else
 return !fclose(f->file);
#endif
}
static COLD bool transfer_read(transfer_file *f,size_t at,void *data,size_t n)
{
#if defined(__3DS__) || defined(PSX_IMPORT_FSFILE_TEST)
 if(!turn())return false;uint32_t got=0;uint64_t began=microseconds();Result rc=FSFILE_Read(f->handle,&got,at,data,n);uint64_t elapsed=microseconds()-began;
 if(metrics){metrics->read_bytes+=got;metrics->read_calls++;metrics->read_us+=elapsed;if(elapsed>metrics->max_read_us)metrics->max_read_us=elapsed;}
 return transfer_result(rc,"read",f->path,began) && got==n;
#else
 (void)at;return fread(data,1,n,f->file)==n;
#endif
}
static COLD bool transfer_write(transfer_file *f,size_t at,const void *data,size_t n)
{
#if defined(__3DS__) || defined(PSX_IMPORT_FSFILE_TEST)
 if(!io_permit(true))return false;uint32_t put=0;uint64_t began=microseconds();Result rc=FSFILE_Write(f->handle,&put,at,data,n,0);uint64_t elapsed=microseconds()-began;
 if(metrics){metrics->written_bytes+=put;metrics->write_calls++;metrics->write_us+=elapsed;if(elapsed>metrics->max_write_us)metrics->max_write_us=elapsed;}
 return transfer_result(rc,"write",f->path,began) && put==n;
#else
 (void)at;return fwrite(data,1,n,f->file)==n;
#endif
}
static COLD bool transfer_flush(transfer_file *f)
{
#if defined(__3DS__) || defined(PSX_IMPORT_FSFILE_TEST)
 if(!turn())return false;uint64_t began=microseconds();Result rc=FSFILE_Flush(f->handle);
 if(metrics){metrics->flush_us+=microseconds()-began;metrics->flush_calls++;}return transfer_result(rc,"flush",f->path,began);
#else
 return flush_file(f->file);
#endif
}
static COLD bool copy_new(const char *from,const char *to,const file_info *want)
{
 if(exists(to))return equal(from,to);
 if(!dirs(to))return false;
 char tmp[PATH_CAP];if(snprintf(tmp,sizeof(tmp),"%s.import-tmp",to)>=(int)sizeof(tmp))return false;
 struct stat st;bool partial=exists(tmp);if(partial && !regular(tmp,&st))return false;
 bool cached=loaded_data && !strcmp(from,loaded_path) && loaded_info.size==want->size && loaded_info.crc==want->crc;
 /* Discard only our checked partial name; CREATE alone need not truncate. */
 if(partial && unlink(tmp))return false;
 transfer_file in,out;bool have_in=!cached && transfer_open(&in,from,false);
 bool have_out=(cached || have_in) && transfer_open(&out,tmp,true);
 bool ok=(cached || have_in) && have_out;unsigned crc=cached?want->crc:0;size_t size=0;unsigned char buf[IO_SLICE];
 if(have_in && (!transfer_size(&in) || in.size!=want->size))ok=false;
 while(ok && size<want->size) {
  size_t n=want->size-size>sizeof(buf)?sizeof(buf):want->size-size;
  const void *bytes=cached?(const unsigned char *)loaded_data+size:buf;
  if(!cached && !transfer_read(&in,size,buf,n)){ok=false;break;}
  if(!cached)crc=crc32(crc,bytes,n);
  if(!transfer_write(&out,size,bytes,n))ok=false;size+=n;
 }
 if(have_in && !transfer_close(&in))ok=false;
 if(have_out){if(ok && !transfer_flush(&out))ok=false;if(!transfer_close(&out))ok=false;}
 ok=ok && size==want->size && crc==want->crc;
 transfer_file verify;bool have_verify=ok && transfer_open(&verify,tmp,false);unsigned verified=0;size_t total=0;
 if(ok && (!have_verify || !transfer_size(&verify) || verify.size!=want->size))ok=false;
 while(ok && total<want->size) {
  size_t n=want->size-total>sizeof(buf)?sizeof(buf):want->size-total;
  if(!transfer_read(&verify,total,buf,n)){ok=false;break;}
  verified=crc32(verified,buf,n);total+=n;
 }
 if(have_verify && !transfer_close(&verify))ok=false;
 ok=ok && total==want->size && verified==want->crc;
 if(!ok) {unlink(tmp);return false;} /* ours, never a player save */
 if(!step())return false;
 return !exists(to) && !rename(tmp,to);
}
static COLD bool archive(const char *root,const char *bucket,const char *rel,const char *from,
                        const file_info *info,char *out)
{
 char tail[PATH_CAP];
 if(snprintf(tail,sizeof(tail),"import/psx3d/%s/%s/%lu-%08x",bucket,rel,
             (unsigned long)info->size,info->crc)>=PATH_CAP || !path(out,root,tail))return false;
 return copy_new(from,out,info);
}
/* On FAT there is a gap between renames. The parked file survives until the
 * new primary is verified. Both versions also have permanent verified copies. */
static COLD bool publish(const char *from,const char *to,const file_info *info)
{
 if(exists(to) && equal(from,to))return true;
 if(resource_error || budget_cancelled)return false;
 char stage[PATH_CAP],old[PATH_CAP];
 if(snprintf(stage,sizeof(stage),"%s.psx3d-new",to)>=(int)sizeof(stage) ||
    snprintf(old,sizeof(old),"%s.psx3d-old",to)>=(int)sizeof(old))return false;
 if(exists(old)) {
  if(!exists(to)) {if(rename(old,to))return false;}
  else return false; /* caller recovers parked files, never guess here */
 }
 if(exists(stage)) {struct stat st;if(!regular(stage,&st) || unlink(stage))return false;}
 if(!copy_new(from,stage,info))return false;
 if(exists(to) && rename(to,old))return false;
 if(!step())return false;
 if(rename(stage,to))return false;
 /* copy_new verified the closed stage; rename does not change its bytes. */
 /* Caller verified the permanent old archive before parking it. Clear the
  * parking name now, so a later player deletion cannot resurrect it. */
 if(exists(old) && unlink(old))return false;
 return step();
}
static COLD bool recover(const char *root,const char *rel,const char *to)
{
 char old[PATH_CAP],saved[PATH_CAP];
 if(snprintf(old,sizeof(old),"%s.psx3d-old",to)>=(int)sizeof(old))return false;
 if(!exists(old))return true;
 if(!exists(to))return !rename(old,to);
 file_info i;if(!inspect(old,OTHER,&i) || !archive(root,"breadbox",rel,old,&i,saved))return false;
 return !unlink(old);
}
static COLD bool receipt(const char *archive_path,bool write)
{
 if(write && (resource_error || budget_cancelled))return false;
 char p[PATH_CAP];if(snprintf(p,sizeof(p),"%s.done",archive_path)>=(int)sizeof(p))return false;
 if(!write) {FILE *f=fopen(p,"rb");bool ok=f && fgetc(f)=='1' && fgetc(f)==EOF;if(f)fclose(f);return ok;}
 file_info one={.size=1,.crc=(unsigned)crc32(0,(const unsigned char *)"1",1)};
 /* A short receipt from a power cut is ours, and can be replaced. */
 if(exists(p) && !receipt(archive_path,false)) {struct stat st;if(!regular(p,&st) || unlink(p))return false;}
 char seed[PATH_CAP];if(snprintf(seed,sizeof(seed),"%s.receipt",archive_path)>=(int)sizeof(seed))return false;
 FILE *f=fopen(seed,"wb");if(!f)return false;
 bool ok=fwrite("1",1,1,f)==1;if(!flush_file(f))ok=false;if(fclose(f))ok=false;
 if(ok)ok=copy_new(seed,p,&one);
 unlink(seed);return ok;
}
/* history_added() may trim the target ring AND another game's oldest
 * entries at the global budget. Preserve the entire existing history tree
 * first. A failed snapshot forbids ring insertion and active replacement.
 * Later entries in this same import already have permanent verified copies. */
static bool history_preserved,history_interrupted;
static psx_import_progress history_progress;
static void *history_progress_ctx;
static COLD bool history_turn(void *ctx)
{(void)ctx;if(turn())return true;history_interrupted=true;return false;}
static COLD bool preserve_history(const char *root)
{
 if(history_preserved)return true;
 char base[PATH_CAP];if(!path(base,root,"backups"))return false;
 DIR *d=opendir(base);if(!d) {history_preserved=errno==ENOENT;return history_preserved;}
 bool ok=true;struct dirent *group;
 while((errno=0,(group=readdir(d)))) {
  if(!history_turn(NULL)){ok=false;break;}
  if(!strcmp(group->d_name,".") || !strcmp(group->d_name,".."))continue;
  char dir[PATH_CAP];struct stat st;
  if(!path(dir,base,group->d_name) || lstat(dir,&st) || !S_ISDIR(st.st_mode)) {ok=false;break;}
  DIR *g=opendir(dir);if(!g) {ok=false;break;}
  struct dirent *e;
  while((errno=0,(e=readdir(g)))) {
   if(!history_turn(NULL)){ok=false;break;}
   if(!strcmp(e->d_name,".") || !strcmp(e->d_name,".."))continue;
   size_t n=strlen(e->d_name),partial=strlen(".import-tmp");
   if(n>=partial && !strcmp(e->d_name+n-partial,".import-tmp"))continue; /* ours, never a retained entry */
   char file[PATH_CAP],rel[PATH_CAP],a[PATH_CAP];file_info i;
   if(history_progress && !history_progress(e->d_name,history_progress_ctx)) {history_interrupted=true;ok=false;break;}
   if(!path(file,dir,e->d_name) ||
      snprintf(rel,sizeof(rel),"backups/%s/%s",group->d_name,e->d_name)>=(int)sizeof(rel) ||
      !inspect(file,OTHER,&i) || !archive(root,"breadbox-history",rel,file,&i,a)) {ok=false;break;}
  }
  if(!e && errno)ok=false;
  closedir(g);if(!ok)break;
 }
 if(!group && errno)ok=false;
 closedir(d);history_preserved=ok;return ok;
}
static COLD bool ring_copy(const char *root,const char *rel,const char *name,const char *from,int type,const file_info *i)
{
 char group[256],out[PATH_CAP];if(strlen(name)>=sizeof(group))return false;
 strcpy(group,name);char *e=strrchr(group,'.');if(!e)return false;
 const char *ext=e;char extension[16];snprintf(extension,sizeof(extension),"%s",ext);*e=0;
 /* No duplicate ring entry after an interruption. Permanent archives are the
  * safety net even when ordinary ring retention later drops this entry. */
 hist_entry_t entries[HIST_CARD_PINS+HIST_CARD_DAYS+1];
 int n=history_list(group,type==CARD,entries,sizeof(entries)/sizeof(entries[0]));
 if(history_interrupted)return false;
 bool found=false;
 for(int j=0;j<n;j++)if(equal(from,entries[j].path)) {strcpy(out,entries[j].path);found=true;break;}
 if(resource_error || budget_cancelled)return false;
 if(!found && !preserve_history(root))return false;
 if(!found && (!history_next(group,type==CARD?HIST_CARD_PIN:HIST_SAVE,extension,i->saved,out,sizeof(out)) || !copy_new(from,out,i)))return false;
 if(type==STATE) {
  char thm[PATH_CAP],base[PATH_CAP],selected[PATH_CAP],to[PATH_CAP],arch[PATH_CAP],thmrel[PATH_CAP];file_info ti;
  strcpy(base,from);char *suffix=strrchr(base,'.');if(suffix && (!strcmp(suffix,".bak") || !strcmp(suffix,".tmp")))*suffix=0;
  if(snprintf(thm,sizeof(thm),"%s.thm",base)>=(int)sizeof(thm) ||
     snprintf(to,sizeof(to),"%s.thm",out)>=(int)sizeof(to) ||
     snprintf(thmrel,sizeof(thmrel),"%s.thm",rel)>=(int)sizeof(thmrel))return false;
  if(source_picture(thm,selected,&ti) && (!archive(root,"pictures",thmrel,selected,&ti,arch) || !copy_new(selected,to,&ti)))return false;
 }
 if(!found)history_added(group,out);
 return !history_interrupted && !resource_error && !budget_cancelled;
}
static COLD bool thumb_matches(const char *src,const char *dst)
{
 char s[PATH_CAP],d[PATH_CAP],selected[PATH_CAP],base[PATH_CAP];file_info i;
 strcpy(base,src);char *suffix=strrchr(base,'.');if(suffix && (!strcmp(suffix,".bak") || !strcmp(suffix,".tmp")))*suffix=0;
 src=base;size_t n=strlen(src),m=strlen(dst);
 if(n<4 || m<4 || n>=sizeof(s) || m>=sizeof(d))return false;
 memcpy(s,src,n-4);s[n-4]=0;memcpy(d,dst,m-4);d[m-4]=0;
 return source_file(s,STATE,selected,&i) && equal(selected,d);
}
/* Identity files disambiguate games with the same save name. A conflicting
 * identity makes every active import for that name conservative, even when
 * the destination slot itself is empty. Never put another game in its ring. */
static COLD bool identity_conflict(const char *src,const char *dest,const char *name)
{
 char group[256],a[PATH_CAP],b[PATH_CAP],selected[PATH_CAP];file_info i;
 strcpy(group,name);char *e=strrchr(group,'.');   /* no extension (a BIOS file): the whole name */
 if(e){*e=0;if(!strcmp(e+1,"thm")) {e=strrchr(group,'.');if(e)*e=0;}}
 char leaf[512];if(snprintf(leaf,sizeof(leaf),"saves/%s.id",group)>=(int)sizeof(leaf) ||
    !path(a,src,leaf) || !path(b,dest,leaf))return true;
 return source_file(a,SETTINGS,selected,&i) && exists(b) && !equal(selected,b);
}
static COLD bool destination_recover(const char *root,const char *rel,const char *to,int type)
{
 if(!recover(root,rel,to))return false;
 if(exists(to))return true;
 char selected[PATH_CAP],a[PATH_CAP];file_info i;
 if(!source_file(to,type,selected,&i))return !resource_error && !budget_cancelled;
 return archive(root,"breadbox",rel,selected,&i,a) && copy_new(a,to,&i);
}
/* A thumbnail cannot finish while its state is still awaiting a successful
 * active/ring publication. A newer BreadBox winner also has a state receipt. */
static COLD bool fast_state_completed(const char *,const char *);
static COLD bool picture_state_done(const char *src,const char *dest,const char *rel)
{
 char state[PATH_CAP],base[PATH_CAP],selected[PATH_CAP],a[PATH_CAP];file_info i;
 size_t n=strlen(rel);if(n<4 || n>=sizeof(state))return false;
 memcpy(state,rel,n-4);state[n-4]=0;
 if(fast_state_completed(dest,state))return true;
 if(!path(base,src,state) || !source_file(base,STATE,selected,&i))return false;
 char tail[PATH_CAP];
 if(snprintf(tail,sizeof(tail),"import/psx3d/source/%s/%lu-%08x",state,
             (unsigned long)i.size,i.crc)>=(int)sizeof(tail) || !path(a,dest,tail))return false;
 return receipt(a,false);
}
/* Per-logical-file durable receipts. Ordinary later launches inspect only
 * the selected source identity, plus the primary when recovery was selected.
 * Unselected backups cannot affect a healthy primary. */
typedef struct {uint32_t magic,crc;unsigned identity,primary;uint64_t size,primary_size;unsigned active;file_info info;char suffix[8];} fast_receipt;
static COLD bool fast_valid(const void *p)
{fast_receipt r=*(const fast_receipt *)p;unsigned crc=r.crc;r.crc=0;
 return r.magic==0x31465249 && !r.suffix[7] && (!r.suffix[0] || !strcmp(r.suffix,".bak") || !strcmp(r.suffix,".tmp")) && crc==crc32(0,(const void *)&r,sizeof(r));}
static COLD bool fast_path(char *p,const char *root,const char *rel)
{char tail[PATH_CAP];return snprintf(tail,sizeof(tail),"import/psx3d/receipts/%s",rel)<PATH_CAP && path(p,root,tail);}
/* A disposable index avoids opening each tiny receipt twice on returning
 * launches. Per-file receipts remain the crash-resume authority. Invalidate
 * the index before the first changed transaction; a crash falls back to them. */
typedef struct receipt_entry {struct receipt_entry *next;char rel[PATH_CAP];fast_receipt receipt;} receipt_entry;
static receipt_entry **receipt_map;
static bool *index_invalid;
static COLD void remember_receipt(const char *rel,const fast_receipt *r)
{
 if(!receipt_map)return;
 receipt_entry *e=*receipt_map;
 while(e && strcmp(e->rel,rel))e=e->next;
 if(!e){e=calloc(1,sizeof(*e));if(!e)return;strcpy(e->rel,rel);e->next=*receipt_map;*receipt_map=e;}
 e->receipt=*r;
}
static COLD bool invalidate_index(const char *dest)
{
 if(!index_invalid || *index_invalid)return true;
 char p[PATH_CAP];if(!path(p,dest,"import/psx3d/receipt-index") || !turn())return false;
 if(unlink(p) && errno!=ENOENT)return false;
 *index_invalid=true;return true;
}
typedef struct {uint32_t magic,count,crc;} receipt_index_header;
typedef struct {char rel[PATH_CAP];fast_receipt receipt;} receipt_disk_entry;
static COLD bool read_receipt_index(const char *dest)
{
 char p[PATH_CAP];if(!path(p,dest,"import/psx3d/receipt-index") || !turn())return false;
 FILE *f=fopen(p,"rb");if(!f)return false;
 receipt_index_header h;receipt_entry *entries=NULL;unsigned crc=0;
 bool ok=fread(&h,1,sizeof(h),f)==sizeof(h) && h.magic==0x31495850 && h.count<=1024;
 for(unsigned i=0;ok && i<h.count;i++) {
  receipt_disk_entry d;if(!turn() || fread(&d,1,sizeof(d),f)!=sizeof(d)){ok=false;break;}
  crc=crc32(crc,(const void *)&d,sizeof(d));
  if(!memchr(d.rel,0,sizeof(d.rel)) || !fast_valid(&d.receipt)){ok=false;break;}
  receipt_entry *e=calloc(1,sizeof(*e));if(!e){ok=false;break;}
  strcpy(e->rel,d.rel);e->receipt=d.receipt;e->next=entries;entries=e;
 }
 ok=ok && crc==h.crc && fgetc(f)==EOF && !ferror(f);if(fclose(f))ok=false;
 while(entries){receipt_entry *e=entries;entries=e->next;if(ok)remember_receipt(e->rel,&e->receipt);free(e);}
 return ok;
}
static COLD bool write_receipt_index(const char *dest)
{
 if(!receipt_map)return true;
 char p[PATH_CAP],tmp[PATH_CAP];
 if(!path(p,dest,"import/psx3d/receipt-index") || snprintf(tmp,sizeof(tmp),"%s.import-tmp",p)>=PATH_CAP || !dirs(p))return false;
 receipt_index_header h={.magic=0x31495850};
 for(receipt_entry *e=*receipt_map;e;e=e->next) {
  receipt_disk_entry d={0};strcpy(d.rel,e->rel);d.receipt=e->receipt;
  if(!turn())return false;h.crc=crc32(h.crc,(const void *)&d,sizeof(d));h.count++;
 }
 if(h.count>1024)return true; /* Optional cache; large sets use per-file receipts. */
 FILE *f=fopen(tmp,"wb");if(!f)return false;bool ok=fwrite(&h,1,sizeof(h),f)==sizeof(h);
 for(receipt_entry *e=*receipt_map;ok && e;e=e->next) {
  receipt_disk_entry d={0};strcpy(d.rel,e->rel);d.receipt=e->receipt;
  ok=turn() && fwrite(&d,1,sizeof(d),f)==sizeof(d);
 }
 if(ok)ok=turn() && flush_file(f);if(fclose(f))ok=false;
 if(ok)ok=turn() && (!unlink(p) || errno==ENOENT) && !rename(tmp,p);
 if(!ok)unlink(tmp);return ok;
}
/* Read-only native offset reads avoid the SD archive driver's expensive
 * per-FILE open/close bookkeeping. Keep the strict device-path rules before
 * entering FSUSER, and preserve Unicode names with an explicit UTF-16 path. */
static __attribute__((unused)) COLD bool identity_path(const char *p)
{
 if(strncmp(p,"sdmc:/",6) || strlen(p)>=PATH_CAP || !p[6])return false;
 for(const char *a=p+6;*a;) {
  const char *b=strchr(a,'/');size_t n=b?(size_t)(b-a):strlen(a);
  if(!n || n>255 || (n==1 && *a=='.') || (n==2 && !memcmp(a,"..",2)))return false;
  for(size_t i=0;i<n;i++)if((unsigned char)a[i]<32 || strchr("\\:*?\"<>|",a[i]))return false;
  if(!b)break;if(!b[1])return false;a=b+1;
 }
 return true;
}
#ifdef PSX_IMPORT_TEST
bool psx_import_test_identity_path(const char *p){return identity_path(p);}
#endif
typedef struct {
 uint64_t size;const char *path;
#if defined(__3DS__) || defined(PSX_IMPORT_FSFILE_TEST)
 Handle handle;
#else
 FILE *file;
#endif
} identity_file;
static COLD bool identity_close(identity_file *f)
{
#if defined(__3DS__) || defined(PSX_IMPORT_FSFILE_TEST)
 (void)turn();uint64_t began=microseconds();Result rc=FSFILE_Close(f->handle);
 if(metrics){metrics->close_us+=microseconds()-began;metrics->close_calls++;}return transfer_result(rc,"identity-close",f->path,began);
#else
 return !fclose(f->file);
#endif
}
static COLD bool identity_open(identity_file *f,const char *p)
{
 f->path=p;
#if defined(__3DS__) || defined(PSX_IMPORT_FSFILE_TEST)
 if(!identity_path(p)){errno=EINVAL;return false;}
 uint16_t wide[PATH_CAP];ssize_t count=utf8_to_utf16(wide,(const uint8_t *)p+5,PATH_CAP-1);
 if(count<0 || count>=PATH_CAP-1){errno=EINVAL;return false;}wide[count]=0;
 if(!turn())return false;uint64_t began=microseconds();
 Result rc=FSUSER_OpenFileDirectly(&f->handle,ARCHIVE_SDMC,fsMakePath(PATH_EMPTY,""),fsMakePath(PATH_UTF16,wide),FS_OPEN_READ,0);
 if(metrics){metrics->open_us+=microseconds()-began;metrics->open_calls++;}if(!transfer_result(rc,"identity-open",p,began))return false;
 if(!turn()){identity_close(f);return false;}
 began=microseconds();rc=FSFILE_GetSize(f->handle,&f->size);
 if(metrics){metrics->stat_us+=microseconds()-began;metrics->stat_calls++;}
 if(!transfer_result(rc,"identity-size",p,began)){identity_close(f);return false;}return true;
#else
 struct stat st;if(!regular(p,&st) || st.st_size<0)return false;
 f->file=fopen(p,"rb");if(!f->file)return false;f->size=st.st_size;
 setvbuf(f->file,NULL,_IONBF,0);return true;
#endif
}
static COLD bool identity_read(identity_file *f,uint64_t at,void *data,size_t n)
{
#if defined(__3DS__) || defined(PSX_IMPORT_FSFILE_TEST)
 if(!turn())return false;uint32_t got=0;uint64_t began=microseconds();
 Result rc=FSFILE_Read(f->handle,&got,at,data,n);uint64_t elapsed=microseconds()-began;
 if(metrics){metrics->read_bytes+=got;metrics->read_calls++;metrics->read_us+=elapsed;if(elapsed>metrics->max_read_us)metrics->max_read_us=elapsed;}
 return transfer_result(rc,"identity-read",f->path,began) && got==n;
#else
 return !fseek(f->file,at,SEEK_SET) && fread(data,1,n,f->file)==n;
#endif
}
static COLD bool cheap_identity(const char *p,int type,uint64_t *size,unsigned *identity)
{
 identity_file f;if(!identity_open(&f,p))return false;size_t n=f.size;
 if(f.size>MAX_FILE){*size=f.size;*identity=0;return identity_close(&f);}
 bool ok=true;unsigned crc=0;state_trailer_t t;bool trailer=false;
 if(type==STATE && n>=sizeof(t)) {
  trailer=identity_read(&f,n-sizeof(t),&t,sizeof(t)) &&
   !memcmp(t.magic,"PSX3Dsv1",8) && t.version==1 && t.data_len<=n-sizeof(t) && !t.disc[sizeof(t.disc)-1];
 }
 if(trailer)crc=crc32(0,(const void *)&t,sizeof(t));
 else if((type==CARD && n==128*1024) || type==SETTINGS || n<=6144) {
  unsigned char buf[IO_SLICE];size_t at=0;
  while(ok && at<n){size_t k=n-at>sizeof(buf)?sizeof(buf):n-at;
   if(!identity_read(&f,at,buf,k)){ok=false;break;}crc=crc32(crc,buf,k);at+=k;}
 } else {
  unsigned char buf[1024];
  for(unsigned j=0;ok && j<6;j++) {
   size_t at=j==5?n-sizeof(buf):j*n/5;
   if(!identity_read(&f,at,buf,sizeof(buf))){ok=false;break;}
   crc=crc32(crc,buf,sizeof(buf));
  }
 }
 if(!identity_close(&f))ok=false;
 if(ok){*size=n;*identity=crc;}return ok;
}
static COLD void *record_load(const char *p,size_t size,bool (*valid)(const void *))
{
 static const char *const suffix[]={"",".bak",".tmp"};
 void *data=malloc(size+1);if(!data)return NULL;
 for(unsigned i=0;i<3;i++) {
  char file[PATH_CAP];if(snprintf(file,sizeof(file),"%s%s",p,suffix[i])>=PATH_CAP || !turn())break;
  FILE *f=fopen(file,"rb");if(!f)continue;
  size_t n=fread(data,1,size+1,f);bool ok=n==size && !ferror(f);if(fclose(f))ok=false;
  if(ok && valid(data))return data;
 }
 free(data);return NULL;
}
static COLD fast_receipt *fast_load(const char *dest,const char *rel)
{
 if(receipt_map)for(receipt_entry *e=*receipt_map;e;e=e->next)if(!strcmp(e->rel,rel)) {
  fast_receipt *r=malloc(sizeof(*r));if(r)*r=e->receipt;return r;
 }
 char p[PATH_CAP];if(!fast_path(p,dest,rel))return NULL;
 fast_receipt *r=record_load(p,sizeof(*r),fast_valid);if(r)remember_receipt(rel,r);return r;
}
static COLD bool fast_same(const fast_receipt *r,const char *base,int type)
{
 char selected[PATH_CAP];uint64_t size=0,primary_size=0;unsigned hash=0,primary=0;
 if(snprintf(selected,sizeof(selected),"%s%s",base,r->suffix)>=PATH_CAP || !cheap_identity(selected,type,&size,&hash))return false;
 if(r->suffix[0] && exists(base) && !cheap_identity(base,type,&primary_size,&primary))return false;
 return r->size==size && r->identity==hash && r->primary_size==primary_size && r->primary==primary;
}
/* Receipts are self-validating, importer-owned metadata. Keep the previous
 * receipt as .bak, and budget each filesystem operation separately. A cut at
 * any point leaves an old receipt or a complete CRC-checked .tmp. */
static COLD bool stage_record(const char *tmp,const void *data,size_t size)
{
 if(!dirs(tmp))return false;
 FILE *f=fopen(tmp,"wb");if(!f)return false;
 bool ok=fwrite(data,1,size,f)==size;
 if(ok)ok=flush_file(f);if(fclose(f))ok=false;return ok;
}
static COLD bool publish_record(const char *p,const char *tmp)
{
 char bak[PATH_CAP];if(snprintf(bak,sizeof(bak),"%s.bak",p)>=PATH_CAP)return false;
 if(unlink(bak) && errno!=ENOENT)return false;
 bool present=exists(p);
 if(present && rename(p,bak))return false;
 if(rename(tmp,p)){int error=errno;(void)rename(bak,p);errno=error;return false;}
 return true;
}
static COLD bool write_fast_receipt(const char *p,const fast_receipt *r)
{
 char tmp[PATH_CAP];if(snprintf(tmp,sizeof(tmp),"%s.tmp",p)>=PATH_CAP)return false;
 return stage_record(tmp,r,sizeof(*r)) && publish_record(p,tmp);
}
static COLD bool fast_save(const char *dest,const char *rel,const char *base,const char *selected,int type,const file_info *info,bool active)
{
 fast_receipt r={.magic=0x31465249,.active=active,.info=*info};char p[PATH_CAP];
 if(strlen(selected)<strlen(base) || strlen(selected+strlen(base))>=sizeof(r.suffix))return false;
 strcpy(r.suffix,selected+strlen(base));
 if(!cheap_identity(selected,type,&r.size,&r.identity) ||
    (r.suffix[0] && exists(base) && !cheap_identity(base,type,&r.primary_size,&r.primary)))return false;
 r.crc=crc32(0,(const void *)&r,sizeof(r));
 bool ok=invalidate_index(dest) && fast_path(p,dest,rel) && dirs(p) && write_fast_receipt(p,&r);
 if(ok)remember_receipt(rel,&r);
 if(!ok && errno==ENOSPC && no_space)*no_space=true;return ok;
}
static COLD bool fast_state_completed(const char *dest,const char *rel)
{fast_receipt *r=fast_load(dest,rel);bool ok=r!=NULL;free(r);return ok;}
static COLD bool fast_picture_winner(const char *dest,const char *rel)
{
 char state[PATH_CAP],file[PATH_CAP];size_t n=strlen(rel);
 if(n<4)return false;memcpy(state,rel,n-4);state[n-4]=0;
 fast_receipt *r=fast_load(dest,state);uint64_t size;unsigned hash;
 bool ok=r && r->active && path(file,dest,state) && cheap_identity(file,STATE,&size,&hash) && size==r->size && hash==r->identity;
 free(r);return ok;
}
static COLD void import_failure(psx_import_result *r,const char *op,const char *p)
{
 int error=errno;r->failed++;
 log_printf("import: failure op=%s path=%s errno=%d\n",op,p,error);errno=error;
}
static COLD bool empty_destination(const char *p)
{
 static const char *const suffix[]={"",".bak",".tmp",".psx3d-old"};char file[PATH_CAP];
 for(unsigned k=0;k<4;k++)if(snprintf(file,sizeof(file),"%s%s",p,suffix[k])>=PATH_CAP || exists(file))return false;
 return true;
}
/* One file copied: counted, and its kind noted for the welcome's PSX3D card. */
static COLD void copied(psx_import_result *r,const char *dir,const char *name,int type)
{
 r->copied++;if(type==STATE && !strstr(name,".sta"))r->slots++;
 r->kinds|=type==CARD?PSX_IMPORT_SAVES:type==STATE || type==THUMB?PSX_IMPORT_POSTCARDS:type==SETTINGS?PSX_IMPORT_SETTINGS:
  type==BLOB?(!strcmp(dir,"bios")?PSX_IMPORT_BIOS:PSX_IMPORT_CHEATS):0;
}
static COLD void import_one(const char *src,const char *dest,const char *dir,const char *name,int type,psx_import_result *r)
{
 resource_error=false;
 char rel[PATH_CAP],s[PATH_CAP],base[PATH_CAP]={0},d[PATH_CAP],a[PATH_CAP],b[PATH_CAP];file_info si,di;
 if(!path(rel,dir,name) || !path(base,src,rel) || !path(d,dest,rel)) {import_failure(r,"join-entry-path",src);return;}
 fast_receipt *prior=transaction_source?fast_load(dest,rel):NULL;
 bool prior_same=prior && !transaction_force && fast_same(prior,base,type);
 if(resource_error || budget_cancelled){free(prior);import_failure(r,"receipt-read-resource",base);return;}
 if(prior_same){free(prior);r->unchanged++;return;}
 if(!invalidate_index(dest)){free(prior);import_failure(r,"invalidate-index",dest);return;}
 transaction_changed=true;
 bool ownership_conflict=transaction_source && identity_conflict(src,dest,name);
 bool clean=transaction_source && empty_destination(d) && !ownership_conflict && (type!=THUMB || fast_picture_winner(dest,rel));
 if(resource_error || budget_cancelled){free(prior);import_failure(r,"probe-resource",d);return;}
 /* Finish validating the retained one-file buffer before starting output.
  * Avoid alternating reads and extending writes on the SD every 64 KiB.
  * Publication uses the same allocation without another source body read. */
 if(!(type==THUMB?source_picture(base,s,&si):source_file(base,type,s,&si))) {if(budget_cancelled){r->interrupted=true;free(prior);return;}if(resource_error){import_failure(r,"validate-resource",base);free(prior);return;}log_printf("import: rejected %s (validation/recovery)\n",base);r->rejected++;free(prior);return;}
 if(prior && prior->info.size==si.size && prior->info.crc==si.crc) {
  bool active=prior->active;free(prior);if(!fast_save(dest,rel,base,s,type,&si,active))import_failure(r,"refresh-receipt",base);else r->unchanged++;return;
 }
 free(prior);
 if(clean) {
  char tail[PATH_CAP],old_archive[PATH_CAP];
  if(snprintf(tail,sizeof(tail),"import/psx3d/source/%s/%lu-%08x",rel,(unsigned long)si.size,si.crc)>=PATH_CAP || !path(old_archive,dest,tail)) {import_failure(r,"archive-path",base);return;}
  if(receipt(old_archive,false))clean=false;
 }
 if(resource_error || budget_cancelled){import_failure(r,"prepare-resource",base);return;}
 if(clean) {
  /* Empty slots need one verified primary, not a duplicate permanent source
   * archive. Conflict losers still use the immutable archive + Backups path. */
  if(!publish(s,d,&si))import_failure(r,"publish",d);else if(!fast_save(dest,rel,base,s,type,&si,true))import_failure(r,"save-receipt",d);else copied(r,dir,name,type);
  if(type==CARD && si.weak)r->weak++;return;
 }
 if(!destination_recover(dest,rel,d,type)) {import_failure(r,"recover-destination",d);return;}
 if(!archive(dest,"source",rel,s,&si,a)) {import_failure(r,"archive-source",s);return;}
 if(receipt(a,false)) {bool active=exists(d) && equal(s,d);if(resource_error || budget_cancelled){import_failure(r,"compare-resource",d);return;}r->unchanged++;if(transaction_source && !fast_save(dest,rel,base,s,type,&si,active))import_failure(r,"save-receipt",base);return;}
 if(type==THUMB && !picture_state_done(src,dest,rel)) {import_failure(r,"picture-state-receipt",base);return;}
 if(type==CARD && si.weak)r->weak++;
 bool conflict=identity_conflict(src,dest,name);
 if(resource_error || budget_cancelled){import_failure(r,"identity-resource",d);return;}
 if(conflict) {r->conflicts++;r->unchanged++;if(!receipt(a,true) || (transaction_source && !fast_save(dest,rel,base,s,type,&si,false)))import_failure(r,"complete-receipt",base);return;}
 struct stat st;
 if(exists(d) && !regular(d,&st)) {import_failure(r,"check-destination-type",d);return;}
 bool present=exists(d),same=present && equal(a,d),win=!present;
 if(resource_error || budget_cancelled){import_failure(r,"compare-resource",d);return;}
 if(present && !same) {
  r->conflicts++;
  /* Preserve ANY existing destination, even a corrupt one, before touching it. */
  if(!inspect(d,OTHER,&di) || !archive(dest,"breadbox",rel,d,&di,b)) {import_failure(r,"archive-destination",d);return;}
  bool valid=inspect(d,type,&di);
  if(resource_error || budget_cancelled){import_failure(r,"destination-resource",d);return;}
  win=type==STATE && valid && si.saved && di.saved && si.saved>di.saved;
  if(type==CARD && valid && !di.weak && !si.weak && !di.saves && si.saves)win=true;
  if(type==THUMB)win=thumb_matches(s,d);
  if(type==STATE || type==CARD) {
   const char *loser=win?d:s;file_info li=win?di:si;
   if((!win || valid) && !ring_copy(dest,rel,name,loser,type,&li)) {import_failure(r,"history-copy",loser);return;}
  }
 }
 if(type==THUMB && !thumb_matches(s,d))win=false;
 if(resource_error || budget_cancelled){import_failure(r,"decision-resource",d);return;}
 if(win && !same) {
  if(!publish(a,d,&si)) {import_failure(r,"publish",d);return;}
  copied(r,dir,name,type);
 } else r->unchanged++;
 if(resource_error || budget_cancelled){import_failure(r,"receipt-resource",d);return;}
 if(!receipt(a,true) || (transaction_source && !fast_save(dest,rel,base,s,type,&si,win || same)))import_failure(r,"complete-receipt",base);
}
static COLD bool eligible(const char *name)
{
 return name[0]!='.' && strlen(name)<240 && !strchr(name,'/') && !strchr(name,'\\') && !strchr(name,':');
}
/* Previously authorized publication interrupted between FAT renames: restore
 * BreadBox's visible primary before presenting any menu, even if import is
 * postponed. This only repairs importer-owned parked files, never PSX3D. */
COLD bool psx_import_recover(const char *root)
{
 const char *const *folders=import_folders;bool ok=true;
 char h[PATH_CAP];if(!path(h,root,"backups") || !dirs(h))return false;
 for(unsigned k=0;k<IMPORT_FOLDERS;k++) {
  char p[PATH_CAP];if(!path(p,root,folders[k]))return false;
  DIR *d=opendir(p);if(!d) {if(errno!=ENOENT)ok=false;continue;}
  struct dirent *e;
  while((errno=0,(e=readdir(d)))) {
   size_t n=strlen(e->d_name),suffix=strlen(".psx3d-old");
   if(n<=suffix || n>=256 || strcmp(e->d_name+n-suffix,".psx3d-old"))continue;
   char name[256],rel[PATH_CAP],to[PATH_CAP];memcpy(name,e->d_name,n-suffix);name[n-suffix]=0;
   if(!eligible(name) || !path(rel,folders[k],name) || !path(to,root,rel) || !recover(root,rel,to))ok=false;
  }
  if(errno)ok=false;
  closedir(d);
 }
 return ok;
}
COLD psx_import_estimate psx_import_size_checked(const char *source, bool (*keep_going)(void *), void *ctx)
{
 psx_import_estimate r={0};
 const char *const *folders=import_folders;
 for(unsigned k=0;k<IMPORT_FOLDERS;k++) {
  if(keep_going && !keep_going(ctx)){r.incomplete=true;break;}
  char p[PATH_CAP];
  if(!path(p,source,folders[k])) {r.incomplete=true;continue;}
  DIR *d=opendir(p);
  if(!d) {if(errno!=ENOENT)r.incomplete=true;continue;}
  struct dirent *e;
  while((errno=0,(e=readdir(d)))) {
   if(keep_going && !keep_going(ctx)){r.incomplete=true;break;}
   if(!eligible(e->d_name))continue;
   char name[256],file[PATH_CAP];strcpy(name,e->d_name);
   char *suffix=strrchr(name,'.');
   if(suffix && (!strcmp(suffix,".bak") || !strcmp(suffix,".tmp")))*suffix=0;
   if(!kind(folders[k],name))continue;
   struct stat st;
   if(!path(file,p,e->d_name)) {r.incomplete=true;continue;}
   if(lstat(file,&st)) {r.incomplete=true;continue;}
   if(!S_ISREG(st.st_mode) || st.st_size<0)continue;
   r.files++;r.bytes+=(uint64_t)st.st_size;
  }
  if(errno)r.incomplete=true;
  closedir(d);
 }
 return r;
}
COLD psx_import_estimate psx_import_size(const char *source)
{
 return psx_import_size_checked(source,NULL,NULL);
}
COLD bool psx_import_available(const char *source)
{
 static const char *const ds[]={"saves","states"};
 for(unsigned i=0;i<2;i++) {
  char p[PATH_CAP];if(!path(p,source,ds[i]))continue;DIR *d=opendir(p);if(!d)continue;
  struct dirent *e;bool found=false;
  while((e=readdir(d)))if(eligible(e->d_name)) {
   char name[256],file[PATH_CAP];struct stat st;strcpy(name,e->d_name);
   char *suffix=strrchr(name,'.');if(suffix && (!strcmp(suffix,".bak") || !strcmp(suffix,".tmp")))*suffix=0;
   int type=kind(ds[i],name);
   if((type==CARD || type==STATE) && path(file,p,e->d_name) && regular(file,&st)) {found=true;break;}
  }
  closedir(d);if(found)return true;
 }
 return false;
}
COLD psx_import_result psx_import_run(const char *source,const char *dest,psx_import_progress progress,void *ctx)
{
 budget_cancelled=false;resource_error=false;history_preserved=false;history_interrupted=false;history_progress=progress;history_progress_ctx=ctx;
 psx_import_result r={0};size_t sn=strlen(source),dn=strlen(dest);
 /* Also reject aliases on the host. Production uses fixed, disjoint roots. */
 bool alias=false;
#ifndef __3DS__
 /* FAT/libctru does not provide distinct inode numbers. Host aliases include
  * symlinks; production uses fixed disjoint roots on a filesystem without them. */
 struct stat ss,ds;
 alias=!stat(source,&ss) && !stat(dest,&ds) && ss.st_dev==ds.st_dev && ss.st_ino==ds.st_ino;
#endif
 if(!sn || !dn || !strncmp(source,dest,sn<dn?sn:dn) || alias) {import_failure(&r,"initialize",dest);return r;}
 char h[PATH_CAP];if(!path(h,dest,"backups") || !dirs(h)) {import_failure(&r,"initialize",dest);return r;}history_set_root(h);
 const char *const *dirs_=import_folders;
 /* States first in their pass, thumbnails second, independent of readdir order. */
 for(unsigned k=0;k<IMPORT_FOLDERS;k++)for(int pass=0;pass<2;pass++) {
  char p[PATH_CAP];if(!path(p,source,dirs_[k])) {import_failure(&r,"path",source);continue;}
  DIR *d=opendir(p);if(!d) {if(errno!=ENOENT)import_failure(&r,"opendir",p);continue;}
  struct dirent *e;
  while((errno=0,(e=readdir(d)))) {
   if(!eligible(e->d_name))continue;
   char name[256];strcpy(name,e->d_name);char *suffix=strrchr(name,'.');
   if(suffix && (!strcmp(suffix,".bak") || !strcmp(suffix,".tmp"))) {
    *suffix=0;
    /* A primary entry will do this logical file once; backup-only saves
     * still appear when a previous PSX3D replace lost power. */
    char primary[PATH_CAP];if(!path(primary,p,name)) {import_failure(&r,"path",source);continue;}
    if(exists(primary))continue;
    if(!strcmp(strrchr(e->d_name,'.'),".tmp")) {
     char bak[PATH_CAP];if(snprintf(bak,sizeof(bak),"%s.bak",primary)>=(int)sizeof(bak)) {import_failure(&r,"path",source);continue;}
     if(exists(bak))continue;
    }
   }
   int type=kind(dirs_[k],name);
   if(!type || (type==THUMB)!=(pass==1))continue;
   if(progress && !progress(e->d_name,ctx)) {r.interrupted=true;closedir(d);return r;}
   import_one(source,dest,dirs_[k],name,type,&r);
   if(history_interrupted) {r.interrupted=true;closedir(d);return r;}
  }
  if(errno)import_failure(&r,"readdir",p);
  closedir(d);
 }
 return r;
}
/* Completion manifest covers metadata and the order-independent per-file
 * receipts. Missing/interrupted manifests are rebuilt from durable receipts. */
typedef struct { uint32_t magic,count,hash,newest,crc; uint64_t bytes; psx_import_result result; } import_manifest;
/* Stack contexts are always resumed by the caller: no second thread runs
 * behind the LOW lane. ARM saves all AAPCS callee-saved integer/VFP state. */
typedef struct {
#ifdef __3DS__
 void *sp;
#else
 ucontext_t context;
#if defined(__SANITIZE_ADDRESS__)
 void *fake_stack;const void *bottom;size_t size;
#endif
#endif
 int error;
} stack_context;
typedef struct {
 stack_context parent,child;void *memory;
 unsigned slices;uint64_t began;
 bool started,cancel,exited;
} import_stack;
struct psx_import_cursor {
 import_stack stack;psx_import_io io;
 char source[PATH_CAP],dest[PATH_CAP];
 DIR *dir;char (*names)[256];unsigned names_count,names_cap,name_at;bool names_ready;
 unsigned folder,pass,phase; bool force,done,preserved,changed;
 import_manifest manifest; psx_import_result result;psx_import_continue resume;
 source_cache *cache;unsigned cached;receipt_entry *receipts;bool index_invalid,index_loaded;
 void *workspace;size_t workspace_capacity;
 directory_cache *directories;
 bool task_mode,task_ok;int task_error;bool (*task)(void *);void *task_ctx;

};
static COLD bool manifest_valid(const void *ptr)
{
 import_manifest m=*(const import_manifest *)ptr;unsigned crc=m.crc;m.crc=0;
 return m.magic==0x324d4950 && crc==crc32(0,(const void *)&m,sizeof(m));
}
static COLD bool manifest_path(char *out,const char *dest)
{return path(out,dest,"import/psx3d/finished");}
COLD psx_import_cursor *psx_import_open(const char *source,const char *dest,bool force)
{
 size_t sn=strlen(source),dn=strlen(dest);
 if(!sn || !dn || sn>=PATH_CAP || dn>=PATH_CAP || !strncmp(source,dest,sn<dn?sn:dn))return NULL;
#ifndef __3DS__
 struct stat a,b;if(!stat(source,&a) && !stat(dest,&b) && a.st_dev==b.st_dev && a.st_ino==b.st_ino)return NULL;
#endif
 psx_import_cursor *c=calloc(1,sizeof(*c));if(!c)return NULL;
 c->workspace_capacity=0x440000;c->workspace=malloc(c->workspace_capacity);

 if(!c->workspace){free(c);errno=ENOMEM;return NULL;}
 budget_cancelled=false;resource_error=false;metrics=&c->io;no_space=&c->result.no_space;strcpy(c->source,source);strcpy(c->dest,dest);c->force=force;c->manifest.magic=0x324d4950;c->phase=6;
 return c;
}
static COLD bool listed(const psx_import_cursor *c,const char *name)
{for(unsigned i=0;i<c->names_count;i++)if(!strcmp(c->names[i],name))return true;return false;}
/* Returns after at most one directory entry; ignored entries cannot turn into
 * an unbounded scan callback either. The UI queues the next LOW callback. */
static COLD bool cursor_step(psx_import_cursor *c,psx_import_result *out)
{
 const char *const *folders=import_folders;
 if(c->done){*out=c->result;return true;}
 errno=0;
 if(c->phase==6) {
  static const char *const meta[]={"config.ini","config.ini.bak","saves/playlog.ini"};
  if(c->folder<3) {
   char p[PATH_CAP];uint64_t size;unsigned hash;
   if(!path(p,c->source,meta[c->folder])){import_failure(&c->result,"metadata-path",c->source);c->done=true;}
   else if(exists(p)) {
    if(!cheap_identity(p,SETTINGS,&size,&hash)){import_failure(&c->result,"metadata-identity",p);c->done=true;}
    else {c->manifest.hash=crc32(c->manifest.hash,(const void *)&hash,sizeof(hash));c->manifest.bytes+=size;c->manifest.count++;}
   }
   c->folder++;*out=c->result;return c->done;
  }
  char p[PATH_CAP];import_manifest *old=NULL;
  if(manifest_path(p,c->dest))old=record_load(p,sizeof(*old),manifest_valid);
  c->changed=c->force || !old;
  free(old);c->phase=1;c->folder=0;*out=c->result;return false;
 }
 if(c->phase==1 && c->folder==IMPORT_FOLDERS){
  char p[PATH_CAP];import_manifest *old=NULL;
  if(manifest_path(p,c->dest))old=record_load(p,sizeof(*old),manifest_valid);
  c->changed|=!old || old->count!=c->manifest.count || old->bytes!=c->manifest.bytes || old->hash!=c->manifest.hash;
  free(old);if(c->changed && !c->result.failed)psx_import_read_continue(c->source,c->dest,&c->resume);
  if((c->changed || !c->index_loaded) && !write_receipt_index(c->dest))import_failure(&c->result,"write-receipt-index",c->dest);
  if(c->changed && !c->result.failed) {
   c->manifest.result=c->result;c->manifest.crc=0;c->manifest.crc=crc32(0,(const void *)&c->manifest,sizeof(c->manifest));
   char marker[PATH_CAP],tmp[PATH_CAP];
   if(!manifest_path(marker,c->dest) || snprintf(tmp,sizeof(tmp),"%s.import-tmp",marker)>=PATH_CAP || !stage_record(tmp,&c->manifest,sizeof(c->manifest)))import_failure(&c->result,"stage-manifest",c->dest);
  }
  c->done=true;*out=c->result;return true;
 }
 char base[PATH_CAP]={0};
 if(!path(base,c->source,folders[c->folder])){import_failure(&c->result,"folder-path",c->source);c->done=true;goto end;}
 /* One directory snapshot serves both state/card and thumbnail passes.
  * It also answers whether a .bak/.tmp has a primary, without opening and
  * statting that primary just to ignore a backup on every returning boot. */
 if(!c->names_ready) {
  if(!c->dir){c->dir=opendir(base);if(!c->dir){if(errno!=ENOENT)import_failure(&c->result,"opendir",base);goto next;}}
  struct dirent *e=readdir(c->dir);
  if(e) {
   if(strlen(e->d_name)>=256){import_failure(&c->result,"directory-name-too-long",base);goto end;}
   if(c->names_count==c->names_cap) {
    unsigned cap=c->names_cap?c->names_cap*2:32;void *names=realloc(c->names,cap*sizeof(*c->names));
    if(!names){import_failure(&c->result,"allocate-directory-names",base);goto end;}c->names=names;c->names_cap=cap;
   }
   strcpy(c->names[c->names_count++],e->d_name);goto end;
  }
  if(errno)import_failure(&c->result,"readdir",base);closedir(c->dir);c->dir=NULL;c->names_ready=true;c->name_at=0;goto end;
 }
 if(c->name_at==c->names_count)goto next;
 char name[256];strcpy(name,c->names[c->name_at++]);
 if(!eligible(name))goto end;
 {
  char *suffix=strrchr(name,'.');
  bool backup=suffix && (!strcmp(suffix,".bak") || !strcmp(suffix,".tmp"));
  bool tmp=backup && !strcmp(suffix,".tmp");if(backup)*suffix=0;
  int type=kind(folders[c->folder],name);
  if(!type || (type==THUMB)!=(c->pass==1))goto end;
  if(backup) {
   if(listed(c,name))goto end;
   if(tmp){char bak[256];if(snprintf(bak,sizeof(bak),"%s.bak",name)>=(int)sizeof(bak)){import_failure(&c->result,"backup-path",base);goto end;}if(listed(c,bak))goto end;}
  }
  char h[PATH_CAP];if(!path(h,c->dest,"backups")){import_failure(&c->result,"history-path",c->dest);goto end;}
  history_set_root(h);history_preserved=c->preserved;history_interrupted=false;history_progress=NULL;history_progress_ctx=NULL;
  /* The legacy app cannot run while BreadBox imports. Reuse its selected
   * validated state across state/thumbnail work; no destination result is
   * reused after a write. Clear the context before yielding the LOW lane. */
  active_cache=&c->cache;active_source=c->source;active_cached=c->cached;
  transaction_source=c->source;transaction_force=c->force;transaction_changed=false;
  history_budget_active=true;history_set_checkpoint(history_turn,c);
  errno=0;import_one(c->source,c->dest,folders[c->folder],name,type,&c->result);
  history_set_checkpoint(NULL,NULL);safe_set_checkpoint(NULL,NULL);history_budget_active=false;
  c->changed|=transaction_changed;transaction_source=NULL;
  char rel[PATH_CAP];
  if(path(rel,folders[c->folder],name)) {
   fast_receipt *r=fast_load(c->dest,rel);
   if(r){c->manifest.count++;c->manifest.bytes+=r->size;
    unsigned hash=crc32(0,(const void *)rel,strlen(rel));hash=crc32(hash,(const void *)&r->identity,sizeof(r->identity));c->manifest.hash+=hash;free(r);}
  }
  c->cached=active_cached;active_cache=NULL;active_source=NULL;unload();
  if(errno==ENOSPC)c->result.no_space=true;
  c->preserved=history_preserved;
 }
 goto end;
next:
 if(++c->pass==2){c->pass=0;c->folder++;free(c->names);c->names=NULL;c->names_count=c->names_cap=0;c->names_ready=false;}c->name_at=0;
end:
 /* A failed history snapshot must forbid every retention mutation. A full
  * SD also stops here; receipts retain the successfully completed files. */
 if(c->result.failed && !c->stack.cancel){log_printf("import: failed op=cursor-step phase=%u folder=%u path=%s errno=%d\n",c->phase,c->folder,base,errno);}
 if(c->result.failed)c->done=true;
 if(errno==ENOSPC)c->result.no_space=true;
 *out=c->result;return c->done;
}
/* All private-stack work is serialized inside psx_import_step/close. The
 * deadline is checked between 64 KiB slices; returning to the parent yields
 * the actual LOW worker, including when HOME/game launch cancels the job. */
static COLD uint64_t microseconds(void)
{
#ifdef __3DS__
 return svcGetSystemTick()/(SYSCLOCK_ARM11/1000000);
#else
 struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;
#endif
}
#ifdef __3DS__
__attribute__((naked,noinline,section(".psx3d_cold.text")))
static void stack_swap(stack_context *from __attribute__((unused)),stack_context *to __attribute__((unused)))
{
 __asm__ volatile("push {r4-r12,lr}\n vpush {d8-d15}\n str sp,[r0]\n ldr sp,[r1]\n vpop {d8-d15}\n pop {r4-r12,pc}\n");
}
#else
#if defined(__SANITIZE_ADDRESS__)
extern void __sanitizer_start_switch_fiber(void **,const void *,size_t);
extern void __sanitizer_finish_switch_fiber(void *,const void **,size_t *);
#endif
static COLD void stack_swap(stack_context *from,stack_context *to)
{
#if defined(__SANITIZE_ADDRESS__)
 __sanitizer_start_switch_fiber(&from->fake_stack,to->bottom,to->size);
#endif
 if(swapcontext(&from->context,&to->context))abort();
#if defined(__SANITIZE_ADDRESS__)
 __sanitizer_finish_switch_fiber(from->fake_stack,&to->bottom,&to->size);
#endif
}
#endif
static COLD void context_switch(stack_context *from,stack_context *to)
{
 /* errno belongs to the suspended operation too: other LOW/visible jobs
  * use this same thread between permits. */
 from->error=errno;errno=to->error;stack_swap(from,to);errno=from->error;
}
static psx_import_cursor *entering_stack;
static COLD bool stack_turn(void *ctx)
{
 psx_import_cursor *c=ctx;import_stack *s=&c->stack;
 if(s->slices && --s->slices && microseconds()-s->began<8000)return true;
 history_set_checkpoint(NULL,NULL);safe_set_checkpoint(NULL,NULL);
 io_turn=NULL;io_context=NULL;context_switch(&s->child,&s->parent);
 if(history_budget_active)history_set_checkpoint(history_turn,c);
 io_turn=stack_turn;io_context=c;safe_set_checkpoint(history_turn,c);safe_set_io_observer(trace_io);s->slices=1;s->began=microseconds();
 return !s->cancel;
}
static COLD void stack_entry(void)
{
 psx_import_cursor *c=entering_stack;import_stack *s=&c->stack;
#ifndef __3DS__
#if defined(__SANITIZE_ADDRESS__)
 __sanitizer_finish_switch_fiber(NULL,&s->parent.bottom,&s->parent.size);
#endif
#endif
 io_turn=stack_turn;io_context=c;safe_set_checkpoint(history_turn,c);safe_set_io_observer(trace_io);s->slices=1;s->began=microseconds();
 if(c->task_mode){c->task_ok=c->task(c->task_ctx);c->task_error=errno;goto completed;}
 if(!c->force)c->index_loaded=read_receipt_index(c->dest);
 while(!s->cancel) {
  if(cursor_step(c,&c->result))break;
  if(!stack_turn(c))break;
 }
completed:
 if(s->cancel){c->result.interrupted=true;c->done=true;}
 if(c->dir){closedir(c->dir);c->dir=NULL;}
 history_set_checkpoint(NULL,NULL);safe_set_checkpoint(NULL,NULL);history_budget_active=false;
 unload();transaction_source=NULL;active_cache=NULL;active_source=NULL;io_turn=NULL;io_context=NULL;
 s->exited=true;
#ifndef __3DS__
#if defined(__SANITIZE_ADDRESS__)
 /* This context will never resume: discard its sanitizer fake stack. */
 __sanitizer_start_switch_fiber(NULL,s->parent.bottom,s->parent.size);
 if(setcontext(&s->parent.context))abort();
#else
 context_switch(&s->child,&s->parent);
#endif
#else
 context_switch(&s->child,&s->parent);
#endif
 abort();
}
static COLD bool stack_open(psx_import_cursor *c)
{
 import_stack *s=&c->stack;size_t size=192*1024;if(!s->memory)s->memory=malloc(size);if(!s->memory)return false;
#ifdef __3DS__
 /* 64 bytes of d8-d15, then ten integer registers r4-r12/lr. Initial SP
  * and every switch stay 8-byte aligned. stack_entry never returns. */
 uint32_t *top=(void *)((uintptr_t)((char *)s->memory+size)&~(uintptr_t)7);
 top-=26;memset(top,0,26*sizeof(*top));top[25]=(uintptr_t)stack_entry;s->child.sp=top;
#else
 if(getcontext(&s->child.context)){free(s->memory);s->memory=NULL;return false;}
 s->child.context.uc_stack.ss_sp=s->memory;s->child.context.uc_stack.ss_size=size;s->child.context.uc_link=NULL;
 makecontext(&s->child.context,stack_entry,0);
#if defined(__SANITIZE_ADDRESS__)
 s->child.bottom=s->memory;s->child.size=size;
#endif
#endif
 s->started=true;return true;
}
COLD bool psx_import_step(psx_import_cursor *c,psx_import_result *out)
{
 c->io.last_op[0]=0;c->io.last_path[0]=0;
 import_stack *s=&c->stack;
 if(!s->started && !c->done && !stack_open(c)){import_failure(&c->result,"allocate-import-stack",c->source);c->done=true;}
 receipt_map=&c->receipts;index_invalid=&c->index_invalid;workspace=&c->workspace;workspace_capacity=&c->workspace_capacity;active_directories=&c->directories;
 if(!c->done){entering_stack=c;context_switch(&s->parent,&s->child);}
 receipt_map=NULL;index_invalid=NULL;workspace=NULL;workspace_capacity=NULL;active_directories=NULL;
 if(c->done){unload();free(c->workspace);c->workspace=NULL;c->workspace_capacity=0;}
 *out=c->result;return c->done;
}
/* Worker teardown also advances one permit at a time. Cancellation stops
 * new work, while mandatory closes/rollback still yield before each syscall. */
COLD bool psx_import_cancel_step(psx_import_cursor *c)
{
 import_stack *s=&c->stack;s->cancel=true;
 if(!s->started || s->exited)return true;
 receipt_map=&c->receipts;index_invalid=&c->index_invalid;workspace=&c->workspace;workspace_capacity=&c->workspace_capacity;active_directories=&c->directories;
 entering_stack=c;context_switch(&s->parent,&s->child);
 receipt_map=NULL;index_invalid=NULL;workspace=NULL;workspace_capacity=NULL;active_directories=NULL;
 return s->exited;
}
static COLD void stack_close(psx_import_cursor *c)
{
 import_stack *s=&c->stack;if(!s->started)return;
 receipt_map=&c->receipts;index_invalid=&c->index_invalid;workspace=&c->workspace;workspace_capacity=&c->workspace_capacity;active_directories=&c->directories;
 s->cancel=true;while(!s->exited){entering_stack=c;context_switch(&s->parent,&s->child);}
 free(s->memory);s->memory=NULL;receipt_map=NULL;index_invalid=NULL;workspace=NULL;workspace_capacity=NULL;active_directories=NULL;
}
COLD void psx_import_cursor_continue(const psx_import_cursor *c,psx_import_continue *out){*out=c->resume;}
COLD psx_import_io psx_import_io_counts(const psx_import_cursor *c){return c->io;}
COLD bool psx_import_changed(const psx_import_cursor *c){return c->changed;}
COLD bool psx_import_task_step(psx_import_cursor *c,bool (*fn)(void *),void *ctx,bool *ok)
{
 c->io.last_op[0]=0;c->io.last_path[0]=0;
 import_stack *s=&c->stack;
 if(!c->task_mode){
  if(!c->done || !s->exited){*ok=false;return true;}
  void *memory=s->memory;memset(s,0,sizeof(*s));s->memory=memory;c->task_mode=true;c->task=fn;c->task_ctx=ctx;
  if(!stack_open(c)){c->task_mode=false;*ok=false;return true;}
 }
 receipt_map=&c->receipts;index_invalid=&c->index_invalid;workspace=&c->workspace;workspace_capacity=&c->workspace_capacity;active_directories=&c->directories;
 entering_stack=c;context_switch(&s->parent,&s->child);
 receipt_map=NULL;index_invalid=NULL;workspace=NULL;workspace_capacity=NULL;active_directories=NULL;
 if(!s->exited)return false;
 c->task_mode=false;*ok=c->task_ok && !s->cancel;errno=c->task_error;return true;
}
COLD bool psx_import_finish(psx_import_cursor *c)
{
 if(!c->done || c->result.failed || c->result.interrupted)return false;
 if(!c->changed)return true;
 char p[PATH_CAP],tmp[PATH_CAP];
 if(!manifest_path(p,c->dest) || snprintf(tmp,sizeof(tmp),"%s.import-tmp",p)>=PATH_CAP)return false;
 return publish_record(p,tmp);
}
COLD void psx_import_close(psx_import_cursor *c)
{if(c){stack_close(c);while(c->directories){directory_cache *next=c->directories->next;free(c->directories);c->directories=next;}free(c->workspace);free(c->names);if(metrics==&c->io){metrics=NULL;no_space=NULL;}while(c->receipts){receipt_entry *next=c->receipts->next;free(c->receipts);c->receipts=next;}while(c->cache){source_cache *next=c->cache->next;free(c->cache);c->cache=next;}if(c->dir)closedir(c->dir);free(c);}}
COLD void psx_import_read_continue(const char *source,const char *dest,psx_import_continue *out)
{
 memset(out,0,sizeof(*out));char line[1024],config[PATH_CAP];
 if(!path(config,source,"config.ini"))return;
 FILE *f=fopen(config,"r");
 while(f && fgets(line,sizeof(line),f)) {
  line[strcspn(line,"\r\n")]=0;char *v=strchr(line,'=');if(!v)continue;*v++=0;
  char *field=!strcmp(line,"last_game")?out->title:!strcmp(line,"last_game_save")?out->save:
   !strcmp(line,"last_game_state")?out->state:!strcmp(line,"last_game_path")?out->rom:NULL;
  if(field && strlen(v)<sizeof(out->title))strcpy(field,v);
 }
 if(f)fclose(f);
 struct stat st;
 if(!out->state[0] || !out->rom[0] || stat(out->rom,&st) ||
    !psx_import_continue_ready(source,dest,out->state,&out->saved))out->saved=0;
}
COLD bool psx_import_continue_ready(const char *source,const char *dest,const char *name,unsigned *saved)
{
 if(!eligible(name))return false;
 char rel[PATH_CAP],base[PATH_CAP],destfile[PATH_CAP];
 if(snprintf(rel,sizeof(rel),"states/%s.sta",name)<PATH_CAP && path(base,source,rel) && path(destfile,dest,rel)) {
  fast_receipt *r=fast_load(dest,rel);
  if(r){uint64_t size;unsigned hash;bool ok=r->active && fast_same(r,base,STATE) && cheap_identity(destfile,STATE,&size,&hash) && size==r->size && hash==r->identity;
   if(ok)*saved=r->info.saved;free(r);if(ok)return true;}
 }
 char a[PATH_CAP],b[PATH_CAP],selected[PATH_CAP];file_info i;
 char leaf[512];
 if(snprintf(leaf,sizeof(leaf),"states/%s.sta",name)>=(int)sizeof(leaf) ||
    !path(a,source,leaf) || !path(b,dest,leaf) ||
    !source_file(a,STATE,selected,&i) || !equal(selected,b))return false;
 *saved=i.saved;return true;
}
