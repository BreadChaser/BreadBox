#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Stable storage keys and opaque adapter values; Settings never interprets them. */
typedef struct { int value; const char *label; } control_binding_t;
typedef struct {
 const char *key, *label;
 int value, inherited;
 bool visible, overridden;
 const control_binding_t *bindings;
 unsigned binding_count;
} control_descriptor_t;
typedef enum { PLAY_ACTION_NONE, PLAY_ACTION_MENU, PLAY_ACTION_FF_TOGGLE,
 PLAY_ACTION_FF_HOLD, PLAY_ACTION_REWIND_HOLD, PLAY_ACTION_STATS } play_action_t;
typedef struct { const char *system; unsigned fields;
 bool (*describe)(unsigned field,bool new_model,control_descriptor_t *out);
} control_schema_t;
extern const control_schema_t control_schemas[];
extern const unsigned control_schema_count;
int control_side(void);
void control_side_set(int left);
/* Legacy configuration bridge, menu-time only. */
int control_inherited_binding(unsigned index);

void control_bindings_snapshot(void);

bool control_describe(const char *system,unsigned field,bool new_model,control_descriptor_t *out);
