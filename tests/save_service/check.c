#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <zlib.h>
#include "bb_save.h"
bool breadbox_import_busy(void){return false;}
#ifdef SLOT_SCAN_TEST   /* run.sh links check.c alone with -Wl,--wrap=readdir,--wrap=stat */
#include <dirent.h>
#include <errno.h>
/* readdir that fails with EIO after a given number of entries (-1: never), for the slot scan */
static int readdir_fail_after=-1;
/* stat that fails with EIO for one name (NULL: never) */
static const char *stat_eio_name;
int __real_stat(const char *path,struct stat *st);
int __wrap_stat(const char *path,struct stat *st)
{
 if(stat_eio_name && !strcmp(path,stat_eio_name)){errno=EIO;return -1;}
 return __real_stat(path,st);
}
struct dirent *__real_readdir(DIR *d);
struct dirent *__wrap_readdir(DIR *d)
{
 if(readdir_fail_after==0){errno=EIO;return NULL;}
 if(readdir_fail_after>0)readdir_fail_after--;
 return __real_readdir(d);
}
#endif
#include "safefile.h"
#include "history.h"
static unsigned char ram_a[128],ram_b[128],*ram=ram_a;
static unsigned char position;
static uint32_t rtc[3]={10,20,30};
static bool partial,relocate,no_battery,lose_ram;
static unsigned restores;
void log_printf(const char *fmt,...){(void)fmt;}
static size_t size(void){return 16;}
static bool save(void *p,size_t n,size_t *len){assert(n==16);memset(p,position,n);*len=n;return true;}
static bb_state_result load(const void *p,size_t n,bool keep)
{
 assert(n==16 && !keep);position=*(unsigned char *)p;
 if(relocate)ram=ram==ram_a?ram_b:ram_a;
 memset(ram,0x11,128);if(position==0xee)lose_ram=true;rtc[0]=1234;return partial?BB_STATE_PARTLY:BB_STATE_OK;
}
static bool battery(void **p,size_t *n){*p=ram;*n=no_battery?0:lose_ram?64:128;return !no_battery;}
static size_t rtc_size(void){return sizeof(rtc);}
/* Like gpSP: whole-word RTC access (STM/LDM fault unaligned on the 3DS, the
 * Old 3DS Emerald Continue crash). The snapshot puts RTC at an odd offset. */
static bool aligned(const void *p){return !((uintptr_t)p&3);}
static bool rtc_get(void *p,size_t n){assert(n==sizeof(rtc) && aligned(p));uint32_t *w=p;w[0]=rtc[0];w[1]=rtc[1];w[2]=rtc[2];return true;}
static bool rtc_set(const void *p,size_t n){assert(n==sizeof(rtc) && aligned(p));const uint32_t *w=p;rtc[0]=w[0];rtc[1]=w[1];rtc[2]=w[2];return true;}
static bool rtc_valid(const void *p,size_t n){assert(n==12 && aligned(p));return ((const uint32_t *)p)[2]<=255;}
static void restored(void){restores++;}
static const bb_save_ops ops={.state_size=size,.state_save=save,.state_load=load,.battery=battery,
 .state_magic="BBGBAST1",.rtc_magic="BBGBART1",.rtc_size=rtc_size,.rtc_get=rtc_get,.rtc_set=rtc_set,.rtc_valid=rtc_valid};
static const bb_system_t test_system={.id="test",.save=&ops};
static bb_save_config config={.system=&test_system,.key="test-unique",.battery_path="battery.sav",.state_base="game",
 .identity=123,.content_bytes=512,.writer_core=1,.restored=restored};
