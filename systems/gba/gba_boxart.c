#include "app_policy.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <minizip/unzip.h>
#include "gba_boxart.h"
#ifndef GBA_BOXART_TABLE
#define GBA_BOXART_TABLE "romfs:/covers/gba-codes.txt"
#endif

void gba_boxart_stem(const char *path, char *out, size_t len)
{
 const char *base=strrchr(path,'/');base=base?base+1:path;
 snprintf(out,len,"%s",base);char *dot=strrchr(out,'.');if(dot)*dot=0;
}
static int add(char out[][BOXART_NAME],int n,int max,const char *name)
{
 char clean[BOXART_NAME];boxart_sanitize(name,clean,sizeof(clean));
 if(!*clean)return n;
 for(int i=0;i<n;i++)if(!strcmp(out[i],clean))return n;
 if(n<max)snprintf(out[n++],BOXART_NAME,"%s",clean);
 return n;
}
int gba_boxart_candidates(const char *path,const char *code,char out[][BOXART_NAME],int max)
{
 if(!path || max<=0)return 0;
 char stem[BOXART_NAME];gba_boxart_stem(path,stem,sizeof(stem));
 int n=0;
 const char *ext=strrchr(path,'.');
 if(ext && !strcasecmp(ext,".zip")) {
  // Directory metadata only: never extract or checksum a ROM for box art.
  unzFile z=unzOpen64(path);int found=0;char inner[1024],chosen[BOXART_NAME];
  unz_global_info64 gi;
  if(z && unzGetGlobalInfo64(z,&gi)==UNZ_OK && gi.number_entry<=1024 && unzGoToFirstFile(z)==UNZ_OK)do {
   unz_file_info64 fi;
   if(unzGetCurrentFileInfo64(z,&fi,inner,sizeof(inner),NULL,0,NULL,0)!=UNZ_OK || fi.size_filename>=sizeof(inner))break;
   inner[fi.size_filename]=0;const char *e=strrchr(inner,'.'),*b=strrchr(inner,'/');b=b?b+1:inner;
   if(e && !strcasecmp(e,".gba") && strncmp(inner,"__MACOSX/",9) && strncmp(b,"._",2)) {gba_boxart_stem(inner,chosen,sizeof(chosen));found++;}
  }while(unzGoToNextFile(z)==UNZ_OK);
  if(z)unzClose(z);
  if(found==1)n=add(out,n,max,chosen);
 }
 n=add(out,n,max,stem);
 // Table is sorted by four-byte cartridge code, with revision names deduplicated.
 // It is tiny compared with ROMs and read only on background cache misses.
 FILE *f=code && strlen(code)==4?fopen(GBA_BOXART_TABLE,"r"):NULL;
 char line[BOXART_NAME+8];
 while(f && n<max && fgets(line,sizeof(line),f)) {
  if(strncmp(line,code,4) || line[4]!='\t')continue;
  line[strcspn(line,"\r\n")]=0;n=add(out,n,max,line+5);
 }
 if(f)fclose(f);
 return n;
}
