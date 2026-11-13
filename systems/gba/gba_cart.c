#include "gba_cart.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <dirent.h>
#include <utime.h>
#include <time.h>
#include <minizip/unzip.h>

static const unsigned char nintendo_logo[156] = {
  0x24,0xff,0xae,0x51,0x69,0x9a,0xa2,0x21,0x3d,0x84,0x82,0x0a,0x84,0xe4,0x09,0xad,
  0x11,0x24,0x8b,0x98,0xc0,0x81,0x7f,0x21,0xa3,0x52,0xbe,0x19,0x93,0x09,0xce,0x20,
  0x10,0x46,0x4a,0x4a,0xf8,0x27,0x31,0xec,0x58,0xc7,0xe8,0x33,0x82,0xe3,0xce,0xbf,
  0x85,0xf4,0xdf,0x94,0xce,0x4b,0x09,0xc1,0x94,0x56,0x8a,0xc0,0x13,0x72,0xa7,0xfc,
  0x9f,0x84,0x4d,0x73,0xa3,0xca,0x9a,0x61,0x58,0x97,0xa3,0x27,0xfc,0x03,0x98,0x76,
  0x23,0x1d,0xc7,0x61,0x03,0x04,0xae,0x56,0xbf,0x38,0x84,0x00,0x40,0xa7,0x0e,0xfd,
  0xff,0x52,0xfe,0x03,0x6f,0x95,0x30,0xf1,0x97,0xfb,0xc0,0x85,0x60,0xd6,0x80,0x25,
  0xa9,0x63,0xbe,0x03,0x01,0x4e,0x38,0xe2,0xf9,0xa2,0x34,0xff,0xbb,0x3e,0x03,0x44,
  0x78,0x00,0x90,0xcb,0x88,0x11,0x3a,0x94,0x65,0xc0,0x7c,0x63,0x87,0xf0,0x3c,0xaf,
  0xd6,0x25,0xe4,0x8b,0x38,0x0a,0xac,0x72,0x21,0xd4,0xf8,0x07
};

static int suffix_eq(const char *s, const char *ext)
{
    size_t a, b;
    if (!s) return 0;
    a = strlen(s); b = strlen(ext);
    if (a < b) return 0;
    s += a - b;
    while (*s && *ext) if (tolower((unsigned char)*s++) != *ext++) return 0;
    return !*ext;
}

bool gba_cart_is_path(const char *path)
{ return suffix_eq(path, ".gba") || suffix_eq(path, ".zip"); }

static int valid_header(const unsigned char h[192])
{
    unsigned sum = 0;
    if (memcmp(h + 4, nintendo_logo, sizeof(nintendo_logo))) return 0;
    for (unsigned i = 0xa0; i <= 0xbc; ++i) sum += h[i];
    return (unsigned char)(-(int)sum - 0x19) == h[0xbd];
}

static int member_is_gba(const char *name)
{
    const char *base=strrchr(name,'/');base=base?base+1:name;
    return strncmp(name,"__MACOSX/",9) && strncmp(base,"._",2) && suffix_eq(name,".gba");
}

static gba_cart_result zip_find(unzFile z, int *is_gba, unz_file_info64 *chosen)
{
    unz_global_info64 gi;
    char name[1024];
    unsigned count = 0;
    if (unzGetGlobalInfo64(z, &gi) != UNZ_OK || !gi.number_entry || gi.number_entry > 1024 || unzGoToFirstFile(z) != UNZ_OK)
        return GBA_CART_CORRUPT;
    *is_gba = 0;
    for (ZPOS64_T i = 0; i < gi.number_entry; ++i) {
        unz_file_info64 fi;
        if (unzGetCurrentFileInfo64(z, &fi, name, sizeof(name), NULL, 0, NULL, 0) != UNZ_OK ||
            fi.size_filename >= sizeof(name)) return GBA_CART_CORRUPT;
        name[fi.size_filename] = 0;
        if (member_is_gba(name)) {
            ++count;
            if (count == 1) { *chosen = fi; *is_gba = 1; }
        }
        if (i + 1 < gi.number_entry && unzGoToNextFile(z) != UNZ_OK) return GBA_CART_CORRUPT;
    }
    return count > 1 ? GBA_CART_MULTI_ROM : GBA_CART_OK;
}

