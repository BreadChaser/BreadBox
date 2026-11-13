#include "gba_touch.h"
gba_touch_config gba_touch_settings={{8,0,18,20},0,0};
bool gba_touch_overridden(unsigned i) {(void)i;return false;}
bool gba_touch_inherited(unsigned i,int *v) {*v=i<4?GBA_TOUCH_DEFAULT.binding[i]:0;return true;}

int control_side(void){return gba_touch_settings.left;}
void control_side_set(int left){gba_touch_settings.left=left;}

bool ps1_touch_descriptor(unsigned f,bool n,control_descriptor_t *d){(void)f;(void)n;(void)d;return false;}
