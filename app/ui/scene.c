// Menu backdrops follow the selected look. Pause dialogs keep the plain page color.
// Call only from menu draw callbacks.
#include <citro2d.h>
#include "scene.h"
#include "theme.h"
#include "app_config.h"
#include "shelf.h"
#include "list_look.h"

void scene_draw(bool is_bottom)
{
	if (config_in_game()) {   // pause dialogs retain the game's picture
		C2D_DrawRectSolid(0, 0, .1f, is_bottom ? 320 : 400, 240, theme()->surface);
		return;
	}
 if(theme_is_room())shelf_scene_backdrop(is_bottom);
 else list_look_backdrop(is_bottom);
}
