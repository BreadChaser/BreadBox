// Spring-animated lists and the list screens' drawing (ui.c)
#pragma once
#include <3ds.h>
#include <math.h>
#include <stdbool.h>

#define FLIST_MAX 2048
typedef struct {
	bool init;
	float hl, hl_v;               // highlighted row (fractional), spring
	float scroll, scroll_v;       // first visible row (fractional), spring
	float scroll_target;
	float lift[FLIST_MAX];        // 0..1, eases toward 1 for the selected row
	u32 motion_tick;             // wall-time milliseconds; reuses the old idle timer word
} flist_t;
void flist_update(flist_t *f, int sel, int n, int rows);
void flist_draw(const flist_t *f, int n, int sel, float x, float y0, float w, float row_h, int rows,
	const char *(*label)(int, void *), const char *(*value)(int, void *), void *ud, u32 accent);
void flist_draw_sections(const flist_t *f, int n, int sel, float x, float y0, float w, float row_h, int rows,
	const char *(*label)(int, void *), const char *(*value)(int, void *), void *ud, u32 accent,
	bool (*header)(int, void *));
int flist_row_at(const flist_t *f, float y0, float row_h, float py);
void flist_snap(flist_t *f, int sel, int n, int rows);
void ui_list_draw(const flist_t *f, int n, int sel,
 const char *(*label)(int, void *), const char *(*value)(int, void *), void *ud,
 u32 accent, bool (*heading)(int, void *));
// Handles repeated directions, clamped selection and select-then-open touch.
// A negative count only applies repeat (caller owns section/letter navigation).
u32 ui_list_input(const flist_t *f, int n, int *sel, u32 keys, bool horizontal);
void ui_list_context(const char *title, const char *right);

void gba_settings_list_draw(const flist_t *list,int row,const char *scope);
void theme_picker_draw(const flist_t *list, int selected, int committed);
