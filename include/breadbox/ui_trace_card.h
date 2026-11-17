#pragma once
#include "ui_pause.h"
#include "ui.h"
#if defined(BB_UI_FIXTURES) || defined(BB_UI_TRACE_HOST)
void ui_trace_card(const flist_t *list,int count,int selected,void (*row)(int,void *,ui_card_row_t *),void *userdata,const ui_card_t *card);
void ui_trace_choices(const char *title,const ui_button_t *tabs,int count,int focus);
void ui_trace_tab(const ui_button_t *button,int index,float x,float width);
void ui_trace_browser(const flist_t *list,int entries,int focus,const char *(*label)(int,void *),const char *(*value)(int,void *));
#else
#define ui_trace_card(...) ((void)0)
#define ui_trace_choices(...) ((void)0)
#define ui_trace_tab(...) ((void)0)
#define ui_trace_browser(...) ((void)0)
#endif
