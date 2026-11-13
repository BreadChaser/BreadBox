#include <assert.h>
#include "room_refresh.h"
int main(void) {
 // Synthetic post-vblank stamps for L'=28/7/65 ms: gaps 3/2/6.
 // Logic intervals alone cannot determine presentation: use observed stamps.
 const unsigned gaps[]={3,2,6}, expected[]={1,0,4};
 uint64_t stamp=0,previous=0;
 for(unsigned i=0;i<3;i++) {
  const uint64_t refresh=16715, hz=1000000;
  unsigned elapsed=gaps[i];
  stamp+=elapsed*refresh;
  unsigned gap=room_refresh_gap(stamp-previous,hz);
  assert(room_refresh_lost(gap,2)==expected[i]);previous=stamp;
 }
 assert(room_refresh_gap(33430,1000000)==2);
 return 0;
}
