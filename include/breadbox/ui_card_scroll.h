#pragma once
#include <stddef.h>
#include "ui.h"
#include "ui_list.h"
#include "input_transport.h"
typedef struct { int top, drag_y, drag_top; bool dragging; } ui_card_scroll_t;
u32 ui_card_scroll_input(ui_card_scroll_t *scroll, flist_t *list, int rows, u32 keys, input_contact_t touch);
