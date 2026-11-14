#include <stdbool.h>
#define APP_BREADBOX
#define SETTINGS_SCOPE_TEST
bool g_3d_screen=true;
bool g_new3ds;
bool room_show_hands;
bool ps1_3d_available(void) {return g_3d_screen && g_new3ds;}
bool bb_session_active(void) {return false;}
/* Audio service substitutes, as in the current settings-tree fixture. */
void sfx_play(int n){(void)n;} void sfx_now(int n){(void)n;} void sfx_default(int n){(void)n;}
void sfx_cancel(void){} void sfx_flush(void){} void sfx_card_closes(void){}
bool sfx_close_said(void){return false;}
#include "../settings/navigation.c"
int main(void) {
 register_options();g_new3ds=true;
 settings_ui_t app={0};
 build_page(&app,CAT_HOME);
 // Hands stay hidden by default, including their appearance settings.
 bool room=theme_room(theme())!=NULL;
 assert(app.count==5 && has_row(&app,F_THEME) && has_row(&app,F_FOLDER) && has_row(&app,F_CREDITS));   // + About / Credits
 assert(!has_row(&app,F_SKIN) && !has_row(&app,F_GLOVES));
 room_show_hands=true;build_page(&app,CAT_HOME);
 assert(app.count==(room?7:5));
 assert(has_row(&app,F_SKIN)==room && has_row(&app,F_GLOVES)==room);
 room_show_hands=false;build_page(&app,CAT_HOME);
 assert(has_row(&app,CATEGORY_ROW+CAT_RESET) && has_row(&app,CATEGORY_ROW+CAT_SAVES));
 for(int page=CAT_DISPLAY;page<=CAT_ADVANCED;page++) {
  build_page(&app,page);
  if(page==CAT_SAVES) {
   assert(app.count==1 && has_row(&app,F_IMPORT));
   assert(!strcmp(row_label(0,&app),"Import again from PSX3D"));
  } else assert(!app.count);
 }
 assert(parent(CAT_THEME)==CAT_HOME);
 system_settings=true;
 settings_ui_t sys={0};build_page(&sys,CAT_HOME);
 assert(has_row(&sys,CATEGORY_ROW+CAT_DISPLAY) && has_row(&sys,CATEGORY_ROW+CAT_ADVANCED));
 assert(!has_row(&sys,F_FOLDER) && !has_row(&sys,F_CREDITS));   // credits belong to the app page only
 build_page(&sys,CAT_DISPLAY);assert(has_row(&sys,F_SCREEN) && !has_row(&sys,F_THEME));
 assert(!strcmp(row_label(0,&sys),"Screen"));
 build_page(&sys,CAT_SAVES);assert(!has_row(&sys,F_IMPORT));
 system_settings=false;
 settings_ui_t game={.game=true};build_page(&game,CAT_HOME);
 assert(has_row(&game,CATEGORY_ROW+CAT_DISPLAY) && !has_row(&game,F_FOLDER));
 build_page(&game,CAT_SAVES);assert(!has_row(&game,F_IMPORT));
 puts("BreadBox App has no emulation settings, Theme returns directly to App, PS1 system/game scopes retained: PASS");
 return 0;
}
