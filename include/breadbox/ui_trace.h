/* Menu inspection for development builds; release builds contain no hooks. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#if defined(BB_UI_FIXTURES) || defined(BB_UI_TRACE_HOST)
void ui_trace_begin(const char *screen,int count,int focus,int top);
void ui_trace_row(int index,const char *label,const char *value,int heading);
void ui_trace_target(int id,int x,int y,int width,int height);
void ui_trace_button(uint32_t key,const char *label,int x,int y,int width,int height);
void ui_trace_sound(int event);
void ui_trace_request(int event);
void ui_trace_viewport(int first,int count);
void ui_trace_reset(void);
void ui_trace_dialog(const char *title,const char *text,int page);
void ui_trace_pick(const char *title,int selected,const char *yes,const char *no);
void ui_trace_continue(const char *title,bool failed);
void ui_trace_qr(const char *label);
const char *ui_trace_json(void);
#else
#define ui_trace_continue(...) ((void)0)
#define ui_trace_qr(...) ((void)0)
#define ui_trace_begin(...) ((void)0)
#define ui_trace_row(...) ((void)0)
#define ui_trace_target(...) ((void)0)
#define ui_trace_button(...) ((void)0)
#define ui_trace_viewport(...) ((void)0)
#define ui_trace_reset(...) ((void)0)
#define ui_trace_sound(...) ((void)0)
#define ui_trace_request(...) ((void)0)
#define ui_trace_dialog(...) ((void)0)
#define ui_trace_pick(...) ((void)0)
#endif
