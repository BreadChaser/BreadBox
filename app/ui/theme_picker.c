#include "ui_trace_views.h"
// Shared theme picker for Settings > Customize.
#include <stddef.h>
#include "theme.h"
#include "ui_style.h"
#include "ui_list.h"
#include "ui.h"
#include "scene.h"
#include "cold_code.h"
static const char *name(int i, void *ud) { (void)ud; return theme_get(i)->name; }

#include "components/theme_picker.inc"
