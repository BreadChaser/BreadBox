// One list presentation, with artwork/font/palette supplied by the look.
#pragma once
#include "ui.h"
#include "shelf.h"
#include "home_model.h"
#include "ui_list.h"

typedef struct {
 int action;
 bool have_continue, at_place;
 C3D_Tex *picture;
 int picture_band;
 const char *title, *played, *continue_title, *continue_when, *games;
 int cover[3], covers;
} bb_home_picture;

void list_look_release(void);
void list_look_backdrop(bool bottom);
void list_look_home_top(const bb_home_picture *home);
void list_look_home_bottom(const bb_home_picture *home);
int list_look_home_hit(const bb_home_picture *home, int x, int y);
enum { LIST_UP, LIST_DOWN, LIST_LEFT, LIST_RIGHT };
int list_look_home_move(const bb_home_picture *home, int direction);
void list_look_library_top(const shelf_view *view);
void list_look_library_bottom(const shelf_view *view, const ui_button_t *actions, int count);
int list_look_library_hit(const shelf_view *view, int x, int y);
bool list_look_library_in_rows(const shelf_view *view, int y);

#if defined(BB_UI_FIXTURES) || defined(BB_UI_TRACE_HOST)
void list_look_library_targets(const shelf_view *v);
void list_look_home_bounds(const bb_home_picture *h,int i,int rect[4]);
#endif
