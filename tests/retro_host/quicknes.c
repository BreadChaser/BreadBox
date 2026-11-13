/* Real core + shared host; CC0 generated cartridge only. GPL-2.0-or-later. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "retro_host.h"
#include "bb_arena.h"
extern const bb_retro_core bb_retro_nes;
extern bool bb_test_nes_cheats(void);

const bb_system_t bb_system_nes={.abi=BB_SYSTEM_ABI,.id="nes"};
bool bb_exec_held(void){return true;}
const bb_system_t *bb_session_system(void){return &bb_system_nes;}
static uint64_t now;static unsigned pictures;static uint32_t hash;
static uint64_t ticks(void){return now;}
static bool picture(const void *p,unsigned w,unsigned h,size_t pitch,enum retro_pixel_format fmt)
{
 assert(p && w>0 && h>0 && fmt==RETRO_PIXEL_FORMAT_RGB565);pictures++;
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
  uint16_t v;memcpy(&v,(const char *)p+y*pitch+x*2,2);hash=(hash^v)*16777619u;
 }
 return true;
}
int main(int argc,char **argv)
{
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);assert(!fseek(f,0,SEEK_END));long len=ftell(f);rewind(f);void *rom=malloc(len);assert(rom && fread(rom,1,len,f)==(size_t)len);fclose(f);
 bb_retro_desc d={.system=&bb_system_nes,.core=&bb_retro_nes,.max_width=256,.max_height=240};
 bb_retro_services s={.ticks=ticks,.ticks_per_second=1000000,.video=picture};
 struct retro_game_info g={.path=argv[1],.data=rom,.size=len};bb_retro_host host;
 assert(bb_retro_open(&host,&d,&s,&g));assert(bb_test_nes_cheats());
 for(unsigned i=0;i<300;i++){now+=16000;assert(bb_retro_step(&host,0));bb_retro_pace(&host);}
 assert(pictures>100 && host.audio_frames>0);
 unsigned char *cpu=d.core->get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
 d.core->cheat_set(0,true,"0010:63");now+=16000;assert(bb_retro_step(&host,0));assert(cpu[16]==99);
 d.core->cheat_set(0,false,"0010:63");cpu[16]=7;now+=16000;assert(bb_retro_step(&host,0));assert(cpu[16]==7);
 d.core->cheat_set(0,true,"0010:63");d.core->cheat_reset();cpu[16]=8;now+=16000;assert(bb_retro_step(&host,0));assert(cpu[16]==8);
 d.core->cheat_set(21,true,"0010:63");d.core->cheat_set(0,true,"invalid!");

 void *ram;size_t n;assert(bb_retro_battery(&host,&ram,&n) && n==8192);
 unsigned char *saved=malloc(n);assert(saved);memcpy(saved,ram,n);
 size_t state_size=d.core->serialize_size(),written=0;void *state=malloc(state_size);assert(state);
 assert(bb_retro_state_save(&host,state,state_size,&written) && written==state_size);
 hash=2166136261u;
 for(unsigned i=0;i<60;i++){now+=16000;assert(bb_retro_step(&host,0));bb_retro_pace(&host);}
 uint32_t expected=hash;
 memset(ram,0x5a,n);assert(bb_retro_state_load(&host,state,state_size,true)==BB_STATE_OK);
 assert(((unsigned char *)ram)[0]==0x5a);
 assert(bb_retro_state_load(&host,state,state_size,false)==BB_STATE_OK);
 hash=2166136261u;
 for(unsigned i=0;i<60;i++){now+=16000;assert(bb_retro_step(&host,0));bb_retro_pace(&host);}
 assert(hash==expected);
 bb_retro_close(&host);assert(!bb_retro_battery(&host,&ram,&n));
 assert(bb_retro_open(&host,&d,&s,&g));assert(bb_retro_battery(&host,&ram,&n));memcpy(ram,saved,n);assert(!memcmp(saved,ram,n));
 bb_retro_close(&host);free(saved);free(state);free(rom);
 puts("real QuickNES shared host: boot, 420 frames, video replay, state + authoritative battery, close/reopen PASS");
}
