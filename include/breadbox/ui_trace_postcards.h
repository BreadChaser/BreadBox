#pragma once
#if defined(BB_UI_FIXTURES) || defined(BB_UI_TRACE_HOST)
#include "postcards.h"
void ui_trace_postcards(const pc_view *view,void (*bounds)(const pc_view *,int,float *,float *,float *,float *,float *));
#else
#define ui_trace_postcards(...) ((void)0)
#endif
