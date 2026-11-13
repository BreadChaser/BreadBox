/* Exercise reverse dependencies, inactive entries and bounded replacement using
 * the production journal. No emulated CPU or pixel compositor is substituted. */
#include <assert.h>
#include "../../gba-core/indexed.h"
const struct GixFrame *gba_index_frame(void){return 0;}
void gba_index_reset(void){}
void gba_index_damage(unsigned r){(void)r;}
#include "../../gba-core/journal.c"
u16 io_registers[512],oam_ram[512],palette_ram[512];
u8 vram[96*1024];
static uint32_t atlas[6*SLOTS*64];
static void drain(void){
 gba_journal_pending=1;refs_dirty=0;gba_journal_row(1,0);
 assert(!queue_count);
}
static void deactivate(void){for(unsigned i=0;i<nactive;i++)active[active_slots[i]]=0;nactive=0;}
static void verify(void){
 for(unsigned s=0;s<nslots;s++)if(active[s]){
  struct Entry *e=&entries[s];unsigned base=e->bank<32?e->bank*16:e->bank==33?256:0;
  assert(!dirty[s]);
  for(unsigned i=0;i<64;i++){
   unsigned index=e->bank>=32?vram[e->tile*32+i]:(vram[e->tile*32+i/2]>>(4*(i&1)))&15;
   unsigned x=i%8,y=i/8,m=(x&1)|((y&1)<<1)|((x&2)<<1)|((y&2)<<2)|((x&4)<<2)|((y&4)<<3);
   for(unsigned k=0;k<6;k++)assert(atlas[(k*SLOTS+s)*64+m]==(index?colors[k][base+index]:0));
  }
 }
}
int main(void){
 for(unsigned i=0;i<sizeof(vram);i++)vram[i]=(i*37+i/13)&255;
 for(unsigned i=0;i<512;i++)palette_ram[i]=(i*29)&32767;
 gba_journal_config(2,atlas);gba_journal_row(0,0);
 reference(10,3);reference(20,32);drain();verify();
 unsigned s=lookup[10][3]-1;
 /* Queue deduplicates repeated tile+palette invalidations. */
 tv[10]++;vram[320]^=1;dirty_tile(10);dirty_tile(10);assert(queue_count==1);
 dirty_palette(3*16+1);assert(queue_count<=2);drain();verify();
 /* A real marked palette write updates only entries that use that index. */
 unsigned pi=3*16+(vram[320]&15);if(!(pi&15))pi=3*16+(vram[320]>>4);
 palette_ram[pi]^=32767;gba_journal_write(5,pi*2,2);drain();verify();
 /* The second physical half of an 8bpp tile has a reverse link too. */
 tv[21]++;vram[21*32]^=1;dirty_tile(21);assert(queue_count==1);drain();verify();
 /* An invisible entry keeps damage until first referenced again. */
 deactivate();tv[10]++;vram[321]^=1;dirty_tile(10);assert(!queue_count&&dirty[s]);
 reference(10,3);assert(queue_count==1);drain();verify();
 /* Coefficients can change while invisible; activation catches that version. */
 deactivate();coeffver++;reference(10,3);assert(queue_count==1);drain();verify();
 /* Force eviction and repeated reuse of already queued IDs. */
 for(unsigned i=0;i<SLOTS*3;i++)reference((i*17)%3072,(i/3072)%32);
 assert(nslots==SLOTS&&queue_count<=SLOTS);drain();verify();
 for(unsigned i=0;i<3072;i++){tv[i]++;vram[i*32]^=3;dirty_tile(i);}
 assert(queue_count<=SLOTS);drain();verify();
 for(unsigned i=0;i<512;i++)dirty_palette(i);
 assert(queue_count<=SLOTS);drain();verify();
 puts("PASS journal reverse links, 8bpp halves, inactive generations, queue dedup and eviction");
}
