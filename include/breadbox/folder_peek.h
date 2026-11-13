/* A shallow look for games in a folder (the folder picker, for the 3DS's own folders at the SD card's root).
 * GPL-2.0-or-later. */
#ifndef BB_FOLDER_PEEK_H
#define BB_FOLDER_PEEK_H
#include <stdbool.h>
/* The folder itself and one level down, bounded so the picker opens at once even beside a camera's thousands
 * of photos: at most FOLDER_PEEK_TOP names in the folder, FOLDER_PEEK_SUB in each of its first FOLDER_PEEK_DIRS
 * subfolders. True as soon as a name is_game() accepts is a file there. */
#define FOLDER_PEEK_TOP 64
#define FOLDER_PEEK_SUB 32
#define FOLDER_PEEK_DIRS 8
bool folder_has_games(const char *dir, bool (*is_game)(const char *name));
#endif
