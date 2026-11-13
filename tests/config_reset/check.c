#include <string.h>
#include <stdlib.h>
static int duplicate_fail=-1;
static char *fault_duplicate(const char *s){if(duplicate_fail==0){duplicate_fail=-1;return NULL;}if(duplicate_fail>0)duplicate_fail--;return strdup(s);}
#define strdup fault_duplicate
/* BreadBox: resetting the PlayStation settings resets PlayStation values only.
 * Runs the real config.c / input.c / theme.c against the host harness (tests/app/host/check.c). */
#define main harness_main
#include "../app/host/check.c"
#undef main
#undef strdup
#include "bb_index.h"
// the real discovery's normalizing rule, reduced: a folder named like a system stands for its parent
bool bb_index_root_normalize(const char *path, char *out, size_t cap)
{
 snprintf(out, cap, "%s", path);
 char *slash = strrchr(out, '/');
 if (slash && slash[1] && !strcmp(slash + 1, "gba")) *slash = 0;
 return true;
}
#include "../../app/ui/breadbox_gamesdir.c"
bool ps1_3d_available(void) { return true; }
bool bb_session_active(void) { return false; }
void sfx_play(int e) { (void)e; }
void sfx_now(int e) { (void)e; }
void sfx_default(int e) { (void)e; }
void sfx_cancel(void) {}
void sfx_flush(void) {}
bool sfx_close_said(void) { return false; }

static int other_than(int v, int n) { return (v + 1) % n; }

