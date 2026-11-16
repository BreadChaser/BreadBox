/* GPL-2.0-or-later. Color transforms are independent of the renderer and SDK. */
#include "gba_color.h"
#include <math.h>
#include <string.h>
/* The core's existing correction table, in RGB555 index order. */
#ifdef __3DS__
extern const uint16_t gpsp_gba_cc_lut[];
#define correction gpsp_gba_cc_lut
#else
extern const uint16_t gba_cc_lut[];
#define correction gba_cc_lut
#endif
const char *const gba_color_names[GBA_COLOR_COUNT]={"Original (vivid)","Corrected","Soft","Bright"};
const char *gba_color_name(int color)
{
 return gba_color_names[color>=0 && color<GBA_COLOR_COUNT?color:0];
}
void gba_color_table(uint16_t table[65536], int color)
{
 unsigned curve[64],curve5[32];
 for(unsigned i=0;i<64;i++)curve[i]=(unsigned)(powf(i/63.0f,0.8f)*63+0.5f);
 for(unsigned i=0;i<32;i++)curve5[i]=(unsigned)(powf(i/31.0f,0.8f)*31+0.5f);
 for(unsigned i=0;i<65536;i++) {
  unsigned corrected=correction[((i&0xffc0)>>1)|(i&31)];
  unsigned r=i>>11,g=i>>5&63,b=i&31;
  if(color==GBA_COLOR_CORRECTED)table[i]=corrected;
  else if(color==GBA_COLOR_SOFT) {
   /* Half of the original color, half of the GBA LCD correction. */
   r=(r+(corrected>>11)+1)/2;
   g=(g+(corrected>>5&63)+1)/2;
   b=(b+(corrected&31)+1)/2;
   table[i]=(r<<11)|(g<<5)|b;
  } else if(color==GBA_COLOR_BRIGHT) {
   table[i]=(curve5[r]<<11)|(curve[g]<<5)|curve5[b];
  } else table[i]=i;
 }
}
void gba_color_upload(uint16_t *out,const void *pixels,size_t pitch,const uint16_t table[65536])
{
 for(unsigned ty=0;ty<20;ty++)for(unsigned tx=0;tx<30;tx++) {
  uint16_t *tile=out+(ty*32+tx)*64;
  const unsigned char *src=(const unsigned char *)pixels+ty*8*pitch+tx*16;
  for(unsigned y=0;y<8;y++) {
   unsigned row=((y&1)<<1)|((y&2)<<2)|((y&4)<<3);
   for(unsigned x=0;x<8;x++) {
    uint16_t c;memcpy(&c,src+y*pitch+x*2,2);
    tile[row+(x&1)+((x&2)<<1)+((x&4)<<2)]=table[c];
   }
  }
 }
}
