/* A shallow, bounded look for games in a folder (folder_peek.h). GPL-2.0-or-later. */
#include "folder_peek.h"
#include "cold_code.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
/* files only, at most `max` names; dirs (optional) collects up to FOLDER_PEEK_DIRS subfolder names */
PSX3D_COLD static bool look(const char *dir, int max, bool (*is_game)(const char *), char (*dirs)[256], int *ndirs)
{
	DIR *d = opendir(dir);
	if (!d) return false;
	bool found = false;
	struct dirent *de;
	for (int n = 0; !found && n < max && (de = readdir(d)); n++) {
		if (de->d_name[0] == '.') continue;
		if (de->d_type == DT_DIR) {
			if (dirs && *ndirs < FOLDER_PEEK_DIRS) snprintf(dirs[(*ndirs)++], 256, "%s", de->d_name);
		} else found = is_game(de->d_name);
	}
	closedir(d);
	return found;
}
PSX3D_COLD bool folder_has_games(const char *dir, bool (*is_game)(const char *name))
{
	char dirs[FOLDER_PEEK_DIRS][256], sub[768];
	int ndirs = 0;
	if (look(dir, FOLDER_PEEK_TOP, is_game, dirs, &ndirs)) return true;
	size_t n = strlen(dir);
	for (int i = 0; i < ndirs; i++) {
		snprintf(sub, sizeof(sub), "%s%s%s", dir, n && dir[n - 1] == '/' ? "" : "/", dirs[i]);
		if (look(sub, FOLDER_PEEK_SUB, is_game, NULL, NULL)) return true;
	}
	return false;
}
