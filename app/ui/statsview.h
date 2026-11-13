#ifndef STATS_VIEW_H
#define STATS_VIEW_H
#include <stdbool.h>
/* All rates describe the last wall-clock second; no core profiling is enabled. */
typedef struct {
	float fps, target, emu, max, present, wait, gpu;
	float xfer, heap, queue, gap;
	unsigned skipped, late, elapsed;
	const char *audio, *renderer;
 bool unavailable_timing; /* adapter has no host/wait/GPU/late instrumentation */
 unsigned shown, frames; /* supplied by the active adapter */
 const char *identity, *detail[5];
} statsview_sample;
void statsview_context(int *fps,bool left,void (*save)(void));
bool statsview_collecting(void);
void statsview_begin(const char *title);
void statsview_frame(void);
unsigned statsview_emulated(void);
void statsview_update(const statsview_sample *s);
void statsview_draw(void);
void statsview_cycle(void);
bool statsview_touch(void);
bool statsview_redraw(int osd_frames);
#endif
