/* Printed carton colors. Keep the cover's hue; folds darken the ink rather
 * than mixing white into it. Computed only when a spine/back is baked. */
#ifndef BB_SHELF_CARTON_H
#define BB_SHELF_CARTON_H
#include <stdint.h>
#include <math.h>
static inline uint32_t shelf_carton_ink(uint32_t paper)
{
 float l=0;
 const float weight[3]={.2126f,.7152f,.0722f};
 for(unsigned i=0;i<3;i++) {
  float c=((paper>>(8*i))&255)/255.0f;
  l+=weight[i]*(c<=.04045f?c/12.92f:powf((c+.055f)/1.055f,2.4f));
 }
 /* Choose the greater WCAG contrast: at least 4.58:1 for opaque ink. */
 return l>.1791288f?0xff000000u:0xffffffffu;
}
#endif
