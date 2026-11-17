#ifndef BB_HOME_VIEW_H
#define BB_HOME_VIEW_H
#include "room.h"
/* The home screen's moves to a place and back. The Room glides its camera there (the keys wait for it); the
   list themes have no camera to watch, so there the move is over at once and the next key counts. */
void home_view_go(room_view *view, int place);
void home_view_back(room_view *view);
#endif
