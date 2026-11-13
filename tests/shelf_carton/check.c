#include "../../app/ui/shelf_carton.h"
#include <assert.h>
#include <stdio.h>
static double linear(unsigned c) {double v=c/255.0;return v<=.04045?v/12.92:pow((v+.055)/1.055,2.4);}
int main(void) {
 /* Independent double-precision contrast oracle over a dense RGB cube,
  * including saturated reds where a brightness-only threshold chooses poorly. */
 for(unsigned r=0;r<256;r+=3)for(unsigned g=0;g<256;g+=3)for(unsigned b=0;b<256;b+=3) {
  uint32_t ink=shelf_carton_ink(0xff000000u|r|g<<8|b<<16);
  double l=.2126*linear(r)+.7152*linear(g)+.0722*linear(b);
  double ratio=ink==0xff000000u?(l+.05)/.05:1.05/(l+.05);
  assert(ratio>=4.58);
 }
 assert(shelf_carton_ink(0xff000000u)==0xffffffffu);
 assert(shelf_carton_ink(0xffffffffu)==0xff000000u);
 puts("Carton ink: RGB cube contrast >=4.58:1, opaque black/white PASS");
}
