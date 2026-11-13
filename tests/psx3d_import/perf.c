/* Production cursor I/O limits, sample changes, force rescan and cancellation. */
#define IMPORT_SEQUENCE_CHECK
#define main fixture_main
#include "check.c"
#undef main
static uint64_t reads,state_reads,crc_bytes;
static bool check_sequence;
static bool fd_path(FILE *f,const char *suffix)
{
 char fd[64],p[2048];snprintf(fd,sizeof(fd),"/proc/self/fd/%d",fileno(f));
 ssize_t n=readlink(fd,p,sizeof(p)-1);assert(n>=0);p[n]=0;
 size_t a=strlen(p),b=strlen(suffix);return a>=b && !strcmp(p+a-b,suffix);
}
static void sequence_write(FILE *f)
{
 if(check_sequence && fd_path(f,"/dest/states/Game.sta.psx3d-new.import-tmp"))
  assert(state_reads>=0x440000); /* The old read/write interleave fails here. */
}
size_t __real_fread(void *,size_t,size_t,FILE *);
size_t __wrap_fread(void *p,size_t size,size_t n,FILE *f)
{size_t got=__real_fread(p,size,n,f);reads+=got*size;if(check_sequence && fd_path(f,"/source/states/Game.sta"))state_reads+=got*size;return got;}
static psx_import_result boot(bool force,bool *changed)
{
 psx_import_cursor *c=psx_import_open(src,dst,force);assert(c);psx_import_result r;
 unsigned ticks=0;while(!psx_import_step(c,&r))assert(++ticks<100000);
 assert(!r.failed && psx_import_finish(c));crc_bytes=psx_import_io_counts(c).crc_bytes;*changed=psx_import_changed(c);psx_import_close(c);return r;
}
static void edit(const char *rel,size_t offset)
{char p[1024];at(p,src,rel);FILE *f=fopen(p,"r+b");assert(f && !fseek(f,offset,SEEK_SET));int b=fgetc(f);assert(b!=EOF && !fseek(f,offset,SEEK_SET));assert(fputc(b^1,f)!=EOF && !fclose(f));}
int main(int argc,char **argv)
{
 assert(psx_import_test_identity_path("sdmc:/3ds/PSX3D/states/Gamé.sta"));
 const char *bad[]={"sdmc://x","sdmc:/x/","sdmc:/x/./y","sdmc:/x/../y","sdmc:/x\\y","sdmc:/x:y","sdmc:/x*","romfs:/x","sdmc:/"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)assert(!psx_import_test_identity_path(bad[i]));
 assert(argc==2 && !chdir(argv[1]));strcpy(src,"source");strcpy(dst,"dest");setup(src);setup(dst);
 size_t bytes=0x440000;void *data=state(300);memset((char *)data+bytes-sizeof(state_trailer_t),0,sizeof(state_trailer_t));
 put(src,"states/Game.sta",data,bytes);free(data);card_file(src,"saves/Game.srm",1);
 bool changed;uint64_t total=bytes+128*1024;reads=0;check_sequence=true;psx_import_result r=boot(false,&changed);check_sequence=false;
 printf("first read=%llu source=%llu\n",(unsigned long long)reads,(unsigned long long)total);
 assert(r.copied==2 && changed && reads<=total*22/10);
 /* One source CRC and one destination CRC; retain room for card identities
  * and receipt/index metadata, but reject hashing the cached write again. */
 assert(crc_bytes<=total*22/10);
 reads=0;r=boot(false,&changed);
 printf("second read=%llu limit=%llu\n",(unsigned long long)reads,(unsigned long long)(128*1024+bytes/100));
 assert(!r.copied && !changed && reads<=128*1024+bytes/100);
 /* The index is only an acceleration: corrupting it falls back to durable
  * per-file receipts and rebuilds it without reading all state payloads. */
 char index[1024];at(index,dst,"import/psx3d/receipt-index");
 FILE *cache=fopen(index,"r+b");assert(cache && fputc(0,cache)!=EOF && !fclose(cache));
 reads=0;r=boot(false,&changed);assert(!r.copied && !changed && reads<=128*1024+bytes/100);
 /* Outside samples: automatic launch leaves it alone; explicit Import again
  * fully validates and preserves the new version through conflict handling. */
 edit("states/Game.sta",bytes/2);r=boot(false,&changed);assert(!changed && !r.copied);
 r=boot(true,&changed);assert(changed && r.conflicts==1);
 /* Spread sample, not header/footer: detected automatically. */
 edit("states/Game.sta",bytes*2/5+20);r=boot(false,&changed);assert(changed && r.conflicts==1);
 /* Card changes away from all samples are still detected. */
 edit("saves/Game.srm",49151);r=boot(false,&changed);assert(changed && r.conflicts==1);
 puts("Import I/O budgets and sampled/full identity behavior: PASS");
}
