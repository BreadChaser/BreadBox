/* The damaged-save question and the Continue question with its held
 * Continue point in one game start, on the real save service, final files, later
 * thread, safe files and history. Each launch is what gba_run does: bb_save_begin (the damage question), then
 * the Continue question (bb_pause_prompt: hold until answered; Back keeps the hold; A lets go and loads 10;
 * X moves the old point aside), sleep/HOME (bb_pause_suspend: a checkpoint) and the teardown (queue, the
 * Continue state, flush, end). Before every case the game has a Continue point (99), its backup (98) and a
 * good battery copy in Backups (0x77).
 *  1 damaged Back, Back again, then Restore + Continue? Back: Continue and backup byte for byte, battery restored
 *  2 Restore then Continue? A (and then play and quit); Restore then Start over
 *  3 Restore, then sleep/wake again and again and HOME > Close at the unanswered Continue question
 *  4 a pause's Continue point right after Restore, and right after "Use it" for a smaller save
 *  5 Start over that cannot move the old point, right after Restore: nothing starts over, nothing replaced */
#define main legacy_check_main
#include "check.c"
#undef main
#include <errno.h>
static int choice,calls;
static int ask(const bb_save_damage *d){(void)d;calls++;return choice;}
static void put(const char *path,const void *p,size_t n){FILE *f=fopen(path,"wb");assert(f);if(n)assert(fwrite(p,1,n,f)==n);fclose(f);}
static long bytes(const char *path){struct stat st;return stat(path,&st)?-1:(long)st.st_size;}
static bool same(const char *path,const void *p,size_t n)
{
 FILE *f=fopen(path,"rb");if(!f)return false;unsigned char b[512];size_t got=fread(b,1,sizeof(b),f);fclose(f);
 return got==n && !memcmp(b,p,n);
}
static int file_position(const char *path)
{
 FILE *f=fopen(path,"rb");if(!f)return -1;
 int p=fseek(f,28,SEEK_SET)?-1:fgetc(f);fclose(f);return p;
}
static long file_bytes(const char *path,unsigned char *out,long cap)
{
 FILE *f=fopen(path,"rb");if(!f)return -1;
 long n=(long)fread(out,1,(size_t)cap,f);fclose(f);return n;
}
static unsigned char kept[2][4096];static long kept_n[2];
static void remember(void){kept_n[0]=file_bytes("game.state10",kept[0],4096);kept_n[1]=file_bytes("game.state10.bak",kept[1],4096);assert(kept_n[0]>0 && kept_n[1]>0);}
static bool unchanged(void)
{
 unsigned char now[4096];
 for(int i=0;i<2;i++){long n=file_bytes(i?"game.state10.bak":"game.state10",now,4096);if(n!=kept_n[i] || memcmp(now,kept[i],(size_t)n))return false;}
 return true;
}
static char top[512];
static const unsigned char seven[7]={1,2,3,4,5,6,7};
/* Continue 99 and its backup 98 from two finished sessions, every battery write 0x77 (the newest copy in
 * Backups is a good 0x77 one), then the battery file damaged (cut to 7 bytes) */
