// Renderer fixture: no ROM/BIOS, exercises the real priority mapper and eye passes.
#include <cassert>
#include <cstdio>
#include <cstring>
#define GBA_STEREO_DEBUG
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
static u16 left[240*160], right[240*160], mono[240*160];
static void frame() {
 for(unsigned y=0;y<160;y++) {write_ioreg(REG_VCOUNT,y);update_scanline();}
}
// Compare optimized rows against the original two complete compositor passes,
// using exactly the same settled depths, registers and affine references.
static void reference_row(unsigned y) {
 u16 l[240],r[240],scroll[4];s32 ax[2],ay[2];
 write_ioreg(REG_VCOUNT,y);
 memcpy(ax,affine_reference_x,sizeof(ax));memcpy(ay,affine_reference_y,sizeof(ay));
 for(unsigned bg=0;bg<4;bg++)scroll[bg]=read_ioreg(REG_BGxHOFS(bg));
 update_scanline();
 if(read_ioreg(REG_DISPCNT)&0x80) {
  for(unsigned x=0;x<240;x++)assert(left[y*240+x]==0xffff && right[y*240+x]==0xffff);
  return;
 }
 if(stereo_flat) return;
 s32 afterx[2],aftery[2];memcpy(afterx,affine_reference_x,sizeof(afterx));memcpy(aftery,affine_reference_y,sizeof(aftery));
 int half[8];
 for(int i=0;i<8;i++) {
  int scaled=stereo_depth[i]*(int)stereo_strength*(int)stereo_ramp;
  half[i]=scaled>=0?(scaled+3932160)/7864320:-((-scaled+3932160)/7864320);
 }
 for(int eye=-1;eye<=1;eye+=2) {
  for(unsigned bg=0;bg<4;bg++) {
   int shift=half[bg]*eye;
   write_ioreg(REG_BGxHOFS(bg),(scroll[bg]-shift)&511);
   if(bg>=2) {
    affine_reference_x[bg-2]=ax[bg-2]-shift*(s16)read_ioreg(REG_BGxPA(bg));
    affine_reference_y[bg-2]=ay[bg-2]-shift*(s16)read_ioreg(REG_BGxPC(bg));
   }
   stereo_obj_shift[bg]=half[4+bg]*eye;
  }
  render_scanline_window(eye<0?l:r);
 }
 assert(!memcmp(l,left+y*240,sizeof(l)));
 assert(!memcmp(r,right+y*240,sizeof(r)));
 for(unsigned bg=0;bg<4;bg++)write_ioreg(REG_BGxHOFS(bg),scroll[bg]);
 memset(stereo_obj_shift,0,sizeof(stereo_obj_shift));
 memcpy(affine_reference_x,afterx,sizeof(afterx));memcpy(affine_reference_y,aftery,sizeof(aftery));
}
int main() {
 gba_screen_pixels=left;
 // Reverse BG numbering: BG3 at front, BG0 farthest. Equal priority tie BG1/BG2.
 const int p[4]={3,1,1,0};
 for(int bg=0;bg<4;bg++) write_ioreg(REG_BGxCNT(bg),p[bg]);
 gba_stereo_config(right,256);
 for(int f=0;f<100;f++) {for(int bg=0;bg<4;bg++) stereo_votes[bg][p[bg]]=160;stereo_frame_depths();}
 assert(stereo_depth[3]==0 && stereo_depth[1]==512 && stereo_depth[2]==1280 && stereo_depth[0]==2048);
 assert(stereo_depth[4]==-512 && stereo_depth[5]==0 && stereo_depth[6]==1536);
 int before=stereo_depth[0];
 for(int bg=0;bg<4;bg++) stereo_votes[bg][0]=160;
 stereo_frame_depths();assert(stereo_depth[0]==before); // one-frame changes cannot jump
 // Main priority-2 actors anchor BG2 at screen while BG3 stays deepest.
 gba_stereo_config(NULL,0);gba_stereo_config(right,256);
 for(int f=0;f<100;f++) {
  for(int bg=0;bg<4;bg++) stereo_votes[bg][bg]=160;
  stereo_obj_votes[2]=1000;stereo_frame_depths();
 }
 assert(stereo_depth[0]==0 && stereo_depth[1]==0 && stereo_depth[2]==0 && stereo_depth[3]==2048);
 assert(stereo_depth[4]==0 && stereo_depth[5]==0 && stereo_depth[6]==-512);
 gba_stereo_config(NULL,0);
 // A stripe tile on BG0 and transparent BG1: shifted eye must sample actual map.
 for(int bg=0;bg<2;bg++) write_ioreg(REG_BGxCNT(bg),((bg+1)<<8) | (bg?4:3));
 for(int x=0;x<8;x++) {vram[x*4]=0x21;vram[x*4+1]=0x43;vram[x*4+2]=0x65;vram[x*4+3]=0x87;}
 palette_ram_converted[0]=0;
 for(int i=1;i<16;i++)palette_ram_converted[i]=i*123;
 write_ioreg(REG_DISPCNT,0x300);frame();memcpy(mono,left,sizeof(mono));
 gba_stereo_config(right,128);
 for(int f=0;f<120;f++)frame();
 assert(memcmp(left,right,sizeof(left))!=0);
 assert(memcmp(left,mono,sizeof(left))!=0); // symmetric: left also moves
 // Source registers restored exactly after both eye renders.
 assert(read_ioreg(REG_BGxHOFS(0))==0 && read_ioreg(REG_BGxHOFS(1))==0);
 for(int i=0;i<5;i++) assert(stereo_obj_shift[i]==0);
 // Disabled immediately restores exact mono pixels, no residual left offset.
 gba_stereo_config(NULL,0);frame();assert(!memcmp(left,mono,sizeof(mono)));
 // A visible OBJ really moves in both eyes; OAM itself stays byte exact.
 for(int i=0;i<128;i++) oam_ram[i*4]=0x200;
 oam_ram[0]=20;oam_ram[1]=40;oam_ram[2]=0;
 memset(vram+0x10000,0x11,32);palette_ram_converted[257]=0xf800;
 write_ioreg(REG_DISPCNT,0x1300);reg[OAM_UPDATED]=1;
 u16 saved_oam[512];memcpy(saved_oam,oam_ram,sizeof(saved_oam));
 gba_stereo_config(right,256);for(int f=0;f<120;f++)frame();
 assert(left[20*240+41]==0xf800 && right[20*240+39]==0xf800);
 assert(left[20*240+39]!=0xf800 && right[20*240+47]!=0xf800);
 assert(!memcmp(oam_ram,saved_oam,sizeof(saved_oam)));
 gba_stereo_config(right,128);
 // Screen-fixed window gates are independently applied after eye sampling.
 write_ioreg(REG_DISPCNT,0x2300);write_ioreg(REG_WIN0H,120);
 write_ioreg(REG_WIN0V,160);write_ioreg(REG_WININ,2);write_ioreg(REG_WINOUT,1);
 for(int f=0;f<120;f++)frame();
 for(int y=0;y<160;y++) for(int x=0;x<120;x++) {
  assert(left[y*240+x]==0 && right[y*240+x]==0);
 }
 assert(left[125]!=right[125]);
 // Alpha effects must still be evaluated in each eye and restore BLDCNT.
 write_ioreg(REG_DISPCNT,0x300);write_ioreg(REG_BLDCNT,0x241);
 write_ioreg(REG_BLDALPHA,0x0808);
 frame();assert(read_ioreg(REG_BLDCNT)==0x241 && read_ioreg(REG_BLDALPHA)==0x0808);
 assert(memcmp(left,right,sizeof(left))!=0);
 // Differential coverage: text/affine, mosaic, nonwrap, fixed and OBJ
 // windows, mixed effects/targets, partial tiles, transparent and opaque pixels.
 unsigned rng=17;
 for(unsigned i=0;i<sizeof(vram);i++){rng=rng*1664525+1013904223;vram[i]=rng>>24;}
 for(unsigned mode=0;mode<3;mode++) for(unsigned variant=0;variant<32;variant++) {
  write_ioreg(REG_DISPCNT,0x1f00|mode|((variant&7)<<13));
  write_ioreg(REG_BLDCNT,((variant>>3)<<6)|0x3f3f);
  write_ioreg(REG_BLDALPHA,0x080d);write_ioreg(REG_BLDY,7);
  write_ioreg(REG_WIN0H,((variant&2?3:19)<<8)|(variant&2?5:133));write_ioreg(REG_WIN1H,(211<<8)|51);
  write_ioreg(REG_WIN0V,160);write_ioreg(REG_WIN1V,160);
  write_ioreg(REG_WININ,0x3f15);write_ioreg(REG_WINOUT,0x173f);
  write_ioreg(REG_MOSAIC,0x1234);
  for(unsigned bg=0;bg<4;bg++) {
   write_ioreg(REG_BGxCNT(bg),(bg<<8)|((variant&1)?0x40:0)|((variant&2)?0x2000:0)|bg);
   write_ioreg(REG_BGxHOFS(bg),bg*79+variant);
   write_ioreg(REG_BGxPA(bg),193);write_ioreg(REG_BGxPC(bg),37);
  }
  for(int i=0;i<8;i++) stereo_depth[i]=(i<4?(3-i)*512:(i-4)*256-512);
  stereo_ramp=60;stereo_flat=false;reg[OAM_UPDATED]=1;
  for(unsigned y=1;y<32;y++) reference_row(y);
 }
 // A transient forced-blank row must not flatten subsequent tile pictures.
 write_ioreg(REG_DISPCNT,0x300);stereo_hold=0;stereo_ramp=60;stereo_flat=false;
 int saved_depth[8];memcpy(saved_depth,stereo_depth,sizeof(saved_depth));
 write_ioreg(REG_VCOUNT,37);write_ioreg(REG_DISPCNT,0x380);update_scanline();
 for(unsigned x=0;x<240;x++)assert(left[37*240+x]==0xffff && right[37*240+x]==0xffff);
 assert(!stereo_hold && stereo_ramp==60 && !stereo_flat);
 write_ioreg(REG_DISPCNT,0x300);write_ioreg(REG_VCOUNT,38);update_scanline();
 assert(!stereo_hold && stereo_ramp==60 && !stereo_flat);
 assert(!memcmp(saved_depth,stereo_depth,sizeof(saved_depth)));
 // Rejected frames are the exact mono image and cannot expose a stale right.
 gba_stereo_admit(0);frame();assert(!gba_stereo_valid());
 memcpy(mono,left,sizeof(mono));gba_stereo_config(NULL,0);frame();
 assert(!memcmp(left,mono,sizeof(mono)));gba_stereo_admit(1);
 puts("GBA stereo: reverse/equal priority, debounce, independent symmetric sampling, register restore, mono off PASS");
}
