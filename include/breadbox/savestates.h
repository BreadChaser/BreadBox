// The save state browser with screenshots (savestates.c)
#pragma once
#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>
#include <stddef.h>
#include "history.h"

// savestates.c - save state browser with screenshots
#define STATE_AUTO -1

bool state_thumb_write(const char *prefix, int slot);
u16 *state_thumb_take(void);
bool state_thumb_save(const char *prefix, int slot, const u16 *px);
bool state_thumb_save_at(const char *prefix, int slot, const u16 *px, u32 saved);   // BreadBox: with the time it was taken
void state_thumb_restore(const char *prefix, int slot);
bool state_thumb_save_file(const char *thm_path, const u16 *px);
// "Continue?" at game start
enum { RESUME_NONE, RESUME_START, RESUME_CONTINUE, RESUME_OLDER, RESUME_BACK };   /* BACK: B, or the app closed */
/* title: the game's name for the tag; over_failed: a Start over could not move the old spot (the question says so) */
int states_resume_prompt(const char *prefix, const char *title, bool over_failed);
// The state browser. Loading has pages: save slots, the save backups' ring
// of automatic saves, memory card copies (L/R). Returns the page of the
// pick (STATES_PAGE_SLOTS: the slot is *slot, else pick->entry) or -1.
// pick->page on entry: the page to start on.
enum { STATES_PAGE_SLOTS, STATES_PAGE_RING, STATES_PAGE_CARDS };
typedef struct { int page; hist_entry_t entry; } states_pick_t;
int states_run(bool saving, const char *prefix, const char *title, int *slot, states_pick_t *pick);
// a slot deleted in the browser went into the ring as this entry
typedef struct { bool done; int slot; hist_entry_t entry; } states_deleted_t;
extern states_deleted_t states_deleted;
bool states_auto_read(const char *prefix, u32 *saved, u16 *px, bool *picture, bool (*cancel)(void *), void *arg);
bool states_auto_texture(C3D_Tex *tex, const u16 *px);
bool states_auto_peek(const char *prefix, u32 *saved, C3D_Tex *tex, bool *tex_ok);
void states_thumb_draw(C3D_Tex *t, float x, float y, float w, float h, float z);
