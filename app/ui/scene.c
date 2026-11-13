// Menu backdrops. Every theme is a room style, so a menu's backdrop is the room's
// (shelf_scene_backdrop); a game's pause dialogs keep the plain page colour.
// Call only from menu draw callbacks.
#include <citro2d.h>
#include "scene.h"
#include "theme.h"
#include "app_config.h"
#include "shelf.h"

void scene_draw(bool is_bottom)
{
	if (config_in_game()) {   // pause dialogs retain the game's picture
		C2D_DrawRectSolid(0, 0, .1f, is_bottom ? 320 : 400, 240, theme()->surface);
		return;
	}
	shelf_scene_backdrop(is_bottom);
}
