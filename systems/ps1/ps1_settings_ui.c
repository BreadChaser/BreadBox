/* PlayStation Settings B model and its binding-card child. No app navigator. */
#include <stdio.h>
#include "ui_format.h"
#include <string.h>
#include "app_paths.h"
#include "app_config.h"
#include "settings_ui.h"
#include "core_options.h"
#include "platform_video.h"
#include "rewind.h"
#include "gamecfg.h"
#include "audio.h"
#include "app_input.h"
#include "presets.h"
#include "ui_list.h"
#include "ui_pause.h"
#include "ui.h"
#include "uisound.h"
#include "app_main.h"
#include "devtools.h"
#include "control_model.h"
#include "bb_settings.h"
#include "bb_feature_ui.h"
#include "ps1_settings.h"
#include "statsview.h"
#include "widescreen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool resume_requested,prelaunch_settings;
enum {CAT_HOME,CAT_DISPLAY,CAT_CONTROLS,CAT_SAVES,CAT_ADVANCED,CAT_RESET,CAT_CHEATS,CAT_BINDINGS,CAT_DEBUG,CAT_COUNT};
enum {F_SCREEN,F_SMOOTH,F_FPS,F_CONTROLLER,F_SIDE /* in Settings > Customize */,F_REWIND,F_RECOMMENDED=7,F_RECORD,F_TONE,F_STEREO=12,F_COUNT};
static const char *const categories[]={"","Picture","Controls","Saves","Speed","","Cheats","Button mappings","Troubleshooting"};
static const char *const labels[]={
 [F_SCREEN]="Screen",[F_SMOOTH]="Smoothing",[F_FPS]="FPS counter",[F_CONTROLLER]="Controller",
 [F_REWIND]="Rewind",[F_RECOMMENDED]="Reset to recommended",
 [F_RECORD]="Record audio",[F_TONE]="Test tone",[F_STEREO]="3D"};
static const char *const category_tips[]={[CAT_BINDINGS]="Change physical buttons, touch buttons and hotkeys.",
 [CAT_CHEATS]="Turn this game's cheat codes on or off."};
