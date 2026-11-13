#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include "app_policy.h"
#include "gba_settings.h"
#include "gba_touch.h"
#include "safefile.h"
#include "gba_saveid.h"

GbaSettings gba_settings = { 1, 0, 0, 0, 1, 5, 1 };
bool gba_settings_solar_game;
static const char *system_path = APP_SD_ROOT "/config/gba.ini";
static char game_path[512];
static unsigned overrides;
gba_touch_config gba_touch_settings = {{GBA_TOUCH_BUTTONS?8:-1,GBA_TOUCH_BUTTONS?0:-1,GBA_TOUCH_BUTTONS?18:-1,GBA_TOUCH_BUTTONS?20:-1},0,0};
/* The stored defaults: with the buttons off (gba_touch.h) every binding is Hide, so nothing differs and nothing is saved. */
static gba_touch_config touch_default(void)
{
 gba_touch_config d=GBA_TOUCH_DEFAULT;
 if(!GBA_TOUCH_BUTTONS)for(unsigned i=0;i<4;i++)d.binding[i]=GBA_TOUCH_HIDE;
 return d;
}
static bool touch_stored(unsigned i) {return i<6 && (GBA_TOUCH_BUTTONS || i>=4);}
static gba_touch_config touch_inherited;
static unsigned touch_overrides;
static const char *const touch_keys[]={"touch_a","touch_b","touch_ff","touch_rewind","touch_side","show_fps"};
static int *touch_field(gba_touch_config *c,unsigned i) {return i<4?&c->binding[i]:i==4?&c->left:&c->fps;}
bool gba_touch_overridden(unsigned i) {gba_touch_config defaults=touch_default();return i<6 && (game_path[0]?(touch_overrides&(1u<<i)):(*touch_field(&gba_touch_settings,i)!=*touch_field(&defaults,i)));}
bool gba_touch_inherited(unsigned i,int *value) {if(i>=6)return false;*value=*touch_field(&touch_inherited,i);return true;}
void gba_touch_setting_set(unsigned i,int value) {
 if(!touch_stored(i))return;
 if(i==4){control_side_set(value);return;}
 *touch_field(&gba_touch_settings,i)=gba_touch_validate(i,value);
 if(game_path[0])touch_overrides|=1u<<i;
 gba_settings_save();
}
void gba_touch_setting_clear(unsigned i) {
 if(!touch_stored(i))return;
 if(i==4){control_side_set(0);return;}
 gba_touch_config defaults=touch_default();
 *touch_field(&gba_touch_settings,i)=*touch_field(game_path[0]?&touch_inherited:&defaults,i);
 touch_overrides&=~(1u<<i);gba_settings_save();
}
static GbaSettings inherited;
static int *field(GbaSettings *s,int row) {
	return row==0?&s->screen_size:row==1?&s->smoothing:row==2?&s->color_correction:row==3?&s->frame_blend:row==4?&s->stereo:row==5?&s->solar:&s->rewind;
}
static const char *keys[]={"screen_size","smoothing","color_correction","frame_blend","stereo","solar","rewind"};
static void read_settings(const char *settings_path, bool game)
{
	safe_recover(settings_path);
	FILE *f=fopen(settings_path,"r"); if(!f)return;
	char line[96],key[48];int value;
	while(fgets(line,sizeof(line),f)) {
		if(sscanf(line,"%47[^=]=%d",key,&value)!=2)continue;
  for(unsigned i=0;i<6;i++)if(touch_stored(i) && !strcmp(key,touch_keys[i])) {   /* a saved binding of the off buttons: ignored */
   *touch_field(&gba_touch_settings,i)=gba_touch_validate(i,value);
   if(game)touch_overrides|=1u<<i;
  }
		if(!strcmp(key,"screen_fit")) {strcpy(key,"screen_size");value=value?1:0;}
		for(int i=0;i<GBA_SETTING_FIELDS;i++) if(!strcmp(key,keys[i])) {
			*field(&gba_settings,i)=i==5?(value<0?0:value>10?10:value):i?!!value:value<0?0:value>2?2:value;
			if(game)overrides|=1u<<i;
		}
	}
	fclose(f);
}

