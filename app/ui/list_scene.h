#ifndef BB_LIST_SCENE_H
#define BB_LIST_SCENE_H
#include <stdbool.h>
/* The PSX3D theme's moving backdrop (top screen). Call only from menu draw callbacks; released before a game. */
bool list_scene_draw(void);
void list_scene_release(void);
#endif
