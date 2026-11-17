#include "app_policy.h"
#include "ui_format.h"
#include <string.h>
#include <stdio.h>
#include "theme.h"
#include "app_config.h"
#include "platform_video.h"
#include "app_input.h"
#include "ui_list.h"
#include "ui_pause.h"
#include "ui.h"
#include "libui.h"
#include "app_main.h"
#include "gba_settings.h"
#include "gba_color.h"
#include "devtools.h"
#include "bb_feature_ui.h"

const char *gba_setting_label(int row,void *ud);
const char *gba_setting_value(int row,void *ud);
/* Rows shown: Sunlight only for a running game with a solar sensor (Boktai),
 * Rewind and its hotkey only on the New 3DS (like PS1). No 3D row: GBA 3D
 * follows the 3D slider. The hotkeys are the app's, shared with PS1. */
static int visible_fields(int *out)
{
	int n=0;
	for(int f=GBA_SET_SCREEN;f<=GBA_SET_HOTKEY_REWIND;f++) {
		if(f==GBA_SET_STEREO) continue;
		if(f==GBA_SET_SOLAR && !gba_settings_solar_game) continue;
		if((f==GBA_SET_REWIND || f==GBA_SET_HOTKEY_REWIND) && !g_new3ds) continue;
		if(out) out[n]=f;
		n++;
	}
	return n;
}
int gba_setting_rows(void) { return visible_fields(NULL); }
int gba_setting_field(int row)
{
	int f[GBA_SET_HOTKEY_REWIND+1],n=visible_fields(f);
	return row>=0 && row<n ? f[row] : GBA_SET_SCREEN;
}
const char *gba_setting_label(int row,void *ud) {
 (void)ud;return (const char *[]){"Screen size","Filter","Color","LCD ghosting","3D","Sunlight","Rewind",
	 "FF keys","Rewind keys"}[gba_setting_field(row)];
}
static const char *hotkey_value(int i)
{
	static char text[32];
	return ui_keys_text(input_hotkey_combo_name(input_hotkey_get(i)),text,sizeof(text));   // "Select + R", "Off"
}
const char *gba_setting_value(int row,void *ud) {
 (void)ud;
 int f=gba_setting_field(row);
 if(f==GBA_SET_SOLAR) return (const char *[]){"0 · Covered","1","2","3","4","5 · Daylight","6","7","8","9","10 · Bright"}[gba_settings.solar];
 if(f==GBA_SET_HOTKEY_FF || f==GBA_SET_HOTKEY_REWIND) return hotkey_value(f-GBA_SET_HOTKEY_FF);
 return (const char *[]){gba_settings.screen_size==0?"Original":gba_settings.screen_size==1?"Fit":"Stretch",
	 gba_settings.smoothing?"Smooth":"Sharp",gba_color_name(gba_settings.color_correction),gba_settings.frame_blend?"On":"Off",
	 "",NULL,gba_settings.rewind?"On":"Off"}[f];
}
const char *gba_setting_source(int row)
{
	int f=gba_setting_field(row);
	return f>=GBA_SET_HOTKEY_FF?"Source: All systems":gba_settings_source(f);
}
void gba_setting_change(int row,int dir)
{
	int f=gba_setting_field(row);
	if(f>=GBA_SET_HOTKEY_FF) {input_hotkey_cycle(f-GBA_SET_HOTKEY_FF,dir);config_save();}
	else gba_settings_set(f,dir);
}
void gba_setting_clear(int row)
{
	int f=gba_setting_field(row);
	if(f<GBA_SET_HOTKEY_FF) gba_settings_clear(f);
 else { int i=f-GBA_SET_HOTKEY_FF; input_hotkey_set(i,input_hotkey_factory(i));config_save(); }
}
void gba_settings_list_draw(const flist_t *list,int row,const char *scope); /* rendered by the shared gba_settings_ui component */


#include "components/gba_settings_ui.inc"
#include "bb_settings.h"
extern const bb_system_t bb_system_gba;