int main(void)
{
 assert(!mkdir("sdmc:",0700)); assert(!mkdir("sdmc:/3ds",0700)); assert(!mkdir(PSX3D_DIR,0700));
 clear_overrides();config_set_core_override("first","old");
 for(int cut=0;cut<2;cut++){
  duplicate_fail=cut;config_set_core_override("new","value");duplicate_fail=-1;
  assert(novr==1 && !config_core_override("new") && !strcmp(config_core_override("first"),"old"));
  config_save();assert(!contains(PSX3D_CONFIG,"core.new="));
 }
 duplicate_fail=0;config_set_core_override("first","replacement");duplicate_fail=-1;
 assert(novr==1 && !strcmp(config_core_override("first"),"old"));
 config_set_core_override("first","replacement");assert(!strcmp(config_core_override("first"),"replacement"));
 clear_overrides();
 game_presets_init(PSX3D_TEST_PRESETS, PSX3D_DIR "/presets");
 clean_input(false);
 config_reset_defaults();
 const int theme0 = g_cfg.theme, skin0 = g_cfg.hand_skin, glove0 = g_cfg.hand_glove;
 const int hot0 = input_hotkey_get(0), hot1 = input_hotkey_get(1);
 const int show_fps0 = g_cfg.show_fps, smooth0 = g_cfg.smooth, stretch0 = g_cfg.stretch, controller0 = g_cfg.controller;

 /* the player changed app values, the shared hotkeys and PlayStation values */
 g_cfg.theme = other_than(theme0, theme_count());
 g_cfg.hand_skin = other_than(skin0, HAND_SKINS);
 g_cfg.hand_glove = other_than(glove0, HAND_GLOVES);
 g_cfg.sounds = !g_cfg.sounds;
 g_cfg.music = !g_cfg.music;
 input_hotkey_set(0, 5);
 input_hotkey_set(1, 7);
 assert(input_hotkey_get(0) != hot0 && input_hotkey_get(1) != hot1);
 g_cfg.show_fps = 2;
 g_cfg.smooth = !smooth0;
 g_cfg.stretch = !stretch0;
 g_cfg.controller = !controller0;
 g_cfg.touch_stick = 3;
 input_map_set(0, INPUT_TURBO_A);
 config_set_user_core_override("pcsx_rearmed_frameskip_type", "disabled");
 snprintf(g_cfg.games_dir, sizeof g_cfg.games_dir, "sdmc:/roms");
 const psx3d_config before = g_cfg;
 const int hk0 = input_hotkey_get(0), hk1 = input_hotkey_get(1);

 config_reset_defaults();

 /* app-owned values and the shared hotkeys stay */
 assert(g_cfg.theme == before.theme && g_cfg.hand_skin == before.hand_skin && g_cfg.hand_glove == before.hand_glove);
 assert(g_cfg.sounds == before.sounds && g_cfg.music == before.music);
 assert(input_hotkey_get(0) == hk0 && input_hotkey_get(1) == hk1);
 /* the library location stays */
 assert(!strcmp(g_cfg.games_dir, "sdmc:/roms"));
 /* PlayStation values are back to the factory values */
 assert(g_cfg.show_fps == show_fps0 && g_cfg.smooth == smooth0 && g_cfg.stretch == stretch0 && g_cfg.controller == controller0);
 assert(g_cfg.touch_stick == -1 && input_map_raw(0) != INPUT_TURBO_A);
 assert(!config_core_override("pcsx_rearmed_frameskip_type"));

 /* and it is what the next start reads */
 config_save();
 g_cfg.theme = theme0; g_cfg.hand_skin = skin0; g_cfg.hand_glove = glove0;
 input_hotkey_set(0, hot0); input_hotkey_set(1, hot1);
 config_load();
 assert(g_cfg.theme == before.theme && g_cfg.hand_skin == before.hand_skin && g_cfg.hand_glove == before.hand_glove);
 assert(input_hotkey_get(0) == hk0 && input_hotkey_get(1) == hk1);
 assert(g_cfg.show_fps == show_fps0 && g_cfg.smooth == smooth0);
 {   /* the saved games folder against a fallback root (review 11Z): a fallback is for this session only */
  static char saved_line[PATH_LEN + 16];
  config_reset_defaults();
  snprintf(g_cfg.games_dir, sizeof g_cfg.games_dir, "sdmc:/games/custom");   // a custom folder; it cannot be read right now
  bb_games_dir_found("sdmc:/roms");                                           // the start found the common root instead
  assert(!strcmp(g_cfg.games_dir, "sdmc:/roms") && bb_games_dir_session && !strcmp(bb_unreadable_dir, "sdmc:/games/custom"));
  config_save();                                                              // anything that saves the config meanwhile
  snprintf(saved_line, sizeof saved_line, "games_dir=sdmc:/games/custom\n");
  assert(contains(PSX3D_CONFIG, saved_line) && !contains(PSX3D_CONFIG, "games_dir=sdmc:/roms\n"));
  assert(!bb_games_dir_retry());                                              // still unreadable
  assert(!mkdir("sdmc:/games", 0700) && !mkdir("sdmc:/games/custom", 0700));
  assert(bb_games_dir_retry() && !strcmp(g_cfg.games_dir, "sdmc:/games/custom") && !bb_unreadable_dir[0] && !bb_games_dir_session);
  bb_games_dir_found("sdmc:/roms");                                           // a second failure, then the player picks a folder
  bb_games_dir_chosen();
  snprintf(g_cfg.games_dir, sizeof g_cfg.games_dir, "sdmc:/chosen");
  config_save();
  assert(contains(PSX3D_CONFIG, "games_dir=sdmc:/chosen\n") && !bb_unreadable_dir[0]);
  snprintf(g_cfg.games_dir, sizeof g_cfg.games_dir, "sdmc:/roms/gba");        // a system folder: its common root is the same choice, kept
  bb_games_dir_found("sdmc:/roms");
  assert(!strcmp(g_cfg.games_dir, "sdmc:/roms") && !bb_games_dir_session && contains(PSX3D_CONFIG, "games_dir=sdmc:/roms\n"));
  g_cfg.games_dir[0] = 0;                                                     // first run: the common root becomes the choice
  bb_games_dir_found("sdmc:/roms");
  assert(!strcmp(g_cfg.games_dir, "sdmc:/roms") && !bb_games_dir_session);
  snprintf(g_cfg.games_dir, sizeof g_cfg.games_dir, "sdmc:/lost");            // nothing found at all: kept, flagged, saved as is
  bb_games_dir_unreadable();
  config_save();
  assert(bb_games_dir_gone && contains(PSX3D_CONFIG, "games_dir=sdmc:/lost\n"));
  bb_games_dir_chosen();
  assert(!bb_games_dir_gone);
 }
 puts("PASS: PlayStation reset keeps theme, hands, sound and shared hotkeys; resets PlayStation values; saved games folder survives a fallback root");
 return 0;
}
