// The library is the room's bookcase (shelf.c): every system's games on its own shelf.
#include "app_policy.h"
#include "bb_registry.h"
#include "shelf.h"
int breadbox_systems_run(char *path, size_t len) { return shelf_run(path, len); }
// A game of `system` ended: the library comes back on that game's shelf with the game selected
// (Continue and the dev remote start games from anywhere).
void breadbox_return_system(const bb_system_t *system, const char *path) { shelf_played(system, path); }
