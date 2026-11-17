#pragma once
#include "ui_list.h"
#include "ui.h"
/* The list looks' (PSX3D, Simple) paint for the shared components; the Room draws its own paper. */
typedef struct {
 u32 row_edge,tile_edge,continue_edge,rule[4],icon;
} bb_look_style;
const bb_look_style *look_style(void);
/* Four short color strokes from x, then a quiet line to right (a title's or a section's rule). */
void look_rule(float x,float y,float right,float stroke,float height);
/* The selection: the gradient bar with the colored edge on its left. */
void look_select(float x,float y,float w,float h,float z);
/* A section row: its name over the small rule. */
void look_section(float x,float y,float w,float row_h,const char *label,u8 alpha);
/* The word a bar cell shows (its label; A without one says OK). */
const char *look_bar_label(const ui_button_t *button);
/* The bar's cells in the list themes: each wide enough for its whole word, the rest of the 320 px shared evenly
   (drawing and touch both use these). Returns the cell count. */
int look_bar_cells(const ui_button_t *buttons, const int *order, int count, float *x, float *w);
/* A notice in its own row above the bar, never over the rows. */
void look_notice(const char *text, u32 ink);
/* A bar cell's word under its icon. */
void look_bar_word(const char *label,float cx,float w,u32 ink);
