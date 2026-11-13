// Core option definitions and lookup (options.c)
#pragma once
#include <stdbool.h>
#include "libretro.h"

// options.c - core option definitions (from SET_VARIABLES) and lookup
void options_register(const struct retro_variable *vars);
const char *options_get(const char *key);
bool options_take_updated(void);
void options_mark_updated(void);
int  options_count(void);
void options_register_v1(const struct retro_core_option_definition *defs);
// The core's option list without a game (it registers through retro_set_environment), and
// whether a key/value is one of them (the PSX3D settings import)
void options_core_list(void);
bool options_supported(const char *key, const char *value);
const char *options_info(int i);
const char *options_impact(int i);
void options_fast_forward(bool on);
const char *options_key(int i);
int options_index(const char *key);
const char *options_game_key(const char *key);
const char *options_desc(int i);
const char *options_value(int i);
const char *options_cycle(int i, int dir);
