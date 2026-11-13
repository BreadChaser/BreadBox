#define main fixture_main
#include "check.c"
#undef main
#include "native_fs.h"
#include <fcntl.h>
static unsigned calls[6],fail_op,injected,handles;
static Result fault(unsigned op){calls[op]++;if(fail_op==op+1){fail_op=0;injected++;return -RD_OUT_OF_MEMORY;}return 0;}
ssize_t utf8_to_utf16(uint16_t *out,const uint8_t *s,size_t n)
{size_t i=0;while(s[i] && i<n){assert(s[i]<128);out[i]=s[i];i++;}return i;}
Result FSUSER_OpenFileDirectly(Handle *h,int archive,FS_Path empty,FS_Path path,unsigned mode,unsigned attrs)
{
 (void)attrs;assert(archive==ARCHIVE_SDMC && empty.data);Result r=fault(0);if(r)return r;
 char p[1024]="sdmc:";const uint16_t *w=path.data;size_t i=0;while(w[i]){p[i+5]=w[i];i++;}p[i+5]=0;
 int fd=open(p,mode&FS_OPEN_WRITE?O_WRONLY|O_CREAT:O_RDONLY,0600);if(fd<0)return -5;
 *h=fd;handles++;return 0;
}
Result FSFILE_GetSize(Handle h,uint64_t *size){Result r=fault(1);if(r)return r;struct stat st;if(fstat(h,&st))return -5;*size=st.st_size;return 0;}
Result FSFILE_Close(Handle h){Result r=fault(2);assert(handles);handles--;assert(!close(h));return r;}
Result FSFILE_Read(Handle h,uint32_t *got,uint64_t at,void *data,uint32_t n)
{Result r=fault(3);if(r)return r;ssize_t k=pread(h,data,n,at);if(k<0)return -5;*got=k;return 0;}
Result FSFILE_Write(Handle h,uint32_t *put,uint64_t at,const void *data,uint32_t n,unsigned flags)
{assert(!flags);Result r=fault(4);if(r)return r;ssize_t k=pwrite(h,data,n,at);if(k<0)return -5;*put=k;return 0;}
Result FSFILE_Flush(Handle h){Result r=fault(5);if(r)return r;return fsync(h)?-5:0;}
static psx_import_result run_native(void)
{psx_import_cursor *c=psx_import_open(src,dst,false);assert(c);psx_import_result r;unsigned ticks=0;bool done=false;
 while(!done){uint64_t before=psx_import_io_counts(c).metadata_calls;done=psx_import_step(c,&r);psx_import_io io=psx_import_io_counts(c);assert(io.metadata_calls-before<=1 && ++ticks<100000);if(io.metadata_calls>before)assert(io.last_op[0] && io.last_path[0]);}if(!r.failed)assert(psx_import_finish(c));psx_import_close(c);assert(!handles);return r;}
int main(int argc,char **argv)
{
 assert(argc==3 && !chdir(argv[1]));unsigned scenario=atoi(argv[2]);
 assert(!__real_mkdir("sdmc:",0700));strcpy(src,"sdmc:/source");strcpy(dst,"sdmc:/dest");setup(src);setup(dst);
 void *body=state(300);put(src,"states/G.st5",body,0x440000);free(body);
 fail_op=scenario;psx_import_result r=run_native();
 if(scenario){assert(injected==1 && r.failed && !r.rejected && !r.copied);r=run_native();}
 assert(!r.failed && !r.rejected && r.copied==1);assert(crcfile(src,"states/G.st5")==crcfile(dst,"states/G.st5"));
 r=run_native();assert(!r.failed && !r.rejected && !r.copied);
 for(unsigned op=1;op<=4;op++){
  fail_op=op;r=run_native();assert(r.failed && !r.rejected && !r.copied);
  r=run_native();assert(!r.failed && !r.rejected && !r.copied);
 }
 assert(calls[0] && calls[2] && calls[3] && calls[4] && calls[5]);
 puts("Native FSFILE explicit-offset transfers, ENOMEM, close and receipt retry: PASS");
}
