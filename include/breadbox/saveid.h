// Which game a save name belongs to (saveid.c)
#pragma once
#include <stdbool.h>
#include <stddef.h>

// saveid.c - which game a save name belongs to (saves/<name>.id)
// Before loading path: the save name to start with. base: the library's name
// for the game (NULL from the folder browser).
void saveid_begin(const char *path, const char *base, char *name, size_t len);
// After loading, before any save is read or written: the game's own save name,
// given the serial the core read (CdromId); records it. true when it differs
// from saveid_begin's. *created: a new name was just made for this game.
bool saveid_confirm(const char *serial, char *name, size_t len, bool *created);
// saveid_begin: the name is another game's (its settings must not apply
// before saveid_confirm); saveid_confirm: the owner record could not be written
extern bool saveid_provisional, saveid_unrecorded;
