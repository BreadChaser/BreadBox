/* Updates and release notes use production loops and the shared card input. */
#define BB_CARDS_EQ
#include "dialogs.c"
#include "bb_update.h"
#include "safefile.h"
#include "whatsnew.h"
static unsigned net_stops,checks;
static bool update_initialized=true,update_job_busy,update_check_now,update_explicit_pending,update_crypto_disabled,update_line_bad;
static int update_preference;
static bb_update_machine update_machine;
static bb_update_manifest update_manifest;
static struct {bb_update_state state;char display[32];} update_view;
static bool update_explicit_check,update_approved,update_postponed,update_install_attempted,update_pending_saved;
static unsigned update_status;static uint64_t update_countdown_since;
static char update_message[160];
static void update_say(const char *text,unsigned ms){(void)ms;snprintf(update_message,sizeof(update_message),"%s",text);}
static const char *update_settings_status(void){return "Could not check for updates";}
#define PREF "updates.txt"
static bool update_developer_busy(void){return true;}
static void update_begin(void){}
static bool update_prefs_ready(void){return true;}
static const char *update_unavailable(void){return "Off while remote is on";}
static void update_poll(bool ready){(void)ready;if(update_check_now){checks++;update_check_now=false;update_explicit_pending=false;}}
bool bb_update_net_stop(void){net_stops++;return true;}
const char *bb_update_preference(void){static const char *const names[]={"Automatic","Ask first","Off"};return names[update_preference];}
static const char *update_card_notice(void){return "Off while remote is on";}
void music_update(void){}
bool bb_whatsnew_take(void){return true;}
bool bb_settings_child_top(void){return false;}
bool bb_update_startup_ready(void){return true;}
#define notes release_notes
#include "../../app/ui/whatsnew_ui.c"
#undef notes
static flist_t update_list;static int update_selected;
static void update_bottom(void *unused);
#include "update-controller.inc"
static void update_bottom(void *unused){(void)unused;ui_card_t c={.title="Updates"};ui_card_draw(&update_list,BB_UPDATE_ROWS,update_selected,update_row,NULL,&c);ui_button_t b[2];ui_bar_draw(b,update_bar(b));}
static void outcome(void){printf("%u %s preference=%d stops=%u checks=%u\n",step++,scenario,update_preference,net_stops,checks);}
int main(int argc,char **argv)
{
 assert(argc==2&&!FT_Init_FreeType(&ft));assert(!FT_New_Face(ft,getenv("PSX3D_TITLE_FONT"),0,&title_face));assert(!FT_New_Face(ft,getenv("PSX3D_BODY_FONT"),0,&body_face));
 config_reset_defaults();g_cfg.theme=theme_from_saved_id(argv[1]);bb_whatsnew_prepare();
 setup("updates-preference","..r.l.r.r.r.d.a.b");bb_update_settings();outcome();
 setup("updates-notes","..d.d.a..b..b");bb_update_settings();outcome();
 for(int accept=0;accept<2;accept++) {
  update_explicit_pending=update_explicit_check=true;update_approved=false;update_view.state=update_machine.state=BB_UPDATE_DOWNLOAD;strcpy(update_view.display,"0.9.3");
  setup(accept?"offer-accept":"offer-later",accept?"..a":"..b");bool answer=update_explicit_result();
  printf("%u offer answer=%d state=%d pending=%d approved=%d explicit=%d\n",step++,answer,update_machine.state,update_explicit_pending,update_approved,update_explicit_check);
  assert(answer==(bool)accept && update_approved==(bool)accept);
 }
 for(int error=0;error<2;error++) {
  update_explicit_pending=true;update_view.state=BB_UPDATE_IDLE;update_status=error?6:7;assert(update_explicit_result());
  printf("%u update-result %s\n",step++,update_message);
 }
 free(release_notes.lines);release_notes.count=12;release_notes.lines=calloc(12,sizeof(*release_notes.lines));for(int i=0;i<12;i++)snprintf(release_notes.lines[i],sizeof(release_notes.lines[i]),"Highlight %d",i+1);
 setup("notes-scroll","..d.d.d.d.d.d.d.u.u.a");bb_whatsnew_open();outcome();
 setup("notes-close","..b");bb_whatsnew_open();outcome();bb_whatsnew_exit();
 FILE *f=fopen(PREF,"rb");assert(f);char data[8]={0};assert(fread(data,1,7,f)>0);fclose(f);printf("PREF %s",data);
 return 0;
}
