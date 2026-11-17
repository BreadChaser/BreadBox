#pragma once
#if defined(BB_UI_FIXTURES) || defined(BB_UI_TRACE_HOST)
#include "list_look.h"
void ui_trace_home(const bb_home_picture *home,const ui_button_t *actions,int count);
void ui_trace_library(const shelf_view *view,const ui_button_t *actions,int count);
void ui_trace_picker(const flist_t *list,int count,int selected,const char *(*name)(int,void *));
void ui_trace_welcome(const char *title,int page,int count);
#else
#define ui_trace_home(...) ((void)0)
#define ui_trace_library(...) ((void)0)
#define ui_trace_picker(...) ((void)0)
#define ui_trace_welcome(...) ((void)0)
#endif
