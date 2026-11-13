// Execute the production direct-color compositor across colors, alignment,
// clipped spans, scalar tails, modes 3/5 and the retained mosaic path.
#include <cassert>
#include <initializer_list>
#include <cstdio>
#include <cstring>
#include "../../gba-core/video.cc"
extern "C" {
void gba_journal_row(unsigned, int) {}
const GixFrame *gba_index_frame(void){return NULL;}
int gba_index_row(unsigned,const int*,int){return 0;}
unsigned gba_index_enabled,gba_journal_enabled;
void gba_index_raster(uint16_t*,uint16_t*,unsigned){}
u16 io_registers[512], oam_ram[512], palette_ram_converted[512];
u8 vram[96*1024];
u32 reg[64], skip_next_frame;
int sprite_limit=1;
}
template<unsigned mode, unsigned width, bool mosaic>
static void check() {
 alignas(4) u16 out[244];
 for(unsigned batch=0;batch<274;batch++) {
  for(unsigned x=0;x<240;x++) ((u16*)vram)[x]=(batch*240+x)&65535;
  for(unsigned align=0;align<2;align++) for(unsigned off=0;off<4;off++)
  for(unsigned start=0;start<4;start++) for(unsigned count: {1u,2u,3u,4u,5u,17u,151u,169u,239u}) {
   unsigned end=start+count;
   memset(out,0xa5,sizeof(out));
   affine_reference_x[0]=off*256;affine_reference_y[0]=0;
   write_ioreg(REG_BG2PA,256);write_ioreg(REG_BG2PC,0);
   write_ioreg(REG_DISPCNT,mode);write_ioreg(REG_MOSAIC,2);
   render_scanline_bitmap<FULLCOLOR,u16,mode,u16,width,160,BLIT,mosaic>(start,end,out+align,palette_ram_converted);
   for(unsigned x=0;x<244;x++) {
    u16 expected=0xa5a5;
    if(x>=align+start && x<align+end && x-align+off<width) {
     unsigned i=x-align-start;
     unsigned source=off+start+(mosaic?(i/3)*3:i);
     expected=convert_palette(((u16*)vram)[source]);
    }
    assert(out[x]==expected);
   }
  }
 }
}
int main() {gba_cpu_config(23);check<3,240,false>();check<5,160,false>();check<3,240,true>();check<5,160,true>();puts("PASS bitmap conversion: all 65536 colors, alignment, tails, clipping, modes 3/5 and mosaic");}
