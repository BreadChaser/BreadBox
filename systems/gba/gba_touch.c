#include "gba_touch.h"
#include "gba_settings.h"
#include "game_palette.h"
#include <stddef.h>
static ui_play_controls panel;
static bool contact_pending=true;
unsigned gba_touch_pending=1;

static const control_binding_t bindings[]={
 {-1,"Hide"},{8,"A"},{0,"B"},{10,"L"},{11,"R"},{3,"Start"},{2,"Select"},
 {4,"Up"},{5,"Down"},{6,"Left"},{7,"Right"},{16,"Turbo A"},{17,"Turbo B"},
 {18,"Fast forward (toggle)"},{19,"Fast forward (hold)"},{20,"Rewind (hold)"}};
static const control_binding_t sides[]={{0,"Right"},{1,"Left"}};
static const control_binding_t fps_values[]={{0,"Off"},{1,"FPS"},{2,"Detailed"}};
void gba_touch_begin(uint32_t keys[16],uint32_t *ta,uint32_t *tb,bool newer,bool rewind)
{
 ui_play_layout l={.new_model=newer,.left=newer && control_side(),.fps_mode=gba_touch_settings.fps};
 for(int i=0;i<4;i++) {
  int id=gba_touch_settings.binding[i],slot=i<2?i+(newer?2:0):i+2;
  const char *label="";
  control_descriptor_t d;
  if(control_describe("gba",i,newer,&d))
   for(unsigned b=0;b<d.binding_count;b++)if(d.bindings[b].value==id)label=d.bindings[b].label;
  if(id==18 || id==19)label="FF";
  if(id==20)label="REW";
  ui_play_slot *s=&l.slots[slot];
  *s=(ui_play_slot){.label=label,.visible=id>=0 && (id!=20 || (newer && rewind)),
   .color=i<2?g_game_palette.stripe[i]:g_game_palette.mark,
   .action=id==18?PLAY_ACTION_FF_TOGGLE:id==19?PLAY_ACTION_FF_HOLD:id==20?PLAY_ACTION_REWIND_HOLD:PLAY_ACTION_NONE};
  if(!s->visible)continue;
  if(id>=0 && id<16)keys[id]|=gba_touch_bit(slot);
  if(id==16)*ta|=gba_touch_bit(slot);
  if(id==17)*tb|=gba_touch_bit(slot);
 }
 ui_play_geometry(&l);ui_play_begin(&panel,&l);contact_pending=true;gba_touch_pending=1;
}
void gba_touch_reset(void) {ui_play_reset(&panel);contact_pending=true;gba_touch_pending=1;}
static bool gba_touch_poll(input_contact_t touch)
{
 if(!touch.held && !contact_pending)return false;
 ui_play_poll(&panel,touch);contact_pending=touch.held;return true;
}
uint32_t gba_touch_keys(void) {return panel.active<0?0:gba_touch_bit(panel.active);}
static void gba_touch_speed(bool *toggle,bool *hold,bool *rewind)
{
 int active=panel.active;
 play_action_t action=active>=0?panel.layout.slots[active].action:PLAY_ACTION_NONE;
 *toggle=panel.pressed && action==PLAY_ACTION_FF_TOGGLE;
 panel.pressed=false;
 *hold=action==PLAY_ACTION_FF_HOLD;*rewind=action==PLAY_ACTION_REWIND_HOLD;
}
void gba_touch_draw(void) {ui_play_draw(&panel);}
void gba_touch_lit(bool ff) {ui_play_speed(&panel,ff);if(panel.dirty)gba_touch_pending=1;}
/* Storage supplies provenance; descriptors expose no system-private types. */
extern bool gba_touch_overridden(unsigned),gba_touch_inherited(unsigned,int *);
bool gba_touch_descriptor(unsigned field,bool newer,control_descriptor_t *out)
{
 static const char *const keys[]={"touch_a","touch_b","touch_ff","touch_rewind","touch_side","show_fps"};
 static const char *const labels[]={"Touch A","Touch B","Fast forward","Rewind","Bottom-screen buttons","FPS counter"};
 if(field>=6 || !out)return false;
 int inherited=0;gba_touch_inherited(field,&inherited);
 *out=(control_descriptor_t){.key=keys[field],.label=labels[field],
  .value=field<4?gba_touch_settings.binding[field]:field==4?control_side():gba_touch_settings.fps,
  .inherited=field==4?0:inherited,.visible=(field!=3 && field!=4)||newer,.overridden=field==4?control_side()!=0:gba_touch_overridden(field),
  .bindings=field<4?bindings:field==4?sides:fps_values,
  .binding_count=field<4?sizeof(bindings)/sizeof(bindings[0])-(newer?0:1):field==4?2:3};
 return true;
}

unsigned gba_touch_dispatch(input_contact_t contact)
{
 unsigned events=0;
 if(gba_touch_poll(contact)) {
  bool toggle,hold,rewind;gba_touch_speed(&toggle,&hold,&rewind);
  events=GBA_TOUCH_SPEED_EVENT | (toggle?GBA_TOUCH_TOGGLE_EVENT:0) |
   (hold?GBA_TOUCH_HOLD_EVENT:0) | (rewind?GBA_TOUCH_REWIND_EVENT:0);
 }
 if(panel.menu){events|=GBA_TOUCH_MENU_EVENT;panel.menu=false;}
 if(panel.stats){events|=GBA_TOUCH_STATS_EVENT;panel.stats=false;}
 if(panel.dirty){events|=GBA_TOUCH_DIRTY_EVENT;panel.dirty=false;}
 gba_touch_pending=contact.held;return events;
}
