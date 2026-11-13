#include <assert.h>
#include <stdio.h>
#include "../../gba-core/solar.h"
int main(void) {
 for(unsigned level=0;level<=10;level++) {
  struct solar_sensor s={0};
  assert(!(solar_pins(&s,2,7,level)&8));
  for(unsigned n=1;n<=256;n++) {
   solar_pins(&s,0,7,level);
   unsigned value=solar_pins(&s,1,7,level);
   assert(!!(value&8)==(n>=solar_threshold(level)));
   assert(s.count==n);
   solar_pins(&s,1,7,level);assert(s.count==n); // no duplicate rising edge
  }
  solar_pins(&s,6,7,level);assert(s.count==256); // CS high leaves sensor alone
  solar_pins(&s,2,7,level);assert(s.count==0);
  solar_pins(&s,8,15,level);assert(solar_pins(&s,9,15,level)&8); // output ownership
 }
 assert(solar_threshold(5)<solar_threshold(0));
 puts("Boktai GPIO reset, clock edges, comparator, direction and 11 light levels: PASS");
}
