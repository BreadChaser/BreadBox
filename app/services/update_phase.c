#include "bb_update_phase.h"
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
static unsigned sequence;
static uint32_t checksum(const char *p,size_t n)
{uint32_t h=2166136261u;while(n--)h=(h^(unsigned char)*p++)*16777619u;return h;}
const char *bb_update_phase_name(unsigned p)
{
 static const char *const names[]={"unknown","flush","log-close","second-SHA","receipt","AM-start","writes-done","GPU-ack","finish","handles-closed","installer-join","covers-exit","devnet-exit","romfs-exit","chainload","HOME-return","failure","next-boot","audio-exit","GPU-exit"};
 return p<sizeof(names)/sizeof(*names)?names[p]:names[0];
}
/* One line: 30 bytes of fields, 8 of checksum over them, newline. */
static bool parse_line(const char *line,bb_update_phase_record *r)
{
 unsigned seq,phase;int used=0;unsigned long result,stamp,hash;
 if(sscanf(line,"BBUP1 %2u %2u %8lx %8lx %8lx%n",&seq,&phase,&result,&stamp,&hash,&used)!=5 || used!=BB_UPDATE_PHASE_LINE-1)return false;
 if(!seq || !phase || phase>BB_UP_LAST || hash!=checksum(line,BB_UPDATE_PHASE_LINE-9))return false;
 *r=(bb_update_phase_record){seq,phase,(uint32_t)result,(uint32_t)stamp};return true;
}
unsigned bb_update_phase_load(const char *path,bb_update_phase_record *out,unsigned max)
{
 char buf[(BB_UPDATE_PHASE_MAX+2)*BB_UPDATE_PHASE_LINE];
 int fd=open(path,O_RDONLY);if(fd<0)return 0;
 ssize_t got=read(fd,buf,sizeof(buf)-1);close(fd);if(got<=0)return 0;
 buf[got]=0;
 unsigned n=0,previous=0;
 for(char *p=buf,*end;n<max && *p;p=end+1){
  end=strchr(p,'\n');
  bool last=!end; /* no newline: a torn final line, complete only if it is exactly one line minus its newline */
  if(last){if(strlen(p)!=BB_UPDATE_PHASE_LINE-1)break;end=p+BB_UPDATE_PHASE_LINE-1;}
  if(end-p!=BB_UPDATE_PHASE_LINE-1)continue;
  *end=0;bb_update_phase_record r;
  if(parse_line(p,&r) && r.sequence>previous){out[n++]=r;previous=r.sequence;}
  if(last)break;
  *end='\n';
 }
 return n;
}
bool bb_update_phase_write(const char *path,unsigned phase,uint32_t result,uint32_t stamp,bool reset)
{
 if(reset)sequence=0;
 else if(!sequence){ /* a later process appends to the journal it finds */
  bb_update_phase_record all[BB_UPDATE_PHASE_MAX];unsigned n=bb_update_phase_load(path,all,BB_UPDATE_PHASE_MAX);
  if(n)sequence=all[n-1].sequence;
 }
 if(!phase || phase>BB_UP_LAST || sequence>=BB_UPDATE_PHASE_MAX)return false;
 char line[BB_UPDATE_PHASE_LINE+1];
 int n=snprintf(line,sizeof(line),"BBUP1 %02u %02u %08lx %08lx ",++sequence,phase,(unsigned long)result,(unsigned long)stamp);
 if(n!=BB_UPDATE_PHASE_LINE-9)return false;
 if(snprintf(line+n,sizeof(line)-n,"%08lx\n",(unsigned long)checksum(line,n))!=9)return false;
 /* A new journal keeps the previous one as phase.prev.txt (one rename; best effort). */
 if(reset && access(path,F_OK)==0){
  char prev[600];size_t len=strlen(path);
  if(len>4 && !strcmp(path+len-4,".txt") && len+6<sizeof(prev)){
   memcpy(prev,path,len-4);strcpy(prev+len-4,".prev.txt");
   remove(prev);(void)rename(path,prev); /* sdmc rename refuses an existing target */
  }
 }
 /* One open, one write, one flush, no retry, no handle kept: a separate small SD file,
  * never the AM destination. Seek instead of O_APPEND: sdmc supports SEEK_END. */
 int fd=open(path,O_RDWR|O_CREAT|(reset?O_TRUNC:0),0666);
 if(fd<0)return false;
 bool ok=true;
 if(!reset){
  off_t size=lseek(fd,0,SEEK_END);char last=0;
  /* Bounded even with repeated torn tails: the file always fits the reader's buffer. */
  ok=size>=0 && size<(off_t)BB_UPDATE_PHASE_MAX*BB_UPDATE_PHASE_LINE;
  /* A hard stop can leave a final line without its newline; an append would glue the
   * new record onto it and the reader would skip both. Terminate it first. This only
   * appends (no truncate, which sdmc may not support, and the evidence stays): the
   * fragment becomes one short line the reader skips. */
  if(ok && size>0)ok=lseek(fd,size-1,SEEK_SET)==size-1 && read(fd,&last,1)==1 && lseek(fd,0,SEEK_END)==size;
  if(ok && size>0 && last!='\n')ok=write(fd,"\n",1)==1;
 }
 ok=ok && write(fd,line,BB_UPDATE_PHASE_LINE)==BB_UPDATE_PHASE_LINE;
 if(ok)ok=fsync(fd)==0;
 if(close(fd))ok=false;
 return ok;
}
void bb_update_phase_text(const bb_update_phase_record *r,unsigned n,char *out,size_t cap)
{
 size_t used=0;if(cap)out[0]=0;
 for(unsigned i=0;i<n && used+1<cap;i++){
  int w=snprintf(out+used,cap-used,"%s%s=%lx",i?" ":"",bb_update_phase_name(r[i].phase),(unsigned long)r[i].result);
  if(w<0 || (size_t)w>=cap-used){out[used]=0;return;}
  used+=(size_t)w;
 }
}
