/* "Use it" for a save that may be from another emulator, killed at every file step (the
 * process ends there, like a power cut) for 512 B, 8 KiB, 32 KiB and 64 KiB. Every step of the battery file
 * and of the Backups folder is a cut point. After each cut:
 *  - the byte-exact original is at the battery path or in Backups;
 *  - once a padded copy exists anywhere (the battery or its .tmp), the original is already in Backups;
 *  - the next start either asks again with the original untouched (Back changes nothing; Use it then works)
 *    or loads the padded save unasked with the original in Backups; never blank, never a .bad-only original. */
#define main legacy_check_main
#include "check.c"
#undef main
#include "gba_cart.h"
#include <errno.h>
#include <ftw.h>
#include <sys/wait.h>
#define BIG (128u*1024u)
static bool armed;static int at,cut_at,questions,answer_now;
static void step(void){if(armed && at++==cut_at)_exit(42);}
static bool watched(const char *p){return strstr(p,"battery.sav") || strstr(p,"history");}
static bool stream(FILE *f)
{
 char l[64],p[1024];snprintf(l,sizeof(l),"/proc/self/fd/%d",fileno(f));
 ssize_t n=readlink(l,p,sizeof(p)-1);if(n<0)return false;p[n]=0;return watched(p);
}
FILE *__real_fopen(const char *,const char *);
FILE *__wrap_fopen(const char *p,const char *m){bool w=watched(p);if(w)step();FILE *f=__real_fopen(p,m);if(w)step();return f;}
size_t __real_fwrite(const void *,size_t,size_t,FILE *);
size_t __wrap_fwrite(const void *p,size_t z,size_t n,FILE *f){bool w=stream(f);if(w)step();size_t r=__real_fwrite(p,z,n,f);if(w)step();return r;}
int __real_fclose(FILE *);
int __wrap_fclose(FILE *f){bool w=stream(f);if(w)step();int r=__real_fclose(f);if(w)step();return r;}
int __real_rename(const char *,const char *);
int __wrap_rename(const char *a,const char *b)   /* the 3DS SD card never renames over a file */
{
 bool w=watched(a) || watched(b);if(w)step();struct stat s;int r;
 if(!stat(b,&s)){errno=EEXIST;r=-1;}else r=__real_rename(a,b);
 if(w)step();return r;
}
int __real_remove(const char *);
int __wrap_remove(const char *p){bool w=watched(p);if(w)step();int r=__real_remove(p);if(w)step();return r;}
static int ask(const bb_save_damage *d){questions++;assert(d->foreign && !d->backup);return answer_now;}
static void put(const char *p,const void *b,size_t n){FILE *f=fopen(p,"wb");assert(f && fwrite(b,1,n,f)==n && !fclose(f));}
static unsigned char full_ram[BIG],original[65536],padded[BIG];
static bool full_battery(void **p,size_t *n){*p=full_ram;*n=sizeof(full_ram);return true;}
static bool native(size_t n){return gba_cart_save_fits(GBA_SAVE_UNKNOWN,GBA_FLASH_64K,n);}
static bool exact(const char *p,const void *want,size_t n)
{
 FILE *f=fopen(p,"rb");if(!f)return false;unsigned char *b=malloc(n+1);assert(b);
 size_t got=fread(b,1,n+1,f);fclose(f);bool ok=got==n && !memcmp(b,want,n);free(b);return ok;
}
static int in_backups(size_t n)
{
 hist_entry_t list[16];int count=history_list("test-test-unique",false,list,16),found=0;
 for(int i=0;i<count;i++)found+=exact(list[i].path,original,n);
 return found;
}
static char *listing;static size_t listing_len;
static int list_add(const char *path,const struct stat *st,int type,struct FTW *ftw)
{
 (void)ftw;if(type!=FTW_F)return 0;
 FILE *f=fopen(path,"rb");assert(f);unsigned char *b=malloc((size_t)st->st_size+1);assert(b);
 size_t n=fread(b,1,(size_t)st->st_size+1,f);fclose(f);
 char line[600];int k=snprintf(line,sizeof(line),"%s %zu %08lx\n",path,n,(unsigned long)crc32(0,b,(uInt)n));free(b);
 listing=realloc(listing,listing_len+(size_t)k+1);assert(listing);memcpy(listing+listing_len,line,(size_t)k+1);listing_len+=(size_t)k;
 return 0;
}
static char *tree(void){listing=NULL;listing_len=0;assert(!nftw(".",list_add,16,FTW_PHYS));return listing?listing:strdup("");}
int main(int argc,char **argv)
{
 assert(argc==2 && !chdir(argv[1]));
 static bb_save_ops sized;static bb_system_t sys;
 sized=ops;sized.battery=full_battery;sized.battery_native=native;sized.battery_fill=0xff;sized.rtc_size=NULL;
 sys=test_system;sys.save=&sized;config.system=&sys;bb_save_on_damage(ask);
 for(size_t i=0;i<sizeof(original);i++)original[i]=(unsigned char)(i*7+3);
 static const size_t sizes[]={512,8192,32768,65536};
 for(unsigned z=0;z<4;z++){
  size_t n=sizes[z];int k;
  memcpy(padded,original,n);memset(padded+n,0xff,BIG-n);
  for(k=0;k<400;k++){
   char dir[64],e[256];snprintf(dir,sizeof(dir),"size%zu-cut%d",n,k);
   assert(!mkdir(dir,0700) && !chdir(dir));history_set_root("history");put("battery.sav",original,n);
   pid_t pid=fork();assert(pid>=0);
   if(!pid){armed=true;cut_at=k;at=0;answer_now=BB_DAMAGE_USE;bool ok=bb_save_begin(&config,e,sizeof(e));_exit(ok?0:1);}
   int status;assert(waitpid(pid,&status,0)==pid && WIFEXITED(status));int rc=WEXITSTATUS(status);
   assert(rc==42 || rc==0);
   /* right after the cut, before anything recovers */
   bool padded_somewhere=exact("battery.sav",padded,BIG) || exact("battery.sav.tmp",padded,BIG);
   assert(exact("battery.sav",original,n) || in_backups(n));
   assert(!padded_somewhere || in_backups(n));
   /* the next start: Back first */
   questions=0;answer_now=BB_DAMAGE_BACK;memset(full_ram,0,sizeof(full_ram));
   char *before=tree();bool loaded=bb_save_begin(&config,e,sizeof(e));char *after=tree();
   if(!loaded){
    assert(questions==1 && !e[0] && exact("battery.sav",original,n) && !strcmp(before,after));
    questions=0;answer_now=BB_DAMAGE_USE;
    assert(bb_save_begin(&config,e,sizeof(e)) && questions==1);
   }else assert(!questions);
   free(before);free(after);
   assert(!memcmp(full_ram,padded,BIG) && exact("battery.sav",padded,BIG) && in_backups(n)==1);
   assert(bb_save_end(false));assert(!chdir(".."));
   if(!rc)break;
  }
  assert(k>=30 && k<400);
  printf("Use it, %zu B: %d cut points: original in Backups before any padded copy, next start asks or loads it: PASS\n",n,k);
 }
 return 0;
}
