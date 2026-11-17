// The letter strip of an A-Z game list (letter_strip.c): one component, each theme gives its place and paint.
#pragma once
#include <stdbool.h>
#include "ui.h"
#include "library_model.h"
typedef struct {
 float x, y, w, h, z;   // the strip's cells (screen or card coordinates, as the caller draws)
 float text_y, size;    // the letters' cell top and text size
 u32 ink, dim;          // a letter with games, without
 bool list;             // the list looks' selection paint (else the Room's ink ring)
 int shown;             // letters in view: all (BB_LETTERS), or a window round the current one
 ui_text_fn text;
} letter_strip_place;
void letter_strip_draw(const bb_library_row *row, int selected, const letter_strip_place *at);
#define LETTER_STRIP_EMPTY (-2)   /* a touch on a letter without games: nothing happens */
int letter_strip_hit(const bb_library_row *row, int selected, const letter_strip_place *at, float x, float y);
