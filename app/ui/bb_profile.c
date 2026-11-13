#include "app_policy.h"
#include "bb_profile.h"
#include "safefile.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
static bool identifier(const char *s) {
 if(!s || !*s || strlen(s)>64)return false;
 for(;*s;s++)if(!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||(*s>='0'&&*s<='9')||*s=='-'||*s=='_'))return false;
 return true;
}
static int choices(const bb_setting_desc *d){return d->kind==BB_SET_TOGGLE?2:d->nvalues;}
static void read_values(bb_profile *p,const char *path,bool game) {
 safe_recover(path);FILE *f=fopen(path,"r");if(!f)return;
 char line[192],key[96],extra;int v;
 while(fgets(line,sizeof(line),f))if(sscanf(line,"%95[^=]=%d %c",key,&v,&extra)==2)
  for(unsigned i=0;i<p->count;i++)if(!strcmp(key,p->schema[i].key) && v>=0 && v<choices(&p->schema[i])) {
   p->value[i]=v;if(game)p->overrides|=1u<<i;else p->system_values|=1u<<i;
  }
 fclose(f);
}
void bb_profile_open(bb_profile *p,const char *system,const char *section,const char *key,const bb_setting_desc *schema,unsigned n,unsigned model) {
 memset(p,0,sizeof(*p));p->schema=schema;p->model=model==BB_NEW?BB_NEW:BB_OLD;p->count=n>BB_PROFILE_MAX?BB_PROFILE_MAX:n;
 for(unsigned i=0;i<p->count;i++)p->value[i]=schema[i].def[model==BB_NEW?BB_NEW:BB_OLD];
 if(!identifier(system)||!identifier(section)||(key&& !identifier(key)))return;
 mkdir(APP_SD_ROOT "/config",0777);mkdir(APP_SD_ROOT "/games",0777);
 char dir[128];snprintf(dir,sizeof(dir),APP_SD_ROOT "/games/%s",system);mkdir(dir,0777);
 snprintf(p->system_path,sizeof(p->system_path),APP_SD_ROOT "/config/%s-%s.ini",system,section);
 if(key)snprintf(p->game_path,sizeof(p->game_path),"%s/%s-%s.ini",dir,key,section);
 read_values(p,p->system_path,false);memcpy(p->inherited,p->value,sizeof(p->value));
 if(p->game_path[0])read_values(p,p->game_path,true);
}
static bool save(bb_profile *p) {
 const char *path=p->game_path[0]?p->game_path:p->system_path;if(!*path)return false;
 FILE *f=safe_begin(path);if(!f)return false;
 for(unsigned i=0;i<p->count;i++)if((p->game_path[0]?p->overrides:p->system_values)&(1u<<i))fprintf(f,"%s=%d\n",p->schema[i].key,p->value[i]);
 return safe_finish(f,path,true);
}
bool bb_profile_change(bb_profile *p,unsigned i,int dir) {
 if(i>=p->count || !choices(&p->schema[i]))return false;
 int old=p->value[i];uint32_t overrides=p->overrides,system=p->system_values;
 p->value[i]=(old+choices(&p->schema[i])+(dir<0?-1:1))%choices(&p->schema[i]);
 if(p->game_path[0])p->overrides|=1u<<i;else p->system_values|=1u<<i;
 if(save(p))return true;
 p->value[i]=old;p->overrides=overrides;p->system_values=system;return false;
}
bool bb_profile_clear(bb_profile *p,unsigned i) {
 if(i>=p->count)return false;
 int old=p->value[i];uint32_t mask=p->overrides,system=p->system_values;
 p->value[i]=p->game_path[0]?p->inherited[i]:p->schema[i].def[p->model];p->overrides&=~(1u<<i);if(!p->game_path[0])p->system_values&=~(1u<<i);
 if(save(p))return true;
 p->value[i]=old;p->overrides=mask;p->system_values=system;return false;
}
const char *bb_profile_source(const bb_profile *p,unsigned i) {
 if(i>=p->count)return "";
 return p->game_path[0]&&(p->overrides&(1u<<i))?"Source: This game":(p->system_values&(1u<<i))?"Source: System defaults":"Source: Built-in default";
}
