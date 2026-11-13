/* J-01: a damaged cartridge save (or clock) with no good copy. The player is asked once:
 * put the newest backup back, start empty, or go back. The damaged file is never deleted. */
#define main legacy_check_main
#ifdef SAFEFILE_TEST
#define safe_test_fwrite base_write
#endif
#include "check.c"
#include "gba_cart.h"
#include <ftw.h>
#undef main
#ifdef SAFEFILE_TEST
#undef safe_test_fwrite
static const char *full_on;   /* this file's writes find the SD card full */
size_t safe_test_fwrite(const void *p,size_t z,size_t n,FILE *f)
{
 char link[64],name[512];snprintf(link,sizeof(link),"/proc/self/fd/%d",fileno(f));
 ssize_t got=full_on?readlink(link,name,sizeof(name)-1):-1;
 if(got>0){name[got]=0;size_t k=strlen(full_on);if((size_t)got>=k && !strcmp(name+got-k,full_on))return 0;}
 return fwrite(p,z,n,f);
}
#endif
static int choice,calls;static bb_save_damage seen;
static int ask(const bb_save_damage *d){calls++;seen=*d;return choice;}
static void put(const char *path,const void *p,size_t n){FILE *f=fopen(path,"wb");assert(f);if(n)assert(fwrite(p,1,n,f)==n);fclose(f);}
static long bytes(const char *path){struct stat st;return stat(path,&st)?-1:(long)st.st_size;}
static bool same(const char *path,const void *p,size_t n)
{
 FILE *f=fopen(path,"rb");if(!f)return false;unsigned char b[512];size_t got=fread(b,1,sizeof(b),f);fclose(f);
 return got==n && !memcmp(b,p,n);
}
static void enter(const char *dir){assert(!mkdir(dir,0700));assert(!chdir(dir));history_set_root("history");calls=0;}
static void leave(void){assert(!chdir(".."));}
static void damaged(const char *dir,const unsigned char *bad,size_t n)
{
 char e[256];
 /* Back: nothing changes on the SD card, no error text, the game does not start */
 char sub[64];snprintf(sub,sizeof(sub),"%s-back",dir);enter(sub);put("battery.sav",bad,n);
 bb_save_on_damage(ask);choice=BB_DAMAGE_BACK;
 assert(!bb_save_begin(&config,e,sizeof(e)) && !e[0] && calls==1 && !seen.backup && !seen.clock);
 assert(same("battery.sav",bad,n) && bytes("battery.sav.bad")<0);leave();
 /* Empty: the damaged file is kept as .bad, the game starts on blank RAM, its next save is written */
 snprintf(sub,sizeof(sub),"%s-empty",dir);enter(sub);put("battery.sav",bad,n);
 memset(ram,0xff,128);choice=BB_DAMAGE_EMPTY;
 assert(bb_save_begin(&config,e,sizeof(e)) && calls==1);
 assert(same("battery.sav.bad",bad,n) && bytes("battery.sav")<0);
 for(int i=0;i<128;i++)assert(ram[i]==0xff);
 memset(ram,0x5a,128);assert(bb_save_flush());assert(bb_save_end(false));disk(0x5a);
 assert(same("battery.sav.bad",bad,n));
 /* the next start: no question, the new save loads */
 memset(ram,0,128);calls=0;begin();assert(!calls && ram[0]==0x5a);assert(bb_save_end(false));
 assert(same("battery.sav.bad",bad,n));leave();
}
/* A smaller save that may be from another emulator, at real sizes: a GBA-sized 128 KiB battery and
 * the real chip rule (gba_cart_save_fits) with the save type gpSP would know before the game runs.
 * It is never converted on its own (merge review B1): always the damaged-save question, with "Use it"
 * (foreign) only when the size fits; Back changes no byte. "Use it" puts the original into Backups
 * byte for byte, then the game gets it padded with 0xFF. */
