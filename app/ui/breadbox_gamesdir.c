#include "app_policy.h"
/* The saved games folder against what the start (or a retry) can read. The saved choice is the player's:
 * a fallback root (a common folder found instead) is used for the session only and never written over it,
 * so a card that failed to read for a moment cannot lose a custom folder. The state lives in config.c
 * (config_save writes bb_unreadable_dir while a fallback is in use). */
#include <string.h>
#include <sys/stat.h>
#include "app_paths.h"
#include "app_log.h"
#include "app_config.h"
#include "games_dir.h"
#include "bb_index.h"

#define COLD __attribute__((noinline,section(".psx3d_cold.text")))   /* menus only: the per-frame code keeps its place */

/* a folder was chosen by the player: the saved choice is replaced, nothing is unavailable any more */
COLD void bb_games_dir_chosen(void)
{
 bb_unreadable_dir[0]=0;
 bb_games_dir_session=false;
 bb_games_dir_gone=false;
}

/* the start (or the first-run screen) found `found` while looking for the saved folder */
COLD void bb_games_dir_found(const char *found)
{
 const char *saved=bb_unreadable_dir[0]?bb_unreadable_dir:g_cfg.games_dir;
 char norm[PATH_LEN];
 /* nothing saved yet, or found is the saved folder (a system folder's parent counts): that is the choice */
 if(!saved[0] || (bb_index_root_normalize(saved,norm,sizeof(norm)) && !strcmp(norm,found))) {
  bool changed=strcmp(found,g_cfg.games_dir) || bb_unreadable_dir[0];
  if(changed)log_printf(saved[0]?"library: migrated system folder to common root %s\n":"library: first run, using common games root %s\n",found);
  snprintf(g_cfg.games_dir,sizeof(g_cfg.games_dir),"%s",found);
  bb_games_dir_chosen();
  if(changed)config_save();
  return;
 }
 /* a fallback: this session only, the saved folder stays in the config and is reported unavailable */
 if(!bb_unreadable_dir[0])snprintf(bb_unreadable_dir,sizeof(bb_unreadable_dir),"%s",saved);
 snprintf(g_cfg.games_dir,sizeof(g_cfg.games_dir),"%s",found);
 bb_games_dir_session=true;
 bb_games_dir_gone=false;
 log_printf("library: saved folder %s cannot be read, using %s for this session only\n",bb_unreadable_dir,found);
}

/* the saved folder cannot be read and nothing else was found: keep it, flag it */
COLD void bb_games_dir_unreadable(void)
{
 snprintf(bb_unreadable_dir,sizeof(bb_unreadable_dir),"%s",g_cfg.games_dir);
 bb_games_dir_session=false;
 bb_games_dir_gone=true;
 log_printf("library: games folder %s is not available\n",bb_unreadable_dir);
}

/* Rescan / SELECT: the saved folder reads again, use it */
COLD bool bb_games_dir_retry(void)
{
 struct stat st;
 if(!bb_unreadable_dir[0] || stat(bb_unreadable_dir,&st) || !S_ISDIR(st.st_mode))return false;
 snprintf(g_cfg.games_dir,sizeof(g_cfg.games_dir),"%s",bb_unreadable_dir);
 log_printf("library: saved folder %s reads again\n",g_cfg.games_dir);
 bb_games_dir_chosen();
 return true;
}
