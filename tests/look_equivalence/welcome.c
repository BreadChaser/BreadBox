/* The complete welcome controller, including Back, Skip and Games folder. */
#define BB_WELCOME_EQ
#include "dialogs.c"
#include "shelf.h"
#include "ui_trace_views.h"
#include "breadbox_import.h"
bool ui_wants_plain;
static room_view room;
static bool room_quit_on,welcome_live=true;static unsigned quit_tick;
static float room_intro=1,clock_s;
static unsigned imported;
int bb_system_total(void){return 27;}
bool bb_index_complete(void){return true;}
bool breadbox_import_settled(unsigned *kinds){*kinds=imported;return true;}
void breadbox_import_notice_quiet(void){}
void shelf_library_changed(void){}
void covers_pump(void){}
static void room_tv_poll(int frame){(void)frame;}
static void room_plain(bool on){ui_wants_plain=on;}
void music_update(void){}
int video_ui_steps(void){return 1;}
static void present_with(void (*bottom)(void *)){bottom(NULL);host_script_present();}
float shelf_scene_line_width(const char *text){return ui_text_width(NULL,UI_LIST_TEXT,text);}
void shelf_scene_card(const shelf_card *card){for(int i=0;i<card->nrows;i++)ui_trace_row(i,card->row[i].label,card->row[i].value,0);}
#include "welcome-controller.inc"
static void outcome(int answer){printf("%u %s result=%d page=%d resume=%d focus=%d welcome=%d sound=%u\n",step++,scenario,answer,welcome_page,welcome_resume,room.focus,config_welcome,sound);}
int main(int argc,char **argv)
{
 assert(argc==2&&!FT_Init_FreeType(&ft));assert(!FT_New_Face(ft,getenv("PSX3D_TITLE_FONT"),0,&title_face));assert(!FT_New_Face(ft,getenv("PSX3D_BODY_FONT"),0,&body_face));
 char path[1024];snprintf(path,sizeof(path),"%s/assets/romfs/room/room.bbr",getenv("BB_REPO_ROOT"));FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long bytes=ftell(f);rewind(f);void *data=malloc(bytes);assert(fread(data,1,bytes,f)==(size_t)bytes);fclose(f);room_file file;char error[128];assert(room_parse(&file,data,bytes,error,sizeof(error)));room_view_init(&room,&file,ROOM_SETTINGS,-1);
 config_reset_defaults();g_cfg.theme=theme_from_saved_id(argv[1]);room.mode=ROOM_HOME;room.focus=ROOM_SETTINGS;
 setup("welcome-next","..a..b..a..a");outcome(welcome_run(0));
 imported=PSX_IMPORT_SETTINGS;
 setup("welcome-import","..a..a..b..a..a");outcome(welcome_run(0));
 setup("welcome-folder","..a..x");outcome(welcome_run(0));
 setup("welcome-resume","..b..a..a..a");outcome(welcome_run(1));
 setup("welcome-close","..");outcome(welcome_run(0));
 free(data);return 0;
}