static const char *const helps[]={
 [F_SCREEN]="Original keeps TV proportions. Stretch fills the screen. Widescreen shows a wider view in games that have a widescreen fix; other games use Original.",
 [F_SMOOTH]="Soften enlarged pixels. Turn off for a sharper picture.",
 [F_FPS]="Show game speed and frame rate. Detailed adds more numbers. Tap the counter to switch views.",
 [F_CONTROLLER]="If the controls don't respond after loading an older save state, switch Controller to Analog and back to Digital once.\n\nDigital lets games switch to analog. Choose Analog for games that don't switch. The shortcut still works.",
 [F_REWIND]="Hold Rewind to go back. Needs free memory.",
 [F_RECOMMENDED]="Remove this game's changes to the Advanced options, so its tested settings apply again. Display, Controls and Saves are kept.",
 [F_RECORD]="Record 30 seconds of game audio for troubleshooting.",
 [F_TONE]="Play a steady tone to check audio output.",
 [F_STEREO]="Use the 3D slider to turn 3D on and set its strength. Screen depth puts the whole picture behind the screen. Real 3D gives every object its own depth; it is experimental and can be slower in busy games.",
};
static const char *const advanced_tested = "This game has tested settings. Changing them can slow it down or break it.";
static const char *const advanced_untested = "This game hasn't been tested yet; these are the standard settings.";
static const char *const advanced_library = "Games with tested settings keep their tested values for these options.";
// The technical core options of the Advanced page, in menu order.
static const char *const advanced_keys[] = {
	"pcsx_rearmed_frameskip_type", "pcsx_rearmed_psxclock", NULL, // NULL: the Compatibility heading
	"pcsx_rearmed_alt_flip", "pcsx_rearmed_gpu_slow_llists", "pcsx_rearmed_cd_turbo",
	"pcsx_rearmed_nostalls", "pcsx_rearmed_nolddelay", "pcsx_rearmed_gteregsunneeded",
	"pcsx_rearmed_nogteflags", "pcsx_rearmed_nosmccheck", "pcsx_rearmed_icache_emulation",
	"pcsx_rearmed_exception_emulation", "pcsx_rearmed_nocompathacks",
};
#define ADVANCED_KEYS (int)(sizeof(advanced_keys) / sizeof(advanced_keys[0]))
#define CORE_ROW 1000
#define CONTROL_ROW 2000
#define CHEAT_ROW 3000
#define CATEGORY_ROW 4000
#define MAX_ROWS 256
typedef struct {bool game;int page,selected[CAT_COUNT],items[MAX_ROWS],count;flist_t list;} settings_ui_t;
static bool frontend_visible(settings_ui_t *s,int i)
{
 if(i==F_STEREO && !ps1_3d_available())return false;
 if(i==F_REWIND && (!s->game || !g_new3ds))return false;
 if(i==F_RECOMMENDED && (!s->game || !game_preset_tested()))return false;
 if((i==F_RECORD || i==F_TONE) && (!s->game || prelaunch_settings || !(g_cfg.debug || settings_ui_debug_capture())))return false;
 return true;
}
static void add(settings_ui_t *s, int item)
{
	if (s->count < MAX_ROWS)
		s->items[s->count++] = item;
}
static void add_frontend(settings_ui_t *s, int item)
{
	if (frontend_visible(s, item))
		add(s, item);
}
static void add_core(settings_ui_t *s, const char *key)
{
	int i = options_index(key);
	if (i >= 0)
		add(s, CORE_ROW + i);
}
static int shared_hotkey_row(int item)
{
 if(item<CONTROL_ROW || item>=CHEAT_ROW)return -1;
 const char *key=input_control_key(item-CONTROL_ROW);
 for(int h=0;h<INPUT_HOTKEY_N;h++)if(key && !strcmp(key,input_hotkey_key_name(h)))return h;
 return -1;
}
static void build_page(settings_ui_t *s,int page)
{
 s->count=0;options_count();
 switch(page) {
 case CAT_DISPLAY:
  add_frontend(s,F_SCREEN);add_frontend(s,F_SMOOTH);add_frontend(s,F_FPS);add_frontend(s,F_STEREO);break;
 case CAT_CONTROLS:
  add(s,CATEGORY_ROW+CAT_BINDINGS);add_frontend(s,F_CONTROLLER);
  add_core(s,"pcsx_rearmed_analog_combo");add_core(s,"pcsx_rearmed_analog_axis_modifier");break;
 case CAT_SAVES:
  add_core(s,"pcsx_rearmed_memcard1");add_core(s,"pcsx_rearmed_memcard2");add_frontend(s,F_REWIND);break;
 case CAT_ADVANCED:
  for(int i=0;i<ADVANCED_KEYS;i++)if(advanced_keys[i])add_core(s,advanced_keys[i]);
  add_frontend(s,F_RECOMMENDED);break;
 case CAT_DEBUG:
  add_frontend(s,F_RECORD);add_frontend(s,F_TONE);add_core(s,"pcsx_rearmed_display_info");break;
 case CAT_BINDINGS:
  for(int i=0;i<input_controls_count();i++) {   /* the shared FF / Rewind keys live in Settings > Customize */
   int slot=input_control_slot(i);if((!g_new3ds && (slot==6 || slot==7)) || shared_hotkey_row(CONTROL_ROW+i)>=0)continue;
   add(s,CONTROL_ROW+i);
  }break;
 }
}
static const char *override_key(int item)
{
	static const char *const keys[] = {
	 [F_SCREEN]="screen", [F_SMOOTH]="smooth", [F_FPS]="show_fps",
	 [F_CONTROLLER]="controller", [F_REWIND]="rewind",
	 [F_STEREO]="stereo_style"};
	if (item >= CORE_ROW && item < CONTROL_ROW)
		return options_game_key(options_key(item - CORE_ROW));
	return item >= 0 && item < F_COUNT && item < (int)(sizeof(keys) / sizeof(keys[0])) ? keys[item] : NULL;
}
static bool overridden(settings_ui_t *s, int item)
{
	if (!s->game)
		return false;
	const char *key = override_key(item);
	return key && config_game_overridden(key);
}
static const char *preset_key(int item)
{
	if (item >= CORE_ROW && item < CONTROL_ROW) {
		const char *key = options_key(item - CORE_ROW);
		if (!strcmp(key, "pcsx_rearmed_frameskip_type")) return "frameskip";
	#ifdef PSX3D_PS1_OVERCLOCK_PRESETS
		if (!strcmp(key, "pcsx_rearmed_psxclock")) return "psxclock";
#endif
		// Technical options keep the preset key name (core key minus prefix).
		return game_preset_core_value(key) ? key + 13 : "";
	}
	if (item == F_CONTROLLER) return "controller";
	if (item >= CONTROL_ROW && item < CHEAT_ROW) {
		const char *key = input_control_key(item - CONTROL_ROW);
		int slot = input_control_slot(item - CONTROL_ROW) - INPUT_PHYSICAL_N;
		bool stick = key && !strcmp(key, "touch_stick");
		bool touch_mode = game_preset_sets("touch_controls");
		if ((stick || (slot >= 0 && slot < 6)) && touch_mode &&
		    game_capabilities().touch_controls != TOUCH_CONTROLS_AUTO)
			return "touch_controls";
		if (stick) return game_preset_sets("stick") ? "stick" : touch_mode ? "touch_controls" : "stick";
		static const char *const keys[] = {"touch_l2", "touch_r2", "touch_l3", "touch_r3", "touch_x1", "touch_x2"};
		if (slot >= 0 && slot < 6) {
			if (game_preset_sets(keys[slot])) return keys[slot];
			const char *need = slot < 2 ? "l2r2" : slot < 4 ? "l3r3" : slot == 4 ? "fast_forward" : "rewind";
			return game_preset_sets(need) ? need : touch_mode ? "touch_controls" : need;
		}
	}
	return "";
}
static bool preset_row(settings_ui_t *s, int item)
{
	const char *key = preset_key(item);
	if (!s->game || !game_preset_sets(key)) return false;
	// Derived touch defaults follow the effective capability too. A user's
	// capability override is not evidence that the preset supplied the binding.
	if (!strcmp(key, "rewind") && config_rewind_on()) return false;
	if (overridden(s, item)) return false;
	if (item >= CONTROL_ROW && item < CHEAT_ROW) {
		int slot = input_control_slot(item - CONTROL_ROW);
		if (slot >= 0 && input_map_raw(slot) != INPUT_BIND_PRESET) return false;
		const char *key = input_control_key(item - CONTROL_ROW);
		if (key && !strcmp(key, "touch_stick") && g_cfg.touch_stick >= 0) return false;
	}
	return true;
}
static const char *screen_note(void)
{
	int policy = ws_console_policy();
	if (config_ws_fix())
		return policy == WS_TOO_SLOW ? g_new3ds ? "Widescreen lowers the speed a lot on New 3DS."
				: "Widescreen lowers the speed a lot on Old 3DS."
			: policy != WS_COSTLY ? NULL
			: g_new3ds ? "Widescreen can lower the speed on New 3DS."
			: "Widescreen can lower the speed on Old 3DS.";
	return "This game has no widescreen fix.";
}
static void sync_picture(void){config_ws_sync();}
static const char *row_value(int item)
{
 if(item>=CATEGORY_ROW)return ">";
 if(item>=CORE_ROW)return options_value(item-CORE_ROW);
 switch(item) {
 case F_CONTROLLER:return config_controller()?"Analog":"Digital";
 case F_SCREEN:return (const char *const[]){"Original","Stretch","Widescreen"}[config_screen_mode()];
 case F_SMOOTH:return g_cfg.smooth?"On":"Off";
 case F_FPS:return (const char *const[]){"Off","FPS","Detailed"}[g_cfg.show_fps];
 case F_STEREO:return g_cfg.stereo_screen?"Screen depth":"Real 3D (experimental)";
 case F_REWIND:return config_rewind_on()?(prelaunch_settings || rewind_active()?"On":"Unavailable"):"Off";
 default:return ">";
 }
}
static void change(settings_ui_t *s,int item,int dir)
{
 if(item>=CORE_ROW){options_cycle(item-CORE_ROW,dir);return;}
 if(!s->game && override_key(item))config_user_changed(override_key(item));
 switch(item) {
 case F_CONTROLLER:
  if(s->game)gamecfg_set_int("controller",!config_controller());else g_cfg.controller^=1;
  if(!prelaunch_settings)input_controller_apply(true);break;
 case F_RECOMMENDED:
  if(ui_confirm_action("Reset to recommended?","Remove this game's changes to the Advanced options. Its tested settings apply again; Display, Controls and Saves are kept.","A  Reset to recommended",s->game && !prelaunch_settings)) {
   const char *keys[ADVANCED_KEYS];int n=0;
   for(int i=0;i<ADVANCED_KEYS;i++)if(advanced_keys[i])keys[n++]=options_game_key(advanced_keys[i])?strdup(options_game_key(advanced_keys[i])):NULL;
   config_game_reset_advanced(keys,n);for(int i=0;i<n;i++)free((void *)keys[i]);
  }break;
 case F_SCREEN:
  if(s->game){int n=config_ws_fix()?3:2;config_game_set_screen((config_screen_mode()+dir+n)%n);sync_picture();}
  else {config_user_changed("stretch");config_user_changed("core.pcsx_rearmed_psx3d_widescreen");config_set_screen_mode((config_screen_mode()+dir+3)%3);}break;
 case F_SMOOTH:g_cfg.smooth^=1;if(s->game)gamecfg_set_int("smooth",g_cfg.smooth);break;
 case F_FPS:
  if(s->game && !prelaunch_settings){statsview_cycle();if(dir<0)statsview_cycle();}
  else g_cfg.show_fps=(g_cfg.show_fps+dir+3)%3;
  if(s->game)gamecfg_set_int("show_fps",g_cfg.show_fps);break;
 case F_STEREO:g_cfg.stereo_screen^=1;if(s->game)gamecfg_set_int("stereo_style",g_cfg.stereo_screen);config_sync_stereo();break;
 case F_REWIND:
  gamecfg_set_int("rewind",!config_rewind_on());options_mark_updated();
  if(!prelaunch_settings){if(config_rewind_on())rewind_start();else rewind_stop();}break;
 case F_RECORD:if(!prelaunch_settings){audio_capture_start();resume_requested=true;}break;
 case F_TONE:if(!prelaunch_settings){audio_test_tone_toggle();resume_requested=true;}break;
 }
}
static void binding_top(void *);static void binding_bottom(void *);
static int binding_bar(settings_ui_t *,ui_button_t *);
static void run_bindings(bool game)
{
 settings_ui_t s={.game=game,.page=CAT_BINDINGS};build_page(&s,CAT_BINDINGS);ui_swap_enter();
 while(aptMainLoop()) {
  if(dev_tick(game && !prelaunch_settings) || dev_ui_pending())break;
  input_poll();ui_button_t bar[UI_BAR_MAX];int nbar=binding_bar(&s,bar);
  u32 k=ui_card_input(&s.list,s.count,&s.selected[CAT_BINDINGS],ui_bar_input(bar,nbar,input_keys_down()));
  k=ui_swap_keys(k,UI_SWAP_MENU);if(k&KEY_B)break;
  int row=s.items[s.selected[CAT_BINDINGS]]-CONTROL_ROW;
  if(k&KEY_Y) {
   if(ui_confirm("Reset controls?","Restore button and touch mappings in this scope? Shared hotkeys, button side (All systems), saves and cheats are kept.",game && !prelaunch_settings))ps1_settings_controls_reset();
  } else if(k&KEY_X) {
   if(game){config_control_clear(row);sfx_play(SFX_VALUE);}
  } else if(k&(KEY_A|KEY_LEFT|KEY_RIGHT)) {
   if(!game)config_user_changed(input_control_key(row));
   input_control_cycle(row,k&KEY_LEFT?-1:1);config_controls_save();sfx_play(SFX_VALUE);
  }
  if(!aptMainLoop())break;
  ui_card_update(&s.list,s.selected[CAT_BINDINGS],s.count);
  if(game && !prelaunch_settings)ui_pause_frame();else music_update();
  ui_begin();video_present_vsync(game && !prelaunch_settings,binding_top,binding_bottom,&s);
 }
 ui_swap_enter();
}
#include "components/settings_bindings.inc"
static bool ps1_setting_changed(const char *key)
{
 if(key && !strcmp(key,"screen"))return config_setting_changed("stretch") || config_setting_changed("core.pcsx_rearmed_psx3d_widescreen");
 return config_setting_changed(key);
}
static void ps1_setting_default(const char *key)
{
 if(!key || config_in_game())return;
 if(!strncmp(key,"core.",5))config_core_default(key+5);
 else if(!strcmp(key,"screen")) {
  config_set_screen_mode(config_factory_value("screen"));
  config_user_forget("stretch");config_user_forget("core.pcsx_rearmed_psx3d_widescreen");
 } else if(!strcmp(key,"smooth"))g_cfg.smooth=config_factory_value(key);
 else if(!strcmp(key,"show_fps"))g_cfg.show_fps=config_factory_value(key);
 else if(!strcmp(key,"controller"))g_cfg.controller=config_factory_value(key);
 else if(!strcmp(key,"stereo_style")){g_cfg.stereo_screen=config_factory_value(key);config_sync_stereo();}
 config_user_forget(key);config_save();
}
static settings_ui_t model;
static bb_settings_context model_context;
static int model_items[MAX_ROWS],model_pages[MAX_ROWS];
static unsigned model_count;
static const char *model_sections[]={"","Picture","Controls","Saves","Speed","", "Cheats","Controls","Troubleshooting"};
static void model_build(void)
{
 model_count=0;
 static const int pages[]={CAT_DISPLAY,CAT_CONTROLS,CAT_SAVES,CAT_ADVANCED,CAT_DEBUG};
 for(unsigned p=0;p<sizeof(pages)/sizeof(*pages);p++) {
  if(pages[p]==CAT_DEBUG && !(g_cfg.debug || settings_ui_debug_capture()))continue;
  build_page(&model,pages[p]);
  for(int i=0;i<model.count && model_count<MAX_ROWS;i++) {
   int item=model.items[i];
   model_items[model_count]=item;model_pages[model_count++]=pages[p];
  }
 }
 /* a running game's Cheats are on its pause card; the shelf's game Settings keep them */
 if(model.game && !model_context.running && model_count<MAX_ROWS){model_items[model_count]=CATEGORY_ROW+CAT_CHEATS;model_pages[model_count++]=CAT_CHEATS;}
}
static bool model_open(const bb_settings_context *context)
{
 resume_requested=false;model_context=*context;model=(settings_ui_t){.game=context->game};prelaunch_settings=context->game && !context->running;
 if(context->game && !context->running) {
  char name[PATH_LEN];snprintf(name,sizeof(name),"%s",context->key && *context->key?context->key:context->title);
  char serial[10];extern bool ps1_settings_identity(const char *,const char *,char *,size_t,char [10]);
  if(!ps1_settings_identity(context->path,context->key,name,sizeof(name),serial)){return false;}
  extern void ps1_settings_cheats_begin(const char *);
  config_game_begin(name);ps1_settings_cheats_begin(name);
  if(serial[0]) {char line[64];snprintf(line,sizeof(line),"CD-ROM ID: %s\n",serial);game_presets_disc_log(line);}
  /* Only serial-wide bundles can be resolved without executing the disc. */
  if(serial[0])ws_load("romfs:/widescreen",serial,0,game_capabilities().widescreen);
  config_ws_sync();

 }
 model_build();return true;
}
static unsigned model_rows(void){model_build();return model_count;}
/* Complete tag copy; full explanations remain in the Help card. */
static const char *model_short_help(int item)
{
 if(item==CATEGORY_ROW+CAT_BINDINGS)return "Map buttons and touch controls.";
 if(item==CATEGORY_ROW+CAT_CHEATS)return "Choose this game's cheats.";
 if(item>=CORE_ROW && item<CONTROL_ROW) {
  static const struct {const char *key,*text;} copy[]={
  {"pcsx_rearmed_frameskip_type","Trade pictures for game speed."},
  {"pcsx_rearmed_psxclock","Set the emulated CPU speed."},
  {"pcsx_rearmed_alt_flip","Change frame timing."},
  {"pcsx_rearmed_gpu_slow_llists","Pace long display lists."},
  {"pcsx_rearmed_cd_turbo","Read game discs faster."},
  {"pcsx_rearmed_nostalls","Skip CPU load stalls."},
  {"pcsx_rearmed_nolddelay","Skip CPU load delays."},
  {"pcsx_rearmed_gteregsunneeded","Skip unused geometry writes."},
  {"pcsx_rearmed_nogteflags","Skip geometry status flags."},
  {"pcsx_rearmed_nosmccheck","Skip code-write checks."},
  {"pcsx_rearmed_icache_emulation","Emulate the CPU cache."},
  {"pcsx_rearmed_exception_emulation","Emulate CPU exceptions."},
  {"pcsx_rearmed_nocompathacks","Disable compatibility fixes."},
  {"pcsx_rearmed_analog_combo","Choose the analog shortcut."},
  {"pcsx_rearmed_analog_axis_modifier","Set analog-stick sensitivity."},
  {"pcsx_rearmed_memcard1","Choose memory card one."},
  {"pcsx_rearmed_memcard2","Choose memory card two."},
  {"pcsx_rearmed_display_info","Show diagnostic information."},
  };
  const char *key=options_key(item-CORE_ROW);
  for(unsigned i=0;i<sizeof(copy)/sizeof(copy[0]);i++)if(!strcmp(key,copy[i].key))return copy[i].text;
  return NULL;
 }
 static const char *const copy[]={
 [F_SCREEN]="Choose the picture's shape.",[F_SMOOTH]="Soften enlarged pixels.",
 [F_FPS]="Show game speed and frame rate.",[F_CONTROLLER]="Choose the controller mode.",
 [F_REWIND]="Hold Rewind to go back.",
 [F_RECOMMENDED]="Restore tested game settings.",[F_RECORD]="Record a short audio sample.",
 [F_TONE]="Play a steady test tone.",[F_STEREO]="Choose picture depth and 3D."
 };
 return item>=0 && item<F_COUNT?copy[item]:NULL;
}
static bool model_read(unsigned index,bb_settings_row *out)
{
 if(index>=model_count)return false;
 int item=model_items[index],page=model_pages[index];
 model.page=page;
 const char *section=model_sections[page];
 if(page==CAT_ADVANCED && item>=CORE_ROW && item<CONTROL_ROW) {
  const char *key=options_key(item-CORE_ROW);
  section=(!strcmp(key,advanced_keys[0]) || !strcmp(key,advanced_keys[1]))?"Speed":"Compatibility";
 }
 if(item==F_RECOMMENDED)section="Compatibility";
 bool action=item>=CATEGORY_ROW || item==F_RECOMMENDED || item==F_RECORD || item==F_TONE;
 const char *key=override_key(item);char corekey[96];
 if(item>=CORE_ROW && item<CONTROL_ROW){snprintf(corekey,sizeof(corekey),"core.%s",options_key(item-CORE_ROW));key=corekey;}
 bool changed=overridden(&model,item);
 const char *help=item==CATEGORY_ROW+CAT_BINDINGS?input_controls_help():item>=CATEGORY_ROW?category_tips[item-CATEGORY_ROW]:item>=CORE_ROW?options_info(item-CORE_ROW):helps[item];
 const char *impact=item>=CORE_ROW && item<CONTROL_ROW?options_impact(item-CORE_ROW):item==F_SCREEN && model.game?screen_note():NULL;
 static char complete_help[1600];
 const char *warning=page==CAT_ADVANCED?(!model.game?advanced_library:game_preset_tested()?advanced_tested:advanced_untested):"";
 snprintf(complete_help,sizeof(complete_help),"%s%s%s%s%s%s%s",warning,*warning?"\n\n":"",impact?impact:"",impact?"\n\n":"",help?help:"",
  preset_row(&model,item)?"\n\nPreset evidence: ":"",preset_row(&model,item)?game_preset_evidence(preset_key(item)):"");
 help=complete_help;
 *out=(bb_settings_row){.id=item,.section=section,.label=item>=CATEGORY_ROW?categories[item-CATEGORY_ROW]:item>=CORE_ROW?options_desc(item-CORE_ROW):labels[item],
  .value=item==F_REWIND && !model_context.running?(config_rewind_on()?"On":"Off"):row_value(item),.help=help,.short_help=model_short_help(item),.kind=action?BB_ROW_ACTION:BB_ROW_VALUE,
  .source=action?BB_SOURCE_NONE:changed?BB_SOURCE_CHANGED:preset_row(&model,item)?BB_SOURCE_RECOMMENDED:BB_SOURCE_SYSTEM,
  .system_changed=ps1_setting_changed(key),.can_clear=!action,.advanced=page==CAT_ADVANCED || page==CAT_DEBUG};
 return true;
}
static void model_change(int id,int dir)
{
 if(id==CATEGORY_ROW+CAT_BINDINGS)run_bindings(model.game);
 else if(id==CATEGORY_ROW+CAT_CHEATS) {extern const bb_cheat_ops bb_legacy_cheats;bb_cheats_ui(&bb_legacy_cheats,model_context.running);}
 else {
  if(id==F_CONTROLLER && model.game && !model_context.running){gamecfg_set_int("controller",!config_controller());options_mark_updated();}
  else if(id==F_REWIND && !model_context.running){gamecfg_set_int("rewind",!config_rewind_on());options_mark_updated();}
  else change(&model,id,dir);
  config_game_store();
 }
 model_build();
}
static void model_clear(int id)
{
 if(model.game) {if(override_key(id))config_game_clear(override_key(id));}
 else {
  char key[96];const char *k=override_key(id);
  if(id>=CORE_ROW && id<CONTROL_ROW){snprintf(key,sizeof(key),"core.%s",options_key(id-CORE_ROW));k=key;}
  ps1_setting_default(k);
 }
 sync_picture();model_build();
}
void ps1_settings_controls_reset(void)
{
 int side=g_cfg.touch_click_left;
 bool side_intent=config_setting_changed("touch_click_left");
 int hotkeys[INPUT_HOTKEY_N];bool intent[INPUT_HOTKEY_N];char *exceptions[INPUT_HOTKEY_N];
 for(int i=0;i<INPUT_HOTKEY_N;i++) {
  hotkeys[i]=input_hotkey_get(i);intent[i]=config_setting_changed(input_hotkey_key_name(i));
  const char *v=config_in_game()?gamecfg_get(input_hotkey_key_name(i)):NULL;
  exceptions[i]=v?strdup(v):NULL;
 }
 config_controls_defaults();
 for(int i=0;i<INPUT_HOTKEY_N;i++) {
  input_hotkey_set(i,hotkeys[i]);
  if(intent[i])config_user_changed(input_hotkey_key_name(i));
 }
 if(side_intent)config_user_changed("touch_click_left");
 g_cfg.touch_click_left=side;input_touch_reset();config_controls_save();
 /* Saving mappings must not migrate/delete legacy shortcut exceptions. */
 for(int i=0;i<INPUT_HOTKEY_N;i++)if(exceptions[i]) {
  gamecfg_set(input_hotkey_key_name(i),exceptions[i]);free(exceptions[i]);
 }
 config_save();
}
static void model_reset(bb_settings_reset scope)
{
 if(scope==BB_RESET_CONTROLS)ps1_settings_controls_reset();
 else if(scope==BB_RESET_RECOMMENDED)change(&model,F_RECOMMENDED,1);
 else if(model.game){config_game_reset();if(model_context.running)rewind_stop();}
 else config_reset_defaults();
 model_build();
}
static bool model_resume_requested(void){return resume_requested;}
static void model_close(void)
{
 config_game_store();if(model_context.game && !model_context.running){extern void ps1_settings_cheats_end(void);ps1_settings_cheats_end();config_game_end();game_presets_reset();}
 config_save();prelaunch_settings=false;
}
const bb_settings_ops bb_ps1_settings={model_open,model_rows,model_read,model_change,model_clear,model_reset,model_close,model_resume_requested};
