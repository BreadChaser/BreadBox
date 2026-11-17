/* Production pause and postcard controllers, with the real save service. */
#define BB_PAUSE_EQ
#include "dialogs.c"
#undef main
#include "postcards.h"
#include "bb_pause.h"
#include "bb_save.h"
#include "history.h"
#include "ui_trace_postcards.h"
#include "gba_settings.h"
#include "gba_speed.h"
static void *linearAlloc(size_t n){return calloc(1,n);}
static void linearFree(void *p){free(p);}
#define kind_name pause_kind_names
#include "pause-controller.inc"
#undef kind_name
int dev_ui_take(void){return 0;}bool sw_framelog;
#include "postcards-trace.inc"
time_t __wrap_time(time_t *out){time_t t=1762905540;if(out)*out=t;return t;}
static unsigned saved_actions,loaded_actions,deleted_actions,reset_actions,control_actions,cheat_actions,setting_actions;
static unsigned char position=7,ram[128];
static size_t state_size(void){return 16;}
static bool state_save(void *out,size_t n,size_t *used){assert(n==16);memset(out,position,n);*used=n;saved_actions++;return true;}
static bb_state_result state_load(const void *p,size_t n,bool keep){assert(n==16&&!keep);position=*(const unsigned char *)p;loaded_actions++;return BB_STATE_OK;}
static bool battery(void **p,size_t *n){*p=ram;*n=sizeof(ram);return true;}
static const bb_save_ops save_ops={.state_size=state_size,.state_save=state_save,.state_load=state_load,.battery=battery,.state_magic="BBGBAST1"};
static void game_pause(bool on){(void)on;}
static void game_rebase(void){}
static void game_reset(void){reset_actions++;}
void pc_card_clear(pc_card *c){memset(c,0,sizeof(*c));}
bool pc_picture(pc_card *c,const u16 *p,unsigned w,unsigned h){(void)c;(void)p;(void)w;(void)h;return true;}
void pc_open(pc_view *v,bool saving,bool cont,const char *title,const char *stamp){memset(v,0,sizeof(*v));v->saving=saving;v->has_continue=cont;v->game=title;v->stamp=stamp;}
void pc_close(pc_view *v){(void)v;}
void pc_notice(pc_view *v,const char *text){v->notice=text;sfx_play(SFX_ERROR);}
void pc_animate(pc_view *v,int anim,int card){v->anim=anim;v->anim_card=card;sfx_play(anim==PC_ANIM_TOSS?SFX_TOSS:SFX_SAVE);if(anim==PC_ANIM_TOSS)deleted_actions++;}
bool pc_animating(const pc_view *v){return v->anim!=PC_ANIM_NONE;}
void pc_tick(pc_view *v,float dt){(void)dt;v->anim=PC_ANIM_NONE;}
#include "postcards-controller.inc"
void postcards_top(void *v){(void)v;}
void postcards_bottom(void *data){pc_view *v=data;ui_trace_postcards(v,pc_layout);ui_bar_draw(v->bar,v->nbar);}
static void draw_paused_top(void *v){(void)v;}
static void game_in_glass(void *v){(void)v;}
static void draw_pause(void *unused){(void)unused;ui_card_t card={.title="Pause"};ui_card_draw(&menu_list,nitems,menu_row,card_row,NULL,&card);ui_bar_draw(pause_bar,PAUSE_BAR);}
static void draw_confirm(void *data){bb_confirm_t *c=data;ui_dialog_draw(c->title,NULL,c->text,c->bar,2);}
static void draw_continue_top(void *unused){(void)unused;}
static void draw_continue(void *unused){(void)unused;ui_trace_begin("Continue?",1,0,0);ui_bar_draw(bb_continue_bar,3);}
static void draw_settings(void *unused){(void)unused;}
void bb_pause_scene_set(const char *title,void (*box)(float *),void (*game)(void *)){(void)title;(void)box;(void)game;}
bool bb_pause_scene_top(const char *tag){(void)tag;return false;}
bool bb_controls_ui(bb_controls *c,bool game){(void)c;(void)game;control_actions++;return false;}
bool bb_cheats_ui(const bb_cheat_ops *c,bool game){(void)c;(void)game;cheat_actions++;return false;}
void bb_settings_run(const bb_system_t *s,const bb_settings_context *c,void (*top)(void *),void *ud){(void)s;(void)c;(void)top;(void)ud;}
void bb_last_picture_save(const char *path,const void *p,int w,int h,int stride){(void)path;(void)p;(void)w;(void)h;(void)stride;}
uint32_t bb_heap_available(void){return 1024*1024;}
bb_controls bb_active_controls={0};
static uint32_t digest(const char *name){FILE *f=fopen(name,"rb");if(!f)return 0;uint32_t h=2166136261u;int c;while((c=fgetc(f))!=EOF)h=(h^(unsigned)c)*16777619u;fclose(f);return h;}
static void outcome(int answer){printf("%u %s answer=%d slot=%u position=%u save=%u load=%u delete=%u reset=%u file=%u\n",step++,scenario,answer,state_slot,position,saved_actions,loaded_actions,deleted_actions,reset_actions,digest("game.state0"));}
static bool settings_child(void){setting_actions++;return false;}
GbaSettings gba_settings=GBA_SETTINGS_DEFAULT;
bool gba_settings_solar_game;
static unsigned fast_forwards,rewinds;
static int rewind_result;
void gba_settings_set(int field,int direction){assert(field==GBA_SET_SOLAR);gba_settings.solar+=direction;}
void gba_speed_ff_next(void){fast_forwards++;}
bool gba_speed_rewind_allowed(void){return g_new3ds;}
unsigned gba_speed_history_kib(void){return rewind_result>0?1024:0;}
int gba_speed_back(unsigned steps){assert(steps==GBA_PAUSE_REWIND_STEPS);rewinds++;return rewind_result;}
#include "gba-pause-rows.inc"
static void system_action(const char *label,const char *name,const char *tail)
{
 rows_build();int row=-1;
 for(int i=0;i<nitems;i++)if(items[i].kind==K_SYSTEM && !strcmp(system_row(i)->label,label))row=i;
 assert(row>=0);char keys[128]="..";
 for(int i=0;i<row;i++)strcat(keys,"d.");strcat(keys,tail);
 setup(name,keys);outcome(bb_pause_run());
 printf("SYSTEM ff=%u rewind=%u solar=%d\n",fast_forwards,rewinds,gba_settings.solar);
}
static void pause_action(int kind,const char *name,const char *tail)
{
 rows_build();int row=-1;for(int i=0;i<nitems;i++)if(items[i].kind==kind)row=i;assert(row>=0);
 char keys[128]="..";for(int i=0;i<row;i++)strcat(keys,"d.");strcat(keys,tail);
 setup(name,keys);outcome(bb_pause_run());
 printf("CHILDREN %u/%u/%u\n",control_actions,cheat_actions,setting_actions);
}
int main(int argc,char **argv)
{
 assert(argc==2&&!FT_Init_FreeType(&ft));assert(!FT_New_Face(ft,getenv("PSX3D_TITLE_FONT"),0,&title_face));assert(!FT_New_Face(ft,getenv("PSX3D_BODY_FONT"),0,&body_face));
 config_reset_defaults();g_cfg.theme=theme_from_saved_id(argv[1]);
 static const bb_cheat_ops cheats={0};
 static const bb_system_t system={.id="test",.short_name="Test",.save=&save_ops,.controls=true,.cheats=&cheats,.pause_rows=pause_rows,.npause_rows=3};
 bb_save_config save_config={.system=&system,.key="equivalence",.state_base="game",.battery_path="battery.sav",.identity=1,.content_bytes=512};char error[256];history_set_root("history");assert(bb_save_begin(&save_config,error,sizeof(error)));
 bb_pause_config pc={.system=&system,.title="Test game",.pause=game_pause,.rebase=game_rebase,.reset=game_reset,.settings=settings_child};bb_pause_begin(&pc);
 setup("save","..a...");outcome(slots_run(true));assert(saved_actions==1 && bb_save_exists(0));
 setup("replace-cancel","..a.b.b");outcome(slots_run(true));
 setup("replace","..a.a...");position=9;outcome(slots_run(true));assert(saved_actions==2);
 setup("load","..a...");position=3;outcome(slots_run(false));assert(loaded_actions==1 && position==9);
 setup("delete-cancel","..x.b.b");outcome(slots_run(false));
 setup("delete","..x.x..b");outcome(slots_run(false));assert(deleted_actions==1 && !bb_save_exists(0));
 setup("board-touch","..t.t.b");fake_touch=(touchPosition){188,38};outcome(slots_run(true));assert(saved_actions==4 && bb_save_exists(1));
 setup("continue-back","..b");outcome(bb_pause_prompt());
 setup("continue-load","..a...");int answer=bb_pause_prompt();outcome(answer);assert(answer==BB_CONTINUE_YES);
 setup("pause-all-rows","..d.d.d.d.d.d.d.u.u.u.u.u.u.u.b...");outcome(bb_pause_run());
 pause_action(K_SETTINGS,"pause-settings","a...b...");
 pause_action(K_CONTROLS,"pause-controls","a...b...");pause_action(K_CHEATS,"pause-cheats","a...b...");
 pause_action(K_RESET,"reset-cancel","a..b..b...");assert(!reset_actions);
 pause_action(K_RESET,"reset-confirm","a..a...");assert(reset_actions==1);
 pause_action(K_UNDO,"undo-reset","a...");
 setup("continue-over-cancel","..x..b..b");outcome(bb_pause_prompt());
 setup("continue-over-confirm","..x..a...");outcome(bb_pause_prompt());
 for(int model=0;model<2;model++) {
  g_new3ds=model;gba_settings_solar_game=true;
  system_action("Fast forward","system-fast-forward","a...");
  system_action("Sunlight","system-sunlight","r.l.a.b...");
  if(model) {
   rewind_result=0;system_action("Rewind","system-rewind-empty","a.b...");
   rewind_result=3;system_action("Rewind","system-rewind","a...");
   rewind_result=-1;system_action("Rewind","system-rewind-failed","a...");
   rewind_result=GBA_REWIND_RESTART;system_action("Rewind","system-rewind-restart","a...");
  }
 }
 pause_action(K_QUIT,"pause-quit","a...");
 assert(bb_save_end(false));bb_save_settle();return 0;
}
