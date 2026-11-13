/* Registry/session operations run only on menu actions and launch/stop. */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <sys/stat.h>
#include "app_policy.h"
#include "bb_registry.h"
static const bb_system_t *active;
static bool resume_requested;
bool bb_session_resume_requested(void){return resume_requested;}
bool bb_session_stereo;
const bb_system_t *bb_session_system(void) { return active; }
bool bb_session_active(void) { return active!=NULL; }
bool bb_session_begin(const bb_system_t *s,const bb_launch *l)
{
 (void)l;
 if(active || !s || s->abi!=BB_SYSTEM_ABI)return false;
 for(unsigned i=0;i<bb_system_count;i++) {
  const bb_shell_ops *ops=bb_systems[i]->shell;
  if(ops && ops->release_menu)ops->release_menu();
 }
 bb_platform_release_menu();
 if(!bb_platform_budget(s)) {bb_platform_restore_menu();return false;}
 active=s;
 if(!bb_platform_claim(s)) {active=NULL;bb_platform_restore_menu();return false;}
 bb_session_stereo=false;return true;
}
/* A thread the app could not stop: nothing it can still touch is freed or released from
 * then on (a system's arena, its menus), and main ends the process at once (exit_terminal). */
static bool abandoned;
void bb_exit_abandon(void) { __atomic_store_n(&abandoned,true,__ATOMIC_RELEASE); }
bool bb_exit_abandoned(void) { return __atomic_load_n(&abandoned,__ATOMIC_ACQUIRE); }
void bb_session_end(void)
{
 if(bb_exit_abandoned()) {bb_session_stereo=false;active=NULL;return;}   /* the core's threads still use the loan */
 bb_platform_drop();bb_session_stereo=false;active=NULL;bb_platform_restore_menu();
}
void bb_session_set_stereo(bool enabled) { bb_session_stereo=enabled; }
static void normalized(const char *s,char out[128])
{
 unsigned n=0;
 for(;s && *s && n<127;s++)if((*s>='a'&&*s<='z')||(*s>='0'&&*s<='9')||(*s>='A'&&*s<='Z'))
  out[n++]=*s>='A'&&*s<='Z'?*s+32:*s;
 out[n]=0;
}
const bb_system_t *bb_system_folder(const char *name)
{
 char n[128],hint[128];normalized(name,n);
 for(unsigned i=0;i<bb_system_count;i++)for(const char *const *h=bb_systems[i]->folder_hints;h && *h;h++) {
  normalized(*h,hint);size_t len=strlen(hint);
  const char *p=n;
  if(strlen(n)>4 && n[0]>='0'&&n[0]<='9'&&n[1]>='0'&&n[1]<='9'&&n[2]>='0'&&n[2]<='9'&&n[3]>='0'&&n[3]<='9')p=n+4;
  if(!strcmp(n,hint)||(!strncmp(p,hint,len) && (!p[len] || p[len]=='v' || !strncmp(p+len,"rev",3))))return bb_systems[i];
 }
 return NULL;
}
bool bb_system_extension(const bb_system_t *s,const char *path)
{
 const char *ext=path?strrchr(path,'.'):NULL;
 for(const char *const *e=s?s->extensions:NULL;ext && e && *e;e++)
  if(!strcasecmp(ext+1,**e=='.'?*e+1:*e))return true;
 return false;
}
__attribute__((section(".psx3d_cold.text")))
const bb_system_t *bb_system_probe_path(const char *path,bb_entry *entry)
{
 if(!path)return NULL;
 unsigned char *head=NULL;size_t need=0,n=0;bool candidate=false;
 for(unsigned i=0;i<bb_system_count;i++)if(bb_system_extension(bb_systems[i],path)) {
  candidate=true;if(bb_systems[i]->probe_bytes>need)need=bb_systems[i]->probe_bytes;
 }
 if(!candidate)return NULL;
 /* The registry declares the header extent; never silently truncate it. */
 if(need && !(head=malloc(need)))return NULL;
 struct stat st;uint64_t size=need && !stat(path,&st)?st.st_size:0;
 FILE *f=need?fopen(path,"rb"):NULL;
 if(f){n=fread(head,1,need,f);fclose(f);}
 const bb_system_t *winner=NULL;unsigned best=0;bool tie=false;
 const char *name=strrchr(path,'/');name=name?name+1:path;
 char parent[512];snprintf(parent,sizeof(parent),"%s",path);
 char *slash=strrchr(parent,'/');if(slash)*slash=0;
 const char *folder=strrchr(parent,'/');folder=folder?folder+1:parent;
 for(unsigned i=0;i<bb_system_count;i++) {
  const bb_system_t *s=bb_systems[i];if(!bb_system_extension(s,path))continue;
  bb_entry e={0};bool hint=bb_system_folder(folder)==s;
  bb_probe_input in={.path=path,.name=name,.head=head,.head_len=n,.size=size,.folder_match=hint};
  bb_probe_result result=s->probe?s->probe(&in,&e):BB_PROBE_MAYBE;
  unsigned score=result==BB_PROBE_YES?4:result==BB_PROBE_MAYBE?1:0;
  if(score && hint)score++;
  if(score>best){winner=s;best=score;tie=false;if(entry)*entry=e;}
  else if(score && score==best)tie=true;
 }
 free(head);
 return tie?NULL:winner;
}
const bb_system_t *bb_system_for_path(const char *path) {return bb_system_probe_path(path,NULL);}
bool room_show_hands = false;
int sw_room3d = 30;   // room3d=30|60|off: the Old 3DS's room in 3D (video.c); defined here with its switch
float sw_quitturn = .7f;   // quitturn=<seconds>: the confirmed Quit's turn and fade (home.c)
bool bb_system_switch(const char *arg)
{
 if(!strcmp(arg,"roomhands=on") || !strcmp(arg,"roomhands=off")){room_show_hands=!strcmp(arg,"roomhands=on");return true;}
 if(!strncmp(arg,"quitturn=",9)){sw_quitturn=strtof(arg+9,NULL);return true;}
 if(!strncmp(arg,"room3d=",7)){sw_room3d=!strcmp(arg+7,"60")?60:!strcmp(arg+7,"off")?0:30;return true;}   // the Old's room in 3D (video.c)
 for(unsigned i=0;i<bb_system_count;i++) {
  const bb_shell_ops *s=bb_systems[i]->shell;
  if(s && s->parse_switch && s->parse_switch(arg))return true;
 }
 return false;
}
const char *bb_system_autostart(void)
{
 for(unsigned i=0;i<bb_system_count;i++) {
  const bb_shell_ops *s=bb_systems[i]->shell;
  if(s && s->autostart) {const char *path=s->autostart();if(path && *path)return path;}
 }
 return NULL;
}
int bb_system_total(void)
{
 int total=0;
 for(unsigned i=0;i<bb_system_count;i++)total+=bb_system_library_count(bb_systems[i]);
 return total;
}
void bb_system_counts(char *out,size_t cap)
{
 size_t used=0;if(!cap)return;out[0]=0;
 const bb_system_t *order[BB_MAX_SYSTEMS];unsigned count=bb_system_display_order(order,BB_MAX_SYSTEMS,false);
 for(unsigned i=0;i<count && used<cap-1;i++) {
  const bb_system_t *s=order[i];int n=bb_system_library_count(s);
  int wrote=snprintf(out+used,cap-used,"%s%d %s",i?" + ":"",n,s->short_name);
  if(wrote<0)return;
  if((size_t)wrote>=cap-used)return;
  used+=wrote;
 }
}

