/* The Continue point around a pause and the Continue question,
 * on the real save service, final files, later thread and safe files.
 * 1: a pause makes the Continue point (taken at once, written by the later thread), so a crash, a flat
 *    battery or a stalled game after it loses nothing; quitting or HOME while still paused writes it once.
 * 2: while the question is unanswered (or after Back) nothing replaces the Continue point or its backup;
 *    Start over moves both to the backups. */
#define main legacy_check_main
#include "check.c"
#undef main
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
static bool exists(const char *path){struct stat st;return !stat(path,&st);}
/* Continue at 99, its backup at 98: two finished sessions */
static void two_sessions(void)
{
 begin();position=98;assert(bb_save_checkpoint(true));assert(bb_save_end(false));
 begin();position=99;assert(bb_save_checkpoint(true));assert(bb_save_end(false));bb_save_settle();
 assert(file_position("game.state10")==99 && file_position("game.state10.bak")==98);
}
static unsigned char kept[2][4096];static long kept_n[2];
static void remember(void){kept_n[0]=file_bytes("game.state10",kept[0],4096);kept_n[1]=file_bytes("game.state10.bak",kept[1],4096);assert(kept_n[0]>0 && kept_n[1]>0);}
static void unchanged(const char *when)
{
 unsigned char now[4096];
 for(int i=0;i<2;i++){
  long n=file_bytes(i?"game.state10.bak":"game.state10",now,4096);
  if(n!=kept_n[i] || memcmp(now,kept[i],(size_t)n)){fprintf(stderr,"%s: the %s changed\n",when,i?"backup":"Continue point");assert(0);}
 }
}
static char top[512];
static void fresh_dir(const char *name){assert(!chdir(top) && !mkdir(name,0700) && !chdir(name));history_set_root("history");}
static void block_state(bool on)
{
 if(on){assert(!mkdir("game.state10.tmp",0700));FILE *f=fopen("game.state10.tmp/x","w");assert(f);fclose(f);}
 else{assert(!unlink("game.state10.tmp/x") && !rmdir("game.state10.tmp"));}
}
int main(int argc,char **argv)
{
 assert(argc==2 && !chdir(argv[1]) && getcwd(top,sizeof(top)));

 /* 1a: pause, resume, play on, then the app dies (no HOME, no end): the pause's point is on the card */
 fresh_dir("crash");two_sessions();
 begin();position=42;assert(bb_save_flush() && bb_save_pause_take());
 assert(file_position("game.state10")==99);   /* taken, nothing written before the card's first frame */
 bb_save_pause_put();bb_save_pause_over();position=50;bb_save_settle();
 assert(file_position("game.state10")==42 && file_position("game.state10.bak")==99);
 assert(bb_save_end(false));bb_save_settle();   /* (the test's own cleanup) */

 /* 1b: Quit from the pause card: one write, the pause's (the end makes none) */
 fresh_dir("quit");two_sessions();
 begin();position=42;assert(bb_save_pause_take());bb_save_pause_put();position=7;
 assert(bb_save_state_end(BB_SLOT_CONTINUE,true));assert(bb_save_end(false));bb_save_settle();
 assert(file_position("game.state10")==42 && file_position("game.state10.bak")==99);

 /* 1c: resumed, then quit: the end's own point, the pause's as its backup */
 fresh_dir("resume");two_sessions();
 begin();position=42;assert(bb_save_pause_take());bb_save_pause_put();bb_save_settle();bb_save_pause_over();position=60;
 assert(bb_save_state_end(BB_SLOT_CONTINUE,true));assert(bb_save_end(false));bb_save_settle();
 assert(file_position("game.state10")==60 && file_position("game.state10.bak")==42);

 /* 1d: HOME and sleep while paused: on the card before the handoff, written once */
 fresh_dir("home");two_sessions();
 begin();position=42;assert(bb_save_pause_take());bb_save_pause_put();
 assert(bb_save_checkpoint(true));assert(file_position("game.state10")==42);
 assert(bb_save_checkpoint(true));assert(bb_save_checkpoint(true));
 assert(file_position("game.state10")==42 && file_position("game.state10.bak")==99);
 assert(bb_save_end(false));bb_save_settle();assert(file_position("game.state10.bak")==99);

 /* 1e: a load from the board changes the game: the end makes its own point again */
 fresh_dir("load");two_sessions();
 begin();position=42;assert(bb_save_state(3,true));position=43;assert(bb_save_pause_take());bb_save_pause_put();bb_save_settle();
 assert(bb_save_state(3,false) && position==42);
 assert(bb_save_state_end(BB_SLOT_CONTINUE,true));assert(bb_save_end(false));bb_save_settle();
 assert(file_position("game.state10")==42 && file_position("game.state10.bak")==43);

 /* 1f: pause after pause while the card is busy: the waiting one is replaced, never more than two held,
  * the final files never fill; only the newest lands */
 fresh_dir("rapid");two_sessions();
 begin();bb_later(slow_job,NULL);
 for(position=1;position<=20;position++){assert(bb_save_pause_take());bb_save_pause_put();bb_save_pause_over();}
 bb_save_settle();
 assert(file_position("game.state10")==20 && file_position("game.state10.bak")==99);
 assert(!bb_save_final_failed() && !bb_save_final_uncaptured());
 assert(bb_save_end(false));bb_save_settle();

 /* 1g: a pause's write that failed is kept to try again; a newer end's point supersedes it (no retry
  * puts the older one back over the newer) */
 fresh_dir("failed");two_sessions();
 begin();block_state(true);position=42;assert(bb_save_pause_take());bb_save_pause_put();bb_save_settle();
 assert(bb_save_final_failed() && file_position("game.state10")==99);
 block_state(false);bb_save_pause_over();position=60;
 assert(bb_save_state_end(BB_SLOT_CONTINUE,true));assert(bb_save_end(false));bb_save_settle();
 assert(!bb_save_final_failed() && file_position("game.state10")==60);
 assert(bb_save_final_retry() && file_position("game.state10")==60);

 /* 2a: the question unanswered: HOME (suspend + exit), sleep and wake again and again, a pause attempt,
  * the end and a libretro end with make_continue: the Continue point and its backup stay byte for byte */
 fresh_dir("held");two_sessions();remember();
 begin();bb_save_continue_hold(true);position=0;   /* the core just loaded: the boot state */
 for(int i=0;i<4;i++)assert(bb_save_checkpoint(true));   /* sleep, wake, sleep, suspend, ... */
 assert(!bb_save_pause_take());
 assert(bb_save_state_end(BB_SLOT_CONTINUE,true));assert(bb_save_end(false));bb_save_settle();unchanged("HOME / sleep / end at the question");
 begin();bb_save_continue_hold(true);position=0;assert(bb_save_checkpoint(true));assert(bb_save_end(true));bb_save_settle();
 unchanged("power-off at the question (libretro end)");
 /* the answer given (Continue): from then on points are made again */
 begin();bb_save_continue_hold(true);bb_save_continue_hold(false);position=5;assert(bb_save_checkpoint(true));
 assert(file_position("game.state10")==5 && file_position("game.state10.bak")==99);assert(bb_save_end(false));bb_save_settle();

 /* 2b: Start over: the old point and its backup go to the backups, the next point has no older one beside it */
 fresh_dir("over");two_sessions();
 begin();bb_save_continue_hold(true);assert(bb_save_continue_aside());
 assert(!exists("game.state10") && !exists("game.state10.bak") && !exists("game.state10.time"));
 hist_entry_t e[4];assert(history_list("test-test-unique",false,e,4)==2);
 position=7;assert(bb_save_pause_take());bb_save_pause_put();bb_save_settle();
 assert(file_position("game.state10")==7 && !exists("game.state10.bak"));
 assert(bb_save_end(false));bb_save_settle();
 puts("Continue: a pause's point on the card after the first frame (crash, Quit once, HOME once, resume, load, rapid pauses, failed write superseded); unanswered question / Back / power-off keep Continue + backup byte for byte; Start over keeps both in the backups: PASS");
 return 0;
}