bool gba_cart_header(const char *path, unsigned char header[192])
{
    if (!path || !header) return false;
    if (suffix_eq(path, ".gba")) {
        FILE *f = fopen(path, "rb");
        int ok = f && fread(header, 1, 192, f) == 192 && valid_header(header);
        if (f) fclose(f);
        return ok;
    }
    if (suffix_eq(path, ".zip")) {
        unzFile z = unzOpen64(path);
        unz_file_info64 fi;
        int has;
        if (!z) return false;
        gba_cart_result r = zip_find(z, &has, &fi);
        int ok = r == GBA_CART_OK && has && fi.uncompressed_size >= 192 &&
                 fi.uncompressed_size <= GBA_CART_MAX_BYTES && !(fi.flag & 1) &&
                 unzGoToFirstFile(z) == UNZ_OK;
        /* Locate the selected member again; enumeration above leaves the last entry selected. */
        if (ok) {
            unz_global_info64 gi; char name[1024]; int found = 0;
            unzGetGlobalInfo64(z, &gi); unzGoToFirstFile(z);
            for (ZPOS64_T i=0; i<gi.number_entry; ++i) {
                unz_file_info64 cur;
                if (unzGetCurrentFileInfo64(z,&cur,name,sizeof(name),NULL,0,NULL,0)!=UNZ_OK) break;
                name[cur.size_filename]=0;
                if (member_is_gba(name)) { found=1; break; }
                if (i+1<gi.number_entry) unzGoToNextFile(z);
            }
            ok = found && unzOpenCurrentFile(z)==UNZ_OK && unzReadCurrentFile(z,header,192)==192 && valid_header(header);
            if (found) unzCloseCurrentFile(z);
        }
        unzClose(z);
        return ok;
    }
    return false;
}

/* Cache names never contain member names. Metadata binds a verified extraction
 * to the full source identity, including the central-directory CRC. */
static char cached_rom[160];
static int ensure_cache(void)
{
    const char *dirs[]={"sdmc:/3ds","sdmc:/3ds/BreadBox","sdmc:/3ds/BreadBox/cache",GBA_CART_CACHE_DIR};
    for(unsigned i=0;i<sizeof(dirs)/sizeof(*dirs);i++)
        if(mkdir(dirs[i],0777) && errno!=EEXIST)return 0;
    remove("sdmc:/3ds/BreadBox/cache/game.gba"); /* obsolete single-slot cache */
    DIR *d=opendir(GBA_CART_CACHE_DIR);struct dirent *e;
    if(d){while((e=readdir(d))) {
        if(strlen(e->d_name)==25 && !strcmp(e->d_name+16,".gba.part") &&
           strspn(e->d_name,"0123456789abcdef")==16) {
            char part[180];snprintf(part,sizeof(part),GBA_CART_CACHE_DIR "/%.25s",e->d_name);remove(part);
        }
    }closedir(d);}

    return 1;
}
static void cache_identity(const char *path,const struct stat *st,const unz_file_info64 *fi,
                           char *identity,size_t cap)
{
    snprintf(identity,cap,"BBGBAZIP1 %llu %lld %08lx %llu\n%s",
        (unsigned long long)st->st_size,(long long)st->st_mtime,(unsigned long)fi->crc,
        (unsigned long long)fi->uncompressed_size,path);
    uint64_t hash=UINT64_C(14695981039346656037);
    for(const unsigned char *p=(const unsigned char *)identity;*p;p++)hash=(hash^*p)*UINT64_C(1099511628211);
    snprintf(cached_rom,sizeof(cached_rom),GBA_CART_CACHE_DIR "/%016llx.gba",(unsigned long long)hash);
}
static int cache_hit(const char *identity,unsigned long long bytes)
{
    char meta[180],actual[1400];struct stat st;unsigned char h[192];
    snprintf(meta,sizeof(meta),"%s.meta",cached_rom);
    FILE *f=fopen(meta,"rb");if(!f)return 0;
    size_t n=fread(actual,1,sizeof(actual)-1,f);actual[n]=0;int ok=!ferror(f)&&feof(f)&&!strcmp(actual,identity);fclose(f);
    if(!ok || stat(cached_rom,&st) || (unsigned long long)st.st_size!=bytes || !gba_cart_header(cached_rom,h))return 0;
    utime(cached_rom,NULL);return 1;
}
/* Only one extracted cartridge survives a launch. Metadata never owns saves. */
static int cache_room(unsigned long long incoming)
{
    (void)incoming;
    DIR *d=opendir(GBA_CART_CACHE_DIR);if(!d)return 0;
    struct dirent *e;int ok=1;
    while((e=readdir(d))) {
        size_t n=strlen(e->d_name);
        if(n!=20 || strcmp(e->d_name+16,".gba") || strspn(e->d_name,"0123456789abcdef")!=16)continue;
        char file[180],meta[190];snprintf(file,sizeof(file),GBA_CART_CACHE_DIR "/%.20s",e->d_name);
        if(!strcmp(file,cached_rom))continue;
        snprintf(meta,sizeof(meta),"%s.meta",file);
        if(remove(file))ok=0;
        if(remove(meta) && errno!=ENOENT)ok=0;
    }
    closedir(d);return ok;
}

