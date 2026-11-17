#include "library_model.h"
#include "cold_code.h"

static const ui_button_t actions[][BB_LIBRARY_ACTIONS] = {
 {{KEY_X,"Sort",UI_ICON_SORT,false},{KEY_Y,"Rescan",UI_ICON_RESCAN,false},
  {KEY_SELECT,"Settings",UI_ICON_TOGGLE,false},{KEY_A,"Open",UI_ICON_OPEN,false}},
 {{KEY_B,"Back",UI_ICON_BACK,false},{KEY_Y,"Favorite",UI_ICON_STAR,false},
  {KEY_X,"Sort",UI_ICON_SORT,false},{KEY_A,"Take out",UI_ICON_OPEN,false}},
 {{KEY_B,"Put back",UI_ICON_BACK,false},{KEY_Y,"Favorite",UI_ICON_STAR,false},
  {KEY_X,"Options",UI_ICON_TOGGLE,false},{KEY_A,"Play",UI_ICON_PLAY,false}},
 {{KEY_B,"Back",UI_ICON_BACK,false},{KEY_Y,"Favorite",UI_ICON_STAR,false},
  {KEY_X,"Options",UI_ICON_TOGGLE,false},{KEY_A,"Play",UI_ICON_PLAY,false}},
};
PSX3D_COLD int bb_library_actions(int phase,bool favorite,const char *primary,ui_button_t *out,int capacity)
{
 if(phase<BB_LIBRARY_SYSTEMS || phase>BB_LIBRARY_LIST || capacity<BB_LIBRARY_ACTIONS)return 0;
 for(int i=0;i<BB_LIBRARY_ACTIONS;i++) {
  out[i]=actions[phase][i];
  if(out[i].key==KEY_Y && phase!=BB_LIBRARY_SYSTEMS)out[i].on=favorite;
  if(out[i].key==KEY_A && primary)out[i].label=primary;
 }
 return BB_LIBRARY_ACTIONS;
}
