#define ADAPTER __attribute__((section(".sys.gba.text")))
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "app_paths.h"
#include "app_config.h"
#include "ui_pause.h"
#include "ui.h"
#include "playlog.h"
#include "app_main.h"
#include "bb_settings.h"
extern const bb_settings_ops bb_gba_settings;
#include "bb_system.h"
#include "gba.h"
#include "gba_saveid.h"
#include "gba_cart.h"
#include "gba_settings.h"
#include "gba_color.h"
#include "gba_picture.h"
#include "gba_boxart.h"
extern const bb_cheat_ops bb_gba_cheats;
extern bool gba_cover_item(int,bb_cover_item *);
#include "bb_index.h"
#include "bb_save.h"
#include "bb_pause.h"
#include "gba_speed.h"
#include <sys/stat.h>
static bool option(const char *);
#ifdef BB_GBA_LUT_PROBE
static int lut_probe;
extern int gba_lut_probe(void);
#endif
bool gba_save_declined;   /* Back at the damaged-save question (gba.c): a quit, not an error */
static ADAPTER bb_run_result run(const bb_launch *l,char *error,size_t cap)
{
 extern void bb_gba_identity_invalidate(void);
 bb_gba_identity_invalidate();
 if(!l->remote)option(NULL);
#ifdef BB_GBA_LUT_PROBE
 if(lut_probe)gba_lut_probe();
#endif
 gba_launch_slot=l->resume?BB_SLOT_CONTINUE:l->slot>=0?bb_slot_file(l->slot):-1;   /* the remote names the card; the file is another number */gba_launch_stereo=l->stereo;
 extern int gba_save_damaged(const bb_save_damage *);
 gba_save_declined=false;bb_save_on_damage(gba_save_damaged);
 gba_picture_begin();
 int result=gba_run(l->path);
 gba_picture_end();
 bb_save_on_damage(NULL);
 bb_gba_identity_invalidate();
 if(result<0 && gba_save_declined)return BB_RUN_QUIT;   /* Back at the damaged-save question */
 if(result<0)snprintf(error,cap,"%s",gba_picture_error()?gba_picture_error():gba_error());
 return result<0?BB_RUN_FAILED:BB_RUN_QUIT;
}
static ADAPTER bool key(const char *name,unsigned frames) {(void)frames;return gba_debug_key(name);}
#include "system_3d.h"
bool gba_3d_available(void) {return system_gba_3d_available(g_3d_screen,g_new3ds,g_cfg.stereo);}
int gba_remote_prof=0,gba_remote_cpuopt=23,gba_remote_journal=0,gba_remote_indexed=0,gba_remote_latch=1;
int gba_launch_slot=-1,gba_launch_stereo=-1;
static bool option(const char *word)
{
 int v;char tail;
#ifdef BB_GBA_LUT_PROBE
 if(!word)lut_probe=0;
 else if(sscanf(word,"lutprobe=%d%c",&v,&tail)==1 && v>=0 && v<=1){lut_probe=v;return true;}
#endif
 if(!word) {gba_remote_prof=0;gba_remote_cpuopt=23;gba_remote_journal=0;gba_remote_indexed=0;gba_remote_latch=1;return true;}
 if(sscanf(word,"prof=%d%c",&v,&tail)==1 && v>=0 && v<=1) {gba_remote_prof=v;return true;}
 if(sscanf(word,"indexed=%d%c",&v,&tail)==1 && v>=0 && v<=2) {gba_remote_indexed=v;return true;}
 if(sscanf(word,"journal=%d%c",&v,&tail)==1 && v>=0 && v<=2) {gba_remote_journal=v;return true;}
 if(sscanf(word,"cpuopt=%d%c",&v,&tail)==1 && v>=0 && v<=31) {gba_remote_cpuopt=v;return true;}
 /* Development A/B only: latch=0 restores unsynchronized mono swaps. */
 if(sscanf(word,"latch=%d%c",&v,&tail)==1 && v>=0 && v<=1) {gba_remote_latch=v;return true;}
 return false;
}
static int status(char *json,size_t cap) {return snprintf(json,cap,",\"cpuopt\":%d,\"prof\":%d,\"journal\":%d,\"indexed\":%d,\"latch\":%d",gba_remote_cpuopt,gba_remote_prof,gba_remote_journal,gba_remote_indexed,gba_remote_latch);}
static const bb_dev_ops dev={.state=gba_debug_state,.key=key,.option=option,.status=status};
extern void gba_library_release(void);
extern bool gba_library_poll(void);
extern bool gba_catalog(int,bb_catalog_item *);
static bool gba_library_poll_adapter(bool refresh){return refresh ? gba_library_poll() : playlog_poll_adoptions();}
static const bb_shell_ops shell={.poll_library=gba_library_poll_adapter,.catalog=gba_catalog,.release_menu=gba_library_release,.count=bb_index_gba_count,
 .continue_path=gba_continue_available,
 .resolve=gba_remote_resolve,.parse_switch=gba_switch,.autostart=gba_autostart};
