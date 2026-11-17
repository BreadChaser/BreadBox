#include "home_model.h"
#include "room.h"
#include "app_policy.h"
#include "ui.h"
#include "cold_code.h"

static const bb_home_place places[] = {
 { E_CONTINUE, ROOM_CONTINUE, "Continue", "Continue", UI_ICON_TV, true },
 { E_LIBRARY, ROOM_LIBRARY, "Collection", "Collection", UI_ICON_BOOKS, false },
 { E_TOGETHER, ROOM_TOGETHER, "Together", "Play together", UI_ICON_TOGETHER, false },
 { E_SETTINGS, ROOM_SETTINGS, "Settings", "Settings", UI_ICON_SCOPE, false },
 { E_SHARE, ROOM_SHARE, "Share", "Share " APP_NAME, UI_ICON_QR, false },
 { E_QUIT, -1, "Quit", "Quit " APP_NAME, UI_ICON_EXIT, false },
};
#define PLACE_COUNT ((int)(sizeof(places) / sizeof(places[0])))

PSX3D_COLD int bb_home_count(bool have_continue)
{
 int count=0;
 for(int i=0;i<PLACE_COUNT;i++)if(!places[i].needs_continue || have_continue)count++;
 return count;
}
PSX3D_COLD const bb_home_place *bb_home_at(int row, bool have_continue)
{
 if(row<0)return NULL;
 for(int i=0;i<PLACE_COUNT;i++) {
  if(places[i].needs_continue && !have_continue)continue;
  if(!row--)return &places[i];
 }
 return NULL;
}
PSX3D_COLD const bb_home_place *bb_home_find(int action)
{
 for(int i=0;i<PLACE_COUNT;i++)if(places[i].action==action)return &places[i];
 return NULL;
}
PSX3D_COLD const bb_home_place *bb_home_place_at(int place)
{
 for(int i=0;i<PLACE_COUNT;i++)if(places[i].place==place)return &places[i];
 return NULL;
}

PSX3D_COLD int bb_home_room_places(int *out,int capacity,bool have_continue)
{
 int count=0;
 for(int i=0;i<bb_home_count(have_continue) && count<capacity;i++) {
  const bb_home_place *p=bb_home_at(i,have_continue);
  if(p->place>=0)out[count++]=p->place;
 }
 return count;
}
PSX3D_COLD int bb_home_neighbor(int place,int step,bool have_continue)
{
 int sequence[ROOM_PLACES];
 int count=bb_home_room_places(sequence,ROOM_PLACES,have_continue),at=0;
 while(at<count && sequence[at]!=place)at++;
 at+=step;
 return at>=0 && at<count ? sequence[at] : -1;
}
