#ifndef BB_CHEATS_H
#define BB_CHEATS_H
#include <stdbool.h>
#define BB_CHEATS_MAX 20
/* RetroArch .cht, with enabled flags persisted in the same file. */
typedef struct {char *name,*code;bool enabled;} bb_cheat;
typedef struct {
 bb_cheat entries[BB_CHEATS_MAX];unsigned count;
 char path[512];
 void (*reset)(void);void (*set)(unsigned,bool,const char *);
} bb_cheats;
void bb_cheats_open(bb_cheats *,const char *,const char *,void (*)(void),void (*)(unsigned,bool,const char *));
void bb_cheats_close(bb_cheats *);
bool bb_cheats_toggle(bb_cheats *,unsigned);
bool bb_cheats_add(bb_cheats *,const char *,const char *);
void bb_cheats_apply(bb_cheats *);
#endif
