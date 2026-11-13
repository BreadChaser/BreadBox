/* GPL-2.0-or-later. Production write journal and early conversion cost probe.
 * No GPU submission/composition yet: converted data is persistent GPU storage.
 * This probe never replaces CPU output or mutates emulated machine state. */
#include "common.h"
#include <stdint.h>
unsigned gba_journal_enabled,gba_journal_pending;
uint32_t gba_journal_vram[96],gba_journal_pal[16],gba_journal_oam[4],gba_journal_regs[3];
static unsigned mode;
static volatile uint32_t *pixels;
static uint64_t ticks[3];
static unsigned counts[10];
static uint32_t tv[3072],pv[34],coeffver=1;
#define SLOTS 2048
static uint16_t lookup[3072][34];
struct Entry {unsigned tile,bank,tv,pv,cv;};
static struct Entry entries[SLOTS];
static uint32_t colors[6][512],usage[SLOTS][8],changed_pal[16];
static unsigned nextslot,nslots,eva,evb,bldy,effect;
static unsigned char active[SLOTS],last_vram[98304];
static uint16_t last_pal[512];
static unsigned char last_oam[1024];
static uint16_t previous_regs[48];
static unsigned refs_dirty=1,palette_dirty=1;
/* Reverse dependencies and a bounded work queue: no cache walk at a line
 * boundary. Palette dependencies are sparse bit sets of cache slot IDs. */
static uint16_t tile_head[3072],tile_next[SLOTS*2],tile_prev[SLOTS*2];
static uint32_t palette_slots[512][SLOTS/32];
static uint16_t queue[SLOTS],active_slots[SLOTS];
static unsigned queued[SLOTS],dirty[SLOTS],queue_head,queue_tail,queue_count,nactive;
static void enqueue(unsigned s){
 dirty[s]=1;if(!active[s]||queued[s])return;
 queued[s]=1;queue[queue_tail]=s;queue_tail=(queue_tail+1)%SLOTS;queue_count++;
}
static void tile_link(unsigned node,unsigned tile){
 unsigned old=tile_head[tile];tile_prev[node]=0;tile_next[node]=old;
 if(old)tile_prev[old-1]=node+1;tile_head[tile]=node+1;
}
static void tile_unlink(unsigned node,unsigned tile){
 unsigned prev=tile_prev[node],next=tile_next[node];
 if(prev)tile_next[prev-1]=next;else tile_head[tile]=next;
 if(next)tile_prev[next-1]=prev;
}
static void palette_links(unsigned s,int add){
 struct Entry *e=&entries[s];unsigned base=e->bank<32?e->bank*16:e->bank==33?256:0;
 for(unsigned w=0;w<8;w++){uint32_t b=usage[s][w];while(b){unsigned i=__builtin_ctz(b)+32*w;b&=b-1;
  if(add)palette_slots[base+i][s>>5]|=1u<<(s&31);
  else palette_slots[base+i][s>>5]&=~(1u<<(s&31));
 }}
}
static void dirty_tile(unsigned tile){
 for(unsigned n=tile_head[tile];n;n=tile_next[n-1])enqueue((n-1)/2);
}
static void dirty_palette(unsigned i){
 for(unsigned w=0;w<SLOTS/32;w++){uint32_t b=palette_slots[i][w];while(b){unsigned s=__builtin_ctz(b)+w*32;b&=b-1;enqueue(s);}}
}

