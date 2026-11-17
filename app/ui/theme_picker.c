#include "ui_trace_views.h"
// Shared appearance picker for Settings > Customize.
#include <stddef.h>
#include "theme.h"
#include "ui_style.h"
#include "ui_list.h"
#include "ui.h"
#include "scene.h"
#include "cold_code.h"
static const char *name(int i, void *ud) { return ud ? theme_look_get(i)->name : theme_get(i)->name; }
void theme_picker_draw(const flist_t *list, int selected, int committed); /* rendered by the shared theme_picker component */

#include "components/theme_picker.inc"

PSX3D_COLD void theme_picker_draw(const flist_t *list,int selected,int committed)
{
 theme_picker_paint(list,selected,committed,false,BB_ROOM_STYLE_COUNT);
}

PSX3D_COLD void theme_picker_draw_look(const flist_t *list,int selected,int committed,bool look)
{
 theme_picker_paint(list,selected,committed,look,look ? BB_LOOK_COUNT : BB_ROOM_STYLE_COUNT);
}
