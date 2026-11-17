/* The PS1 pause controller runs against a deterministic game and disk. */
#define BB_PS1_EQ
#include "dialogs.c"
#include "bb_pause.h"
#include "bb_slots.h"
#include "postcards.h"
#include "ui_format.h"
#include "savestates.h"
#include "history.h"
#include "devtools.h"
#include <unistd.h>
static unsigned effects[12];
uint32_t bb_heap_available(void){return 1024*1024;}
void retro_reset(void){effects[3]++;}
static bool game_menu_open,clip_game_enabled=true,have_disk=true;
static int state_slot;
static unsigned game_frames,menu_disc;
static const char *const undo_names[]={"","load","save","reset","start game","delete","restore"};
enum {UNDO_NONE,UNDO_LOAD,UNDO_SAVE,UNDO_RESET,UNDO_START,UNDO_DELETE,UNDO_RESTORE};
static struct {int what;bool redo;u64 at;} undo={.what=UNDO_SAVE};
typedef struct {hist_entry_t e;unsigned id;} save_ref_t;
static save_ref_t ref_entry(const hist_entry_t *e){save_ref_t r={.e=*e};return r;}
static unsigned disk_count(void){return 2;}
static unsigned disk_index(void){return 0;}
static bool disk_eject(bool b){effects[0]++;return true;}
static bool disk_select(unsigned i){effects[1]=i;return true;}
static struct {unsigned (*get_num_images)(void);unsigned (*get_image_index)(void);bool (*set_eject_state)(bool);bool (*set_image_index)(unsigned);} disk={disk_count,disk_index,disk_eject,disk_select};
states_deleted_t states_deleted;
static u64 osGetTime(void){return host_script_tick/(SYSCLOCK_ARM11/1000)+60000;}
int dev_ui_slot;static int state_last_result;
#define STATE_PARTLY 4
#define PAUSE_REWIND_STEPS 18
#define LIST_BACK(k) ((k)&KEY_START?((k)|KEY_B):(k))
static bool aptShouldClose(void){return false;}
static void menu_draw_top(void *ud){}
static void menu_draw_bottom(void *ud);
static void menu_game(void *ud){}
static const char *card_title(void){return "Test PS1";}
static const char *game_title="Test PS1";
void video_game_box(float *box){memset(box,0,4*sizeof(float));}
void bb_pause_scene_set(const char *title,void (*box)(float *),void (*game)(void *)){}
void playlog_pause(bool on){}
static void bottom_backlight(bool on){}
void audio_pause(bool on){}
static void cards_save_now(void){}
static bool autosave_is_due(void){return false;}
static void autosave_start(const char *why){}
bool dev_remote_game(void){return false;}
int dev_ui_take(void){return 0;}
static bool rewind_active(void){return true;}
static int rewind_step(void){effects[2]++;return 0;}
static void rewind_reset(void){effects[3]++;}
static void undo_run(void){effects[4]++;}
static void undo_set(int what,int slot,const save_ref_t *r){undo.what=what;}
static bool safety_copy(hist_kind_t kind,const save_ref_t *a,save_ref_t *b){return true;}
void emu_osd_alert(const char *message){effects[5]++;}
static void osd(const char *format,...){effects[5]++;}
bool emu_record_start(void){effects[6]++;return true;}
static bool continue_prompt(const char *prefix){effects[7]++;return false;}
static void slot_paths(int i,char *p,char *t,size_t n){snprintf(p,n,"slot%d",i);snprintf(t,n,"thumb%d",i);}
static void slot_save_over(void){char p[64];snprintf(p,sizeof(p),"slot%u",state_slot);FILE *f=fopen(p,"wb");assert(f);fputs("state",f);fclose(f);effects[8]++;}
static void state_load(void){effects[9]++;}
static void ring_restore(const hist_entry_t *e){effects[9]++;}
static void card_put_back(const hist_entry_t *e){effects[9]++;}
int states_run(bool saving,const char *prefix,const char *title,int *slot,states_pick_t *pick){effects[10]++;return STATES_PAGE_SLOTS;}
bool settings_ui_run(bool game,void (*top)(void *)){effects[11]++;return false;}
bool settings_ui_cheats(void){effects[11]++;return false;}
static void make_path(char *out,size_t n,const char *a,const char *b){snprintf(out,n,"%s/%s",a,b);}
#define kind_name pause_kind_names
#include "ps1-menu.inc"
#undef kind_name
static void menu_draw_bottom(void *ud)
{
 menu_t *m=ud;ui_button_t bar[UI_BAR_MAX];ui_card_t card={.title="PS1 Pause"};
 ui_card_draw(&m->fl,m->nitems,m->sel,menu_card_row,m,&card);ui_bar_draw(bar,menu_bar(m,bar));
}
static int run_case(const char *name,const char *keys)
{
 setup(name,keys);int answer=menu_run(DEV_UI_NONE);printf("%u %s result=%d sound=%u effects",step++,name,answer,sound);
 for(unsigned i=0;i<12;i++)printf("/%u",effects[i]);printf(" slot=%u\n",state_slot);
 return answer;
}
static int run_row(int kind,const char *name,const char *tail)
{
 menu_t m={0};menu_build(&m);int row=-1;
 for(int i=0;i<m.nitems;i++)if(m.items[i]==kind)row=i;
 assert(row>=0);char keys[128]="..";
 for(int i=0;i<row;i++)strcat(keys,"d.");strcat(keys,tail);
 return run_case(name,keys);
}
int main(int argc,char **argv)
{
 assert(argc==2&&!FT_Init_FreeType(&ft));assert(!FT_New_Face(ft,getenv("PSX3D_TITLE_FONT"),0,&title_face));assert(!FT_New_Face(ft,getenv("PSX3D_BODY_FONT"),0,&body_face));
 config_reset_defaults();g_cfg.theme=theme_from_saved_id(argv[1]);host_menu_frames=100000;
 for(int model=0;model<2;model++) {
  g_new3ds=model;
  run_case("all-rows","..d.d.d.d.d.d.d.d.d.d.d.d.u.u.u.u.u.u.u.u.u.u.u.u.b...");
  run_case("resume","..a...");run_case("undo","..d.a...");run_case("quick-save","..x...b...");
  run_case("save-board","..d.d.a...");run_case("load-board","..d.d.d.a...");
  run_case("settings","..d.d.d.d.a..b...");run_case("cheats","..d.d.d.d.d.a..b...");
  run_case("disc","..d.d.d.d.d.d.r.l.a...");run_case("clip","..d.d.d.d.d.d.d.a...");
  run_case("fast-forward","..d.d.d.d.d.d.d.d.a...");
  if(model)run_case("rewind","..d.d.d.d.d.d.d.d.d.a..b...");
  run_case("touch","..t.b...");
  unsigned resets=effects[3];
  assert(run_row(M_RESET,"reset-cancel","a..b..b..."));assert(effects[3]==resets);
  assert(run_row(M_RESET,"reset-confirm","a..a..."));assert(effects[3]==resets+2);
  assert(!run_row(M_EXIT,"quit-game","a..."));
 }
 return 0;
}
