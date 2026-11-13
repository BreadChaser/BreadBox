/* Exercise the production swizzler against independent pixel addressing. */
#include "../../systems/gba/gba_upload.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(void) {
 unsigned seed=42;
 for(unsigned variant=0;variant<2;variant++)for(unsigned padding=0;padding<20;padding++)for(unsigned alignment=0;alignment<4;alignment++) {
  size_t pitch=480+padding;unsigned char *raw=malloc(pitch*160+4);uint16_t out[256*256];
  for(unsigned i=0;i<pitch*160+4;i++) {seed=seed*1664525+1013904223;raw[i]=seed>>24;}
  memset(out,0xa7,sizeof(out));if(variant)gba_upload_tiles_local(out,raw+alignment,pitch);else gba_upload_tiles(out,raw+alignment,pitch);
  for(unsigned y=0;y<256;y++)for(unsigned x=0;x<256;x++) {
   unsigned idx=((y>>3)*32+(x>>3))*64+(x&1)+((y&1)<<1)+((x&2)<<1)+((y&2)<<2)+((x&4)<<2)+((y&4)<<3);
   uint16_t expected=0xa7a7;if(x<240&&y<160)memcpy(&expected,raw+alignment+y*pitch+2*x,2);
   assert(out[idx]==expected);
  }
  free(raw);
 }
 puts("PASS: all pixels, tile padding, 20 pitches and four byte alignments");
}
