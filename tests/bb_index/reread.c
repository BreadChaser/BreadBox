#define _DEFAULT_SOURCE
/* Deterministic directory I/O around the real index, adapters and allocator. */
#include "bb_index.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <malloc.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

unsigned bb_index_test_pending_live(void);
unsigned bb_index_test_pending_real_live(void);
unsigned bb_index_test_pending_allocs(void);
unsigned bb_index_test_evictions(void);
unsigned bb_index_test_rereads(void);
extern unsigned bb_index_test_cap,bb_index_test_block,bb_index_test_fail_pending_at;

typedef enum { STABLE, METADATA_ERROR, BALANCED_RENAME, CASE_PERMUTATION } change;
typedef struct { char path[512]; unsigned pos; struct dirent entry; } directory;
static change active;
static bool ties,renamed;
static unsigned root_reads,metadata_failures,filler=3600,heavy=1000;
static const char *const plain[]={"a","b","c"};
static const char *const variants[]={"alpha","Alpha","ALPHA"};

DIR *__wrap_opendir(const char *path)
{
    assert(!strcmp(path,"r") || !strncmp(path,"r/",2));
    directory *d=calloc(1,sizeof(*d));assert(d);
    snprintf(d->path,sizeof(d->path),"%s",path);
    if(!strcmp(path,"r")) root_reads++;
    return (DIR *)d;
}
static struct dirent *emit(directory *d,const char *name,unsigned type)
{
    memset(&d->entry,0,sizeof(d->entry));
    snprintf(d->entry.d_name,sizeof(d->entry.d_name),"%s",name);
    d->entry.d_type=type;
    return &d->entry;
}
static void numbered(char *name,char prefix,unsigned i,unsigned len)
{
    memset(name,'x',len);name[len]=0;
    char number[16];snprintf(number,sizeof(number),"%c%05u",prefix,i);
    memcpy(name,number,6);
}
struct dirent *__wrap_readdir(DIR *dir)
{
    directory *d=(directory *)dir;unsigned i=d->pos++;char name[256];
    if(!strcmp(d->path,"r")) {
        if(i<3) {
            unsigned at=active==CASE_PERMUTATION && root_reads>=3?2-i:i;
            const char *entry=ties?variants[at]:plain[at];
            if(renamed && active==BALANCED_RENAME) entry=at==0?"y":at==1?"0":"c";
            return emit(d,entry,active==METADATA_ERROR?DT_UNKNOWN:DT_DIR);
        }
        if(i-3<filler) { numbered(name,'z',i-3,255);return emit(d,name,DT_DIR); }
    } else if(!strchr(d->path+2,'/') && strlen(d->path+2)<32) {
        if(i<heavy) { numbered(name,'s',i,100);return emit(d,name,DT_DIR); }
        if(i==heavy) return emit(d,"game.cue",DT_REG);
    }
    return NULL;
}
int __wrap_closedir(DIR *dir)
{
    directory *d=(directory *)dir;
    if(active==BALANCED_RENAME && !strcmp(d->path,"r/a")) renamed=true;
    free(d);return 0;
}
int __real_lstat(const char *path,struct stat *st);
int __wrap_lstat(const char *path,struct stat *st)
{
    if(strncmp(path,"r/",2)) return __real_lstat(path,st);
    if(active==METADATA_ERROR && root_reads>1 && !strcmp(path,"r/c")) {
        metadata_failures++;errno=EIO;return -1;
    }
    memset(st,0,sizeof(*st));st->st_mode=S_IFDIR|0700;return 0;
}
static void scan(void)
{
    root_reads=metadata_failures=0;renamed=false;
    bb_index_start("r",true);
    for(unsigned i=0;bb_index_busy() && i<30000;i++)usleep(1000);
    assert(!bb_index_busy());
    assert(!bb_index_test_pending_live() && !bb_index_test_pending_real_live());
}
typedef struct { unsigned char *bytes; size_t size; char games[3][512]; } saved;
static unsigned char *read_cache(size_t *size)
{
    FILE *f=fopen("reread-index","rb");assert(f);
    assert(!fseek(f,0,SEEK_END));long len=ftell(f);assert(len>0);
    rewind(f);unsigned char *bytes=malloc((size_t)len);assert(bytes);
    assert(fread(bytes,1,(size_t)len,f)==(size_t)len);assert(!fclose(f));
    *size=(size_t)len;return bytes;
}
static saved seed(void)
{
    active=STABLE;scan();
    assert(bb_index_status()==BB_INDEX_COMPLETE && bb_index_complete());
    assert(bb_index_ps1_count()==3 && root_reads==4);
    saved s={0};s.bytes=read_cache(&s.size);
    for(int i=0;i<3;i++) {
        assert(bb_index_ps1_get(i,s.games[i],sizeof(s.games[i])));
        char expected[512];snprintf(expected,sizeof(expected),"r/%s/game.cue",ties?variants[2-i]:plain[i]);
        assert(!strcmp(s.games[i],expected));
    }
    return s;
}
static void unchanged(const saved *s,bb_index_status_t status)
{
    assert(bb_index_status()==status && bb_index_complete() && bb_index_ps1_count()==3);
    for(int i=0;i<3;i++) {
        char path[512];assert(bb_index_ps1_get(i,path,sizeof(path)));
        assert(!strcmp(path,s->games[i]));
    }
    size_t size;unsigned char *bytes=read_cache(&size);
    assert(size==s->size && !memcmp(bytes,s->bytes,size));free(bytes);
    assert(!bb_index_test_pending_live() && !bb_index_test_pending_real_live());
}
static void changed(change how)
{
    ties=how==CASE_PERMUTATION;
    saved s=seed();active=how;
    unsigned evictions=bb_index_test_evictions(),rereads=bb_index_test_rereads();
    scan();unchanged(&s,BB_INDEX_ERROR);
    assert(bb_index_test_evictions()>evictions && bb_index_test_rereads()>rereads);
    if(how==METADATA_ERROR) assert(metadata_failures);
    if(how==BALANCED_RENAME) assert(renamed);
    if(how==CASE_PERMUTATION) assert(root_reads>=3);
    free(s.bytes);
}
static void allocation_sweep(void)
{
    filler=30;heavy=100;bb_index_test_cap=14336;bb_index_test_block=1024;
    unsigned evictions=bb_index_test_evictions(),rereads=bb_index_test_rereads();
    saved s=seed();unsigned allocations=bb_index_test_pending_allocs();
    assert(allocations>3 && bb_index_test_evictions()>evictions && bb_index_test_rereads()>rereads);
    for(unsigned i=1;i<=allocations;i++) {
        bb_index_test_fail_pending_at=i;scan();unchanged(&s,BB_INDEX_LIMITED);
    }
    bb_index_test_fail_pending_at=0;scan();unchanged(&s,BB_INDEX_COMPLETE);
    printf("bb_index: all %u pending allocation failures release storage and retain cache: ok\n",allocations);
    free(s.bytes);
}
int main(int argc,char **argv)
{
    assert(argc==2);
    mallopt(M_MMAP_THRESHOLD,32*1024*1024);
    if(!strcmp(argv[1],"metadata-error")) changed(METADATA_ERROR);
    else if(!strcmp(argv[1],"balanced-rename")) changed(BALANCED_RENAME);
    else if(!strcmp(argv[1],"case-permutation")) changed(CASE_PERMUTATION);
    else { assert(!strcmp(argv[1],"allocation-sweep"));allocation_sweep(); }
    bb_index_shutdown();while(!bb_index_shutdown_done())usleep(1000);
    printf("bb_index: %s: ok\n",argv[1]);return 0;
}
