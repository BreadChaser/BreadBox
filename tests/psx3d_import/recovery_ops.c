/* A healthy text primary needs no tmp probe/removal. Text tmp is never read
 * as recovery input and the next transaction safely truncates it. */
#include "safefile.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static unsigned removes,stats,renames;static bool counting,fail_stat;
int __real_remove(const char *);
int __wrap_remove(const char *p){if(counting)removes++;return __real_remove(p);}
int __real_stat(const char *,struct stat *);
int __wrap_stat(const char *p,struct stat *st){if(counting){stats++;if(fail_stat){errno=EIO;return -1;}}return __real_stat(p,st);}
int __real_rename(const char *,const char *);
int __wrap_rename(const char *a,const char *b){if(counting)renames++;return __real_rename(a,b);}
static void put(const char *p,const char *s){FILE *f=fopen(p,"w");assert(f && fputs(s,f)>=0 && !fclose(f));}
int main(int argc,char **argv)
{
 assert(argc==2 && !chdir(argv[1]));put("text","old");put("text.tmp","abandoned");
 counting=true;for(unsigned i=0;i<3;i++)safe_recover("text");counting=false;
 assert(stats==3 && !removes && !renames && !access("text.tmp",F_OK));
 FILE *f=safe_begin("text");assert(f && fputs("new",f)>=0 && safe_finish(f,"text",true));assert(access("text.tmp",F_OK));
 assert(!rename("text","text.bak"));put("text.tmp","partial");removes=stats=renames=0;
 FILE *errors=tmpfile();assert(errors);int stderr_copy=dup(2);assert(stderr_copy>=0);
 assert(dup2(fileno(errors),2)>=0);
 counting=true;fail_stat=true;safe_recover("text");counting=false;
 fflush(stderr);assert(dup2(stderr_copy,2)>=0);close(stderr_copy);
 rewind(errors);char diagnostic[256];assert(fgets(diagnostic,sizeof(diagnostic),errors));
 assert(strstr(diagnostic,"safe_recover: stopped path=text errno=") && errno==EIO);fclose(errors);
 assert(stats==1 && !removes && !renames && !access("text.bak",F_OK) && !access("text.tmp",F_OK));
 struct stat missing;fail_stat=false;errno=0;assert(stat("text",&missing)==-1 && errno==ENOENT);
 removes=stats=renames=0;fail_stat=false;counting=true;safe_recover("text");counting=false;
 assert(stats==2 && removes==1 && renames==1 && !access("text",F_OK) && access("text.tmp",F_OK));
 extern bool safe_recovery_probe(void);assert(safe_recovery_probe());
 assert(access("probe",F_OK) && access("probe.bak",F_OK) && access("probe.tmp",F_OK));
 puts("Text recovery skips absent-tmp cleanup, retains crash recovery, stops on I/O errors: PASS");
}
