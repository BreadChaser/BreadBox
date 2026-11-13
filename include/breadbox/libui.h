// The title font and the menus' backdrop (libui.c)
#pragma once
#include <citro2d.h>
#include <stddef.h>

// libui.c - the title font and the menus' top-screen backdrop
void libui_init(void);
C2D_Font libui_font(void);
void libui_exit(void);
// behind menus that draw no top screen of their own
void libui_draw_backdrop(void);
