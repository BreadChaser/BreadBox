#pragma once
#include "ui_list.h"
#include "ui.h"
typedef struct {
 u32 row_edge,tile_edge,continue_edge,rule[4],icon;
} bb_look_style;
const bb_look_style *look_style(void);
void look_list_draw(const flist_t *list,int n,int selected,float x,float y,float width,float row_height,int visible,
 const char *(*label)(int,void *),const char *(*value)(int,void *),void *userdata,bool (*heading)(int,void *));
