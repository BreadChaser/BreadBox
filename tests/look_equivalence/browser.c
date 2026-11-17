/* The real folder browser reads isolated folders and writes its real config. */
#define _GNU_SOURCE
#define BB_BROWSER_EQ
#include "dialogs.c"
#include "devtools.h"
#include "breadbox_formats.h"
#include "bb_index.h"
static unsigned chosen;
const char *slow_mode_notice(bool full){return NULL;}
int dev_ui_take(void){return 0;}
bool bb_formats_match(const char *name){return strstr(name,".gba") || strstr(name,".cue");}
void bb_formats_systems(char *out,size_t n){snprintf(out,n,"PlayStation, Game Boy Advance");}
void bb_games_dir_chosen(void){chosen++;}
bool bb_index_root_normalize(const char *path,char *out,size_t n){snprintf(out,n,"%s",path);return true;}
#define entry_t browser_entry_t
#define join browser_join
#include "../../app/ui/browser.c"
#undef join
#undef entry_t
static void run_case(const char *name,const char *keys)
{
 char path[512]={0};setup(name,keys);bool answer=browser_run(path,sizeof(path));
 printf("%u %s answer=%d path=%s root=%s chosen=%u sound=%u\n",step++,name,answer,path,g_cfg.games_dir,chosen,sound);
}
int main(int argc,char **argv)
{
 assert(argc==2&&!FT_Init_FreeType(&ft));assert(!FT_New_Face(ft,getenv("PSX3D_TITLE_FONT"),0,&title_face));assert(!FT_New_Face(ft,getenv("PSX3D_BODY_FONT"),0,&body_face));
 config_reset_defaults();g_cfg.theme=theme_from_saved_id(argv[1]);
 mkdir("sdmc:",0700);mkdir("sdmc:/3ds",0700);mkdir(PSX3D_DIR,0700);mkdir("sdmc:/roms",0700);mkdir("sdmc:/roms/PS1",0700);mkdir("sdmc:/roms/GBA",0700);
 FILE *f=fopen("sdmc:/roms/GBA/Test.gba","wb");assert(f);fputs("rom",f);fclose(f);snprintf(g_cfg.games_dir,sizeof(g_cfg.games_dir),"sdmc:/roms");
 run_case("folder-navigation","..d.d.r.l.y.b.b..b");
 run_case("folder-choose","..a");
 run_case("folder-launch","..d.a....");
 run_case("folder-touch","..t.t.b");
 return 0;
}
