/* An SD card error while the battery (or clock) file is asked about
 * is not "no save yet". The real save service (bb_save, bb_final, save_writer, safefile, history),
 * with EIO or EACCES on the metadata, open or read of battery.sav, its .bak or its sole .tmp, alone
 * and mixed with a full card, a failing close or a failing rename later. The game never starts on
 * an empty battery while a copy may be there, no damaged-save question is asked for it, and the
 * original bytes stay on the card, also after the card answers again and the game saves twice. */
#define main legacy_check_main
#include "check.c"
#undef main
#include <dirent.h>
#include <errno.h>

enum {ON_STAT=1,ON_OPEN=2,ON_READ=4};
enum {LATER_NONE,LATER_FULL,LATER_CLOSE,LATER_RENAME,LATER_N};
static struct {bool armed;const char *name;int error;unsigned on;int later;} fault;
static bool fd_is(FILE *f,const char *name)
{
 char link[64],got[600];snprintf(link,sizeof(link),"/proc/self/fd/%d",fileno(f));
 ssize_t n=readlink(link,got,sizeof(got)-1);if(n<=0)return false;got[n]=0;size_t k=strlen(name);
 return (size_t)n>k && got[n-k-1]=='/' && !strcmp(got+n-k,name);
}
int __real_stat(const char *,struct stat *);
int __wrap_stat(const char *p,struct stat *s)
{if(fault.armed && (fault.on&ON_STAT) && !strcmp(p,fault.name)){errno=fault.error;return -1;}return __real_stat(p,s);}
FILE *__real_fopen(const char *,const char *);
FILE *__wrap_fopen(const char *p,const char *m)
{
 if(fault.armed && (fault.on&ON_OPEN) && m[0]=='r' && !strcmp(p,fault.name)){errno=fault.error;return NULL;}
 if(fault.armed && fault.later==LATER_FULL && m[0]=='w'){errno=ENOSPC;return NULL;}
 return __real_fopen(p,m);
}
size_t __real_fread(void *,size_t,size_t,FILE *);
size_t __wrap_fread(void *p,size_t z,size_t n,FILE *f)
{if(fault.armed && (fault.on&ON_READ) && fd_is(f,fault.name)){errno=fault.error;return 0;}return __real_fread(p,z,n,f);}
int __real_fclose(FILE *);
int __wrap_fclose(FILE *f)
{bool fail=fault.armed && fault.later==LATER_CLOSE && fd_is(f,"battery.sav.tmp");int r=__real_fclose(f);if(fail){errno=EIO;return EOF;}return r;}
int __real_rename(const char *,const char *);
int __wrap_rename(const char *a,const char *b)
{if(fault.armed && fault.later==LATER_RENAME){errno=EIO;return -1;}return __real_rename(a,b);}

static int damage_calls;
static int ask_damage(const bb_save_damage *d){(void)d;damage_calls++;return BB_DAMAGE_EMPTY;}
static void put(const char *path,int value,size_t n)
{unsigned char b[128];memset(b,value,sizeof(b));FILE *f=__real_fopen(path,"wb");assert(f && fwrite(b,1,n,f)==n);__real_fclose(f);}
static bool holds(const char *path,int value)   /* exactly 128 bytes of value */
{
 unsigned char b[129];FILE *f=__real_fopen(path,"rb");if(!f)return false;
 size_t n=fread(b,1,sizeof(b),f);__real_fclose(f);if(n!=128)return false;
 for(unsigned i=0;i<128;i++)if(b[i]!=value)return false;
 return true;
}
static bool kept(int value)   /* a battery file or a backup in history holds it byte for byte */
{
 DIR *d=opendir(".");struct dirent *e;bool found=false;
 while(d && !found && (e=readdir(d)))found=!strncmp(e->d_name,"battery.sav",11) && holds(e->d_name,value);
 if(d)closedir(d);
 hist_entry_t list[64];int n=history_list("test-test-unique",false,list,64);
 for(int i=0;i<n && !found;i++)found=holds(list[i].path,value);
 return found;
}
static bool absent(const char *p){struct stat st;return __real_stat(p,&st) && errno==ENOENT;}
static int cases;
static void enter(void){char dir[32];snprintf(dir,sizeof(dir),"case%d",cases++);assert(!mkdir(dir,0700) && !chdir(dir));history_set_root("history");damage_calls=0;}
static void leave(void){assert(!chdir(".."));}
static const char *kind(unsigned on){return on==ON_STAT?"stat":on==ON_OPEN?"open":on==(ON_STAT|ON_OPEN)?"stat+open":"read";}

/* A real 128-byte battery of 42, no backup, EIO on its open and stat at the
 * start; the damage question would refuse. Then the card answers again and the game saves twice. */
