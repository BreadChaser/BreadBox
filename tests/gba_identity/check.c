#define _GNU_SOURCE
#include "gba_cart.h"
#include "gba_saveid.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <minizip/zip.h>
#include <zlib.h>
static unsigned char rom[65536];
static void write_rom(const char *path) {
 FILE *f=fopen(path,"wb");assert(f);assert(fwrite(rom,1,sizeof(rom),f)==sizeof(rom));assert(!fclose(f));
}
int main(void) {
 const char *dirs[]={"sdmc:","sdmc:/3ds","sdmc:/3ds/BreadBox","sdmc:/3ds/BreadBox/saves","sdmc:/3ds/BreadBox/saves/gba","sdmc:/3ds/BreadBox/states","sdmc:/3ds/BreadBox/states/gba","sdmc:/3ds/BreadBox/config","a","b"};
 for(unsigned i=0;i<sizeof(dirs)/sizeof(*dirs);i++)mkdir(dirs[i],0700);
 /* Fixture header supplied by the cart suite, no copyrighted ROM contents. */
 FILE *header=fopen("header","rb");assert(header);assert(fread(rom,1,192,header)==192);fclose(header);
 memcpy(rom+0xac,"TEST",4);unsigned sum=0;for(unsigned i=0xa0;i<=0xbc;i++)sum+=rom[i];rom[0xbd]=-sum-0x19;
 write_rom("a/Same.gba");unsigned char h[192];uint32_t crc;char a[GBA_ID_LEN],b[GBA_ID_LEN],full[GBA_ID_LEN],moved[GBA_ID_LEN];
 assert(gba_saveid_resolve("a/Same.gba",h,&crc,a));assert(!strncmp(a,"gbs-TEST-",9));assert(!memcmp(h,rom,192));
 rom[300]=1;write_rom("b/Same.gba");assert(gba_saveid_resolve("b/Same.gba",h,&crc,b));assert(strcmp(a,b));
 assert(!rename("a/Same.gba","Moved.gba"));assert(gba_saveid_path("Moved.gba",moved));assert(!strcmp(a,moved));
 /* ZIP re-extraction keeps the content owner despite the one-game cache. */
 zipFile z=zipOpen("one.zip",APPEND_STATUS_CREATE);assert(z);
 assert(zipOpenNewFileInZip(z,"nested.gba",NULL,NULL,0,NULL,0,NULL,Z_DEFLATED,Z_DEFAULT_COMPRESSION)==ZIP_OK);
 assert(zipWriteInFileInZip(z,rom,sizeof(rom))==ZIP_OK);assert(zipCloseFileInZip(z)==ZIP_OK);assert(zipClose(z,NULL)==ZIP_OK);
 assert(gba_saveid_path("one.zip",moved));assert(!strcmp(b,moved));
 const char *raw;assert(gba_cart_prepare("one.zip",&raw)==GBA_CART_OK);char cached[200];strcpy(cached,raw);assert(!remove(cached));
 assert(gba_saveid_path("one.zip",moved));assert(!strcmp(b,moved));
 /* Older full-CRC data is selected only after hashing actual contents. A
  * different body with the same basename/code never adopts that owner's save. */
 crc=crc32(0,rom,sizeof(rom));gba_saveid_header(h,crc,full);
 char file[256];snprintf(file,sizeof(file),"sdmc:/3ds/BreadBox/saves/gba/%s.sav",full);
 FILE *f=fopen(file,"wb");assert(f);fputs("old progress",f);fclose(f);
 assert(gba_saveid_path("b/Same.gba",moved));assert(!strcmp(full,moved));
 assert(gba_saveid_path("Moved.gba",moved));assert(strcmp(full,moved));
 f=fopen(file,"rb");char data[32]={0};assert(f);assert(fread(data,1,12,f)==12);fclose(f);assert(!strcmp(data,"old progress"));
 /* A later full-CRC cache population must not strand an existing sampled
  * owner. The cache itself is only a speed hint, never save ownership. */
 snprintf(file,sizeof(file),"sdmc:/3ds/BreadBox/saves/gba/%s.sav",b);
 f=fopen(file,"wb");assert(f);fputs("sampled progress",f);fclose(f);
 assert(gba_saveid_resolve("b/Same.gba",h,&crc,moved));assert(!strcmp(b,moved));
 assert(gba_saveid_path("one.zip",moved));assert(!strcmp(b,moved));
 puts("GBA sampled/raw/ZIP/moved identities, same-name isolation, exact full-CRC legacy selection: PASS");
}
