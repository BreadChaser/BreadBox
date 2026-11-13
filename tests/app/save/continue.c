/* Production PS1 final Continue controller, with fake core and real atomic files.
 * The source fragment is generated verbatim by run.sh; no reimplemented policy. */
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "bb_final.h"
#include "history.h"
typedef uint16_t u16;typedef uint32_t u32;
#define PATH_LEN 256
#define PSX3D_COLD
#define PSX3D_STATE_DIR "."
enum {STATE_AUTO=10,STATE_WHY_MANUAL,STATE_WHY_EXIT,STATE_WHY_TIMER};
static unsigned state_reason;
static char game_name[PATH_LEN]="original";
static unsigned char core[128];
static int fault,thumbs,adoptions;static bool oom;
static void (*queued)(void *);static void *queued_arg;
void *__real_malloc(size_t);
void *__wrap_malloc(size_t n){return oom?NULL:__real_malloc(n);}
FILE *__real_fopen(const char *,const char *);
FILE *__wrap_fopen(const char *p,const char *m){return fault==1 && strchr(m,'w')?NULL:__real_fopen(p,m);}
int safe_test_rename(const char *a,const char *b){return fault==4?-1:rename(a,b);}
size_t safe_test_fwrite(const void *p,size_t z,size_t n,FILE *f){return fault==2?0:fwrite(p,z,n,f);}
size_t safe_test_fread(void *p,size_t z,size_t n,FILE *f){return fread(p,z,n,f);}
int safe_test_fclose(FILE *f){int r=fclose(f);return fault==3?-1:r;}
static size_t retro_serialize_size(void){return sizeof(core);}
static bool retro_serialize(void *p,size_t n){assert(n==sizeof(core));memcpy(p,core,n);return true;}
static bool valid(const void *p){return !memcmp(p,"STv4 PCSX",9);}
static safe_format_t emu_state_format(void){return (safe_format_t){.size=sizeof(core),.head=32,.valid=valid};}
static void state_ext(char *p,size_t n,int slot){assert(slot==STATE_AUTO);snprintf(p,n,".sta");}
static void make_path(char *p,size_t n,const char *root,const char *ext){snprintf(p,n,"%s/%s%s",root,game_name,ext);}
static void state_seal(void *p,size_t n){(void)p;(void)n;}
static u16 *state_thumb_take(void){u16 *p=malloc(2);if(p)*p=123;return p;}
static bool state_thumb_save_at(const char *p,int slot,const u16 *px,u32 saved)
{assert(!strcmp(p,"./original") && slot==STATE_AUTO && *px==123 && saved);thumbs++;return true;}
static void state_file_info(const char *p,unsigned *reason,unsigned *saved,unsigned *id)
{(void)p;*reason=STATE_WHY_EXIT;*saved=1;*id=0;}
bool history_adopt(const char *g,hist_kind_t k,const char *ext,const char *p,const char *thm,time_t saved,hist_entry_t *out)
{assert(!strcmp(g,"original") && k==HIST_EXIT && !strcmp(ext,".sta") && saved);(void)p;(void)thm;(void)out;adoptions++;return true;}
static void log_printf(const char *fmt,...){(void)fmt;}
static void bb_later(void (*fn)(void *),void *arg){assert(!queued);queued=fn;queued_arg=arg;}
#include "continue.inc"
static void capture(unsigned char value){memset(core,value,sizeof(core));memset(core,0,32);memcpy(core,"STv4 PCSX",9);}
static void durable(unsigned char value){unsigned char p[128];FILE *f=fopen("original.sta","rb");assert(f && fread(p,1,sizeof(p),f)==sizeof(p));fclose(f);assert(p[80]==value);}
int main(int argc,char **argv)
{
 assert(argc==2 && !chdir(argv[1]));
 for(int cut=1;cut<=4;cut++){
  strcpy(game_name,"original");capture(1);assert(state_save_auto_later());queued(queued_arg);queued=NULL;durable(1);
  /* The existing codec accepts older header suffixes, which remain backups. */
  FILE *legacy=fopen("original.sta","r+b");assert(legacy);char header[32]="STv4 PCSX v4.0";
  assert(fwrite(header,1,sizeof(header),legacy)==sizeof(header));fclose(legacy);
  capture(2);fault=cut;assert(state_save_auto_later());
  memset(core,0,sizeof(core));strcpy(game_name,"destroyed-core");queued(queued_arg);queued=NULL;
  assert(bb_final_failed());u32 attempted=0;assert(bb_final_status("./original.sta",&attempted)==BB_FINAL_FAILED && attempted);
  assert(!bb_final_retry());fault=0;assert(bb_final_retry());durable(2);
  FILE *old=fopen("original.sta.bak","rb");assert(old && fread(header,1,32,old)==32 && !strcmp(header,"STv4 PCSX v4.0"));
  assert(!fseek(old,80,SEEK_SET) && fgetc(old)==1);fclose(old);
 }
 assert(thumbs==8 && adoptions>0);
 strcpy(game_name,"original");capture(3);fault=1;assert(state_save_auto_later());queued(queued_arg);queued=NULL;
 assert(bb_final_failed());bb_final_discard();fault=0;durable(2);
 oom=true;assert(!state_save_auto_later());oom=false;assert(bb_final_failed() && !bb_final_retry());bb_final_discard();durable(2);
 puts("PS1 Continue: all file faults, destroyed core/context, immutable retry/history, discard and allocation refusal PASS");return 0;
}
