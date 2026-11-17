// Exercise font switching without loading resources from the menu frame.
#include <assert.h>
#include <stdio.h>
#include "app_config.h"
#include "theme.h"
#include "list_look.h"
psx3d_config g_cfg;
C2D_Font title_font;
static int loads, frees, shelves, backdrops, releases, font;
static C2D_Font C2D_FontLoad(const char *path) { (void)path;loads++;return &font; }
static void C2D_FontFree(C2D_Font value) { assert(value==&font);frees++; }
void log_printf(const char *format,...) { (void)format; }
void list_look_prepare(void){backdrops++;}
void list_look_release(void){releases++;}
bool shelf_scene_prepare(void) { shelves++;return true; }
#include "../../app/ui/theme.c"
#include "../../app/ui/look_theme.c"
#include "../../app/ui/look_fonts.c"
int main(void)
{
 for(int initial=0;initial<BB_LOOK_COUNT;initial++) {
  g_cfg.theme=theme_from_saved_id(theme_look_get(initial)->saved_id);
  libui_init();assert(loads==initial+1);
  int prepared=shelves,art=backdrops;
  for(int next=0;next<BB_LOOK_COUNT;next++) {
   theme_select_look(next);
   assert(title_font==(next==BB_LOOK_SIMPLE ? NULL : &font));
   assert(loads==initial+1 && shelves==prepared && backdrops==art);
  }
  libui_init();assert(loads==initial+1);
  libui_release_menu();assert(title_font==&font && frees==initial && releases==3*initial+1);
  theme_select_look(BB_LOOK_SIMPLE);assert(title_font==&font);
  libui_init();assert(!title_font && loads==initial+1 && backdrops==art+1);
  theme_select_look(BB_LOOK_PSX3D);assert(title_font==&font);
  libui_release_menu();libui_init();assert(title_font==&font && loads==initial+1);
  libui_exit();assert(!title_font && frees==initial+1 && releases==3*initial+3);
 }
 puts("PASS: runtime look changes reuse prepared art and fonts without resource work");
}
