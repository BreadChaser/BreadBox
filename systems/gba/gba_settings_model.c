/* Settings B adapter. Existing storage and input policy remain authoritative. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "bb_settings.h"
#include "app_input.h"
#include "app_main.h"
#include "bb_settings.h"
#include "gba_settings.h"
#include "gba_touch.h"
#include "gba_cart.h"
#include "gba_saveid.h"
#include "bb_feature_ui.h"
#include "bb_registry.h"
#include <stdio.h>
#include <string.h>
#include "ui_format.h"
extern const char *gba_setting_value(int,void *);
extern const bb_cheat_ops bb_gba_cheats;
static bb_settings_context context;
static char identity[GBA_ID_LEN];
static int fields[32];static unsigned count;
static bb_controls bindings;
static bb_cheats menu_cheats;
static int cheat_count(void){return menu_cheats.count;}
static const char *cheat_label(int i){return menu_cheats.entries[i].name;}
static const char *cheat_value(int i){return menu_cheats.entries[i].enabled?"On":"Off";}
static void cheat_toggle(int i){bb_cheats_toggle(&menu_cheats,i);}
static const char *cheat_add(void){return bb_cheats_keyboard(&menu_cheats);}
static const bb_cheat_ops menu_cheat_ops={cheat_count,cheat_label,cheat_value,cheat_toggle,cheat_add};
/* The shared shortcuts (FF keys, Rewind keys, bottom-screen buttons) live once, in Settings > Customize.
   In a running game the pause card owns Controls (Button mappings) and Cheats, so they are not repeated here. */
