#ifndef BB_PROFILE_H
#define BB_PROFILE_H
#include "bb_system.h"
#define BB_PROFILE_MAX 32
/* Shared bounded, atomic settings store: game > system > built-in. */
typedef struct {
 const bb_setting_desc *schema; unsigned count;
 int value[BB_PROFILE_MAX], inherited[BB_PROFILE_MAX];
 uint32_t overrides, system_values; unsigned model;
 char system_path[256], game_path[256];
} bb_profile;
void bb_profile_open(bb_profile *,const char *system,const char *section,const char *key,
                     const bb_setting_desc *,unsigned,unsigned model);
bool bb_profile_change(bb_profile *,unsigned,int);
bool bb_profile_clear(bb_profile *,unsigned);
const char *bb_profile_source(const bb_profile *,unsigned);
#endif
