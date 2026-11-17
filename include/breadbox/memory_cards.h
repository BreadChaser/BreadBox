// PS1 memory cards on the SD card, crash-safe (cards.c)
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "history.h"

// cards.c - memory cards on the SD card, crash-safe
void cards_begin(const char *srm_path);   // a game starts: read its cards, settle the pad
bool cards_check(void);                   // write changed cards; true if one was
typedef enum { CARD_WRITE_CLEAN, CARD_WRITE_SAVED, CARD_WRITE_FAILED } card_write_result;
card_write_result cards_finalize(void); /* retains failures in shared final-save owner */
card_write_result cards_save_now(void);                // leaving, sleep, HOME, pause menu
void cards_end(void);
// A card that could not be loaded when the game started (the game then runs
// on an empty stand-in): emu.c asks the player before the game runs.
typedef enum { CARD_TROUBLE_NONE, CARD_TROUBLE_MEMORY, CARD_TROUBLE_DAMAGED } card_trouble_t;
card_trouble_t cards_trouble(char *label, size_t len);   // the first one, its name
bool cards_retry(void);                   // read them again; true: all loaded now
void cards_accept(void);                  // play on (a damaged file stays as .bad)
bool cards_trouble_backup(hist_entry_t *copy);        // its newest good copy in Backups
bool cards_trouble_restore(const hist_entry_t *copy); // put it back; true: all loaded now
// The question before the game runs (card_question.c), until every card is loaded or the
// player chose; false: the player quit, or the system closed the app inside the dialog.
bool cards_question(void);
int card_saves(const void *card, size_t size);   // saves on a PS1 card image
// save backups of the cards (history.h): which group/ext a card's copies use
bool cards_backup_group(int i, char *group, size_t glen, char *ext, size_t elen, const char **label);
bool cards_put_back(const hist_entry_t *copy);   // then restart the game
int cards_saves_now(int i);
