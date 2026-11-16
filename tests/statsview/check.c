// Host-side checks for the real stats formatter/cache, without the 3DS SDK.
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef struct { char value[128]; } C2D_Text;
typedef void *C2D_TextBuf;
typedef struct { int px, py; } touchPosition;
#define KEY_TOUCH 1
#define C2D_WithColor 0
#define C2D_Color32(r,g,b,a) ((u32)(r) | (u32)(g)<<8 | (u32)(b)<<16 | (u32)(a)<<24)
static struct { int show_fps, touch_click_left; } g_cfg;
static bool g_new3ds;
#define C2D_AlignRight 1
static int labels, bars;
static float label_x, bar_end;
static u32 label_align;
static void *libui_font(void) { return NULL; }
static void ui_text_pixels(void *font, float x, float y, float pixels, float spacing, float width, u32 color, u32 align, const char *s) {
 labels++; label_x=x; label_align=align;
 assert(width==62 || width==-62);
}
static void C2D_DrawRectSolid(float x,float y,float z,float w,float h,u32 c) { bars++; bar_end=x+w; }
static bool sw_stats, sw_prof, linked;
static int g_syscore_pct=30, parses, saves, sw_pmu0=-1;
static u32 keys;
static touchPosition touch;
char CdromId[10]="SCUS94900";
static bool dev_enabled(void) { return false; }
static bool log_linked(void) { return linked; }
static const char *options_get(const char *s) {
 if (strstr(s,"frameskip")) return "auto";
 if (strstr(s,"clock")) return "57";
 if (strstr(s,"readahead")) return "256";
 return "enabled";
}
static bool config_widescreen(void) { return false; }
static void config_save(void) { saves++; }
static unsigned audio_occupancy(void) { return 45; }
static size_t linearSpaceFree(void) { return 2048*1024; }
static C2D_TextBuf C2D_TextBufNew(int n) { return (void*)1; }
static void C2D_TextBufClear(C2D_TextBuf b) {}
static void C2D_TextParse(C2D_Text *t, C2D_TextBuf b, const char *s) { snprintf(t->value,sizeof(t->value),"%s",s); parses++; }
static void C2D_TextOptimize(C2D_Text *t) {}
static void C2D_DrawText(C2D_Text *t, int flags, float x, float y, float z, float sx, float sy, u32 color) {}
static void ui_rect(float x,float y,float w,float h,u32 c) {}
static bool input_stats_touched(void) { return g_cfg.show_fps && keys && touch.py >= 218; }
typedef struct { unsigned top, drawn; } emu_frontend_sample;
static emu_frontend_sample front = {.top=60, .drawn=30};
static const emu_frontend_sample *emu_frontend_stats(void) { return &front; }
static u32 hidKeysHeld(void) { return keys; }
static void hidTouchRead(touchPosition *t) { *t=touch; }
#include "../../include/breadbox/game_palette.h"
game_palette_t g_game_palette;
#include "../../app/ui/statsview.c"
static void frames(unsigned n) { while(n--) statsview_frame(); }
static unsigned staged;
static void stage_mode(void){staged++;}
static void adapter_update(statsview_sample *s)
{ s->shown=front.drawn<front.top?front.drawn:front.top;statsview_update(s); }
int main(void) {
 statsview_begin("Crash Bandicoot");
 assert(!statsview_collecting() && parses==0);
 frames(60); assert(emulated==0);
 statsview_cycle(); assert(g_cfg.show_fps==1 && saves==1);
 statsview_sample s={.fps=30,.target=60,.emu=20,.max=28,.present=2,.wait=1,.gpu=.2,.skipped=30,.late=9,.elapsed=1000,
  .detail={"Frameskip: auto"},
  .audio="audio in=44100/60 calls queue=100..3000 (target 3072) underruns=7 drop=0",
  .renderer="pica: cmdlist 1ms; state 1ms; ends: atlas 1; evictions by size 0 0 0 0; 22 batches 88 prims 17 tile decodes (0 evictions 0 invalidations) 19 uploads 3 syncs 2 readbacks"};
 frames(60); adapter_update(&s); // discard the partial/hidden interval
 frames(60); adapter_update(&s);
 // A native-30 game at full emulated speed shows 30, even with 60 repaints.
 assert(!strcmp(brief,"30 FPS") && !strcmp(speed_label,"100% speed"));
 assert(statsview_redraw(0)); assert(!statsview_redraw(0));
 int fps_parses=parses;
 s.emu=21; s.max=29; s.gpu=.3;
 frames(60); adapter_update(&s);
 assert(!statsview_redraw(0) && parses==fps_parses);
 s.fps=60; front.top=90; frames(60); adapter_update(&s); assert(!statsview_redraw(0));
 front.drawn=31; frames(60); adapter_update(&s); assert(statsview_redraw(0));
 assert(!statsview_redraw(0));
 // Auto skip separates shown pictures from emulated time and host loops.
 front.top=front.drawn=9; frames(50); adapter_update(&s);
 assert(!strcmp(brief,"9 FPS") && !strcmp(speed_label,"83% speed"));
 // Fast forward may draw several pictures between top presentations.
 front.top=14; front.drawn=40; frames(60); adapter_update(&s);
 assert(!strcmp(brief,"14 FPS"));
 // Rates use actual elapsed time, not an assumed exactly-one-second sample.
 s.elapsed=2000; front.top=60; front.drawn=30; frames(120); adapter_update(&s);
 assert(!strcmp(brief,"15 FPS") && !strcmp(speed_label,"100% speed"));
 s.elapsed=0; frames(60); adapter_update(&s);
 assert(!strcmp(brief,"0 FPS") && !strcmp(speed_label,"0% speed"));
 s.elapsed=1000; front.top=60; front.drawn=30;
 frames(120); adapter_update(&s); assert(!strcmp(speed_label,"200% speed"));
 assert(statsview_redraw(0));
 statsview_draw(); assert(labels==2 && bars==4 && label_x==12 && bar_end==74 && label_align==0);
 g_new3ds=true; g_cfg.touch_click_left=1;statsview_context(NULL,true,NULL);
 statsview_draw(); assert(labels==4 && bars==8 && label_x==308 && bar_end==308 && label_align==C2D_AlignRight);
 g_new3ds=false; g_cfg.touch_click_left=0;statsview_context(NULL,false,NULL);
 statsview_cycle(); assert(g_cfg.show_fps==2);
 frames(60); adapter_update(&s);
 assert(!strcmp(lines[1],"Emulated 60.0/60  speed 100%"));
 assert(!strcmp(lines[2],"Shown 30.0/s  host 60.0/s  late 9"));
 assert(!strcmp(lines[3],"Frameskip: auto  skip 30"));
 assert(strstr(lines[7],"underruns 7"));
 assert(strstr(lines[10],"19/17  sync/read 3/2"));
 assert(strstr(lines[11],"88/22"));
 assert(statsview_redraw(0)); assert(!statsview_redraw(0));
 int old=parses; frames(60); adapter_update(&s); assert(parses==old); assert(!statsview_redraw(0));
 // Extra emulated frames count toward speed even when not presented (FF).
 frames(120); adapter_update(&s); assert(strstr(lines[1],"200%"));
 s.target=50; frames(50); adapter_update(&s); assert(strstr(lines[1],"100%"));
 s.audio="audio off"; frames(50); adapter_update(&s); assert(!strcmp(lines[7],"Audio off"));
 s.target=60; s.audio="underruns=7";
 keys=KEY_TOUCH; touch.py=190; assert(!statsview_touch()); // touch controls/menu retained
 touch.py=222; assert(statsview_touch() && g_cfg.show_fps==0);
 assert(statsview_redraw(0)); keys=0; statsview_redraw(0); assert(!statsview_redraw(0));
 statsview_begin("Crash Bandicoot"); assert(statsview_redraw(0)); assert(!statsview_redraw(0));
 sw_stats=true; assert(statsview_collecting()); sw_stats=false;
 sw_prof=true; assert(statsview_collecting()); sw_prof=false;
 linked=true; assert(statsview_collecting()); linked=false;
 // Independent adapter setting and deferred callback; never PS1 config_save.
 int gba_fps=1,prior_saves=saves;statsview_context(&gba_fps,false,stage_mode);
 statsview_cycle();assert(gba_fps==2 && staged==1 && saves==prior_saves && !g_cfg.show_fps);
 statsview_begin("GBA fixture");s.unavailable_timing=true;s.detail[0]="Frameskip: n/a";
 frames(60);adapter_update(&s);frames(60);adapter_update(&s);
 assert(strstr(lines[2],"host n/a  late n/a") && strstr(lines[5],"wait n/a   GPU n/a"));
 assert(strstr(lines[3],"Frameskip: n/a"));
 statsview_context(NULL,false,NULL);statsview_begin("Crash Bandicoot");s.unavailable_timing=false;s.detail[0]="Frameskip: auto";
 // Cross-system replacement must erase old samples even when FPS is Off,
 // retain adapter callbacks, and initialize compact/detailed on either side.
 for(int mode=0;mode<3;mode++)for(int left=0;left<2;left++) {
  g_cfg.show_fps=mode;statsview_context(NULL,left,NULL);statsview_begin("PS1");
  assert(statsview_redraw(0) && !statsview_redraw(0));
  assert(!strcmp(brief,"        ") && !strcmp(speed_label,"         "));
  int other=mode;statsview_context(&other,left,stage_mode);statsview_begin("GBA");
  assert(statsview_redraw(0) && !statsview_redraw(0));
  assert(!strcmp(brief,"        ") && !strcmp(lines[0],"PERFORMANCE / waiting for stats"));
  statsview_context(NULL,left,NULL);statsview_begin("PS1 again");
  assert(statsview_redraw(0) && !statsview_redraw(0));
  assert(fps_mode==&g_cfg.show_fps && mirrored==!!left && save_mode==config_save);
 }
 // Write real formatted rows for the offline mockup.
 g_cfg.show_fps=2; frames(60); adapter_update(&s); frames(60); adapter_update(&s);
 for(int i=0;i<ROWS;i++) puts(lines[i]);
 puts("checks passed");
}