bool bb_system_init(void)
{
 for(unsigned i=0;i<bb_system_count;i++) {
  const bb_shell_ops *s=bb_systems[i]->shell;
  if(s && s->init && !s->init()) {
   while(i) {s=bb_systems[--i]->shell;if(s && s->deinit)s->deinit();}
   return false;
  }
 }
 return true;
}
void bb_system_deinit(void)
{
 for(unsigned i=bb_system_count;i;) {
  const bb_shell_ops *s=bb_systems[--i]->shell;
  if(s && s->deinit)s->deinit();
 }
}
void bb_system_resume(const char *path,bool enabled)
{
 resume_requested=enabled;
 const bb_system_t *s=bb_system_for_path(path);
 if(s && s->shell && s->shell->resume)s->shell->resume(enabled);
}
const char *bb_session_title(void)
{
 return active && active->shell && active->shell->title?active->shell->title():"";
}

void bb_session_tick(void) { if(active && active->tick)active->tick(); }
__attribute__((weak)) unsigned bb_platform_model(void) {return BB_OLD;}
int bb_setting(const bb_system_t *s,unsigned row)
{
 if(!s || row>=s->nsettings)return 0;
 return s->setting_value?s->setting_value(row):s->settings[row].def[bb_platform_model()==BB_NEW?BB_NEW:BB_OLD];
}

int bb_system_cover_id(const bb_system_t *s,int index)
{
 if(index<0 || index>=65536)return -1;
 for(unsigned i=0;i<bb_system_count;i++)if(bb_systems[i]==s)return ((int)i+1)*65536+index;
 return -1;
}

int bb_system_library_count(const bb_system_t *s)
{
 return s && s->shell && s->shell->count?s->shell->count():bb_platform_count(s);
}
