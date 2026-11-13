// Shared production drawing for Settings > Room > Room style.
#include <stddef.h>
#include "theme.h"
#include "ui_style.h"
#include "ui_list.h"
#include "ui.h"
#include "scene.h"
static const char *name(int i, void *ud) { (void)ud; return theme_get(i)->name; }
void theme_picker_draw(const flist_t *list, int selected, int committed); /* rendered by the shared theme_picker component */

#include "components/theme_picker.inc"