static void begin(void){char error[256];assert(bb_save_begin(&config,error,sizeof(error)));}
static void block_tmp(bool on)   /* a non-empty directory where the write's temporary file goes */
{
 if(on){assert(!mkdir("battery.sav.tmp",0700));FILE *f=fopen("battery.sav.tmp/x","w");assert(f);fclose(f);}
 else{assert(!unlink("battery.sav.tmp/x") && !rmdir("battery.sav.tmp"));}
}
static int asks,answer;static bool closing_now,close_in_ask;
static bool closing(void){return closing_now;}
static int fake_ask(void *ctx){(void)ctx;asks++;if(close_in_ask)closing_now=true;if(answer==BB_SAVE_RETRY && asks==1)block_tmp(false);return answer;}
static void slow_job(void *arg){(void)arg;usleep(150000);FILE *f=fopen("barrier.txt","w");assert(f);fputs("done",f);fclose(f);}
static void disk(unsigned value)
{
 safe_format_t fmt={.size=128};safe_info_t info;unsigned char *p=safe_load("battery.sav",&fmt,0,&info);assert(p);
 for(unsigned i=0;i<128;i++)assert(p[i]==value);free(p);
}
static void verify_legacy(void)
{
 FILE *f=fopen("game.state0","rb");assert(f);unsigned char h[28],data[144];
 assert(fread(h,1,28,f)==28 && fread(data,1,144,f)==144 && fgetc(f)==EOF);fclose(f);
 uint32_t fields[5];memcpy(fields,h+8,20);
 assert(!memcmp(h,"BBGBAST1",8) && fields[0]==1 && fields[1]==144 && fields[2]==123 && fields[3]==512);
 assert(fields[4]==crc32(0,data,144));
 for(unsigned i=0;i<16;i++)assert(data[i]==1);
 for(unsigned i=16;i<144;i++)assert(data[i]==1);
 f=fopen("battery.rtc","rb");assert(f);unsigned char r[24];assert(fread(r,1,24,f)==24 && fgetc(f)==EOF);fclose(f);
 uint32_t crc;memcpy(&crc,r+20,4);assert(!memcmp(r,"BBGBART1",8) && crc==crc32(0,r,20));
}
#ifdef SAFEFILE_TEST
extern int safe_test_crash_after;
int safe_test_rename(const char *a,const char *b){if(!access(b,F_OK))return -1;return rename(a,b);}
size_t safe_test_fwrite(const void *p,size_t z,size_t n,FILE *f){return fwrite(p,z,n,f);}
size_t safe_test_fread(void *p,size_t z,size_t n,FILE *f){return fread(p,z,n,f);}
int safe_test_fclose(FILE *f){return fclose(f);}
int main(int argc,char **argv)
{
 assert(argc==2);assert(!chdir(argv[1]));
 for(int cut=0;cut<16;cut++){
  char dir[32];snprintf(dir,sizeof(dir),"cut%d",cut);assert(!mkdir(dir,0700));assert(!chdir(dir));history_set_root("history");
  begin();memset(ram,1,128);assert(bb_save_flush());
  safe_test_crash_after=cut;memset(ram,2,128);assert(bb_save_queue());bb_save_end(false);safe_test_crash_after=-1;bb_save_final_discard();
  safe_format_t fmt={.size=128};safe_info_t info;unsigned char *p=safe_load("battery.sav",&fmt,0,&info);assert(p);
  assert(p[0]==1 || p[0]==2);for(unsigned i=1;i<128;i++)assert(p[i]==p[0]);free(p);
  assert(!chdir(".."));
 }
 puts("Service asynchronous history + safefile: 16 injected power cuts retain exact committed old/new battery PASS");return 0;
}
#else
int main(int argc,char **argv)
{
 assert(argc==2);assert(!chdir(argv[1]));history_set_root("history");
 assert(!bb_save_queue() && !bb_save_state(0,true));begin();
 char error[80];assert(!bb_save_begin(&config,error,sizeof(error))); /* no replacement while writer lives */
 memset(ram,1,128);position=1;assert(bb_save_flush());assert(bb_save_state(0,true));verify_legacy();
 assert(bb_save_exists(0) && !bb_save_exists(BB_SLOT_QUICK));
 // Quick states are private: preserve postcard 9 and permit Undo before load.
 position=9;assert(bb_save_state(9,true));position=12;assert(bb_save_state(BB_SLOT_QUICK,true));
 position=13;assert(bb_save_state(BB_SLOT_UNDO,true));assert(bb_save_state(BB_SLOT_QUICK,false) && position==12);
 assert(bb_save_state(BB_SLOT_UNDO,false) && position==13);
 assert(bb_save_state(9,false) && position==9);position=1;
 { /* the pause card's slot scan: one pass over the folder, the same answer as a stat per slot,
    * other games' files and half-written ones not counted (GBA pause ~1 s on the Old: N9) */
  const char *decoys[]={"game.state3.tmp","gamex.state4","other.state5","game.state","game.state6x","game.state123",
   "game.state01","game.state00"};
  for(unsigned i=0;i<sizeof(decoys)/sizeof(*decoys);i++){FILE *f=fopen(decoys[i],"wb");assert(f);fclose(f);}
  uint32_t one_by_one=0;
  for(unsigned slot=0;slot<=BB_SLOT_LAST;slot++)if(bb_save_exists(slot))one_by_one|=1u<<slot;
  assert(one_by_one==(1u<<0|1u<<9|1u<<BB_SLOT_QUICK|1u<<BB_SLOT_UNDO));
  assert(bb_save_slots_present()==one_by_one);
  /* an SD read error part way through the folder: never a partial answer (it would call postcard 0
   * free and X would write over it), the stat per slot instead; at the first entry and later ones */
#ifdef SLOT_SCAN_TEST
  for(int fail=0;fail<4;fail++){readdir_fail_after=fail;assert(bb_save_slots_present()==one_by_one);}
  /* and an SD error on a slot's own file during that fallback: unsure counts as used, never as free;
   * only "no such file" makes a postcard free (the X check before a quick save asks bb_save_absent) */
  readdir_fail_after=0;stat_eio_name="game.state0";
  assert(bb_save_slots_present()&1u);assert(!bb_save_absent(0));
  stat_eio_name="game.state5";assert(!bb_save_absent(5));assert(bb_save_slots_present()&1u<<5);
  stat_eio_name=NULL;assert(bb_save_absent(5) && !bb_save_absent(0));
  readdir_fail_after=-1;
#endif
  for(unsigned i=0;i<sizeof(decoys)/sizeof(*decoys);i++)assert(!remove(decoys[i]));
 }
 assert(!bb_save_state(BB_SLOT_LAST+1,true));
 assert(bb_save_time("game.state0","BBGBAST1")>0);
 assert(bb_save_time_write("game.state0",0xdeadbeef,123));assert(!bb_save_time("game.state0","BBGBAST1"));
 memset(ram,2,128);position=2;assert(bb_save_checkpoint(true));assert(bb_save_end(false));
 memset(ram,0,128);begin();assert(bb_save_state(10,false));assert(position==2);disk(2);
 rtc[0]=9876;relocate=true;assert(bb_save_state(0,false));assert(position==1 && rtc[0]==9876);
 assert(ram[0]==2);assert(bb_save_flush());disk(2);
 unsigned before=restores;partial=true;assert(!bb_save_state(0,false));assert(restores==before && ram[0]==2 && rtc[0]==9876);partial=false;
 assert(!bb_save_end(false));begin();
 memset(ram,3,128);assert(bb_save_flush());disk(3);
 remove("battery.sav");remove("battery.sav.bak");memset(ram,4,128);assert(bb_save_flush());disk(4);
 FILE *f=fopen("blocked","wb");assert(f);fclose(f);history_set_root("blocked");
 position=5;memset(ram,5,128);assert(!bb_save_state(0,false));assert(position==5);disk(4);
 history_set_root("history");assert(bb_save_flush());disk(5);
 /* CRC-corrupt state current recovers the validated previous committed file. */
 position=6;assert(bb_save_state(1,true));position=7;assert(bb_save_state(1,true));
 f=fopen("game.state1","r+b");assert(f);assert(!fseek(f,30,SEEK_SET));fputc(0xff,f);fclose(f);
 assert(bb_save_state(1,false));assert(position==6 && ram[0]==5);
 /* Repeated suspend/sleep/exit checkpoints contain no GPU dependency. */
 for(unsigned i=0;i<3;i++)assert(bb_save_checkpoint(true));
 rtc[0]=4444;assert(bb_save_end(true));rtc[0]=0;begin();assert(rtc[0]==4444);
 assert(bb_save_end(false));assert(!bb_save_queue());
 /* A core that loses/re-sizes RAM during restore cannot poison future saves. */
 begin();position=0xee;assert(bb_save_state(2,true));
 assert(!bb_save_state(2,false));assert(!bb_save_flush());disk(5);
 assert(!bb_save_end(false));lose_ram=false;
 /* New test_system with no battery and no RTC still gets states/Continue. */
 bb_save_ops bare=ops;bare.rtc_size=NULL;bare.state_magic=NULL;bb_system_t other=test_system;other.save=&bare;
 no_battery=true;config.system=&other;config.battery_path="none.sav";config.state_base="none";begin();
 position=9;assert(bb_save_checkpoint(true));assert(bb_save_state(10,false));assert(position==9);assert(bb_save_end(true));
 /* A game's end: the Continue state made now, written after the menu is back (bb_later),
  * read back once settled; the menus see it while it is on its way (bb_save_writing). */
 position=11;begin();assert(bb_save_state_end(10,true));assert(bb_save_end(false));
 char later[64];snprintf(later,sizeof(later),"%s.state10",config.state_base);
 uint32_t when=1;(void)bb_save_writing(later,&when);bb_save_settle();
 assert(!bb_save_writing(later,NULL));
 position=0;begin();assert(bb_save_state(10,false));assert(position==11);assert(bb_save_end(false));
 /* a whole file read by that thread (its own buffer), and one taken back before it ran */
 bb_later_file rf={later,NULL,0,0,NULL};bb_later_read(&rf);assert(bb_later_wait(&rf) && rf.size>32 && rf.buf);free(rf.buf);
 bb_later_file gone={"no-such-file",NULL,0,0,NULL};bb_later_read(&gone);bb_later_cancel(&gone);bb_save_settle();
 assert(bb_later_state(&gone)==BB_LATER_FAILED && !gone.buf);
 /* round 10: HOME / sleep wait for what a game's end queued (bb_save_barrier, the APT hook's body) */
 bb_later(slow_job,NULL);assert(access("barrier.txt",F_OK));bb_save_barrier();assert(!access("barrier.txt",F_OK));
 /* The updater's bounded settle: a job still running keeps its worker (and file ownership),
  * a later call with time to spare finishes it; no job at all is trivially settled. */
 remove("barrier.txt");bb_later(slow_job,NULL);
 assert(!bb_save_settle_timeout(5000000ULL) && access("barrier.txt",F_OK));  /* 5 ms: still writing */
 assert(!bb_save_background_idle());
 assert(bb_save_settle_timeout(10000000000ULL) && !access("barrier.txt",F_OK));
 assert(bb_save_settle_timeout(0));
 /* round 10: a final battery write that fails is kept (sticky): no new session over it, a retry writes it */
 no_battery=false;config.system=&test_system;config.battery_path="battery.sav";config.state_base="game";
 begin();memset(ram,0x21,128);position=21;assert(bb_save_state_end(10,true));
 memset(ram,0x22,128);
 block_tmp(true);          /* the card refuses the write */
 assert(bb_save_end(false));bb_save_settle();
 assert(bb_save_final_failed());
 {char e[128];assert(!bb_save_begin(&config,e,sizeof(e)) && strstr(e,"not on the SD card"));}
 assert(!bb_save_final_retry() && bb_save_final_failed());   /* still refused: still kept */
 block_tmp(false);
 assert(bb_save_final_retry() && !bb_save_final_failed());disk(0x22);
 /* failed again and let go by the player: the session ends, the card keeps the last good one */
 begin();assert(bb_save_state_end(10,true));memset(ram,0x23,128);block_tmp(true);
 assert(bb_save_end(false));bb_save_settle();assert(bb_save_final_failed());
 bb_save_final_discard();assert(!bb_save_final_failed());block_tmp(false);disk(0x22);
 begin();assert(bb_save_end(false));bb_save_settle();
 /* round 11, N1 (the reviewer's start race): a game's end queued, its final write fails only
  * while the next game's start settles; the next game's begin is refused and its teardown's
  * unconditional end must not write ITS ram over the kept game's battery */
 begin();memset(ram,0x11,128);assert(bb_save_flush());disk(0x11);
 block_tmp(true);bb_later(slow_job,NULL);
 assert(bb_save_state_end(10,true));memset(ram,0x22,128);assert(bb_save_end(false));
 assert(!bb_save_final_failed());                 /* not known yet */
 bb_save_settle();assert(bb_save_final_failed());  /* release_menu's settle: known now */
 memset(ram,0x33,128);                            /* the next core's own RAM */
 {char e[256];assert(!bb_save_begin(&config,e,sizeof(e)));}
 block_tmp(false);
 assert(bb_save_end(false));                      /* that core's teardown: not its session, nothing done */
 disk(0x11);assert(bb_save_final_failed());
 assert(bb_save_final_retry() && !bb_save_final_failed());disk(0x22);
 /* N2: exit while the failure is still to come: the choke point settles first, then asks */
 unlink("barrier.txt");
 begin();block_tmp(true);bb_later(slow_job,NULL);
 assert(bb_save_state_end(10,true));memset(ram,0x44,128);assert(bb_save_end(false));
 assert(!bb_save_final_failed());
 asks=0;answer=BB_SAVE_RETRY;closing_now=close_in_ask=false;
 assert(bb_save_resolve(true,closing,fake_ask,NULL));   /* settled, failure seen, asked, retried (unblocked) */
 assert(asks==1 && !bb_save_final_failed());disk(0x44);
 /* N2, closed by the system: no question, one more try (it works now), then stop */
 begin();block_tmp(true);
 assert(bb_save_state_end(10,true));memset(ram,0x55,128);assert(bb_save_end(false));
 bb_save_settle();assert(bb_save_final_failed());block_tmp(false);
 asks=0;closing_now=true;
 assert(!bb_save_resolve(true,closing,fake_ask,NULL) && asks==0 && !bb_save_final_failed());disk(0x55);
 /* N3: HOME > Close inside the dialog: the caller is told to stop (false), the data got its try */
 begin();block_tmp(true);
 assert(bb_save_state_end(10,true));memset(ram,0x66,128);assert(bb_save_end(false));
 bb_save_settle();assert(bb_save_final_failed());
 asks=0;closing_now=false;close_in_ask=true;answer=BB_SAVE_LATER;
 assert(!bb_save_resolve(true,closing,fake_ask,NULL) && asks==1);
 assert(!bb_save_final_failed());disk(0x55);            /* still refused: let go only because the system closed */
 block_tmp(false);closing_now=close_in_ask=false;
 /* the menu's look: "Later" keeps it, a later answer writes it */
 begin();block_tmp(true);
 assert(bb_save_state_end(10,true));memset(ram,0x77,128);assert(bb_save_end(false));
 asks=0;answer=BB_SAVE_LATER;
 assert(bb_save_resolve(true,closing,fake_ask,NULL) && asks==1 && bb_save_final_failed());
 block_tmp(false);answer=BB_SAVE_RETRY;asks=1;   /* asks==1: fake_ask does not unblock again */
 assert(bb_save_resolve(false,closing,fake_ask,NULL) && !bb_save_final_failed());disk(0x77);
 puts("Service: legacy bytes, writer drain, battery authority/relocation/partial failure, history refusal, corruption fallback, Continue/restart, optional RAM/RTC, suspend barrier, failed final battery kept + retry / let go, start race / exit settle / close in dialog (round 11) PASS");
 return 0;
}

#endif
