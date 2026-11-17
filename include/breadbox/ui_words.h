#ifndef UI_WORDS_H
#define UI_WORDS_H
/* The words every system says the same way. Defines only (no includes, no code): the PS1 frame file
 * (systems/ps1/emu.c) includes it without changing a byte of its pinned code. */
/* A saved moment is a postcard everywhere; a slot is only its
 * number. In-game saves stay "memory card" (PS1) and "game save" (GBA). Every system says these the same. */
#define UI_PC_SAVED "Postcard %d saved"
#define UI_PC_SAVED_OVER "Postcard %d saved. The old one is in the backups"
#define UI_PC_LOADED "Postcard %d loaded"
#define UI_PC_SAVE_FAILED "Could not save the postcard. Check the SD card."
#define UI_PC_LOAD_FAILED "Could not load the postcard."
#define UI_PC_ALL_USED "All 12 postcards are used. Pick one to replace."
#define UI_PC_NOTHING_SAVED "Nothing saved yet. Press X to save a postcard."
/* The Continue question (the shared pause component): its words when Start over could not move the old spot */
#define UI_CONTINUE_OVER_FAILED "Could not start over: your spot is kept. Check the SD card."   /* the postcard's red foot, two lines */
#endif
