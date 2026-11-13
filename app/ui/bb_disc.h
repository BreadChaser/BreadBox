#ifndef BB_DISC_H
#define BB_DISC_H
#include <stdbool.h>
/* Bounded content probe; unreadable, unsupported or malformed CHDs return false. */
bool bb_disc_is_ps1_chd(const char *path);
#endif