/* Descriptor rows -> GbaSettings fields. No 3D row: GBA 3D follows the slider. */
static const int fields[]={GBA_SET_SCREEN,GBA_SET_SMOOTH,GBA_SET_COLOR,GBA_SET_BLEND,GBA_SET_SOLAR,GBA_SET_REWIND};
static int setting_value(unsigned row)
{
 const int *values[]={&gba_settings.screen_size,&gba_settings.smoothing,&gba_settings.color_correction,&gba_settings.frame_blend,&gba_settings.solar,&gba_settings.rewind};
 return row<6?*values[row]:0;
}
static void settings_open(const char *key){(void)key;gba_settings_load();}
static const char *setting_source(unsigned i){return gba_settings_source(i<6?fields[i]:GBA_SET_SCREEN);}
static void setting_change(unsigned row,int dir) {if(row<6)gba_settings_set(fields[row],dir);}
static const char *const filters[] __attribute__((section(".gba_picture.const")))={"Sharp","Smooth"};
static const char *const sizes[]={"Original","Fit","Stretch"};
#define TOGGLE(k,l,c) {.key=k,.label=l,.kind=BB_SET_TOGGLE,.category=c,.models=BB_MODEL_ALL,.flags=BB_SET_LIVE}
static const bb_setting_desc settings[]={
 {.key="screen_size",.label="Screen size",.kind=BB_SET_CHOICE,.category=BB_CAT_DISPLAY,.models=BB_MODEL_ALL,.values=sizes,.nvalues=3,.def={1,1}},
 {.key="smoothing",.label="Filter",.kind=BB_SET_CHOICE,.category=BB_CAT_DISPLAY,.models=BB_MODEL_ALL,.flags=BB_SET_LIVE,.values=filters,.nvalues=2},
 {.key="color_correction",.label="Color",.kind=BB_SET_CHOICE,.category=BB_CAT_DISPLAY,.models=BB_MODEL_ALL,.flags=BB_SET_LIVE,.values=gba_color_names,.nvalues=GBA_COLOR_COUNT},
 TOGGLE("frame_blend","LCD ghosting",BB_CAT_DISPLAY),
 /* Boktai's solar sensor: only shown for games that have one */
 {.key="solar",.label="Sunlight",.kind=BB_SET_SLIDER,.category=BB_CAT_CONTROLS,.models=BB_MODEL_ALL,.flags=BB_SET_LIVE|BB_SET_GAME_ONLY,.def={5,5}},
 /* like PS1: New 3DS only (memory and a spare core), on unless turned off */
 {.key="rewind",.label="Rewind",.kind=BB_SET_TOGGLE,.category=BB_CAT_ADVANCED,.models=BB_MODEL_NEW,.flags=BB_SET_LIVE,.def={0,1}}
};
static const char *solar_value(void)
{
 static char out[24];
 snprintf(out,sizeof(out),"%d · %s",gba_settings.solar,gba_settings.solar<4?"Dim":gba_settings.solar<7?"Soft":"Bright");
 return out;
}
static bool solar_change(int dir) {gba_settings_set(GBA_SET_SOLAR,dir);return false;}
static bool solar_visible(void) {return gba_settings_solar_game;}
/* Fast forward and Rewind in the pause card: the hotkeys of the game, and the same thing from the menu. */
static const char *ff_value(void) {return bb_pause_hotkey(0);}
static bool ff_change(int dir) {(void)dir;gba_speed_ff_next();return true;}
static const char *rewind_value(void) {return bb_pause_hotkey(1);}
static bool rewind_visible(void) {return gba_speed_rewind_allowed();}
static bool rewind_enabled(void) {return gba_speed_history_kib()>0;}
static bool rewind_change(int dir)
{
 (void)dir;
 int steps=gba_speed_back(GBA_PAUSE_REWIND_STEPS);
 if(steps>0)return true;
 if(!steps){bb_pause_notice("Nothing to rewind yet. Play a little first.");return false;}
 if(steps==GBA_REWIND_RESTART){bb_pause_reset();bb_pause_notice("Rewind failed: the game was restarted.");return true;}
 bb_pause_notice("Rewind failed: the game goes on from here.");
 return true;
}
static void picture_box(float *box)
{
 float sx=gba_settings.screen_size==0?1.0f:gba_settings.screen_size==2?400.0f/240.0f:1.5f,sy=gba_settings.screen_size==0?1.0f:gba_settings.screen_size==2?240.0f/160.0f:1.5f;
 box[0]=(400-240*sx)/2;box[1]=(240-160*sy)/2;box[2]=240*sx;box[3]=160*sy;
}
static const bb_pause_row pause_rows[]={
 {.label="Fast forward",.value=ff_value,.activate=ff_change,.icon=UI_ICON_FF,.tone=UI_TONE_BLUE},
 {.label="Rewind",.value=rewind_value,.activate=rewind_change,.visible=rewind_visible,.enabled=rewind_enabled,.icon=UI_ICON_REWIND,.tone=UI_TONE_BLUE},
 {.label="Sunlight",.value=solar_value,.activate=solar_change,.visible=solar_visible,.icon=UI_ICON_SUN,.tone=UI_TONE_RED,.arrows=true}};
