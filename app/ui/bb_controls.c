#include "bb_controls.h"
#include <libretro.h>
#include <string.h>
static const char *const targets[]={"None","A","B","L","R","Start","Select","Up","Down","Left","Right","Turbo A","Turbo B"};
static const int ids[]={-1,RETRO_DEVICE_ID_JOYPAD_A,RETRO_DEVICE_ID_JOYPAD_B,RETRO_DEVICE_ID_JOYPAD_L,RETRO_DEVICE_ID_JOYPAD_R,RETRO_DEVICE_ID_JOYPAD_START,RETRO_DEVICE_ID_JOYPAD_SELECT,RETRO_DEVICE_ID_JOYPAD_UP,RETRO_DEVICE_ID_JOYPAD_DOWN,RETRO_DEVICE_ID_JOYPAD_LEFT,RETRO_DEVICE_ID_JOYPAD_RIGHT};
static const char *const combos[]={"Off","X","Y","Select + R","Select + L","L + R","Start + Select"};
#define MAP(k,l,d) {.key=k,.label=l,.values=targets,.kind=BB_SET_CHOICE,.category=BB_CAT_CONTROLS,.models=BB_MODEL_ALL,.nvalues=13,.def={d,d}}
#define HOT(k,l,d) {.key=k,.label=l,.values=combos,.kind=BB_SET_CHOICE,.category=BB_CAT_CONTROLS,.models=BB_MODEL_ALL,.nvalues=7,.def={d,d}}
static const bb_setting_desc schema[]={
 MAP("a","A button",1),MAP("b","B button",2),MAP("x","X button",0),MAP("y","Y button",0),
 MAP("l","L button",3),MAP("r","R button",4),MAP("zl","ZL button",0),MAP("zr","ZR button",0),
 MAP("start","Start button",5),MAP("select","Select button",6),MAP("up","Up / Circle Pad",7),
 MAP("down","Down / Circle Pad",8),MAP("left","Left / Circle Pad",9),MAP("right","Right / Circle Pad",10),
 HOT("pause","Pause hotkey",1),HOT("quick_save","Quick save hotkey",0),HOT("quick_load","Quick load hotkey",0)
};
/* The Old 3DS has no ZL / ZR (rows 6 and 7): the list leaves them out there. */
bool bb_controls_row_shown(const bb_controls *c,unsigned row) {return (row!=6 && row!=7) || c->profile.model==BB_NEW;}
unsigned bb_controls_rows_shown(const bb_controls *c) {unsigned n=0;for(unsigned i=0;i<BB_CONTROL_ROWS;i++)n+=bb_controls_row_shown(c,i);return n;}
unsigned bb_controls_row_at(const bb_controls *c,unsigned shown) {
 for(unsigned i=0;i<BB_CONTROL_ROWS;i++)if(bb_controls_row_shown(c,i) && !shown--)return i;
 return BB_CONTROL_ROWS-1;
}
void bb_controls_open(bb_controls *c,const char *sys,const char *key,unsigned model,const uint32_t *keys,bool shoulders) {
 memcpy(c->schema,schema,sizeof(schema));c->speed_reserved=shoulders; /* Select+shoulder is reserved for speed controls. */
 if(!shoulders)for(unsigned i=4;i<6;i++)c->schema[i].def[0]=c->schema[i].def[1]=0;
 bb_profile_open(&c->profile,sys,"controls",key,c->schema,BB_CONTROL_ROWS,model);memcpy(c->keys,keys,sizeof(c->keys));
}
uint32_t bb_controls_hotkey(const bb_controls *c,unsigned row) {
 if(row<BB_CONTROL_PAUSE || row>=BB_CONTROL_ROWS)return 0;
 const uint32_t *k=c->keys;
 uint32_t combos[]={0,k[2],k[3],k[9]|k[5],k[9]|k[4],k[4]|k[5],k[8]|k[9]};
 return combos[c->profile.value[row]];
}
void bb_controls_map(const bb_controls *c,uint32_t out[16],uint32_t *a,uint32_t *b) {
 memset(out,0,16*sizeof(*out));*a=*b=0;
 for(unsigned i=0;i<BB_CONTROL_BUTTONS;i++) {
  int v=c->profile.value[i];if(v==11)*a|=c->keys[i];else if(v==12)*b|=c->keys[i];else if(v>0 && v<11)out[ids[v]]|=c->keys[i];
 }
}
const char *bb_controls_value(const bb_controls *c,unsigned row) {return row<BB_CONTROL_ROWS?(row<BB_CONTROL_PAUSE?targets:combos)[c->profile.value[row]]:"";}

const char *bb_controls_warning(const bb_controls *c,unsigned row) {
 uint32_t mask=bb_controls_hotkey(c,row);if(!mask)return NULL;
 if(c->speed_reserved && (mask==(c->keys[9]|c->keys[4]) || mask==(c->keys[9]|c->keys[5])))
  return "Reserved for fast-forward / rewind. Choose another hotkey.";
 for(unsigned i=BB_CONTROL_PAUSE;i<BB_CONTROL_ROWS;i++)if(i!=row && mask==bb_controls_hotkey(c,i))
  return "Hotkey already assigned. Choose another hotkey.";
 return NULL;
}
bool bb_controls_change(bb_controls *c,unsigned row,int dir) {
 if(row>=BB_CONTROL_ROWS)return false;
 if(row<BB_CONTROL_PAUSE)return bb_profile_change(&c->profile,row,dir);
 int old=c->profile.value[row],step=dir<0?-1:1;
 // Skip unavailable assignments without ever writing a conflicting profile.
 for(unsigned n=1;n<=7;n++) {
  int next=(old+7+n*step)%7;
  c->profile.value[row]=next;bool valid=!bb_controls_warning(c,row);c->profile.value[row]=old;
  if(valid) {
   // bb_profile_change accepts one step; temporarily start beside the candidate.
   c->profile.value[row]=(next+7-step)%7;
   bool ok=bb_profile_change(&c->profile,row,step);
   if(!ok)c->profile.value[row]=old;
   return ok;
  }
 }
 return false;
}
unsigned bb_controls_actions(const bb_controls *c,uint32_t held,uint32_t down,uint32_t *gameheld) {
 unsigned actions=0;*gameheld=held;
 for(unsigned row=BB_CONTROL_PAUSE;row<BB_CONTROL_ROWS;row++) {
  uint32_t combo=bb_controls_hotkey(c,row);
  if(combo && (held&combo)==combo){*gameheld&=~combo;if((down&combo) && !bb_controls_warning(c,row) && !actions)actions|=1u<<(row-BB_CONTROL_PAUSE);}
 }
 return actions;
}
