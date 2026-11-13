/* GPL-2.0-or-later. Exercise production bitmap intermediates and composition. */
#include <cassert>
#include <initializer_list>
#include <cstdio>
#include <cstring>
#include "../../gba-core/video.cc"
extern "C" {
unsigned gba_index_enabled;
void gba_journal_row(unsigned,int) {}
const GixFrame *gba_index_frame(void){return NULL;}
int gba_index_row(unsigned,const int*,int){return 0;}
void gba_index_raster(uint16_t*,uint16_t*,unsigned){}
u16 io_registers[512],oam_ram[512],palette_ram_converted[512];
u8 vram[96*1024];u32 reg[64],skip_next_frame;int sprite_limit=1;
}
static u16 bright(u16 c,unsigned y,bool up) {
 unsigned out=0;
#ifdef USE_XBGR1555_FORMAT
 const unsigned shifts[]={0,5,10};
#else
 const unsigned shifts[]={0,6,11};
#endif
 for(unsigned shift:shifts){unsigned v=(c>>shift)&31;out|=(up?v+((31-v)*y>>4):v-(v*y>>4))<<shift;}
 return out;
}
int main() {
 for(unsigned c=0;c<32768;c++)for(unsigned y=0;y<=16;y++)for(bool up:{false,true})
  assert(effect_brightness(convert_palette(c),y,up)==bright(convert_palette(c),y,up));
 // A black direct pixel is opaque and retains flags and the previous layer.
 u32 pixel=0x456;bitmap_pixel_write<STCKCOLOR,u32,3,u16>(&pixel,0,NULL,0x600,17);
 assert(pixel==0x04561600 && bitmap_effect_colors[17]==0);
 pixel=0x456;bitmap_pixel_write<INDXCOLOR,u32,5,u16>(&pixel,0x7fff,NULL,0x400,17);
 assert(pixel==0x1400 && bitmap_effect_colors[17]==convert_palette(0x7fff));
 // Indexed zero remains transparent.
 pixel=0x456;bitmap_pixel_write<STCKCOLOR,u32,4,u8>(&pixel,0,NULL,0x600,17);assert(pixel==0x456);
 // A mode-5 window span wholly beyond the bitmap must leave its backdrop.
 u16 row[240];for(auto &c:row)c=0x1234;
 affine_reference_x[0]=affine_reference_y[0]=0;
 write_ioreg(REG_BG2PA,256);write_ioreg(REG_BG2PC,0);write_ioreg(REG_DISPCNT,5);
 render_scanline_bitmap<FULLCOLOR,u16,5,u16,160,128,BLIT,false>(185,240,row,NULL);
 for(auto c:row)assert(c==0x1234);
 puts("PASS bitmap effects: all 32768 colours x 17 coefficients x brighten/darken, direct flags/stack/opaque zero, indexed transparency, mode-5 clipping");
}
