// The letter strip of an A-Z game list: a window of letters round the current game's, the current one lit,
// letters without games dimmed; a touch on a letter with games goes to its first game. Every theme draws it
// here; the caller gives the place, the text routine and the inks.
#include <citro2d.h>
#include "letter_strip.h"
#include "look_style.h"
#include "theme.h"
#include "ui_style.h"
#include "cold_code.h"

static PSX3D_COLD int window_first(int current, int shown)
{
 int first=current-shown/2;
 return first<0 ? 0 : first>BB_LETTERS-shown ? BB_LETTERS-shown : first;
}
#include "components/letter_strip.inc"
PSX3D_COLD int letter_strip_hit(const bb_library_row *row, int selected, const letter_strip_place *at, float x, float y)
{
 if(!row || selected<0 || selected>=row->n || y<at->y || y>=at->y+at->h || x<at->x || x>=at->x+at->w)return -1;
 int first[BB_LETTERS];
 bb_library_letters(row,first);
 int cell=(int)((x-at->x)*at->shown/at->w);
 int game=first[window_first(bb_library_letter(row->box[selected].title),at->shown)+cell];
 return game>=0 ? game : LETTER_STRIP_EMPTY;
}
