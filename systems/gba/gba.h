#ifndef BREADBOX_GBA_H
#define BREADBOX_GBA_H
#include <stdbool.h>
bool gba_active(void);
bool gba_layer3d_display(void);
bool gba_debug_key(const char *key);

bool gba_debug_state(int slot, bool save);
bool gba_remote_resolve(const char *title, char *path, size_t cap);
int gba_run(const char *path);
// Remote-load slot and 3D (bb_launch slot/stereo, -1 = none), set by the adapter before gba_run.
extern int gba_launch_slot, gba_launch_stereo;
bool gba_continue_available(const char *path, unsigned *saved);
const char *gba_error(void);
bool gba_switch(const char *arg);
const char *gba_autostart(void);
#endif
