#include "gba_saveid.h"
#include "gba_cart.h"
#include "app_policy.h"
#include "safefile.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <zlib.h>
#include <errno.h>
#include <dirent.h>

/* Full CRC is only needed to locate already-existing full-CRC data. Never
 * infer ownership from a stale path/header in an older library index. */
extern bool gba_cart_index_fingerprint(const char *,unsigned char [192],uint32_t *) __attribute__((weak));

void gba_saveid_header(const unsigned char h[192], uint32_t crc, char id[GBA_ID_LEN])
{
    char code[5];
    for(unsigned i=0;i<4;i++) {
        unsigned char c=h[0xac+i];
        code[i]=(c>='A'&&c<='Z')||(c>='0'&&c<='9')?c:'_';
    }
    code[4]=0;
    snprintf(id,GBA_ID_LEN,"gba-%s-%08lx",code,(unsigned long)crc);
}
bool gba_saveid_resolve(const char *path,unsigned char h[192],uint32_t *crc,char id[GBA_ID_LEN])
{
    bool full=false;uint32_t sample;
    if(!gba_cart_identity(path,h,crc,&full,&sample))return false;
    /* Existing sampled data remains authoritative even if a different ROM
     * with the same game code later gains a legacy full-CRC save. */
    gba_saveid_header(h,sample,id);memcpy(id,"gbs",3);
    char prefix[GBA_ID_LEN];gba_saveid_header(h,0,prefix);prefix[9]=0;
    const char *dirs[]={APP_SD_ROOT "/saves/gba",APP_SD_ROOT "/states/gba",APP_SD_ROOT "/config"};
    bool legacy=false,owned=false;
    for(unsigned i=0;i<sizeof(dirs)/sizeof(*dirs);i++) {
        DIR *d=opendir(dirs[i]);if(!d)continue;
        struct dirent *entry;
        while((entry=readdir(d))) {
            size_t len=strlen(entry->d_name);
            if(len<=17 || entry->d_name[17]!='.')continue;
            if(!strncmp(entry->d_name,id,17))owned=true;
            if(!strncmp(entry->d_name,prefix,9))legacy=true;
        }
        closedir(d);
    }
    if(owned){*crc=sample;return true;}
    if(full){gba_saveid_header(h,*crc,id);return true;}
    if(legacy && gba_cart_index_fingerprint) {
        if(!gba_cart_index_fingerprint(path,h,crc))return false;
        gba_saveid_header(h,*crc,id);return true;
    }
    gba_saveid_header(h,*crc,id);memcpy(id,"gbs",3);return true;
}
bool gba_saveid_path(const char *path, char id[GBA_ID_LEN])
{
    unsigned char h[192];uint32_t crc;
    return gba_saveid_resolve(path,h,&crc,id);
}
bool gba_saveid_copy(const char *old, const char *next)
{
    struct stat st;
    if(!stat(next,&st))return true;
    if(errno!=ENOENT)return false;
    if(stat(old,&st))return errno==ENOENT;
    if(st.st_size<0 || st.st_size>16*1024*1024)return false;
    FILE *f=fopen(old,"rb");if(!f)return false;
    void *data=malloc(st.st_size?st.st_size:1);
    bool ok=data && fread(data,1,st.st_size,f)==(size_t)st.st_size && !ferror(f);
    fclose(f);
    if(ok) {safe_format_t fmt={.size=st.st_size};ok=safe_write(next,&fmt,data,true);}
    free(data);return ok;
}
/* The ownership record is a crash-safe text file: the claim, then the same name completed.
 * (A sized binary write kept the shorter claim as <game>.id.bad on every first start.) */
static bool owner_write(const char *owner, const char *text)
{
    FILE *f=safe_begin(owner);
    return f && safe_finish(f,owner,fputs(text,f)>=0);
}
bool gba_saveid_migrate(const char *path, const char *id)
{
    const char *base=strrchr(path,'/');base=base?base+1:path;
    char name[256],old[512],next[512],owner[512],claimed[GBA_ID_LEN]={0};
    if(strlen(base)>=sizeof(name))return false;
    strcpy(name,base);char *dot=strrchr(name,'.');if(dot)*dot=0;
    snprintf(owner,sizeof(owner),APP_SD_ROOT "/saves/gba/%s.id",name);
    safe_recover(owner);
    FILE *f=fopen(owner,"rb");
    if(f) {size_t n=fread(claimed,1,sizeof(claimed)-1,f);bool ok=!ferror(f);fclose(f);
        if(!ok)return false;
        claimed[n]=0;
        bool complete=n && claimed[n-1]=='\n';if(complete)claimed[--n]=0;
        if(strcmp(claimed,id) || complete)return true;
    } else {
        /* First adoption owns the ambiguous old name, just like PS1 saveid.
         * Claim BEFORE copying; a failed/partial migration is safely retryable. */
        if(errno!=ENOENT)return false;
        if(!owner_write(owner,id))return false;
    }
    snprintf(old,sizeof(old),APP_SD_ROOT "/saves/gba/%s.sav",name);
    snprintf(next,sizeof(next),APP_SD_ROOT "/saves/gba/%s.sav",id);
    if(!gba_saveid_copy(old,next))return false;
    /* Keep all legacy recovery candidates, without opening/renaming them. */
    char from[528],to[528];
    const char *ext[]={".bak",".tmp"};
    for(unsigned j=0;j<2;j++) {
        snprintf(from,sizeof(from),"%s%s",old,ext[j]);snprintf(to,sizeof(to),"%s%s",next,ext[j]);
        if(!gba_saveid_copy(from,to))return false;
    }
    for(unsigned i=0;i<12;i++) {
        snprintf(old,sizeof(old),APP_SD_ROOT "/states/gba/%s.state%u",name,i);
        snprintf(next,sizeof(next),APP_SD_ROOT "/states/gba/%s.state%u",id,i);
        if(!gba_saveid_copy(old,next))return false;
        for(unsigned j=0;j<2;j++) {
            snprintf(from,sizeof(from),"%s%s",old,ext[j]);snprintf(to,sizeof(to),"%s%s",next,ext[j]);
            if(!gba_saveid_copy(from,to))return false;
        }
    }
    /* Completion is durable: deleting a migrated slot must not resurrect it
     * from the retained legacy copy on the next launch. */
    snprintf(claimed,sizeof(claimed),"%s\n",id);
    return owner_write(owner,claimed);
}

bool gba_state_time_write(const char *path,uint32_t crc,uint32_t saved)
{
    char file[544];uint32_t stamp[2]={crc,saved};
    if(snprintf(file,sizeof(file),"%s.time",path)>=(int)sizeof(file))return false;
    safe_format_t fmt={.size=sizeof(stamp)};
    return safe_write(file,&fmt,stamp,true);
}
uint32_t gba_state_time(const char *path)
{
    /* Version-1 header: magic[8], version, bytes, rom, rom_size, payload CRC.
     * Keep the existing state format readable; metadata is optional. */
    unsigned char header[28];uint32_t crc,stamp[2];char file[544];
    FILE *f=fopen(path,"rb");
    bool ok=f && fread(header,1,sizeof(header),f)==sizeof(header);
    if(f)fclose(f);
    if(!ok || memcmp(header,"BBGBAST1",8))return 0;
    memcpy(&crc,header+24,4);
    if(snprintf(file,sizeof(file),"%s.time",path)>=(int)sizeof(file))return 0;
    f=fopen(file,"rb");ok=f && fread(stamp,1,sizeof(stamp),f)==sizeof(stamp);
    if(f)fclose(f);
    return ok && stamp[0]==crc?stamp[1]:0;
}
