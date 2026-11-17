/* Real PS1 postcard browser; texture preparation is a platform boundary. */
#define BB_PS1_EQ
#include "dialogs.c"
#include "postcards.h"
#include "savestates.h"
#include "bb_slots.h"
#include "ui_trace_postcards.h"
#include "ui_motion.h"
#include "safefile.h"
#include <sys/stat.h>
states_deleted_t states_deleted;
int dev_ui_take(void){return 0;}
void video_gpu_idle(void){}
void emu_osd_alert(const char *text){(void)text;}
bool state_file_info(const char *path,unsigned *reason,unsigned *saved,unsigned *id){(void)path;*reason=0;*saved=1;*id=1;return true;}
void pc_card_clear(pc_card *c){memset(c,0,sizeof(*c));}
void pc_open(pc_view *v,bool saving,bool cont,const char *title,const char *stamp){memset(v,0,sizeof(*v));v->saving=saving;v->has_continue=cont;v->game=title;v->stamp=stamp;}
void pc_close(pc_view *v){(void)v;}
void pc_animate(pc_view *v,int anim,int card){v->anim=anim;v->anim_card=card;sfx_play(SFX_TOSS);}
bool pc_animating(const pc_view *v){return v->anim!=PC_ANIM_NONE;}
void pc_tick(pc_view *v,float dt){(void)dt;v->anim=PC_ANIM_NONE;}
#include "postcards-controller.inc"
#include "postcards-trace.inc"
#include "ps1-slots-types.inc"
static int states_bar(const browser_t *,ui_button_t *);
void C3D_TexDelete(C3D_Tex *tex){(void)tex;}
static void scan(browser_t *b,const char *prefix)
{
 for(int i=0;i<SLOTS;i++){char path[512];snprintf(path,sizeof(path),"%s.st%d",prefix,i);b->slot[i].used=access(path,F_OK)==0;b->slot[i].saved=1;}
 b->ring_n=history_list(prefix,false,b->ring_e,RING);for(int i=0;i<b->ring_n;i++)b->ring[i].used=true;
}
static void card_select(browser_t *b){b->card_saves=-1;}
static void release(browser_t *b){(void)b;}
static void draw_top(void *b){(void)b;}
static void draw_bottom(void *data);
#include "ps1-slots-controller.inc"
static void draw_bottom(void *data)
{
 browser_t *b=data;
 if(b->page==STATES_PAGE_SLOTS)ui_trace_postcards(&slot_view,pc_layout);
 else {int n=b->page==STATES_PAGE_RING?SLOTS:b->cards_n;int sel=b->page==STATES_PAGE_RING?b->ring_sel:b->card_sel;ui_trace_begin(b->page==STATES_PAGE_RING?"Auto-saves":"Memory cards",n,sel,(int)state_list.scroll_target);for(int i=0;i<n;i++)ui_trace_row(i,"Backup","",0);}
 ui_button_t bar[UI_BAR_MAX];ui_bar_draw(bar,states_bar(b,bar));
}
static void run_case(const char *name,const char *keys,bool saving)
{
 setup(name,keys);int slot=0;states_pick_t pick={0};int answer=states_run(saving,"ps1","Test PS1",&slot,&pick);
 printf("%u %s answer=%d slot=%d page=%d exists=%d deleted=%d\n",step++,name,answer,slot,pick.page,access("ps1.st0",F_OK)==0,states_deleted.done);
}
int main(int argc,char **argv)
{
 assert(argc==2&&!FT_Init_FreeType(&ft));assert(!FT_New_Face(ft,getenv("PSX3D_TITLE_FONT"),0,&title_face));assert(!FT_New_Face(ft,getenv("PSX3D_BODY_FONT"),0,&body_face));config_reset_defaults();g_cfg.theme=theme_from_saved_id(argv[1]);history_set_root("history");
 FILE *f=fopen("ps1.st0","wb");assert(f);fputs("state",f);fclose(f);
 run_case("load","..a",false);run_case("replace-cancel","..a.b.b",true);run_case("replace","..a.a",true);
 run_case("delete-cancel","..x.b.b",false);run_case("delete","..x.x..b",false);assert(access("ps1.st0",F_OK));
 run_case("empty-save","..a",true);run_case("all-slots","..d.d.d.d.r.r.u.u.l.l.b",true);
 run_case("backup-pages","..r.d.u.r.d.u.l.l.b",false);run_case("touch-slot","..t.b",true);
 hist_entry_t entries[4];assert(history_list("ps1",false,entries,4)==1);FILE *saved=fopen(entries[0].path,"rb");assert(saved);char bytes[8]={0};assert(fread(bytes,1,5,saved)==5&&!strcmp(bytes,"state"));fclose(saved);puts("BACKUP state");return 0;
}