gba_cart_result gba_cart_prepare_progress(const char *path, const char **rom_path, bool (*progress)(unsigned, unsigned))
{
    if (!path || !rom_path) return GBA_CART_INVALID;
    *rom_path = NULL;
    if (suffix_eq(path, ".7z")) return GBA_CART_UNSUPPORTED;
    if (suffix_eq(path, ".gba")) {
        struct stat st; unsigned char h[192];
        if (stat(path,&st) || st.st_size < 192) return GBA_CART_IO_ERROR;
        if ((unsigned long long)st.st_size > GBA_CART_MAX_BYTES) return GBA_CART_TOO_LARGE;
        if (!gba_cart_header(path,h)) return GBA_CART_INVALID;
        *rom_path=path; return GBA_CART_OK;
    }
    if (!suffix_eq(path, ".zip")) return GBA_CART_INVALID;
    unzFile z=unzOpen64(path); unz_file_info64 fi; int has=0;
    if (!z) return GBA_CART_CORRUPT;
    gba_cart_result r=zip_find(z,&has,&fi);
    if (r!=GBA_CART_OK) {unzClose(z);return r;}
    if (!has) {unzClose(z);return GBA_CART_INVALID;}
    if (fi.flag & 1) {unzClose(z);return GBA_CART_UNSUPPORTED;}
    if (fi.uncompressed_size<192) {unzClose(z);return GBA_CART_INVALID;}
    if (fi.uncompressed_size>GBA_CART_MAX_BYTES) {unzClose(z);return GBA_CART_TOO_LARGE;}
    if (!ensure_cache()) {unzClose(z);return GBA_CART_IO_ERROR;}
    struct stat source;char identity[1400],part[180],meta[180];
    if(strlen(path)>1024 || stat(path,&source)){unzClose(z);return GBA_CART_IO_ERROR;}
    cache_identity(path,&source,&fi,identity,sizeof(identity));
    if(cache_hit(identity,fi.uncompressed_size)){bool ok=cache_room(0);unzClose(z);if(!ok)return GBA_CART_IO_ERROR;*rom_path=cached_rom;return GBA_CART_OK;}
    snprintf(meta,sizeof(meta),"%s.meta",cached_rom);remove(meta);remove(cached_rom);
    if(!cache_room(fi.uncompressed_size)){unzClose(z);return GBA_CART_IO_ERROR;}
    snprintf(part,sizeof(part),"%s.part",cached_rom);remove(part);
    FILE *out=fopen(part,"wb");
    if (!out) {unzClose(z);return GBA_CART_IO_ERROR;}
    /* Return to the selected member without using its name as a filesystem path. */
    unz_global_info64 gi; unzGetGlobalInfo64(z,&gi); unzGoToFirstFile(z);
    char name[1024]; int found=0;
    for (ZPOS64_T i=0;i<gi.number_entry;++i) {
        unz_file_info64 cur;
        if (unzGetCurrentFileInfo64(z,&cur,name,sizeof(name),NULL,0,NULL,0)!=UNZ_OK) break;
        name[cur.size_filename]=0;
        if (member_is_gba(name)) {found=1;break;}
        if(i+1<gi.number_entry) unzGoToNextFile(z);
    }
    int ok=found && unzOpenCurrentFile(z)==UNZ_OK;
    unsigned char scratch[32768]; unsigned long long total=0;
    if (progress && !progress(0, (unsigned)fi.uncompressed_size)) ok=0;
    while(ok) {
        int n=unzReadCurrentFile(z,scratch,sizeof(scratch));
        if(n<0) {ok=0;break;}
        if(n==0) break;
        total+=(unsigned)n;
        if(total>GBA_CART_MAX_BYTES || fwrite(scratch,1,(size_t)n,out)!=(size_t)n) {ok=0;break;}
        if (progress && (total % (256*1024) == 0 || total == fi.uncompressed_size) && !progress((unsigned)total, (unsigned)fi.uncompressed_size)) {ok=0;break;}
    }
    if(found && unzCloseCurrentFile(z)!=UNZ_OK) ok=0; /* minizip reports CRC failures here */
    if(total!=fi.uncompressed_size) ok=0;
    if(fclose(out)) ok=0;
    unzClose(z);
    struct stat after;
    if(stat(path,&after) || after.st_size!=source.st_size || after.st_mtime!=source.st_mtime)ok=0;
    if(!ok) {remove(part);return GBA_CART_CORRUPT;}
    unsigned char h[192];
    FILE *check=fopen(part,"rb");
    int valid=check && fread(h,1,sizeof(h),check)==sizeof(h) && valid_header(h);
    if(check)fclose(check);
    if(!valid) {remove(part);return GBA_CART_INVALID;}
    if(rename(part,cached_rom)){remove(part);return GBA_CART_IO_ERROR;}
    FILE *m=fopen(meta,"wb");
    int written=m && fwrite(identity,1,strlen(identity),m)==strlen(identity);
    if(m && fclose(m))written=0;
    if(!written){remove(meta);remove(cached_rom);return GBA_CART_IO_ERROR;}
    *rom_path=cached_rom; return GBA_CART_OK;
}