static void rebuild_model(void)
{
 count=0;
 for(int r=0;r<gba_setting_rows();r++)if(gba_setting_field(r)<=GBA_SET_BLEND)fields[count++]=gba_setting_field(r);
 control_descriptor_t d;if(control_describe("gba",5,g_new3ds,&d) && d.visible)fields[count++]=105;
 if(GBA_TOUCH_BUTTONS)   /* the on-screen buttons' rows: off in 1.0 (gba_touch.h) */
  for(unsigned i=0;i<4;i++)if(control_describe("gba",i,g_new3ds,&d) && d.visible)fields[count++]=100+(int)i;
 if(!context.running)fields[count++]=200;
 if(g_new3ds)fields[count++]=GBA_SET_REWIND;
 if(gba_settings_solar_game)fields[count++]=GBA_SET_SOLAR;
 if(context.game && !context.running)fields[count++]=201;
}
static bool open_model(const bb_settings_context *c)
{
 context=*c;identity[0]=0;
 if(!c->running) {
  if(c->game) {
   if(!gba_saveid_path(c->path,identity))return false;
   gba_settings_for_identity(c->path,identity);
   unsigned char head[192];gba_settings_solar_game=gba_cart_header(c->path,head) &&
    (!memcmp(head+0xac,"U3I",3)||!memcmp(head+0xac,"U32",3)||!memcmp(head+0xac,"U33",3));
  } else {gba_settings_load();gba_settings_solar_game=false;}
 } else if(c->path)gba_saveid_path(c->path,identity);
 if(c->game && !c->running)bb_cheats_open(&menu_cheats,"gba",identity,NULL,NULL);
 bb_controls_device_open(&bindings,"gba",c->game?identity:NULL,true);
 rebuild_model();return true;
}
static unsigned rows(void){return count;}
static int legacy_row(int f) {for(int i=0;i<gba_setting_rows();i++)if(gba_setting_field(i)==f)return i;return 0;}
static const char *const help[]={
 "Original keeps the cartridge picture at its native size. Fit keeps its shape. Stretch fills the screen. GBA depth follows the 3D slider.",
 "Sharp keeps pixel edges. Smooth softens enlarged pixels. Original screen size is pixel-exact.",
 "Original keeps vivid cartridge colors. Corrected matches the GBA LCD. Soft mixes the two. Bright lifts midtones.",
 "Mix this picture with the previous one for flicker transparency. Moving edges can look softer.",
 "GBA depth follows the 3D slider. Unsupported games use a flat picture.",
 "Set the light reaching this cartridge's solar sensor, from covered to bright sunlight.",
 "Keep recent moments to rewind. Needs free memory on New Nintendo 3DS."
};
static const char *const short_help[]={
 "Choose the picture's size.", "Choose sharp or smooth pixels.", "Choose cartridge colors.",
 "Blend flickering pictures.", "Set depth with the 3D slider.",
 "Choose the solar sensor light.", "Keep recent moments to rewind."
};
static bool read_row(unsigned index,bb_settings_row *out)
{
 if(index>=count)return false;int f=fields[index];
 *out=(bb_settings_row){.id=f,.kind=BB_ROW_VALUE,.can_clear=true,.source=BB_SOURCE_SYSTEM};
 if(f>=200) {
  out->section=f==200?"Controls":"Cheats";out->label=f==200?"Button mappings":"Cheats";out->value=">";
  out->help=f==200?"Change buttons, turbo buttons and pause, quick-save and quick-load shortcuts.":"Turn this game's cheat codes on or off. Saves and cheat definitions are kept when settings reset.";
  out->short_help=f==200?"Map buttons and shortcuts.":"Choose this game's cheats.";
  out->kind=BB_ROW_ACTION;out->source=BB_SOURCE_NONE;out->can_clear=false;return true;
 }
 if(f>=100) {
  control_descriptor_t d;if(!control_describe("gba",f-100,g_new3ds,&d))return false;
  out->section=f==105?"Picture":"Controls";out->label=d.label;out->value="";
  for(unsigned i=0;i<d.binding_count;i++)if(d.bindings[i].value==d.value)out->value=d.bindings[i].label;
  out->help=f==105?"Show new pictures per second and game speed on the bottom screen.":"Choose this touch control's action. The touch screen accepts one contact at a time.";
  out->short_help=f==105?"Show game speed and frame rate.":"Choose this touch action.";
  out->source=context.game && d.overridden?BB_SOURCE_CHANGED:BB_SOURCE_SYSTEM;
  out->system_changed=!context.game && d.overridden;return true;
 }
 int row=legacy_row(f);out->section=f<=GBA_SET_BLEND?"Picture":f==GBA_SET_SOLAR?"Cartridge":"Saves";
 out->label=gba_setting_label(row,NULL);out->value=gba_setting_value(row,NULL);out->help=help[f];out->short_help=short_help[f];
 out->source=gba_settings_overridden(f)?BB_SOURCE_CHANGED:BB_SOURCE_SYSTEM;
 if(out->source==BB_SOURCE_SYSTEM) {
  GbaSettings d=GBA_SETTINGS_DEFAULT;
  int current[]={gba_settings.screen_size,gba_settings.smoothing,gba_settings.color_correction,gba_settings.frame_blend,gba_settings.stereo,gba_settings.solar,gba_settings.rewind};
  int factory[]={d.screen_size,d.smoothing,d.color_correction,d.frame_blend,d.stereo,d.solar,d.rewind};
  out->system_changed=current[f]!=factory[f];
 }
 return true;
}
static void change_row(int id,int dir)
{
 if(id==200) {bb_controls_ui(context.running?&bb_active_controls:&bindings,context.running);return;}
 if(id==201) {bb_cheats_ui(context.running?&bb_gba_cheats:&menu_cheat_ops,context.running);return;}
 if(id>=100) {
  control_descriptor_t d;if(!control_describe("gba",id-100,g_new3ds,&d))return;
  for(unsigned i=0;i<d.binding_count;i++)if(d.bindings[i].value==d.value) {
   unsigned next=(i+d.binding_count+(dir<0?-1:1))%d.binding_count;
   gba_touch_setting_set(id-100,d.bindings[next].value);return;
  }
 } else gba_setting_change(legacy_row(id),dir);
}
static void clear_row(int id)
{
 if(id>=200)return;
 if(id>=100){gba_touch_setting_clear(id-100);return;}
 if(context.game)gba_settings_clear(id);
 else {
  GbaSettings defaults=GBA_SETTINGS_DEFAULT;
  int *v[]={&gba_settings.screen_size,&gba_settings.smoothing,&gba_settings.color_correction,&gba_settings.frame_blend,&gba_settings.stereo,&gba_settings.solar,&gba_settings.rewind};
  int d[]={defaults.screen_size,defaults.smoothing,defaults.color_correction,defaults.frame_blend,defaults.stereo,defaults.solar,defaults.rewind};
  *v[id]=d[id];gba_settings_save();
 }
}
static void reset_model(bb_settings_reset scope)
{
 if(scope==BB_RESET_SCOPE) {
  if(context.game)for(int i=0;i<GBA_SETTING_FIELDS;i++)gba_settings_clear(i);
  else {gba_settings=GBA_SETTINGS_DEFAULT;gba_settings_save();}
 }
 bb_controls *c=context.running?&bb_active_controls:&bindings;
 for(unsigned i=0;i<c->profile.count;i++)bb_profile_clear(&c->profile,i);
 for(unsigned i=0;i<4;i++)gba_touch_setting_clear(i);
 if(scope==BB_RESET_SCOPE)gba_touch_setting_clear(5);
}
static void close_model(void){gba_settings_save();if(!context.running){bb_cheats_close(&menu_cheats);gba_settings_load();gba_settings_solar_game=false;}}
const bb_settings_ops bb_gba_settings={open_model,rows,read_row,change_row,clear_row,reset_model,close_model};
