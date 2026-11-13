#pragma once
/* PS1 mapping and touch policy. Public compatibility entry points remain in app_input.h. */
#include "control_model.h"
bool ps1_touch_descriptor(unsigned field,bool new_model,control_descriptor_t *out);

void control_bindings_snapshot(void);