static void setup(const char *name)
{
 assert(!chdir(top) && !mkdir(name,0700) && !chdir(name));history_set_root("history");
 bb_save_on_damage(NULL);memset(ram,0x77,128);
 begin();position=98;assert(bb_save_checkpoint(true));assert(bb_save_end(false));
 begin();position=99;assert(bb_save_checkpoint(true));assert(bb_save_end(false));bb_save_settle();
 assert(file_position("game.state10")==99 && file_position("game.state10.bak")==98);
 remember();
 history_fake_now=1700000000;assert(history_add("test-test-unique",HIST_SAVE,".sav",ram,128));history_fake_now=0;
 put("battery.sav",seven,7);bb_save_on_damage(ask);calls=0;memset(ram,0,128);
}
static bool launch(int damage_answer)
{
 char e[256];choice=damage_answer;
 bool ok=bb_save_begin(&config,e,sizeof(e));
 assert(ok || !e[0]);   /* Back: no error text */
 return ok;
}
static void teardown(void)   /* gba_run's: queue, the Continue state (held: none), flush, end */
{
 assert(bb_save_queue());assert(bb_save_state_end(BB_SLOT_CONTINUE,true));assert(bb_save_flush());
 assert(bb_save_end(false));bb_save_settle();
}
static bool bad_kept(void){return same("battery.sav.bad",seven,7);}
static int fails;
#define EXPECT(c,what) do{if(!(c)){fprintf(stderr,"%s: %s\n",what,#c);fails++;}}while(0)
int main(int argc,char **argv)
{
 assert(argc==2 && !chdir(argv[1]) && getcwd(top,sizeof(top)));

 /* 1: Back at the damage question, twice, then Restore and Back at Continue? */
 setup("back-back");
 EXPECT(!launch(BB_DAMAGE_BACK) && calls==1 && same("battery.sav",seven,7) && bytes("battery.sav.bad")<0 && unchanged(),"damage Back");
 EXPECT(!launch(BB_DAMAGE_BACK) && calls==2 && same("battery.sav",seven,7) && unchanged(),"damage Back again");
 EXPECT(launch(BB_DAMAGE_RESTORE) && calls==3 && ram[0]==0x77,"Restore");
 bb_save_continue_hold(true);   /* asked; B: the hold stays to the end */
 teardown();
 EXPECT(unchanged(),"Restore, Continue? Back: Continue point and backup byte for byte");
 disk(0x77);EXPECT(bad_kept(),"Restore, Continue? Back: damaged file kept");
 /* the next start: no damage question, the Continue question again with the same point */
 calls=0;EXPECT(launch(BB_DAMAGE_BACK) && !calls && ram[0]==0x77,"after Restore + Back: starts without a question");
 bb_save_continue_hold(true);bb_save_continue_hold(false);EXPECT(bb_save_state(BB_SLOT_CONTINUE,false) && position==99,"Continue loads 99");
 teardown();

 /* 2a: Restore, then Continue? A: 99 loads, play, quit: the new point, 99 as its backup, battery restored */
 setup("restore-continue");
 EXPECT(launch(BB_DAMAGE_RESTORE) && ram[0]==0x77,"Restore");
 bb_save_continue_hold(true);bb_save_continue_hold(false);position=0;
 EXPECT(bb_save_state(BB_SLOT_CONTINUE,false) && position==99,"Continue loads 99 after Restore");
 position=60;teardown();
 EXPECT(file_position("game.state10")==60 && file_position("game.state10.bak")==99,"Restore, Continue, quit");
 disk(0x77);EXPECT(bad_kept(),"damaged file kept");
 /* 2b: Restore, then Start over: both old points to the backups, the game's own next */
 setup("restore-over");
 EXPECT(launch(BB_DAMAGE_RESTORE),"Restore");
 bb_save_continue_hold(true);EXPECT(bb_save_continue_aside(),"Start over after Restore");
 {hist_entry_t e[8];int n=history_list("test-test-unique",false,e,8),states=0;for(int i=0;i<n;i++)states+=!strcmp(e[i].ext,".state10");EXPECT(states==2,"both old points in Backups");}
 position=7;teardown();
 EXPECT(file_position("game.state10")==7 && bytes("game.state10.bak")<0,"Start over: the next point alone");
 disk(0x77);EXPECT(bad_kept(),"damaged file kept");

 /* 3: Restore, then at the unanswered question sleep/wake four times and HOME > Close (the prompt returns
  * Back: the teardown runs with the hold) */
 setup("restore-sleep-close");
 EXPECT(launch(BB_DAMAGE_RESTORE),"Restore");
 bb_save_continue_hold(true);position=0;
 for(int i=0;i<4;i++)EXPECT(bb_save_checkpoint(true),"sleep at the question");
 EXPECT(!bb_save_pause_take(),"no pause point while asked");
 teardown();
 EXPECT(unchanged(),"sleep x4 + Close at Continue? after Restore: Continue point and backup byte for byte");
 disk(0x77);EXPECT(bad_kept(),"damaged file kept");

 /* 4a: Restore, Continue, then pause at once: the pause's point lands (99 its backup), the battery the restored one */
 setup("restore-pause");
 EXPECT(launch(BB_DAMAGE_RESTORE),"Restore");
 bb_save_continue_hold(true);bb_save_continue_hold(false);EXPECT(bb_save_state(BB_SLOT_CONTINUE,false),"Continue");
 position=42;EXPECT(bb_save_flush() && bb_save_pause_take(),"pause after Restore");bb_save_pause_put();bb_save_settle();
 EXPECT(file_position("game.state10")==42 && file_position("game.state10.bak")==99,"the pause's point after Restore");
 disk(0x77);bb_save_pause_over();position=43;teardown();
 EXPECT(file_position("game.state10")==43 && file_position("game.state10.bak")==42 && bad_kept(),"quit after the pause");
 {hist_entry_t e[8];int n=history_list("test-test-unique",false,e,8),good=0;for(int i=0;i<n;i++)good+=!strcmp(e[i].ext,".sav") && e[i].when==1700000000;EXPECT(good==1,"the good copy stays in Backups");}

 /* 4b: a smaller save that may be from another emulator: asked; "Use it", then pause at once (first start: no
  * Continue point yet) */
 assert(!chdir(top) && !mkdir("small-pause",0700) && !chdir("small-pause"));history_set_root("history");
 {
  static bb_save_ops sized;static bb_system_t sys;
  sized=ops;sized.battery_native=NULL;sized.battery_fill=0xff;sys=test_system;sys.save=&sized;
  bool native64(size_t n);sized.battery_native=native64;
  unsigned char orig[64];for(int i=0;i<64;i++)orig[i]=(unsigned char)(i*3+1);
  put("battery.sav",orig,64);bb_save_on_damage(ask);calls=0;memset(ram,0,128);
  config.system=&sys;
  EXPECT(!launch(BB_DAMAGE_BACK) && calls==1 && same("battery.sav",orig,64) && bytes("battery.sav.bad")<0,"asked; Back changes nothing");
  EXPECT(launch(BB_DAMAGE_USE) && calls==2 && !memcmp(ram,orig,64) && ram[64]==0xff,"Use it");
  position=42;EXPECT(bb_save_flush() && bb_save_pause_take(),"pause right after");bb_save_pause_put();bb_save_settle();
  EXPECT(file_position("game.state10")==42 && bytes("game.state10.bak")<0,"the pause's point");
  bb_save_pause_over();position=44;teardown();
  EXPECT(file_position("game.state10")==44 && file_position("game.state10.bak")==42,"quit after the pause");
  safe_format_t fmt={.size=128};safe_info_t info;unsigned char *p=safe_load("battery.sav",&fmt,0,&info);
  EXPECT(p && !memcmp(p,orig,64) && p[64]==0xff && p[127]==0xff,"the padded battery on the card");free(p);
  hist_entry_t e[8];int n=history_list("test-test-unique",false,e,8),originals=0;
  for(int i=0;i<n;i++)if(!strcmp(e[i].ext,".sav") && e[i].size==64)originals+=same(e[i].path,orig,64);
  EXPECT(originals==1,"the original byte for byte in Backups");
  /* the next start: no question, no second conversion */
  calls=0;memset(ram,0,128);EXPECT(launch(BB_DAMAGE_BACK) && !calls && !memcmp(ram,orig,64) && ram[100]==0xff,"next start");
  bb_save_continue_hold(true);teardown();
  config.system=&test_system;
 }

 /* 5: Restore, then a Start over that cannot move the old point (the game's backups folder cannot be written): it
  * stays held: sleep, a pause, the end replace nothing; the battery is the restored one */
 setup("restore-over-fails");
 EXPECT(launch(BB_DAMAGE_RESTORE),"Restore");
 bb_save_continue_hold(true);
 assert(!chmod("history/test-test-unique",0500));
 bool over=bb_save_continue_aside();
 assert(!chmod("history/test-test-unique",0700));
 EXPECT(!over,"Start over refused");
 EXPECT(unchanged(),"Start over refused: Continue and backup where they were");
 position=42;EXPECT(bb_save_checkpoint(true),"sleep");EXPECT(!bb_save_pause_take(),"no pause point");
 position=43;teardown();
 EXPECT(unchanged(),"failed Start over after Restore: nothing replaced");
 disk(0x77);EXPECT(bad_kept(),"damaged file kept");

 if(fails)return 1;
 puts("damage question + Continue question: Back, Back, Restore + Continue? Back; Restore + Continue / Start over; sleep x4 + Close at the question; a pause point right after Restore and after Use it for a smaller save; a failed Start over after Restore: Continue point, backup, battery and Backups as they must be: PASS");
 return 0;
}
bool native64(size_t n){return n==64;}