void gba_settings_load(void)
{
	game_path[0]=0;overrides=touch_overrides=0;
 gba_touch_settings=touch_default();
	gba_settings=GBA_SETTINGS_DEFAULT;
	read_settings(system_path,false);
	inherited=gba_settings;touch_inherited=gba_touch_settings;
}
void gba_settings_for_identity(const char *path, const char *identity)
{
	gba_settings_load();
	unsigned hash=2166136261u;
	for(const unsigned char *p=(const unsigned char *)path;*p;p++)hash=(hash^*p)*16777619u;
	char legacy[512],id[GBA_ID_LEN];
	snprintf(legacy,sizeof(legacy),APP_SD_ROOT "/config/gba-%08x.ini",hash);
	if(identity || gba_saveid_path(path,id)) {
		snprintf(game_path,sizeof(game_path),APP_SD_ROOT "/config/%s.ini",identity?identity:id);
		if(!gba_saveid_copy(legacy,game_path))read_settings(legacy,true);
	} else snprintf(game_path,sizeof(game_path),"%s",legacy);
	read_settings(game_path,true);
}
void gba_settings_for_game(const char *path)
{
	gba_settings_for_identity(path,NULL);
}
void gba_settings_save(void)
{
	mkdir(APP_SD_ROOT "/config",0777);
	const char *path=game_path[0]?game_path:system_path;
	FILE *f=safe_begin(path);if(!f)return;
	for(int i=0;i<GBA_SETTING_FIELDS;i++) if(i!=GBA_SET_STEREO && (!game_path[0] || (overrides&(1u<<i))))
		fprintf(f,"%s=%d\n",keys[i],*field(&gba_settings,i));
 gba_touch_config defaults=touch_default();
	for(unsigned i=0;i<6;i++)if(i!=4 && touch_stored(i) && (game_path[0]?(touch_overrides&(1u<<i)):(*touch_field(&gba_touch_settings,i)!=*touch_field(&defaults,i))))
  fprintf(f,"%s=%d\n",touch_keys[i],*touch_field(&gba_touch_settings,i));
 safe_finish(f,path,true);
}
void gba_settings_set(int row,int dir)
{
	int *v=field(&gba_settings,row);
	*v=row==0?(*v+3+dir)%3:row==5?(*v+11+dir)%11:!*v;
	if(game_path[0]) overrides|=1u<<row;
	gba_settings_save();
}
void gba_settings_clear(int row)
{
	if(row < 0 || row >= GBA_SETTING_FIELDS)return;
	GbaSettings defaults=GBA_SETTINGS_DEFAULT;
	*field(&gba_settings,row)=*field(game_path[0]?&inherited:&defaults,row);
	overrides&=~(1u<<row);
	gba_settings_save();
}
bool gba_settings_overridden(int row)
{ return row>=0 && row<GBA_SETTING_FIELDS && game_path[0] && (overrides&(1u<<row)); }
const char *gba_settings_source(int row)
{
	return gba_settings_overridden(row)?"Source: This game":"Source: Game Boy Advance defaults";
}

void gba_touch_fps_save_global(void)
{
 int selected=gba_touch_settings.fps;
 char saved_path[sizeof(game_path)];strcpy(saved_path,game_path);
 GbaSettings saved_settings=gba_settings;gba_touch_config saved_touch=gba_touch_settings;
 unsigned saved_overrides=overrides,saved_touch_overrides=touch_overrides;
 game_path[0]=0;gba_settings=inherited;gba_touch_settings=touch_inherited;
 gba_touch_settings.fps=selected;gba_settings_save();touch_inherited.fps=selected;
 strcpy(game_path,saved_path);gba_settings=saved_settings;gba_touch_settings=saved_touch;
 overrides=saved_overrides;touch_overrides=saved_touch_overrides;
}
static const char *intro_path=APP_SD_ROOT "/config/touch-controls-seen-v2";
bool gba_touch_intro(void)
{
 if(!GBA_TOUCH_BUTTONS)return false;   /* no buttons to introduce */
 FILE *f=fopen(intro_path,"r");if(f){fclose(f);return false;}return true;
}
void gba_touch_intro_shown(void)
{
 mkdir(APP_SD_ROOT "/config",0777);FILE *f=safe_begin(intro_path);
 if(f){fputs("1\n",f);safe_finish(f,intro_path,true);}
}
