#include "fs_path.h"
#include <assert.h>
#include <stdio.h>
#include <sys/stat.h>
static void join(const char *a,const char *b,const char *want){char p[1024];assert(fs_path_join(p,sizeof(p),a,b));assert(!strcmp(p,want));}
int main(void){
 join("sdmc:/3ds/PSX3D","","sdmc:/3ds/PSX3D");
 join("sdmc:/3ds/PSX3D/","config.ini","sdmc:/3ds/PSX3D/config.ini");
 join("sdmc:/","/config.ini","sdmc:/config.ini");join("/","","/");join("","states","states");join("","","");
 char p[8]="abc/";assert(fs_path_join(p,sizeof(p),p,"def"));assert(!strcmp(p,"abc/def"));
 assert(!fs_path_join(p,7,"abc","def") && errno==ENAMETOOLONG && !p[0]);
 struct stat st;
 const char *bad[]={"a//b","a/","a/./b","a/../b","a\\b","sdmc://a",""};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){errno=0;assert(lstat(bad[i],&st)==-1 && errno==EINVAL);assert(!fopen(bad[i],"rb") && errno==EINVAL);}
 char longpart[257];memset(longpart,'x',256);longpart[256]=0;assert(lstat(longpart,&st)==-1 && errno==ENAMETOOLONG);
 char longpath[1025];memset(longpath,'x',1024);longpath[1024]=0;assert(!fopen(longpath,"rb") && errno==ENAMETOOLONG);
 puts("path join and strict sdmc model PASS");
}
