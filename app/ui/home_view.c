#include "home_view.h"
#include "look_theme.h"
#include "cold_code.h"

#define HOME_VIEW_STEP (1.0f/60.0f)
#define HOME_VIEW_STEPS 120   /* two glides' worth: always enough to arrive */

static PSX3D_COLD void arrive(room_view *view)
{
 if(theme_is_room())return;
 for(int i=0;i<HOME_VIEW_STEPS && view->mode!=ROOM_AT && view->mode!=ROOM_HOME;i++)room_view_step(view,HOME_VIEW_STEP);
}
PSX3D_COLD void home_view_go(room_view *view, int place) { room_view_go(view,place);arrive(view); }
PSX3D_COLD void home_view_back(room_view *view) { room_view_back(view);arrive(view); }