gba_cart_result gba_cart_prepare(const char *path, const char **rom_path)
{
    return gba_cart_prepare_progress(path, rom_path, NULL);
}

bool gba_cart_too_large(const char *path)
{
    if (!path) return false;
    if (suffix_eq(path, ".gba")) {
        struct stat st;
        return !stat(path,&st) && (unsigned long long)st.st_size > GBA_CART_MAX_BYTES;
    }
    if (!suffix_eq(path, ".zip")) return false;
    unzFile z=unzOpen64(path); unz_file_info64 fi; int has=0;
    if (!z) return false;
    bool big=zip_find(z,&has,&fi)==GBA_CART_OK && has && fi.uncompressed_size>GBA_CART_MAX_BYTES;
    unzClose(z); return big;
}

/* The launch error card shows the line before the newline as its title and wraps the rest. */
#define CART_ERROR_TITLE "Could not start the game\n"
const char *gba_cart_error(gba_cart_result r)
{
    switch(r) {
    case GBA_CART_MULTI_ROM: return CART_ERROR_TITLE "This ZIP contains multiple GBA games. Choose a ZIP with one .gba file.";
    case GBA_CART_TOO_LARGE: return CART_ERROR_TITLE "This GBA game is larger than the 32 MiB limit.";
    case GBA_CART_UNSUPPORTED: return CART_ERROR_TITLE "Encrypted ZIPs and .7z archives are not supported.";
    case GBA_CART_IO_ERROR: return CART_ERROR_TITLE "Could not read the GBA game or write its cache file. Check the SD card.";
    case GBA_CART_CORRUPT: return CART_ERROR_TITLE "This ZIP is corrupt or could not be extracted completely.";
    case GBA_CART_INVALID: return CART_ERROR_TITLE "This file does not contain a valid GBA game.";
    default: return CART_ERROR_TITLE "Could not prepare the GBA game.";
    }
}

/* Existing full CRC save keys remain unchanged. Exceptional legacy adoption
 * populates a durable cache; ordinary launch validates bounded content samples.
 * No filename or FAT mtime participates: renames/raw copies keep ownership.
 * Like CRC32 itself, sampled freshness is non-adversarial, not authentication. */
