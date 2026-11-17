// Presentation identity; saved IDs remain owned by theme.c.
#pragma once
#include <stdbool.h>

enum { BB_LOOK_ROOM, BB_LOOK_PSX3D, BB_LOOK_SIMPLE, BB_LOOK_COUNT };
typedef struct {
 const char *name, *saved_id;
 bool artwork, display_font;
} bb_look;
const bb_look *theme_look_get(int look);
int theme_look(void);
bool theme_is_room(void);
void theme_select(int theme_index);   /* one of theme_get()'s themes: the Room's lighting or a list look */
void theme_select_look(int look);     /* that look's first theme */
