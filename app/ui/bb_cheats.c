#define _POSIX_C_SOURCE 200809L
#include "app_policy.h"
#include "bb_cheats.h"
#include "safefile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
static bool text_ok(const char *s) {return s&&*s&&!strpbrk(s,"\"\r\n");}
static bool id_ok(const char *s) {return s&&*s&&strspn(s,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")==strlen(s);}
void bb_cheats_close(bb_cheats *c) {
 if(c->reset)c->reset();
 for(unsigned i=0;i<BB_CHEATS_MAX;i++){free(c->entries[i].name);free(c->entries[i].code);}
 memset(c,0,sizeof(*c));
}
void bb_cheats_apply(bb_cheats *c) {
 if(!c->reset||!c->set)return;
 c->reset();unsigned n=0;
 for(unsigned i=0;i<c->count;i++)if(c->entries[i].enabled)c->set(n++,true,c->entries[i].code);
}
void bb_cheats_open(bb_cheats *c,const char *sys,const char *key,void (*reset)(void),void (*set)(unsigned,bool,const char *)) {
 bb_cheats_close(c);c->reset=reset;c->set=set;if(reset)reset();
 if(!id_ok(sys)||!id_ok(key))return;
 mkdir(APP_SD_ROOT "/cheats",0777);char dir[256];snprintf(dir,sizeof(dir),APP_SD_ROOT "/cheats/%s",sys);mkdir(dir,0777);
 snprintf(c->path,sizeof(c->path),"%s/%s.cht",dir,key);safe_recover(c->path);
 FILE *f=fopen(c->path,"r");if(!f)return;
 char line[2048],field[16];int i;
 while(fgets(line,sizeof(line),f)) {
  if(sscanf(line,"cheat%d_%15[a-z]",&i,field)!=2||i<0||i>=BB_CHEATS_MAX)continue;
  char *v=strchr(line,'=');if(!v)continue;v++;while(*v==' '||*v=='\t')v++;
  char *end=v+strlen(v);while(end>v && (end[-1]=='\n'||end[-1]=='\r'||end[-1]==' '))*--end=0;
  if(*v=='"'){v++;if(end>v && end[-1]=='"')*--end=0;}
  bb_cheat *e=&c->entries[i];
  if(!strcmp(field,"enable"))e->enabled=!strcmp(v,"true")||!strcmp(v,"1");
  else if(text_ok(v) && strlen(v)<1024) {
   char **out=!strcmp(field,"desc")?&e->name:!strcmp(field,"code")?&e->code:NULL;
   if(out){free(*out);*out=strdup(v);}
  }
 }
 fclose(f);
 unsigned n=0;for(unsigned j=0;j<BB_CHEATS_MAX;j++) {
  bb_cheat e=c->entries[j];memset(&c->entries[j],0,sizeof(e));
  if(e.name&&e.code)c->entries[n++]=e;else{free(e.name);free(e.code);}
 }
 c->count=n;bb_cheats_apply(c);
}
static bool save(bb_cheats *c) {
 if(!c->path[0])return false;
 FILE *f=safe_begin(c->path);if(!f)return false;
 fprintf(f,"cheats = %u\n",c->count);
 for(unsigned i=0;i<c->count;i++)fprintf(f,"cheat%u_desc = \"%s\"\ncheat%u_code = \"%s\"\ncheat%u_enable = %s\n",i,c->entries[i].name,i,c->entries[i].code,i,c->entries[i].enabled?"true":"false");
 return safe_finish(f,c->path,true);
}
bool bb_cheats_toggle(bb_cheats *c,unsigned i) {
 if(i>=c->count)return false;
 c->entries[i].enabled=!c->entries[i].enabled;
 if(!save(c)){c->entries[i].enabled=!c->entries[i].enabled;return false;}bb_cheats_apply(c);return true;
}
bool bb_cheats_add(bb_cheats *c,const char *name,const char *code) {
 if(c->count>=BB_CHEATS_MAX || !text_ok(name)||!text_ok(code)||strlen(name)>128||strlen(code)>1023)return false;
 bb_cheat *e=&c->entries[c->count];e->name=strdup(name);e->code=strdup(code);e->enabled=false;
 if(e->name&&e->code){c->count++;if(save(c))return true;c->count--;}
 free(e->name);free(e->code);memset(e,0,sizeof(*e));return false;
}
