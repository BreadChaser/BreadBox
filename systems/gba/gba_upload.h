/* RGB565 rows -> PICA 8x8 Morton tiles. GPL-2.0-or-later.
 * Copy horizontal pixel pairs: their two Morton addresses are adjacent.
 * memcpy keeps unaligned source rows and strict aliasing well-defined. */
#ifndef BB_GBA_UPLOAD_H
#define BB_GBA_UPLOAD_H
#include <stdint.h>
#include <string.h>
static inline void gba_upload_tiles(uint16_t *out, const void *pixels, size_t pitch)
{
    for(unsigned y=0;y<160;y++) {
        const unsigned char *in=(const unsigned char *)pixels+y*pitch;
        unsigned row=((y&1)<<1)|((y&2)<<2)|((y&4)<<3);
        uint16_t *tile=out+(y>>3)*32*64+row;
        for(unsigned x=0;x<240;x+=8,tile+=64,in+=16) {
            memcpy(tile,   in,   4);
            memcpy(tile+4, in+4, 4);
            memcpy(tile+16,in+8, 4);
            memcpy(tile+20,in+12,4);
        }
    }
}
/* Tile order keeps each destination cache line live until all eight rows
 * have been written, rather than revisiting it across the whole image. */
static inline void gba_upload_tiles_local(uint16_t *out,const void *pixels,size_t pitch)
{
    for(unsigned ty=0;ty<20;ty++)for(unsigned tx=0;tx<30;tx++) {
        uint16_t *tile=out+(ty*32+tx)*64;
        const unsigned char *src=(const unsigned char *)pixels+ty*8*pitch+tx*16;
        for(unsigned y=0;y<8;y++) {
            unsigned row=((y&1)<<1)|((y&2)<<2)|((y&4)<<3);
            const unsigned char *in=src+y*pitch;
            memcpy(tile+row, in,4);memcpy(tile+row+4,in+4,4);
            memcpy(tile+row+16,in+8,4);memcpy(tile+row+20,in+12,4);
        }
    }
}

#endif
