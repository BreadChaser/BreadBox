#define _GNU_SOURCE
#include "gba_cart.h"
#include <minizip/zip.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>
#include <dirent.h>

static const unsigned char logo[156] = {
  0x24,0xff,0xae,0x51,0x69,0x9a,0xa2,0x21,0x3d,0x84,0x82,0x0a,0x84,0xe4,0x09,0xad,0x11,0x24,0x8b,0x98,0xc0,0x81,0x7f,0x21,0xa3,0x52,0xbe,0x19,0x93,0x09,0xce,0x20,0x10,0x46,0x4a,0x4a,0xf8,0x27,0x31,0xec,0x58,0xc7,0xe8,0x33,0x82,0xe3,0xce,0xbf,0x85,0xf4,0xdf,0x94,0xce,0x4b,0x09,0xc1,0x94,0x56,0x8a,0xc0,0x13,0x72,0xa7,0xfc,0x9f,0x84,0x4d,0x73,0xa3,0xca,0x9a,0x61,0x58,0x97,0xa3,0x27,0xfc,0x03,0x98,0x76,0x23,0x1d,0xc7,0x61,0x03,0x04,0xae,0x56,0xbf,0x38,0x84,0x00,0x40,0xa7,0x0e,0xfd,0xff,0x52,0xfe,0x03,0x6f,0x95,0x30,0xf1,0x97,0xfb,0xc0,0x85,0x60,0xd6,0x80,0x25,0xa9,0x63,0xbe,0x03,0x01,0x4e,0x38,0xe2,0xf9,0xa2,0x34,0xff,0xbb,0x3e,0x03,0x44,0x78,0x00,0x90,0xcb,0x88,0x11,0x3a,0x94,0x65,0xc0,0x7c,0x63,0x87,0xf0,0x3c,0xaf,0xd6,0x25,0xe4,0x8b,0x38,0x0a,0xac,0x72,0x21,0xd4,0xf8,0x07
};
static unsigned char rom[4096];
static size_t read_bytes;
size_t __real_fread(void *,size_t,size_t,FILE *);
size_t __wrap_fread(void *p,size_t n,size_t count,FILE *f)
{ size_t got=__real_fread(p,n,count,f);read_bytes+=n*got;return got; }


