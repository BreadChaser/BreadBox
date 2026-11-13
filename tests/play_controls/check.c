/* Production widget, adapters and storage; contact traces and retained-target policy. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ui_play_controls.h"
#include "gba_touch.h"
#include "game_palette.h"
#include "gba_settings.h"
game_palette_t g_game_palette;
void ui_play_draw(ui_play_controls *p) {p->draws++;}
static void poll(ui_play_controls *p,int x,int y,bool held,bool down)
{ui_play_poll(p,(input_contact_t){x,y,held,down});}
static void widget(void)
{
 for(int newer=0;newer<2;newer++)for(int left=0;left<=newer;left++) {
  ui_play_layout l={.new_model=newer,.left=left};
  l.slots[newer?2:0].visible=l.slots[newer?3:1].visible=l.slots[4].visible=true;
  ui_play_geometry(&l);
  assert(l.slots[newer?2:0].rect.x==(newer?(left?12:208):92));
  assert(l.slots[4].rect.h==112 && l.slots[4].rect.x==(left?252:12));
  ui_play_controls p={0};ui_play_begin(&p,&l);
  assert(ui_play_redraw(&p));assert(!ui_play_redraw(&p));
  poll(&p,25,20,true,true);assert(!ui_play_take_menu(&p)); /* held menu launch */
  poll(&p,0,0,false,false);
  for(int i=0;i<600;i++) {poll(&p,0,0,false,false);assert(!ui_play_redraw(&p));}
  ui_play_rect b=l.slots[newer?2:0].rect;
  poll(&p,b.x,b.y,true,true);assert(p.active==(newer?2:0));assert(ui_play_redraw(&p));
  poll(&p,left?270:30,20,true,false);assert(p.active<0 && !ui_play_take_menu(&p));assert(ui_play_redraw(&p));
  /* Gesture owner cannot move to a neighbour or Menu. */
  b=l.slots[newer?3:1].rect;poll(&p,b.x+2,b.y+2,true,false);assert(p.active<0);
  poll(&p,0,0,false,false);
  poll(&p,left?270:30,20,true,true);assert(ui_play_take_menu(&p));assert(!ui_play_take_menu(&p));
  ui_play_reset(&p);poll(&p,b.x+2,b.y+2,true,false);assert(p.active<0);
  poll(&p,0,0,false,false);
  l.fps_mode=2;ui_play_begin(&p,&l);poll(&p,0,0,false,false);
  poll(&p,150,225,true,true);assert(ui_play_take_stats(&p) && !ui_play_take_menu(&p));
  poll(&p,150,195,true,false);assert(!ui_play_take_menu(&p));
  poll(&p,0,0,false,false);poll(&p,150,195,true,true);assert(ui_play_take_menu(&p));
  ui_play_speed(&p,true);assert(ui_play_redraw(&p));ui_play_speed(&p,true);assert(!ui_play_redraw(&p));
 }
 ui_play_layout blank={0};ui_play_controls p={0};ui_play_begin(&p,&blank);poll(&p,0,0,false,false);
 poll(&p,150,80,true,true);assert(ui_play_take_menu(&p));
 ui_play_layout stick={.stick=true};stick.slots[0].visible=true;ui_play_geometry(&stick);ui_play_begin(&p,&stick);
 poll(&p,0,0,false,false);poll(&p,200,120,true,true);poll(&p,231,120,true,false);
 assert(p.stick_x==31 && p.active<0);poll(&p,30,20,true,false);assert(!ui_play_take_menu(&p));
}
static void adapter(void)
{
 assert(control_schema_count==2);assert(!strcmp(control_schemas[0].system,"ps1") && !strcmp(control_schemas[1].system,"gba"));
 assert(control_schemas[1].describe==gba_touch_descriptor);
 control_descriptor_t found;assert(control_describe("gba",2,true,&found));
 assert(!strcmp(found.label,"Fast forward"));assert(!control_describe("missing",0,true,&found));
 assert(!control_describe("gba",6,true,&found));
 for(int model=0;model<2;model++) {
  uint32_t keys[16]={0},ta=0,tb=0;gba_touch_settings=GBA_TOUCH_DEFAULT;
  gba_touch_begin(keys,&ta,&tb,model,model);
  assert(keys[8]==1u<<(model?18:16));assert(keys[0]==1u<<(model?19:17));
  control_descriptor_t d;
  for(unsigned i=0;i<6;i++) {assert(gba_touch_descriptor(i,model,&d));assert(d.key && d.label && d.bindings && d.binding_count);}
  assert(!gba_touch_descriptor(6,model,&d));
  assert(gba_touch_descriptor(3,model,&d) && d.visible==(bool)model);
  assert(gba_touch_step((input_contact_t){0})&GBA_TOUCH_DIRTY_EVENT);
  for(int idle=0;idle<600;idle++)assert(!gba_touch_step((input_contact_t){0}));
  unsigned edge=gba_touch_step((input_contact_t){30,90,true,true});
  assert((edge&GBA_TOUCH_TOGGLE_EVENT) && !(edge&(GBA_TOUCH_HOLD_EVENT|GBA_TOUCH_REWIND_EVENT)));
  assert(!(gba_touch_step((input_contact_t){30,90,true,false})&GBA_TOUCH_TOGGLE_EVENT));
  assert(!(gba_touch_step((input_contact_t){100,230,true,false})&GBA_TOUCH_TOGGLE_EVENT));
  assert(!(gba_touch_step((input_contact_t){30,90,true,false})&GBA_TOUCH_TOGGLE_EVENT));
  gba_touch_step((input_contact_t){0});
  unsigned events=gba_touch_step((input_contact_t){model?250:140,70,true,true});
  assert(events&GBA_TOUCH_DIRTY_EVENT);assert(gba_touch_keys()==gba_touch_bit(model?2:0));
  assert(!(gba_touch_keys()&(1u<<20)));
  for(unsigned slot=0;slot<6;slot++)assert(!(gba_touch_bit(slot)&(1u<<20)));
  gba_touch_step((input_contact_t){0});
  for(int frame=0;frame<600;frame++)assert(!gba_touch_step((input_contact_t){0}));
  gba_touch_reset();gba_touch_step((input_contact_t){30,90,true,false});assert(!gba_touch_keys());
 }
}
int main(void) {widget();adapter();puts("PASS: Old/New/mirror geometry, capture priority, drag-out, release block, 600 idle frames, dirty edges and neutral descriptors");}