static void review_probe(void)
{
 enter();put("battery.sav",42,128);bb_save_on_damage(ask_damage);
 fault=(typeof(fault)){.armed=true,.name="battery.sav",.error=EIO,.on=ON_STAT|ON_OPEN};
 memset(ram,17,128);char error[256]="";bool started=bb_save_begin(&config,error,sizeof(error));
 fault.armed=false;
 if(started){assert(bb_save_flush());memset(ram,18,128);assert(bb_save_flush());assert(bb_save_end(false));}
 printf("review probe: begin=%d damage_questions=%d original kept=%d\n",started,damage_calls,kept(42));
 assert(!started && !damage_calls && error[0] && holds("battery.sav",42));
 begin();assert(ram[0]==42);memset(ram,18,128);assert(bb_save_flush());assert(bb_save_end(false));
 assert(holds("battery.sav",18) && kept(42));
 leave();
}

/* Every sole copy, every error, every kind: not started, no question, no file written; then the
 * card answers again and that copy loads. */
static void sole_copies(void)
{
 static const char *const names[]={"battery.sav","battery.sav.bak","battery.sav.tmp"};
 static const int errors[]={EIO,EACCES};
 static const unsigned kinds[]={ON_STAT,ON_OPEN,ON_STAT|ON_OPEN,ON_READ};
 for(unsigned t=0;t<3;t++)for(unsigned e=0;e<2;e++)for(unsigned k=0;k<4;k++){
  enter();put(names[t],42,128);bb_save_on_damage(ask_damage);
  fault=(typeof(fault)){.armed=true,.name=names[t],.error=errors[e],.on=kinds[k]};
  memset(ram,17,128);char error[256]="";bool started=bb_save_begin(&config,error,sizeof(error));
  fault.armed=false;
  if(started || damage_calls || !error[0] || !holds(names[t],42)){
   fprintf(stderr,"%s %s on %s: started=%d questions=%d\n",names[t],strerror(errors[e]),kind(kinds[k]),started,damage_calls);abort();
  }
  for(unsigned o=0;o<3;o++)assert(o==t || absent(names[o]));
  begin();assert(ram[0]==42 && ram[127]==42);assert(bb_save_end(false));assert(kept(42));
  leave();
 }
}

/* The clock file too: an RTC the card cannot read stops the start (no "clock starts over"). */
static void clock_file(void)
{
 static const unsigned kinds[]={ON_STAT,ON_OPEN,ON_STAT|ON_OPEN,ON_READ};
 for(unsigned k=0;k<4;k++){
  enter();bb_save_on_damage(ask_damage);
  rtc[0]=77;begin();memset(ram,5,128);assert(bb_save_flush());assert(bb_save_end(false));
  assert(!unlink("battery.sav"));   /* only the clock is there */
  struct stat before;assert(!__real_stat("battery.rtc",&before));
  fault=(typeof(fault)){.armed=true,.name="battery.rtc",.error=EIO,.on=kinds[k]};
  rtc[0]=1;char error[256]="";bool started=bb_save_begin(&config,error,sizeof(error));
  fault.armed=false;
  assert(!started && !damage_calls && error[0]);
  struct stat after;assert(!__real_stat("battery.rtc",&after) && after.st_size==before.st_size);
  begin();assert(rtc[0]==77);assert(bb_save_end(false));
  leave();
 }
}

/* During play: the battery (42, its backup 41) cannot be read when the game saves, and the card is
 * full / a close fails / renames fail. Not saved (or saved as any save would be); then the card answers and two saves go
 * through: 42 is still on the card or in Backups. */
static void mixed(void)
{
 static const unsigned kinds[]={ON_STAT,ON_OPEN,ON_STAT|ON_OPEN,ON_READ};
 static const char *const names[]={"battery.sav","battery.sav.bak"};
 for(unsigned t=0;t<2;t++)for(unsigned k=0;k<4;k++)for(int later=0;later<LATER_N;later++){
  enter();put("battery.sav.bak",41,128);put("battery.sav",42,128);
  begin();assert(ram[0]==42);
  fault=(typeof(fault)){.armed=true,.name=names[t],.error=EIO,.on=kinds[k],.later=later};
  memset(ram,50,128);bool saved=bb_save_flush();
  fault.armed=false;
  /* the save needs both: the current file goes to Backups first, and the backup is deleted only once
   * it reads back whole: not saved, nothing changed */
  assert(!saved && holds("battery.sav",42) && holds("battery.sav.bak",41));
  /* from here the backup two saves old goes as on any save (the battery keeps the current copy in
   * Backups before each write); the newest original must survive */
  memset(ram,51,128);assert(bb_save_flush());memset(ram,52,128);assert(bb_save_flush());assert(bb_save_end(false));
  if(!kept(42)){fprintf(stderr,"%s %s then %d: original lost\n",names[t],kind(kinds[k]),later);abort();}
  assert(holds("battery.sav",52));
  leave();
 }
}