static void init_rom(void)
{
    memset(rom, 0, sizeof(rom));
    memcpy(rom+4,logo,sizeof(logo));
    for (unsigned i=0xa0;i<=0xbc;i++) rom[i]=(unsigned char)i;
    unsigned s=0;for(unsigned i=0xa0;i<=0xbc;i++)s+=rom[i];
    rom[0xbd]=(unsigned char)(-((int)s)-0x19);
}
static void zip_file(zipFile z,const char *name,const void *data,unsigned n)
{
    assert(zipOpenNewFileInZip(z,name,NULL,NULL,0,NULL,0,NULL,0,0)==ZIP_OK);
    assert(zipWriteInFileInZip(z,data,n)==ZIP_OK);
    assert(zipCloseFileInZip(z)==ZIP_OK);
}
static void write_zip(const char *path,int count,int corrupt)
{
    zipFile z=zipOpen(path,APPEND_STATUS_CREATE);assert(z);
    for(int i=0;i<count;i++) zip_file(z,i?"other.GBA":"folder/../safe.GbA",rom,sizeof(rom));
    assert(zipClose(z,NULL)==ZIP_OK);
    if(corrupt) {
        FILE *f=fopen(path,"r+b");assert(f);
        long payload=30+(long)strlen("folder/../safe.GbA");
        assert(fseek(f,payload+300,SEEK_SET)==0);int c=fgetc(f);assert(c!=EOF);
        assert(fseek(f,-1,SEEK_CUR)==0);fputc(c^0x40,f);fclose(f);
    }
}
static void write_big_zip(void)
{
    zipFile z=zipOpen("big.zip",APPEND_STATUS_CREATE);assert(z);
    assert(zipOpenNewFileInZip(z,"big.gba",NULL,NULL,0,NULL,0,NULL,Z_DEFLATED,Z_DEFAULT_COMPRESSION)==ZIP_OK);
    unsigned char zeros[32768]={0};
    for(unsigned i=0;i<GBA_CART_MAX_BYTES/sizeof(zeros)+1;i++)
        assert(zipWriteInFileInZip(z,zeros,sizeof(zeros))==ZIP_OK);
    assert(zipCloseFileInZip(z)==ZIP_OK);assert(zipClose(z,NULL)==ZIP_OK);
}
static unsigned progress_calls;
static bool cancelled(unsigned bytes, unsigned total)
{
    assert(total==GBA_CART_MAX_BYTES);progress_calls++;
    return bytes<256*1024;
}
static void write_max_zip(void)
{
    zipFile z=zipOpen("max.zip",APPEND_STATUS_CREATE);assert(z);
    assert(zipOpenNewFileInZip(z,"big.gba",NULL,NULL,0,NULL,0,NULL,Z_DEFLATED,Z_DEFAULT_COMPRESSION)==ZIP_OK);
    unsigned char scratch[32768]={0};memcpy(scratch,rom,sizeof(rom));
    assert(zipWriteInFileInZip(z,scratch,sizeof(scratch))==ZIP_OK);memset(scratch,0,sizeof(scratch));
    for(unsigned i=1;i<GBA_CART_MAX_BYTES/sizeof(scratch);i++) assert(zipWriteInFileInZip(z,scratch,sizeof(scratch))==ZIP_OK);
    assert(zipCloseFileInZip(z)==ZIP_OK);assert(zipClose(z,NULL)==ZIP_OK);
}
int main(void)
{
    mkdir("sdmc:",0777);
    init_rom();
    FILE *f=fopen("direct.GBA","wb");assert(f);fwrite(rom,1,sizeof(rom),f);fclose(f);
    unsigned char h[192];const char *out=NULL;
    assert(gba_cart_is_path("a.GbA")&&gba_cart_is_path("a.ZIP")&&!gba_cart_is_path("a.7z"));
    assert(gba_cart_header("direct.GBA",h));
    assert(gba_cart_prepare("direct.GBA",&out)==GBA_CART_OK&&!strcmp(out,"direct.GBA"));
    rom[0xbd]^=1;f=fopen("bad.gba","wb");fwrite(rom,1,sizeof(rom),f);fclose(f);rom[0xbd]^=1;
    assert(!gba_cart_header("bad.gba",h));
    write_zip("one.zip",1,0);
    assert(gba_cart_header("one.zip",h));
    assert(gba_cart_prepare("one.zip",&out)==GBA_CART_OK&&strstr(out,GBA_CART_CACHE_DIR "/"));
    assert(gba_cart_header(out,h));
    char first[200];snprintf(first,sizeof(first),"%s",out);
    struct stat before,after;assert(!stat(out,&before));
    /* A cache hit does not invoke extraction (a rejecting callback proves it). */
    assert(gba_cart_prepare_progress("one.zip",&out,cancelled)==GBA_CART_OK);
    assert(!strcmp(first,out) && progress_calls==0);
    /* Truncated cached output is discarded and extracted again. */
    assert(!truncate(first,128));assert(gba_cart_prepare("one.zip",&out)==GBA_CART_OK);
    assert(!stat(out,&after) && after.st_size==sizeof(rom));
    /* Distinct paths must not alias; source metadata and CRC invalidate too. */
    write_zip("other.zip",1,0);assert(gba_cart_prepare("other.zip",&out)==GBA_CART_OK);assert(strcmp(first,out));
    struct utimbuf newer={before.st_atime,before.st_mtime+10};assert(!utime("one.zip",&newer));
    assert(gba_cart_prepare("one.zip",&out)==GBA_CART_OK && strcmp(first,out));
    char changed[200];snprintf(changed,sizeof(changed),"%s",out);
    rom[300]^=1;write_zip("one.zip",1,0);rom[300]^=1;assert(!utime("one.zip",&newer));
    assert(gba_cart_prepare("one.zip",&out)==GBA_CART_OK && strcmp(changed,out));
    /* Launching a different ZIP removes the old extracted ROM, but a repeat
     * launch of the same ZIP reuses it without an extraction callback. */
    char retained[200];snprintf(retained,sizeof(retained),"%s",out);
    write_zip("evict.zip",1,0);assert(gba_cart_prepare("evict.zip",&out)==GBA_CART_OK);
    assert(access(retained,F_OK));
    assert(gba_cart_prepare_progress("evict.zip",&out,cancelled)==GBA_CART_OK);
    DIR *cache_dir=opendir(GBA_CART_CACHE_DIR);assert(cache_dir);unsigned extracted=0;struct dirent *ce;
    while((ce=readdir(cache_dir)))if(strlen(ce->d_name)==20 && !strcmp(ce->d_name+16,".gba"))extracted++;
    closedir(cache_dir);assert(extracted==1);

    zipFile sidecar=zipOpen("mac.zip",APPEND_STATUS_CREATE);assert(sidecar);
    zip_file(sidecar,"__MACOSX/._Game.gba",rom,sizeof(rom));
    zip_file(sidecar,"._Game.gba",rom,sizeof(rom));
    zip_file(sidecar,"Game.gba",rom,sizeof(rom));
    assert(zipClose(sidecar,NULL)==ZIP_OK);
    assert(gba_cart_header("mac.zip",h));
    assert(gba_cart_prepare("mac.zip",&out)==GBA_CART_OK);
    write_zip("many.zip",2,0);
    assert(!gba_cart_header("many.zip",h));
    assert(gba_cart_prepare("many.zip",&out)==GBA_CART_MULTI_ROM);
    write_zip("crc.zip",1,1);
    assert(gba_cart_prepare("crc.zip",&out)!=GBA_CART_OK);
    f=fopen("broken.zip","wb");assert(f);fwrite("not a zip",1,9,f);fclose(f);
    assert(gba_cart_prepare("broken.zip",&out)==GBA_CART_CORRUPT);
    write_big_zip();
    assert(gba_cart_prepare("big.zip",&out)==GBA_CART_TOO_LARGE);
    /* Over the limit: identification failures say so (64 MiB GBA Video carts). */
    assert(gba_cart_too_large("big.zip") && !gba_cart_too_large("one.zip") && !gba_cart_too_large("direct.GBA"));
    f=fopen("huge.gba","wb");assert(f);fwrite(rom,1,sizeof(rom),f);fclose(f);
    assert(!truncate("huge.gba",(off_t)GBA_CART_MAX_BYTES+1) && gba_cart_too_large("huge.gba"));
    assert(gba_cart_header("huge.gba",h) && gba_cart_prepare("huge.gba",&out)==GBA_CART_TOO_LARGE);
    assert(!gba_cart_too_large("missing.gba") && !gba_cart_too_large("broken.zip"));
    /* The launch error card: a title line, then the reason (it truncates a one-line message). */
    {const char *e=gba_cart_error(GBA_CART_TOO_LARGE),*nl=strchr(e,'\n');
     assert(nl && !strncmp(e,"Could not start the game",(size_t)(nl-e)) && strstr(nl,"32 MiB limit"));}
    write_max_zip();
    assert(gba_cart_header("max.zip",h));
    assert(gba_cart_prepare_progress("max.zip",&out,cancelled)!=GBA_CART_OK);
    assert(progress_calls>=2);
    assert(gba_cart_prepare("max.zip",&out)==GBA_CART_OK);
    struct stat st;assert(!stat(out,&st) && st.st_size==GBA_CART_MAX_BYTES);
    unsigned char fp[192];uint32_t raw_crc,zip_crc;
    assert(gba_cart_index_fingerprint("max.zip",fp,&zip_crc));
    assert(gba_cart_fingerprint(out,fp,&raw_crc));
    assert(gba_cart_index_fingerprint(out,fp,&raw_crc));assert(raw_crc==zip_crc);
    read_bytes=0;
    assert(gba_cart_fingerprint(out,fp,&raw_crc));assert(raw_crc==zip_crc);
    assert(read_bytes<=192+5*4096+40); /* Independent of the 32 MiB size. */
    /* Same size and mtime, changed body sample: fail closed, worker rehashes. */
    struct stat original;assert(!stat(out,&original));
    FILE *mut=fopen(out,"r+b");assert(mut);assert(!fseek(mut,original.st_size-1,SEEK_SET));fputc(1,mut);fclose(mut);
    struct utimbuf stamp={original.st_atime,original.st_mtime};assert(!utime(out,&stamp));
    assert(gba_cart_fingerprint(out,fp,&raw_crc));
    assert(gba_cart_index_fingerprint(out,fp,&raw_crc));assert(raw_crc!=zip_crc);
    /* Bad cache entries never yield a save owner; the worker repairs them. */
    DIR *ids=opendir("sdmc:/3ds/BreadBox/cache/gba-id");assert(ids);struct dirent *entry;
    while((entry=readdir(ids)))if(strstr(entry->d_name,".id")) {
        char file[512];snprintf(file,sizeof(file),"sdmc:/3ds/BreadBox/cache/gba-id/%s",entry->d_name);
        FILE *bad=fopen(file,"wb");assert(bad);fputs("torn",bad);fclose(bad);
    }
    closedir(ids);assert(gba_cart_fingerprint(out,fp,&raw_crc));
    assert(gba_cart_index_fingerprint(out,fp,&raw_crc));assert(raw_crc!=zip_crc);
    mkdir("twin-a",0700);mkdir("twin-b",0700);
    f=fopen("twin-a/Same.gba","wb");assert(f);assert(fwrite(rom,1,sizeof(rom),f)==sizeof(rom));fclose(f);
    assert(gba_cart_fingerprint("twin-a/Same.gba",fp,&raw_crc));
    assert(gba_cart_index_fingerprint("twin-a/Same.gba",fp,&raw_crc));
    rom[300]^=1; /* Identical header, different body: distinct save owner. */
    f=fopen("twin-b/Same.gba","wb");assert(f);assert(fwrite(rom,1,sizeof(rom),f)==sizeof(rom));fclose(f);
    assert(gba_cart_fingerprint("twin-b/Same.gba",fp,&zip_crc));
    assert(gba_cart_index_fingerprint("twin-b/Same.gba",fp,&zip_crc));assert(raw_crc!=zip_crc);
    assert(!rename("twin-a/Same.gba","Moved.gba"));
    assert(gba_cart_fingerprint("Moved.gba",fp,&zip_crc));assert(raw_crc==zip_crc);
    puts("gba_cart: raw/ZIP headers, CRC, multiple members, 32 MiB streaming, cancellation and bounds: PASS");
    return 0;
}