#define BIG (128u*1024u)
static unsigned char big[BIG];
static uint32_t chip=GBA_SAVE_UNKNOWN,banks=GBA_FLASH_64K;
static bool big_battery(void **p,size_t *n){*p=big;*n=BIG;return true;}
static bool fits(size_t n){return gba_cart_save_fits(chip,banks,n);}
static void use_sizes(bool on)
{
 static bb_save_ops sized;static bb_system_t sys;
 sized=ops;sized.battery=big_battery;sized.battery_native=fits;sized.battery_fill=0xff;sized.rtc_size=NULL;
 sys=test_system;sys.save=&sized;
 config.system=on?&sys:&test_system;chip=GBA_SAVE_UNKNOWN;banks=GBA_FLASH_64K;
}
static bool same_big(const char *path,const void *p,size_t n)
{
 FILE *f=fopen(path,"rb");if(!f)return false;unsigned char *b=malloc(n+1);assert(b);
 size_t got=fread(b,1,n+1,f);fclose(f);bool ok=got==n && !memcmp(b,p,n);free(b);return ok;
}
/* every file under the current folder, name and bytes: a Back must leave it exactly as it was */
static char *tree_text;static size_t tree_len;
static int tree_add(const char *path,const struct stat *st,int type,struct FTW *ftw)
{
 (void)ftw;if(type!=FTW_F)return 0;
 FILE *f=fopen(path,"rb");assert(f);unsigned char *b=malloc((size_t)st->st_size+1);assert(b);
 size_t n=fread(b,1,(size_t)st->st_size+1,f);fclose(f);assert(n==(size_t)st->st_size);
 char line[600];int k=snprintf(line,sizeof(line),"%s %zu %08lx\n",path,n,(unsigned long)crc32(0,b,(uInt)n));free(b);
 tree_text=realloc(tree_text,tree_len+(size_t)k+1);assert(tree_text);memcpy(tree_text+tree_len,line,(size_t)k+1);tree_len+=(size_t)k;
 return 0;
}
static int line_cmp(const void *a,const void *b){return strcmp(*(char *const *)a,*(char *const *)b);}
static char *tree(void)
{
 tree_text=NULL;tree_len=0;assert(!nftw(".",tree_add,16,FTW_PHYS));
 if(!tree_text)return strdup("");
 char *lines[256];int n=0;for(char *t=strtok(tree_text,"\n");t;t=strtok(NULL,"\n")){assert(n<256);lines[n++]=t;}
 qsort(lines,(size_t)n,sizeof(*lines),line_cmp);
 size_t len=0;for(int i=0;i<n;i++)len+=strlen(lines[i])+1;
 char *out=malloc(len+1);assert(out);out[0]=0;for(int i=0;i<n;i++){strcat(out,lines[i]);strcat(out,"\n");}
 free(tree_text);return out;
}
static const size_t chip_sizes[]={512,8192,32768,65536};
/* B1: asked (Use it offered when `offered`), Back changes nothing, the game does not start */
static void asked_back(bool offered)
{
 char e[256],*before=tree();choice=BB_DAMAGE_BACK;calls=0;memset(big,0xa5,BIG);
 assert(!bb_save_begin(&config,e,sizeof(e)) && !e[0] && calls==1 && seen.foreign==offered && !seen.clock);
 char *after=tree();assert(!strcmp(before,after));free(before);free(after);
 for(size_t i=0;i<BIG;i++)assert(big[i]==0xa5);
 /* "Use it" answered where it was not offered is a Back */
 if(!offered){
  before=tree();choice=BB_DAMAGE_USE;calls=0;
  assert(!bb_save_begin(&config,e,sizeof(e)) && !e[0] && calls==1);
  after=tree();assert(!strcmp(before,after));free(before);free(after);
 }
}
/* "Use it": converted, the original byte for byte in Backups, the next start loads it unasked */
static void use_it(size_t n,const unsigned char *orig,int backups_before)
{
 char e[256];hist_entry_t list[8];
 choice=BB_DAMAGE_USE;calls=0;memset(big,0,BIG);
 assert(bb_save_begin(&config,e,sizeof(e)) && calls==1 && seen.foreign);
 assert(!memcmp(big,orig,n));for(size_t i=n;i<BIG;i++)assert(big[i]==0xff);
 assert(bytes("battery.sav")==(long)BIG && same_big("battery.sav.bad",orig,n) && bytes("battery.sav.tmp")<0);
 int count=history_list("test-test-unique",false,list,8),originals=0;assert(count==backups_before+1);
 for(int i=0;i<count;i++)originals+=list[i].size==(long)n && same_big(list[i].path,orig,n);
 assert(originals==1);
 assert(bb_save_end(false));
 memset(big,0,BIG);calls=0;begin();assert(!calls && !memcmp(big,orig,n) && big[n]==0xff);assert(bb_save_end(false));
}
static void smaller(void)
{
 unsigned char *orig=malloc(BIG),*good=malloc(BIG);assert(orig && good);
 for(size_t i=0;i<BIG;i++)orig[i]=(unsigned char)(i*5+3);
 memset(good,0x77,BIG);char dir[64],e[256];hist_entry_t list[8];
 /* review probe 1: BreadBox's own first 128 KiB write (no backup yet), cut to 512 B, EEPROM cartridge */
 enter("first-write-cut");use_sizes(true);chip=GBA_SAVE_EEPROM;put("game.id","test-test-unique\n",17);
 begin();memcpy(big,good,BIG);assert(bb_save_flush() && bb_save_end(false));
 assert(!history_list("test-test-unique",false,list,8) && !truncate("battery.sav",512));
 asked_back(true);use_sizes(false);leave();
 /* review probe 2: two writes, the player deletes the backups; owner marker absent, a legacy claim, damaged */
 for(int marker=0;marker<3;marker++){
  snprintf(dir,sizeof(dir),"backups-deleted-%d",marker);enter(dir);use_sizes(true);chip=GBA_SAVE_EEPROM;
  if(marker)put("game.id",marker==1?"test-test-unique\n":"damaged",marker==1?17:7);
  begin();memcpy(big,good,BIG);assert(bb_save_flush());big[0]=0x88;assert(bb_save_flush());assert(bb_save_end(false));
  int count=history_list("test-test-unique",false,list,8);assert(count>0);
  for(int i=0;i<count;i++)assert(!unlink(list[i].path));
  assert(!unlink("battery.sav.bak") && !history_list("test-test-unique",false,list,8) && !truncate("battery.sav",512));
  asked_back(true);use_sizes(false);leave();
 }
 for(int k=0;k<4;k++){
  size_t n=chip_sizes[k];
  /* review probe 3: an unknown chip, each size: asked (Use it offered), Back changes nothing */
  snprintf(dir,sizeof(dir),"unknown%zu",n);enter(dir);use_sizes(true);put("battery.sav",orig,n);
  asked_back(true);
  /* ... Use it: converted, original in Backups; cut again later it is asked about (with that backup
   * of the original, not offered as Restore: it is not a BreadBox save) */
  use_it(n,orig,0);
  assert(!truncate("battery.sav",(off_t)n));asked_back(true);assert(!seen.backup);use_sizes(false);leave();
  /* a BreadBox 128 KiB save cut to this size with an intact backup: asked with it; Restore puts it back */
  snprintf(dir,sizeof(dir),"cut%zu",n);enter(dir);use_sizes(true);
  history_fake_now=1700000000;assert(history_add("test-test-unique",HIST_SAVE,".sav",good,BIG));history_fake_now=0;
  put("battery.sav",good,n);asked_back(true);assert(seen.backup);
  choice=BB_DAMAGE_RESTORE;memset(big,0,BIG);calls=0;
  assert(bb_save_begin(&config,e,sizeof(e)) && calls==1 && seen.backup && seen.foreign);
  assert(big[0]==0x77 && big[n]==0x77 && big[BIG-1]==0x77 && same_big("battery.sav",good,BIG) && bytes("battery.sav.bad")==(long)n);
  assert(history_list("test-test-unique",false,list,8)==1);
  assert(bb_save_end(false));use_sizes(false);leave();
  /* ... or Start empty: as before, the file kept as .bad, blank RAM */
  snprintf(dir,sizeof(dir),"empty%zu",n);enter(dir);use_sizes(true);put("battery.sav",orig,n);
  choice=BB_DAMAGE_EMPTY;memset(big,0xff,BIG);
  assert(bb_save_begin(&config,e,sizeof(e)) && calls==1 && seen.foreign && same_big("battery.sav.bad",orig,n) && bytes("battery.sav")<0);
  for(size_t i=0;i<BIG;i++)assert(big[i]==0xff);
  assert(!history_list("test-test-unique",false,list,8));assert(bb_save_end(false));use_sizes(false);leave();
  /* ... Use it with BreadBox backups there: they stay, the original joins them */
  snprintf(dir,sizeof(dir),"use-with-backup%zu",n);enter(dir);use_sizes(true);
  history_fake_now=1700000000;assert(history_add("test-test-unique",HIST_SAVE,".sav",good,BIG));history_fake_now=0;
  put("battery.sav",orig,n);use_it(n,orig,1);use_sizes(false);leave();
 }
 /* a size that contradicts the type gpSP knows: no Use it; a fitting one: offered, and used */
 static const struct {uint32_t chip,banks;size_t n;bool in;} known[]={
  {GBA_SAVE_EEPROM,GBA_FLASH_64K,512,true},{GBA_SAVE_EEPROM,GBA_FLASH_64K,8192,true},
  {GBA_SAVE_EEPROM,GBA_FLASH_64K,32768,false},{GBA_SAVE_EEPROM,GBA_FLASH_64K,65536,false},   /* EEPROM cartridge, other chip's size */
  {GBA_SAVE_SRAM,GBA_FLASH_64K,512,false},{GBA_SAVE_SRAM,GBA_FLASH_64K,8192,false},          /* EEPROM size, SRAM cartridge */
  {GBA_SAVE_SRAM,GBA_FLASH_64K,65536,false},
  {GBA_SAVE_FLASH,GBA_FLASH_64K,512,false},{GBA_SAVE_FLASH,GBA_FLASH_64K,8192,false},        /* ... flash cartridge */
  {GBA_SAVE_FLASH,GBA_FLASH_64K,32768,false},
  {GBA_SAVE_SRAM,GBA_FLASH_64K,32768,true},{GBA_SAVE_FLASH,GBA_FLASH_64K,65536,true},
  {GBA_SAVE_FLASH,GBA_FLASH_128K,65536,false},{GBA_SAVE_UNKNOWN,GBA_FLASH_128K,32768,false},
 };
 for(size_t k=0;k<sizeof(known)/sizeof(known[0]);k++){
  snprintf(dir,sizeof(dir),"known%zu",k);enter(dir);use_sizes(true);chip=known[k].chip;banks=known[k].banks;
  put("battery.sav",orig,known[k].n);asked_back(known[k].in);
  if(known[k].in)use_it(known[k].n,orig,0);
  use_sizes(false);leave();
 }
 /* any other size is damaged (no Use it); a system without the rule asks without it too */
 enter("small33");use_sizes(true);put("battery.sav",orig,33);asked_back(false);use_sizes(false);leave();
 enter("unlisted");put("battery.sav",orig,32);asked_back(false);leave();
 free(orig);free(good);
}
#ifdef SAFEFILE_TEST
/* Review finding 2: a power cut at every step of Restore, and a full SD card: the next start asks again or has the restored save, never blank RAM. */
static void restore_cuts(void)
{
 char e[256];unsigned char seven[7]={1,2,3,4,5,6,7},good[128],junk[20]={9};memset(good,0x77,128);
 for(int cut=0;;cut++){
  char dir[32];snprintf(dir,sizeof(dir),"restore-cut%d",cut);enter(dir);
  history_fake_now=1700000000;assert(history_add("test-test-unique",HIST_SAVE,".sav",good,128));history_fake_now=0;
  put("battery.sav",seven,7);put("battery.sav.bak",junk,20);put("battery.sav.tmp",junk,5);put("battery.sav.bad",junk,10);
  choice=BB_DAMAGE_RESTORE;memset(ram,0,128);
  safe_test_crash_after=cut;bool ok=bb_save_begin(&config,e,sizeof(e));bool cut_off=safe_test_crash_after==-2;safe_test_crash_after=-1;
  if(!cut_off){assert(ok && ram[0]==0x77);disk(0x77);assert(bb_save_end(false) && bytes("battery.sav.bad")==10);leave();assert(cut>3);break;}
  assert(!ok && e[0]);
  calls=0;choice=BB_DAMAGE_BACK;memset(ram,0,128);   /* relaunch */
  ok=bb_save_begin(&config,e,sizeof(e));
  assert(ok?(!calls && ram[0]==0x77 && ram[127]==0x77):(calls==1 && bytes("battery.sav")==7));
  if(ok)assert(bb_save_end(false));
  assert(bytes("battery.sav.bad")==10);leave();assert(cut<20);
 }
 /* SD full while the copy is written (after the leftovers were set aside): not "put back",
  * no start; the damaged save stays and is asked about again */
 enter("restore-full");
 history_fake_now=1700000000;assert(history_add("test-test-unique",HIST_SAVE,".sav",good,128));history_fake_now=0;
 put("battery.sav",seven,7);put("battery.sav.bak",junk,20);choice=BB_DAMAGE_RESTORE;memset(ram,0,128);full_on="battery.sav.tmp";
 assert(!bb_save_begin(&config,e,sizeof(e)) && strstr(e,"could not be put back") && ram[0]==0);full_on=NULL;
 assert(bytes("battery.sav")==7 && bytes("battery.sav.bak.bad")==20 && bytes("battery.sav.tmp")<0);
 calls=0;choice=BB_DAMAGE_BACK;assert(!bb_save_begin(&config,e,sizeof(e)) && calls==1);leave();
}
#endif
int main(int argc,char **argv)
{
 assert(argc==2);assert(!chdir(argv[1]));
 unsigned char seven[7]={1,2,3,4,5,6,7},garbage[300];
 for(unsigned i=0;i<sizeof(garbage);i++)garbage[i]=(unsigned char)(i*37+11);
 damaged("truncated",seven,sizeof(seven));
 damaged("zero",seven,0);
 damaged("garbage",garbage,sizeof(garbage));
 char e[256];
 /* No question to ask (other callers): refused as before, nothing moved */
 enter("noask");put("battery.sav",seven,7);bb_save_on_damage(NULL);
 assert(!bb_save_begin(&config,e,sizeof(e)) && strstr(e,"damaged"));
 assert(same("battery.sav",seven,7) && bytes("battery.sav.bad")<0);leave();
 bb_save_on_damage(ask);
 /* A good copy in Backups: offered with its time, put back; the backup itself stays */
 enter("backup");
 unsigned char good[128];memset(good,0x77,128);
 history_fake_now=1700000000;assert(history_add("test-test-unique",HIST_SAVE,".sav",good,128));history_fake_now=0;
 put("battery.sav",seven,7);choice=BB_DAMAGE_RESTORE;memset(ram,0,128);
 assert(bb_save_begin(&config,e,sizeof(e)) && calls==1 && seen.backup && seen.when==1700000000);
 assert(ram[0]==0x77 && ram[127]==0x77);disk(0x77);assert(same("battery.sav.bad",seven,7));
 hist_entry_t list[8];int n=history_list("test-test-unique",false,list,8);assert(n==1 && same(list[0].path,good,128));
 assert(bb_save_end(false));leave();
 /* Backup offered but the player starts empty: still never lost */
 enter("backup-empty");
 history_fake_now=1700000000;assert(history_add("test-test-unique",HIST_SAVE,".sav",good,128));history_fake_now=0;
 put("battery.sav",seven,7);choice=BB_DAMAGE_EMPTY;memset(ram,0xff,128);
 assert(bb_save_begin(&config,e,sizeof(e)) && seen.backup && ram[0]==0xff);
 assert(same("battery.sav.bad",seven,7) && history_list("test-test-unique",false,list,8)==1);
 assert(bb_save_end(false));leave();
 /* An older .bad is never replaced: the next one is .bad2; a damaged .bak goes aside too */
 enter("second");put("battery.sav.bad",garbage,10);put("battery.sav",seven,7);put("battery.sav.bak",garbage,20);
 choice=BB_DAMAGE_EMPTY;assert(bb_save_begin(&config,e,sizeof(e)));
 assert(same("battery.sav.bad",garbage,10) && same("battery.sav.bad2",seven,7) && same("battery.sav.bak.bad",garbage,20));
 assert(bytes("battery.sav")<0 && bytes("battery.sav.bak")<0);
 assert(bb_save_end(false));leave();
 /* A damaged clock file: asked as the clock, kept as .bad, the game starts */
 enter("clock");put("battery.rtc",garbage,9);choice=BB_DAMAGE_EMPTY;
 assert(bb_save_begin(&config,e,sizeof(e)) && calls==1 && seen.clock && same("battery.rtc.bad",garbage,9));
 assert(bb_save_end(false));leave();
 /* Review finding 1: restoring again and again, with .bad and .bad2 already there, never
  * removes one: each damaged file gets the next free name */
 enter("repeat");put("battery.sav.bad",garbage,10);put("battery.sav.bad2",garbage,11);
 history_fake_now=1700000000;assert(history_add("test-test-unique",HIST_SAVE,".sav",good,128));history_fake_now=0;
 for(int round=0;round<3;round++){
  put("battery.sav",seven,(size_t)3+round);choice=BB_DAMAGE_RESTORE;calls=0;memset(ram,0,128);
  assert(bb_save_begin(&config,e,sizeof(e)) && calls==1 && ram[5]==0x77);disk(0x77);assert(bb_save_end(false));
  char bad[32];snprintf(bad,sizeof(bad),"battery.sav.bad%d",3+round);assert(bytes(bad)==3+round);
 }
 put("battery.sav",seven,7);choice=BB_DAMAGE_EMPTY;assert(bb_save_begin(&config,e,sizeof(e)));
 assert(bytes("battery.sav.bad6")==7 && bytes("battery.sav")<0);assert(bb_save_end(false));
 assert(bytes("battery.sav.bad")==10 && bytes("battery.sav.bad2")==11 && bytes("battery.sav.bad3")==3);
 leave();
 smaller();
#ifdef SAFEFILE_TEST
 restore_cuts();
#endif
 puts("damaged saves: ok");
 return 0;
}