static bool same_file(const char *path,const unsigned char *want,size_t n)
{
 unsigned char *b=malloc(n+1);FILE *f=__real_fopen(path,"rb");size_t got=f?fread(b,1,n+1,f):0;if(f)__real_fclose(f);
 bool ok=got==n && !memcmp(b,want,n);free(b);return ok;
}
static size_t slurp(const char *path,unsigned char *b,size_t cap)
{FILE *f=__real_fopen(path,"rb");size_t n=f?fread(b,1,cap,f):0;if(f)__real_fclose(f);return n;}

/* The clock file's backup (no history hook) cannot be read
 * when the clock changes again. The write stops; the backup is the old clock byte for byte. */
static void clock_backup(void)
{
 static const unsigned kinds[]={ON_STAT,ON_OPEN,ON_READ};
 for(unsigned k=0;k<3;k++){
  enter();begin();memset(ram,42,128);rtc[0]=42;assert(bb_save_flush());rtc[0]=43;assert(bb_save_flush());
  unsigned char before[64];size_t n=slurp("battery.rtc.bak",before,sizeof(before));assert(n==24 && before[8]==42);
  fault=(typeof(fault)){.armed=true,.name="battery.rtc.bak",.error=EIO,.on=kinds[k]};
  rtc[0]=44;bool saved=bb_save_flush();
  fault.armed=false;
  if(saved || !same_file("battery.rtc.bak",before,n)){fprintf(stderr,"clock backup %s: saved=%d, 42 lost\n",kind(kinds[k]),saved);abort();}
  assert(bb_save_flush());assert(bb_save_end(false));   /* the card answers: the clock is written */
  leave();
 }
}

/* ... and the final files (the Continue point, bb_final): its backup cannot be read when the next point
 * is written. The write fails and is kept for a retry; the backup stays byte for byte. */
static void final_backup(void)
{
 static const unsigned kinds[]={ON_STAT,ON_OPEN,ON_READ};
 for(unsigned k=0;k<3;k++){
  enter();begin();position=3;assert(bb_save_state(BB_SLOT_CONTINUE,true));position=4;assert(bb_save_state(BB_SLOT_CONTINUE,true));
  static unsigned char before[4096];size_t n=slurp("game.state10.bak",before,sizeof(before));assert(n>16);
  fault=(typeof(fault)){.armed=true,.name="game.state10.bak",.error=EIO,.on=kinds[k]};
  position=5;bool written=bb_save_state(BB_SLOT_CONTINUE,true);
  fault.armed=false;
  if(written || !same_file("game.state10.bak",before,n)){fprintf(stderr,"Continue backup %s: written=%d, old point lost\n",kind(kinds[k]),written);abort();}
  (void)bb_save_end(false);
  assert(bb_save_final_failed() && bb_save_final_retry());   /* the card answers: the retry writes point 5 */
  begin();assert(bb_save_state(BB_SLOT_CONTINUE,false) && position==5);assert(bb_save_end(false));
  leave();
 }
}

/* A Continue point the SD card cannot stat is not "no Continue": the start asks
 * (bb_save_exists), and if it then cannot be read, nothing in this session makes a new one over it
 * (a checkpoint, the end); the next start still has it. */
static void continue_discovery(void)
{
 enter();begin();position=7;assert(bb_save_state(BB_SLOT_CONTINUE,true));assert(bb_save_end(false));
 begin();
 fault=(typeof(fault)){.armed=true,.name="game.state10",.error=EIO,.on=ON_STAT};
 bool asked=bb_save_exists(BB_SLOT_CONTINUE);
 bb_save_continue_hold(true);bb_save_continue_hold(false);   /* the prompt, answered Continue */
 bool loaded=bb_save_state(BB_SLOT_CONTINUE,false);
 fault.armed=false;
 printf("Continue with a stat error: asked=%d loaded=%d\n",asked,loaded);
 assert(asked && !loaded);
 position=9;(void)bb_save_checkpoint(true);(void)bb_save_end(true);bb_save_settle();
 begin();position=0;assert(bb_save_state(BB_SLOT_CONTINUE,false) && position==7);assert(bb_save_end(false));
 leave();
}

int main(int argc,char **argv)
{
 assert(argc==2 && !chdir(argv[1]));
 review_probe();
 sole_copies();
 clock_file();
 mixed();
 clock_backup();
 final_backup();
 continue_discovery();
 puts("SD read errors (battery/backup/sole tmp/clock/Continue; stat/open/read; EIO/EACCES; then full/close/rename): never an empty start, no question, no backup deleted unread, Continue asked and kept: PASS");
 return 0;
}
