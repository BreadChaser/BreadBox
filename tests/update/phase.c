/* Update phase journal: round trip, resume by a later process, torn and damaged
 * files, bounds, and the exact line format the developer remote shows. */
#include "bb_update_phase.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
static char path[512];
static void put(const char *text,size_t n)
{int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC,0600);assert(fd>=0 && write(fd,text,n)==(ssize_t)n);close(fd);}
static size_t slurp(char *out,size_t cap)
{int fd=open(path,O_RDONLY);assert(fd>=0);ssize_t n=read(fd,out,cap);close(fd);assert(n>=0);return (size_t)n;}
/* A later process: re-exec this test so the writer's in-memory sequence really starts at
 * zero again (a fork would inherit the parent's). */
static char dir[512];
static void in_child(unsigned phase,uint32_t result,bool reset)
{
 pid_t pid=fork();assert(pid>=0);
 if(!pid){
  char a[16],b[16];snprintf(a,sizeof(a),"%u",phase);snprintf(b,sizeof(b),"%u",(unsigned)result);
  execl("/proc/self/exe","phase",dir,a,b,reset?"1":"0",(char *)NULL);_exit(99);
 }
 int st;assert(waitpid(pid,&st,0)==pid && WIFEXITED(st) && !WEXITSTATUS(st));
}
int main(int argc,char **argv)
{
 if(argc==5){ /* the re-exec'd later process: append one record */
  snprintf(path,sizeof(path),"%s/phase.txt",argv[1]);
  return bb_update_phase_write(path,(unsigned)atoi(argv[2]),(uint32_t)atoi(argv[3]),7,argv[4][0]=='1')?0:1;
 }
 assert(argc==2);snprintf(dir,sizeof(dir),"%s",argv[1]);snprintf(path,sizeof(path),"%s/phase.txt",argv[1]);
 bb_update_phase_record r[BB_UPDATE_PHASE_MAX+4];char file[4096];
 assert(!bb_update_phase_load(path,r,BB_UPDATE_PHASE_MAX)); /* missing file */

 /* Round trip: fixed line size, in order, results and stamps preserved. */
 assert(bb_update_phase_write(path,BB_UP_FLUSH,1,1000,true));
 assert(bb_update_phase_write(path,BB_UP_LOG_CLOSE,1,1010,false));
 assert(bb_update_phase_write(path,BB_UP_AM_START,0xdeadbeef,1020,false));
 struct stat st;assert(!stat(path,&st) && st.st_size==3*BB_UPDATE_PHASE_LINE);
 assert(bb_update_phase_load(path,r,BB_UPDATE_PHASE_MAX)==3);
 assert(r[0].sequence==1 && r[0].phase==BB_UP_FLUSH && r[0].result==1 && r[0].stamp==1000);
 assert(r[2].sequence==3 && r[2].phase==BB_UP_AM_START && r[2].result==0xdeadbeef && r[2].stamp==1020);
 char text[256];bb_update_phase_text(r,3,text,sizeof(text));
 assert(!strcmp(text,"flush=1 log-close=1 AM-start=deadbeef"));
 bb_update_phase_text(r,3,text,12);assert(strlen(text)<12); /* truncates, never overruns */
 assert(!strcmp(bb_update_phase_name(BB_UP_NEXT_BOOT),"next-boot") && !strcmp(bb_update_phase_name(99),"unknown"));
 /* Phases are appended: the numbers already in journals on consoles never move. */
 assert(BB_UP_NEXT_BOOT==17 && BB_UP_AUDIO_EXIT==18 && BB_UP_GPU_EXIT==19 && BB_UP_LAST==BB_UP_GPU_EXIT);
 assert(!strcmp(bb_update_phase_name(BB_UP_AUDIO_EXIT),"audio-exit") && !strcmp(bb_update_phase_name(BB_UP_GPU_EXIT),"GPU-exit"));
 size_t good=slurp(file,sizeof(file));assert(good==3*BB_UPDATE_PHASE_LINE);

 /* A later process resumes the sequence instead of restarting it. */
 in_child(BB_UP_NEXT_BOOT,BB_UP_AM_START,false);
 assert(bb_update_phase_load(path,r,BB_UPDATE_PHASE_MAX)==4 && r[3].sequence==4 && r[3].phase==BB_UP_NEXT_BOOT);
 /* ...and a reset starts a new journal. */
 in_child(BB_UP_FLUSH,1,true);
 assert(bb_update_phase_load(path,r,BB_UPDATE_PHASE_MAX)==1 && r[0].sequence==1);

 /* Torn final record: every cut length keeps the complete lines before it. */
 put(file,good);
 for(size_t cut=1;cut<BB_UPDATE_PHASE_LINE;cut++){
  put(file,2*BB_UPDATE_PHASE_LINE+cut);
  /* A cut that keeps all 38 bytes of the line (only the newline is missing) is a valid record. */
  assert(bb_update_phase_load(path,r,BB_UPDATE_PHASE_MAX)==(cut==BB_UPDATE_PHASE_LINE-1?3u:2u));
 }
 /* A torn final line followed by the next boot's append: the append must not be glued
  * to the fragment (the old code lost both). Every cut length, then a second boot. */
 for(size_t cut=1;cut<BB_UPDATE_PHASE_LINE;cut++){
  put(file,2*BB_UPDATE_PHASE_LINE+cut);
  in_child(BB_UP_NEXT_BOOT,BB_UP_LOG_CLOSE,false);
  unsigned got=bb_update_phase_load(path,r,BB_UPDATE_PHASE_MAX);
  /* A cut of 38 bytes is a complete line missing only its newline: it validates once terminated. */
  unsigned kept=cut==BB_UPDATE_PHASE_LINE-1?3:2;
  assert(got==kept+1 && r[got-1].phase==BB_UP_NEXT_BOOT && r[got-1].result==BB_UP_LOG_CLOSE);
  assert(r[got-1].sequence==r[got-2].sequence+1);
  /* The repair happens once: the next append adds exactly one line, no further newline. */
  struct stat before;assert(!stat(path,&before));
  in_child(BB_UP_FAILURE,2,false);
  struct stat after;assert(!stat(path,&after) && after.st_size==before.st_size+BB_UPDATE_PHASE_LINE);
  assert(bb_update_phase_load(path,r,BB_UPDATE_PHASE_MAX)==got+1);
 }
 /* Repeated torn tails stay bounded: the file never outgrows the reader's buffer, and
  * the reader still sees every valid record. */
 {put(file,3*BB_UPDATE_PHASE_LINE);unsigned loops=0;
  for(;loops<64;loops++){
   if(!bb_update_phase_write(path,BB_UP_NEXT_BOOT,1,1,false))break;
   int fd=open(path,O_WRONLY|O_APPEND);assert(fd>=0 && write(fd,"BBUP1 0",7)==7);close(fd); /* crash: torn tail */
  }
  assert(loops<64 && !stat(path,&st) && st.st_size<(off_t)(BB_UPDATE_PHASE_MAX+2)*BB_UPDATE_PHASE_LINE);
  assert(bb_update_phase_load(path,r,BB_UPDATE_PHASE_MAX)>=3);}
 /* A reset keeps the previous journal as phase.prev.txt (one file, replaced each time). */
 {char prev[600];snprintf(prev,sizeof(prev),"%s/phase.prev.txt",argv[1]);unlink(prev);
  put(file,good);in_child(BB_UP_FLUSH,1,true);
  int fd=open(prev,O_RDONLY);assert(fd>=0);char old[4096];assert(read(fd,old,sizeof(old))==(ssize_t)good && !memcmp(old,file,good));close(fd);
  assert(bb_update_phase_load(path,r,BB_UPDATE_PHASE_MAX)==1);
  in_child(BB_UP_LOG_CLOSE,1,false);in_child(BB_UP_FLUSH,2,true); /* second reset replaces it */
  fd=open(prev,O_RDONLY);assert(fd>=0 && read(fd,old,sizeof(old))==2*BB_UPDATE_PHASE_LINE);close(fd);
  unlink(prev);unlink(path); /* no journal to keep when the file is missing: nothing to rename */
  assert(bb_update_phase_write(path,BB_UP_FLUSH,1,1,true) && access(prev,F_OK));}
 put(file,good);
 /* A torn line that still ends in a newline fails its checksum. */
 for(size_t at=0;at<BB_UPDATE_PHASE_LINE-1;at++){
  char bad[4096];memcpy(bad,file,good);bad[2*BB_UPDATE_PHASE_LINE+at]^=0x01;put(bad,good);
  unsigned got=bb_update_phase_load(path,r,BB_UPDATE_PHASE_MAX);
  assert(got==2); /* flipping any byte of the third record drops exactly that record */
 }
 /* Damage in the middle skips only that record; later valid records survive. */
 {char bad[4096];memcpy(bad,file,good);bad[BB_UPDATE_PHASE_LINE+10]^=0x01;put(bad,good);
  assert(bb_update_phase_load(path,r,BB_UPDATE_PHASE_MAX)==2 && r[0].sequence==1 && r[1].sequence==3);}
 /* Garbage, binary noise, empty, over-long and non-terminated files never crash or invent records. */
 put("",0);assert(!bb_update_phase_load(path,r,BB_UPDATE_PHASE_MAX));
 {char noise[3000];for(unsigned i=0;i<sizeof(noise);i++)noise[i]=(char)(i*37+11);put(noise,sizeof(noise));
  assert(!bb_update_phase_load(path,r,BB_UPDATE_PHASE_MAX));
  memset(noise,'A',sizeof(noise));put(noise,sizeof(noise));assert(!bb_update_phase_load(path,r,BB_UPDATE_PHASE_MAX));
  memset(noise,'\n',sizeof(noise));put(noise,sizeof(noise));assert(!bb_update_phase_load(path,r,BB_UPDATE_PHASE_MAX));}
 /* Sequence numbers must increase; a replayed or reordered record is ignored. */
 {char dup[4096];memcpy(dup,file,good);memcpy(dup+good,file,BB_UPDATE_PHASE_LINE);put(dup,good+BB_UPDATE_PHASE_LINE);
  assert(bb_update_phase_load(path,r,BB_UPDATE_PHASE_MAX)==3);}
 /* Unknown phase numbers never validate, even with a correct checksum shape. */
 assert(!bb_update_phase_write(path,0,1,1,true) && !bb_update_phase_write(path,BB_UP_LAST+1,1,1,true));

 /* Bounded: at most BB_UPDATE_PHASE_MAX records and bytes, and max limits the reader. */
 assert(bb_update_phase_write(path,BB_UP_FLUSH,1,1,true));
 unsigned wrote=1;while(bb_update_phase_write(path,BB_UP_FAILURE,2,1,false))wrote++;
 assert(wrote==BB_UPDATE_PHASE_MAX && !stat(path,&st) && st.st_size==(off_t)BB_UPDATE_PHASE_MAX*BB_UPDATE_PHASE_LINE);
 assert(bb_update_phase_load(path,r,BB_UPDATE_PHASE_MAX)==BB_UPDATE_PHASE_MAX && bb_update_phase_load(path,r,5)==5);
 assert(bb_update_phase_load(path,r,0)==0);
 /* A write that cannot happen is reported, not hidden. */
 char missing[600];snprintf(missing,sizeof(missing),"%s/no-such-dir/phase.txt",argv[1]);
 assert(!bb_update_phase_write(missing,BB_UP_FLUSH,1,1,true));
 unlink(path);
 puts("PASS: phase journal round trip, resume, torn/damaged/garbage files, bounds, write failure");
 return 0;
}