static bool continue_available(const bb_content_id *id,uint32_t *saved)
{
 char path[PATH_LEN];struct stat st;
 snprintf(path,sizeof(path),APP_SD_ROOT "/states/gba/%s.state10",id->key);
 if(stat(path,&st))return false;
 if(saved)*saved=gba_state_time(path);return true;
}
extern size_t gba_session_state_size(void);
extern bool gba_session_state_save(void *,size_t,size_t *);
extern bb_state_result gba_session_state_load(const void *,size_t,bool);
extern bool gba_session_battery(void **,size_t *);
extern uint32_t gba_session_generation(void);
extern size_t gba_session_rtc_size(void);
extern bool gba_session_rtc_get(void *,size_t),gba_session_rtc_set(const void *,size_t);
static bb_state_result picture_restore(const void *p,size_t n,bool keep)
{gba_picture_reset();return gba_session_state_load(p,n,keep);}
static bool rtc_valid(const void *p,size_t n){uint32_t word=256;if(n==12)memcpy(&word,(const char *)p+8,4);return word<=255;}
/* gpSP keeps every cartridge save in one 128 KiB buffer from offset 0 (EEPROM 512 B / 8 KiB,
 * SRAM 32 KiB, flash 64 KiB); mGBA and others write the chip's own size. Erased chips read 0xFF.
 * The save type gpSP set while loading the cartridge (its game list, then the ROM's library
 * strings) decides which of those sizes may be offered as "Use it" at the damaged-save question
 * (never converted without it); bb_save_begin runs after the load. */
extern uint32_t gpsp_backup_type_reset,gpsp_flash_bank_cnt;
static bool native_size(size_t n){return gba_cart_save_fits(gpsp_backup_type_reset,gpsp_flash_bank_cnt,n);}
static const bb_save_ops save={.state_size=gba_session_state_size,.state_save=gba_session_state_save,
 .state_load=picture_restore,.battery=gba_session_battery,.battery_generation=gba_session_generation,
 .state_magic="BBGBAST1",.rtc_magic="BBGBART1",.rtc_size=gba_session_rtc_size,
 .rtc_get=gba_session_rtc_get,.rtc_set=gba_session_rtc_set,.rtc_valid=rtc_valid,
 .battery_native=native_size,.battery_fill=0xff};
static const char *const extensions[]={".gba",".agb",".zip",NULL};
static const char *const hints[]={"gba","game boy advance","nintendo game boy advance","2001",NULL};
extern unsigned char __bb_gba_bss_start[],__bb_gba_bss_end[];
extern bb_probe_result bb_gba_probe(const bb_probe_input *,bb_entry *);
extern bool bb_gba_identify(const char *,const bb_entry *,bb_content_id *,char *,size_t);
/* The cardboard box on the shelf: 125 x 125 x 23 mm (NA / EU box; scans give 23 mm), purple side band. */
static const bb_package package={.width=125,.height=125,.depth=23,.style=BB_PACKAGE_CARTON,
 .spine_head=0xff7c2d3a,.band=0xff8e3a4a,.band_label="GAME BOY ADVANCE"};
const bb_system_t bb_system_gba={.settings_model=&bb_gba_settings,.controls=true,.control_shoulders=true,.settings_open=settings_open,.setting_source=setting_source,.cheats=&bb_gba_cheats,.abi=BB_SYSTEM_ABI,.id="gba",.name="Game Boy Advance",.short_name="GBA",.release_year=2001,.release_month=3,
 .arena_start=__bb_gba_bss_start,.arena_end=__bb_gba_bss_end,
 .models=BB_MODEL_ALL,.link=BB_LINK_STATIC,.stereo=BB_STEREO_LAYERS,
 .budget={{.heap_kib=6656,.linear_kib=512,.exec_kib=8320},{.heap_kib=6656,.linear_kib=512,.exec_kib=8320}},
 .stereo_available=gba_3d_available,
 .probe_bytes=0,.probe=bb_gba_probe,.identify=bb_gba_identify,
 .settings=settings,.nsettings=sizeof(settings)/sizeof(*settings),.setting_value=setting_value,.setting_change=setting_change,
 .save=&save,.pause_rows=pause_rows,.npause_rows=3,.picture_box=picture_box,.continue_available=continue_available,
 .extensions=extensions,.folder_hints=hints,.run=run,.shell=&shell,.dev=&dev,
 .cover_item=gba_cover_item,.cover_candidates=gba_boxart_candidates,
 .cover_db="Nintendo - Game Boy Advance",.preset_dir="romfs:/presets/gba",.package=&package};

/* Core ABI bridge; all arena ownership and protection belong to the session. */
bool bb_gba_jit(void **rom,void **ram,size_t rom_size,size_t ram_size)
{
 size_t bytes=0;unsigned char *p=bb_session_system()==&bb_system_gba?bb_exec_arena(&bytes):NULL;
 *rom=*ram=NULL;
 if(!p || rom_size>bytes || ram_size>bytes-rom_size)return false;
 *rom=p;*ram=p+rom_size;return true;
}
