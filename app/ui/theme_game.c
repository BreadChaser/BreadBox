// Cold preparation only: no allocation, timers or theme access in game draws.
#include "theme.h"
game_palette_t g_game_palette __attribute__((section(".theme_game.bss")));
void theme_game_begin(void)
{
	const theme_t *t = theme();
	theme_menu_refresh();
	g_game_palette = t->game;   // every room style brings its own gameplay palette
}
