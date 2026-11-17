// Words with a per-theme term: one table, the Room's word beside the list themes' word (PSX3D v0.5.2 said "state").
#pragma once
#include <string.h>
#include "ui_words.h"
#include <stdbool.h>
#include "cold_code.h"
/* Weak: a program without the themes (host tests of the save code) says the Room's words. */
extern bool theme_is_room(void) __attribute__((weak));
#define UI_TERM_LIST (theme_is_room && !theme_is_room())
enum { UI_TERM_SAVE, UI_TERM_LOAD, UI_TERM_SAVED, UI_TERM_LOADED, UI_TERM_LOADED_BACKUP, UI_TERM_SAVE_FAILED, UI_TERM_LOAD_FAILED,
 UI_TERM_NOTHING_SAVED, UI_TERM_COUNT };
static const char *const ui_terms[UI_TERM_COUNT][2] __attribute__((unused)) = {
 {"Save postcard","Save state"},{"Load postcard","Load state"},
 {UI_PC_SAVED,"State %d saved"},{UI_PC_LOADED,"State %d loaded"},{UI_PC_LOADED " from its backup","State %d loaded from its backup"},
 {UI_PC_SAVE_FAILED,"Could not save the state. Check the SD card."},{UI_PC_LOAD_FAILED,"Could not load the state."},
 {UI_PC_NOTHING_SAVED,"Nothing saved yet. Press X to save a state."},
};
static PSX3D_COLD __attribute__((unused)) const char *ui_term(int term)
{
 return term>=0 && term<UI_TERM_COUNT ? ui_terms[term][UI_TERM_LIST] : "";
}
/* A label as this theme says it: the code keeps the Room's words (the PS1 pause matches its rows by them);
   shared drawing shows the list themes' word in its place. Not cold: the cold code ahead of the GBA save dialogs
   keeps its size (tools/layout). */
static __attribute__((noinline, unused)) const char *ui_term_shown(const char *text)
{
 for(int i=0;text && UI_TERM_LIST && i<UI_TERM_COUNT;i++)if(!strcmp(text,ui_terms[i][0]))return ui_terms[i][1];
 return text;
}