#ifdef __3DS__
extern unsigned long long svcGetSystemTick(void);
#define NOW() svcGetSystemTick()
#else
#include <time.h>
static uint64_t now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000000000ull+t.tv_nsec;}
#define NOW() now()
#endif
static unsigned bits(uint32_t x){return __builtin_popcount(x);}
void gba_journal_vram_range(unsigned offset,unsigned last){
 if(!gba_journal_enabled)return;
 unsigned end=(offset+last)>>5;
 for(unsigned n=offset>>5;n<=end;n++)gba_journal_mark(gba_journal_vram,n);
 gba_journal_pending=1;
}
void gba_journal_reset(void){
 gba_index_reset();
 memset(gba_journal_vram,255,sizeof(gba_journal_vram));memset(gba_journal_pal,255,sizeof(gba_journal_pal));
 memset(gba_journal_oam,255,sizeof(gba_journal_oam));memset(gba_journal_regs,255,sizeof(gba_journal_regs));
 memset(lookup,0,sizeof(lookup));memset(entries,0,sizeof(entries));memset(tv,0,sizeof(tv));memset(pv,0,sizeof(pv));
 memset(last_vram,255,sizeof(last_vram));memset(last_pal,255,sizeof(last_pal));memset(last_oam,255,sizeof(last_oam));memset(active,0,sizeof(active));
 memset(previous_regs,255,sizeof(previous_regs));
 memset(tile_head,0,sizeof(tile_head));memset(tile_next,0,sizeof(tile_next));memset(tile_prev,0,sizeof(tile_prev));
 memset(palette_slots,0,sizeof(palette_slots));memset(usage,0,sizeof(usage));
 memset(queued,0,sizeof(queued));memset(dirty,0,sizeof(dirty));queue_head=queue_tail=queue_count=nactive=0;
 nextslot=nslots=0;refs_dirty=palette_dirty=1;gba_journal_pending=1;
}
void gba_journal_config(unsigned m,void *atlas){mode=m;pixels=atlas;gba_journal_enabled=m!=0;memset(ticks,0,sizeof(ticks));memset(counts,0,sizeof(counts));gba_journal_reset();}
void gba_journal_read(uint64_t *t,unsigned *c){memcpy(t,ticks,sizeof(ticks));memcpy(c,counts,sizeof(counts));memset(ticks,0,sizeof(ticks));memset(counts,0,sizeof(counts));}
static unsigned clamp(unsigned n){return n>16?16:n;}
static uint32_t pack(unsigned r,unsigned g,unsigned b,unsigned a){return r<<24|g<<16|b<<8|a;}
static void palette(void){
 eva=clamp(read_ioreg(REG_BLDALPHA)&31);evb=clamp((read_ioreg(REG_BLDALPHA)>>8)&31);bldy=clamp(read_ioreg(REG_BLDY)&31);effect=(read_ioreg(REG_BLDCNT)>>6)&3;
 for(unsigned i=0;i<512;i++){
  unsigned c=eswap16(palette_ram[i]),r=c&31,g=(c>>5)&31,b=(c>>10)&31;
  colors[0][i]=pack(((r*eva)>>4)*8,((g*eva)>>4)*8,((b*eva)>>4)*8,255);
  colors[1][i]=pack(((r*eva)&15)*16,((g*eva)&15)*16,((b*eva)&15)*16,255);
  colors[2][i]=pack(((r*evb)>>4)*8,((g*evb)>>4)*8,((b*evb)>>4)*8,255);
  colors[3][i]=pack(((r*evb)&15)*16,((g*evb)&15)*16,((b*evb)&15)*16,255);
  colors[4][i]=pack(r*8,g*8,b*8,255);
  if(effect==2){r+=((31-r)*bldy)>>4;g+=((31-g)*bldy)>>4;b+=((31-b)*bldy)>>4;}
  if(effect==3){r-=(r*bldy)>>4;g-=(g*bldy)>>4;b-=(b*bldy)>>4;}
  colors[5][i]=pack(r*8,g*8,b*8,255);
 }
 palette_dirty=0;
}
static unsigned version(unsigned tile,unsigned bank){return tv[tile]+(bank>=32?tv[(tile+1)%3072]:0);}
static void decode(unsigned slot){
 palette_links(slot,0);
 struct Entry *e=&entries[slot];unsigned bank=e->bank,tile=e->tile,base=bank<32?bank*16:bank==33?256:0;
 const unsigned char *src=vram+tile*32;
 memset(usage[slot],0,sizeof(usage[slot]));
 for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++){
  unsigned i=y*8+x,index=bank>=32?src[i]:((src[i>>1]>>((i&1)*4))&15);
  if(index)usage[slot][index>>5]|=1u<<(index&31);
  unsigned m=(x&1)|((y&1)<<1)|((x&2)<<1)|((y&2)<<2)|((x&4)<<2)|((y&4)<<3);
  for(unsigned k=0;k<6;k++)pixels[(k*SLOTS+slot)*64+m]=index?colors[k][base+index]:0;
 }
 palette_links(slot,1);dirty[slot]=0;
 e->tv=version(tile,bank);e->pv=pv[bank];e->cv=coeffver;counts[5]++;
}
static void reference(unsigned tile,unsigned bank){
 tile%=3072;if(bank>=32)tile&=~1u;
 unsigned slot=lookup[tile][bank];if(slot){slot--;if(!active[slot]){active[slot]=1;active_slots[nactive++]=slot;}if(dirty[slot]||entries[slot].cv!=coeffver)enqueue(slot);return;}
 slot=nextslot++%SLOTS;
 if(nslots<SLOTS)nslots++;
 else {struct Entry *old=&entries[slot];lookup[old->tile][old->bank]=0;
 palette_links(slot,0);tile_unlink(slot*2,old->tile);if(old->bank>=32)tile_unlink(slot*2+1,old->tile+1);
 /* A slot may already occur in this rebuild's active list. Retain its ID. */
 }
 if(!active[slot]){active[slot]=1;active_slots[nactive++]=slot;}
 lookup[tile][bank]=slot+1;entries[slot]=(struct Entry){tile,bank,~0u,~0u,~0u};
 memset(usage[slot],0,sizeof(usage[slot]));tile_link(slot*2,tile);if(bank>=32)tile_link(slot*2+1,tile+1);enqueue(slot);
}
static void references(void){
 for(unsigned i=0;i<nactive;i++)active[active_slots[i]]=0;nactive=0;
 unsigned d=read_ioreg(REG_DISPCNT);if((d&7)>=3){refs_dirty=0;return;}
 for(unsigned bg=0;bg<4;bg++)if(d&(256u<<bg)){
  unsigned c=read_ioreg(REG_BGxCNT(bg)),base=((c>>8)&31)*2048,n=1024u<<((c>>14)&1);if(c&0x8000)n*=2;
  /* Text only in this first cost probe; affine uses fallback in full backend. */
  if((d&7)==2 || ((d&7)==1 && bg==2))continue;
  for(unsigned i=0;i<n;i++){
   unsigned e=eswap16(address16(vram,(base+i*2)&65535));
   reference(((c>>2)&3)*512+(e&1023)*(c&128?2:1),c&128?32:e>>12);
  }
 }
 if(d&4096)for(unsigned i=0;i<128;i++){
  unsigned a=eswap16(oam_ram[i*4]),b=eswap16(oam_ram[i*4+1]),c=eswap16(oam_ram[i*4+2]);
  unsigned shape=a>>14,size=b>>14;if(shape==3||(!(a&256)&&(a&512)))continue;
  static const unsigned ws[3][4]={{8,16,32,64},{16,32,32,64},{8,8,16,32}},hs[3][4]={{8,16,32,64},{8,8,16,32},{16,32,32,64}};
  unsigned w=ws[shape][size],h=hs[shape][size];int x=b&511;if(x>=256)x-=512;
  if(x>=240||x+(int)(a&512?w*2:w)<=0||((a&255)>=160&&(a&255)+(a&512?h*2:h)<=256))continue;
  unsigned step=a&8192?2:1,bank=a&8192?33:16+(c>>12),first=c&1023;if(step==2)first&=~1u;
  for(unsigned y=0;y<h/8;y++)for(unsigned x=0;x<w/8;x++)reference(2048+(first+y*(d&64?w/8*step:32)+x*step)%1024,bank);
 }
 refs_dirty=0;counts[6]++;
}
#ifdef BB_JOURNAL_AUDIT
#undef NDEBUG
#include <assert.h>
static unsigned char audit_vram[98304],audit_pal[1024],audit_oam[1024];
static unsigned audit_initialized;
static void audit(void){
 if(audit_initialized){
  for(unsigned i=0;i<3072;i++)if(memcmp(audit_vram+i*32,vram+i*32,32))assert(gba_journal_vram[i>>5]&(1u<<(i&31)));
  for(unsigned i=0;i<512;i++)if(memcmp(audit_pal+i*2,(char*)palette_ram+i*2,2))assert(gba_journal_pal[i>>5]&(1u<<(i&31)));
  for(unsigned i=0;i<128;i++)if(memcmp(audit_oam+i*8,(char*)oam_ram+i*8,8))assert(gba_journal_oam[i>>5]&(1u<<(i&31)));
 }
 memcpy(audit_vram,vram,sizeof(audit_vram));memcpy(audit_pal,palette_ram,sizeof(audit_pal));memcpy(audit_oam,oam_ram,sizeof(audit_oam));audit_initialized=1;
}
#endif
void gba_journal_row(unsigned row,int skipped){
 if(!mode)return;
#ifdef BB_JOURNAL_AUDIT
 audit();
#endif
 /* Journal remains pending through skipped guest frames; source versions are
  * consumed before drawn rows. This probe doesn't submit the mutated atlas. */
 if(skipped)return;
 if(!row)counts[0]++;
 gba_index_damage(row);
 if(gba_index_frame()){
  if(gba_journal_pending){memset(gba_journal_vram,0,sizeof(gba_journal_vram));memset(gba_journal_pal,0,sizeof(gba_journal_pal));memset(gba_journal_oam,0,sizeof(gba_journal_oam));memset(gba_journal_regs,0,sizeof(gba_journal_regs));gba_journal_pending=0;}
  return;
 }
 if(!gba_journal_pending)return;
 uint64_t start=NOW();gba_journal_pending=0;unsigned changed=0;
 memcpy(changed_pal,gba_journal_pal,sizeof(changed_pal));
 for(unsigned w=0;w<96;w++){uint32_t b=gba_journal_vram[w];gba_journal_vram[w]=0;counts[1]+=bits(b);while(b){unsigned i=__builtin_ctz(b)+w*32;b&=b-1;if(!memcmp(last_vram+i*32,vram+i*32,32))continue;memcpy(last_vram+i*32,vram+i*32,32);tv[i]++;dirty_tile(i);changed=refs_dirty=1;}}
 for(unsigned w=0;w<16;w++){uint32_t b=gba_journal_pal[w];gba_journal_pal[w]=0;counts[2]+=bits(b);while(b){unsigned i=__builtin_ctz(b)+w*32;b&=b-1;if(last_pal[i]==palette_ram[i]){changed_pal[w]&=~(1u<<(i&31));continue;}last_pal[i]=palette_ram[i];dirty_palette(i);pv[i/16]++;pv[i<256?32:33]++;changed=palette_dirty=1;}}
 for(unsigned w=0;w<4;w++){uint32_t b=gba_journal_oam[w];counts[3]+=bits(b);gba_journal_oam[w]=0;while(b){unsigned i=__builtin_ctz(b)+w*32;b&=b-1;if(!memcmp(last_oam+i*8,(char*)oam_ram+i*8,8))continue;memcpy(last_oam+i*8,(char*)oam_ram+i*8,8);refs_dirty=1;}}
 for(unsigned w=0;w<3;w++){uint32_t b=gba_journal_regs[w];gba_journal_regs[w]=0;while(b){unsigned i=__builtin_ctz(b)+w*32;b&=b-1;if(i>=48||i==2||i==3)continue;unsigned v=eswap16(io_registers[i]);if(v==previous_regs[i])continue;previous_regs[i]=v;counts[4]++;if(row)counts[7]++;if(i==0||(i>=4&&i<=7))refs_dirty=1;if(i>=40&&i<=42){coeffver++;for(unsigned a=0;a<nactive;a++)enqueue(active_slots[a]);changed=palette_dirty=1;}}}
 ticks[0]+=NOW()-start;if(mode==1||!pixels)return;
 start=NOW();if(refs_dirty)references();ticks[1]+=NOW()-start;
 if(!changed && !palette_dirty && nslots==0)return;
 start=NOW();if(palette_dirty)palette();
 while(queue_count){unsigned s=queue[queue_head];queue_head=(queue_head+1)%SLOTS;queue_count--;queued[s]=0;
  if(active[s]&&dirty[s])decode(s);
 }
 ticks[2]+=NOW()-start;counts[8]=nslots;
}
