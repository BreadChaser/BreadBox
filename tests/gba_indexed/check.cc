/* Production CPU renderer vs production immutable indexed lists. */
#include <cassert>
#include <cstdio>
#include <cstring>
#include "../../gba-core/video.cc"
extern "C" {
#include "../../gba-core/indexed.c"
u16 io_registers[512],oam_ram[512],palette_ram[512],palette_ram_converted[512];
u8 vram[96*1024];u32 reg[64],skip_next_frame;int sprite_limit=1;
/* Production enables the damage journal together with the compositor. */
unsigned gba_journal_enabled=1,gba_journal_pending;
uint32_t gba_journal_vram[96],gba_journal_pal[16],gba_journal_oam[4],gba_journal_regs[3];
void gba_journal_row(unsigned row,int){gba_journal_pending=1;gba_index_damage(row);memset(gba_journal_vram,0,sizeof(gba_journal_vram));memset(gba_journal_pal,0,sizeof(gba_journal_pal));memset(gba_journal_oam,0,sizeof(gba_journal_oam));memset(gba_journal_regs,0,sizeof(gba_journal_regs));}
}
static u16 cpu[38400],right[38400],expected[38400],er[38400];
static unsigned char raw4[GIX_SLOTS*32],raw8[GIX_SLOTS*64];
int main(){
 gba_screen_pixels=cpu;
 for(unsigned test=0;test<13;test++){
  memset(io_registers,0,sizeof(io_registers));memset(vram,0,sizeof(vram));
  for(unsigned i=0;i<128;i++)oam_ram[i*4]=512;
  for(unsigned i=0;i<512;i++){unsigned c=(i*997+i*i*13)&32767;palette_ram[i]=c;palette_ram_converted[i]=((c&31)<<11)|((c>>5&31)<<6)|(c>>10&31);}
  for(unsigned i=0;i<16384;i++)vram[i]=(i*73+i/17)&255;
  for(unsigned i=0;i<2048;i++)address16(vram,16384+i*2)=(i&255)|((i&3)<<10)|((i&15)<<12);
  write_ioreg(REG_DISPCNT,0x1300);write_ioreg(REG_BG0CNT,8<<8);write_ioreg(REG_BG1CNT,(8<<8)|1|(test&1?128:0));
  write_ioreg(REG_BG0HOFS,test*9);write_ioreg(REG_BG1HOFS,59);write_ioreg(REG_BG0VOFS,29);
  oam_ram[0]=23;oam_ram[1]=17|0x4000;oam_ram[2]=5;
  if(test&2){write_ioreg(REG_BLDCNT,0x3f00|0x3f|((test&1?3:2)<<6));write_ioreg(REG_BLDY,11);}
  if(test&4){write_ioreg(REG_DISPCNT,0x7300);write_ioreg(REG_WIN0H,31<<8|190);write_ioreg(REG_WIN0V,10<<8|130);write_ioreg(REG_WIN1H,170<<8|45);write_ioreg(REG_WIN1V,145<<8|20);write_ioreg(REG_WININ,0x3b35);write_ioreg(REG_WINOUT,0x2e);}
  if(test>=8){write_ioreg(REG_BLDCNT,0x3f00|0x3f|64);write_ioreg(REG_BLDALPHA,11|11<<8);oam_ram[0]|=1024;}
  reg[OAM_UPDATED]=1;gba_index_config(2,raw4,raw8);
  memset(gba_journal_vram,255,sizeof(gba_journal_vram));memset(gba_journal_pal,255,sizeof(gba_journal_pal));memset(gba_journal_oam,255,sizeof(gba_journal_oam));memset(gba_journal_regs,255,sizeof(gba_journal_regs));
  for(unsigned y=0;y<160;y++){write_ioreg(REG_VCOUNT,y);if(y==79||test==12){write_ioreg(REG_BG0HOFS,test==12?y:47);gba_journal_regs[0]|=1u<<8;}update_scanline();}
  gba_index_raster(expected,NULL,160);
  unsigned errors=0;for(unsigned i=0;i<38400;i++)if(cpu[i]!=expected[i]){if(errors<3)fprintf(stderr,"test%u pixel%u cpu%x gpu%x\n",test,i,cpu[i],expected[i]);errors++;}
  assert(!errors);assert(!gba_index_frame()->fallback);
  if(test==12)assert(gba_index_frame()->nb==160);
 }
 puts("PASS indexed text/OBJ priority, palette, flips, windows, blend and register bands");
}