#define FP_DIR "sdmc:/3ds/BreadBox/cache/gba-id"
#define FP_BLOCK 4096u
#define FP_SAMPLES 5u
static void fp32(unsigned char *p, uint32_t v)
{ for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i)); }
static uint32_t fp_read32(const unsigned char *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static bool fp_sample(FILE *f, size_t size, unsigned char key[32])
{
    unsigned char buf[FP_BLOCK];
    memcpy(key,"BBGBAID1",8);fp32(key+8,(uint32_t)size);
    size_t bytes=size<FP_BLOCK?size:FP_BLOCK;
    for(unsigned i=0;i<FP_SAMPLES;i++) {
        size_t offset=(size-bytes)*i/(FP_SAMPLES-1);
        if(fseek(f,(long)offset,SEEK_SET) || fread(buf,1,bytes,f)!=bytes)return false;
        fp32(key+12+4*i,(uint32_t)crc32(0,buf,bytes));
    }
    return true;
}
static void fp_path(const unsigned char key[32], char path[160])
{
    uint64_t hash=UINT64_C(14695981039346656037);
    for(unsigned i=0;i<32;i++)hash=(hash^key[i])*UINT64_C(1099511628211);
    snprintf(path,160,FP_DIR "/%016llx.id",(unsigned long long)hash);
}
static bool fingerprint(const char *path, unsigned char h[192], uint32_t *crc, bool populate, bool *legacy, uint32_t *sample)
{
    if(legacy)*legacy=false;
    if(!gba_cart_header(path,h))return false;
    if(suffix_eq(path,".zip")) {
        const char *raw=NULL;
        if(gba_cart_prepare(path,&raw)!=GBA_CART_OK)return false;
        return fingerprint(raw,h,crc,populate,legacy,sample);
    }
    struct stat before,after;
    if(stat(path,&before) || before.st_size<192 || before.st_size>GBA_CART_MAX_BYTES)return false;
    FILE *f=fopen(path,"rb");if(!f)return false;
    unsigned char key[32],record[40];char cache[160];bool ok=false;
    if(!fp_sample(f,(size_t)before.st_size,key))goto done;
    if(sample)*sample=(uint32_t)crc32(0,key,sizeof(key));
    fp_path(key,cache);
    FILE *cached=fopen(cache,"rb");
    if(cached) {
        ok=fread(record,1,sizeof(record),cached)==sizeof(record) && fgetc(cached)==EOF &&
            !memcmp(key,record,32) && fp_read32(record+36)==(uint32_t)crc32(0,record,36);
        fclose(cached);
        if(ok){*crc=fp_read32(record+32);if(legacy)*legacy=true;goto done;}
    }
    if(!populate) {
        /* Versioned sample-derived key: path-independent, bounded launch cost.
         * Full-CRC records from earlier builds still take precedence above. */
        *crc=(uint32_t)crc32(0,key,sizeof(key));
        ok=true;goto done;
    }
    /* Large reads amortize the SD service round trip. The former 4 KiB reads
     * issued 8192 synchronous requests per 32 MiB cart. Launch heap, not stack. */
    unsigned char *buf=malloc(256*1024);if(!buf)goto done;
    uLong hash=crc32(0,NULL,0);size_t total=0,n;
    ok=fseek(f,0,SEEK_SET)==0;
    while(ok && (n=fread(buf,1,256*1024,f))) {
        hash=crc32(hash,buf,n);total+=n;
        if(total>GBA_CART_MAX_BYTES){ok=false;break;}
    }
    free(buf);
    ok=ok && !ferror(f) && total==(size_t)before.st_size;
    if(!ok)goto done;
    /* Recheck samples before publishing; don't cache an interrupted replacement. */
    unsigned char verified[32];
    if(!fp_sample(f,total,verified) || memcmp(key,verified,32)){ok=false;goto done;}
    if(stat(path,&after) || before.st_size!=after.st_size || before.st_mtime!=after.st_mtime){ok=false;goto done;}
    *crc=(uint32_t)hash;if(legacy)*legacy=true;
    const char *dirs[]={"sdmc:/3ds","sdmc:/3ds/BreadBox","sdmc:/3ds/BreadBox/cache",FP_DIR};
    for(unsigned i=0;i<sizeof(dirs)/sizeof(*dirs);i++)
        if(mkdir(dirs[i],0777) && errno!=EEXIST){ok=false;goto done;}
    memcpy(record,key,32);fp32(record+32,*crc);fp32(record+36,(uint32_t)crc32(0,record,36));
    char part[176];snprintf(part,sizeof(part),"%s.part",cache);
    FILE *out=fopen(part,"wb");
    ok=out && fwrite(record,1,sizeof(record),out)==sizeof(record);
    if(out && fclose(out))ok=false;
    if(ok) { /* FAT cannot rename over an existing damaged entry. */
        remove(cache);ok=rename(part,cache)==0;
    }
    if(!ok)remove(part);
done:
    fclose(f);
    if(stat(path,&after) || before.st_size!=after.st_size || before.st_mtime!=after.st_mtime)ok=false;
    return ok;
}
bool gba_cart_fingerprint(const char *path, unsigned char h[192], uint32_t *crc)
{ return fingerprint(path,h,crc,false,NULL,NULL); }
bool gba_cart_index_fingerprint(const char *path, unsigned char h[192], uint32_t *crc)
{ return fingerprint(path,h,crc,true,NULL,NULL); }

bool gba_cart_identity(const char *path, unsigned char h[192],uint32_t *crc,bool *legacy,uint32_t *sample)
{ return fingerprint(path,h,crc,false,legacy,sample); }
