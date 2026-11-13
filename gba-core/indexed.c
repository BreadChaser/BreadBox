/* GPL-2.0-or-later. CPU records writes and tile geometry; PICA owns pixels. */
#include "common.h"
#include "indexed.h"
#ifdef __3DS__
extern unsigned long long svcGetSystemTick(void);
#define INDEX_NOW() svcGetSystemTick()
#else
#include <time.h>
static uint64_t index_now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000000000ull+t.tv_nsec;}
#define INDEX_NOW() index_now()
#endif
unsigned gba_index_enabled;
static struct GixFrame f;
static struct GixStats stats;
static unsigned char *raw[2];
static uint16_t slotmap[2][3072],updates[2][GIX_SLOTS];
static unsigned used[2],nupdates[2],frame_generation,slot_frame[2][GIX_SLOTS];
static uint32_t versions[3072],slot_version[2][GIX_SLOTS];
static unsigned char shadow[98304];
static uint32_t pver[34],palette_serial;
static uint16_t oldpal[512],oldoam[512],oldregs[48];
static unsigned damage,initialized,frame_fallback,last_why;
static unsigned morton(unsigned x,unsigned y){return (x&1)|((y&1)<<1)|((x&2)<<1)|((y&2)<<2)|((x&4)<<2)|((y&4)<<3);}
void gba_index_reset(void){initialized=0;damage=1;memset(slotmap,0,sizeof(slotmap));memset(versions,0,sizeof(versions));memset(used,0,sizeof(used));memset(slot_frame,0,sizeof(slot_frame));memset(&f,0,sizeof(f));}
void gba_index_config(unsigned n,void *r4,void *r8){gba_index_enabled=n;raw[0]=(unsigned char*)r4;raw[1]=(unsigned char*)r8;memset(&stats,0,sizeof(stats));gba_index_reset();}
const struct GixFrame *gba_index_frame(void){return gba_index_enabled?&f:NULL;}
const unsigned char *gba_index_raw(unsigned bits){return raw[bits==8];}
const uint16_t *gba_index_updates(unsigned bits,unsigned *n){*n=nupdates[bits==8];return updates[bits==8];}
struct GixStats gba_index_stats(void){return stats;}
void gba_index_damage(unsigned row){
 if(!gba_index_enabled)return;
 uint64_t begin=INDEX_NOW();
 if(!row){
  /* Reclaim only between frames, after the frontend reader fence. */
  if(used[0]>GIX_SLOTS-512||used[1]>GIX_SLOTS-512){memset(slotmap,0,sizeof(slotmap));used[0]=used[1]=0;}
  memset(&f,0,sizeof(f));frame_fallback=0;nupdates[0]=nupdates[1]=0;frame_generation++;stats.frames++;damage=1;}
 if(!gba_journal_pending){stats.ticks[0]+=INDEX_NOW()-begin;return;}
 for(unsigned w=0;w<96;w++){uint32_t b=gba_journal_vram[w];stats.journal_words++;while(b){unsigned t=w*32+__builtin_ctz(b);b&=b-1;
  if(initialized&&!memcmp(shadow+t*32,vram+t*32,32))continue;
  memcpy(shadow+t*32,vram+t*32,32);versions[t]++;stats.dirty++;damage=1;
 }}
 for(unsigned w=0;w<16;w++){uint32_t b=gba_journal_pal[w];while(b){unsigned i=w*32+__builtin_ctz(b);b&=b-1;if(!initialized||oldpal[i]!=palette_ram[i]){oldpal[i]=palette_ram[i];pver[i/16]=pver[i<256?32:33]=++palette_serial;stats.palette_entries++;damage=1;}}}
 for(unsigned w=0;w<4;w++){uint32_t b=gba_journal_oam[w];while(b){unsigned i=w*32+__builtin_ctz(b);b&=b-1;if(!initialized||memcmp(oldoam+i*4,oam_ram+i*4,8)){memcpy(oldoam+i*4,oam_ram+i*4,8);damage=1;}}}
 for(unsigned w=0;w<3;w++){uint32_t b=gba_journal_regs[w];while(b){unsigned i=w*32+__builtin_ctz(b);b&=b-1;if(i>=48||i==2||i==3)continue;uint16_t v=eswap16(io_registers[i]);if(!initialized||v!=oldregs[i]){oldregs[i]=v;damage=1;}}}
 initialized=1;stats.ticks[0]+=INDEX_NOW()-begin;
}
static unsigned tile(unsigned t,unsigned bits){
 unsigned b=bits==8;if(b)t&=~1u;t%=3072;
 unsigned id=slotmap[b][t],ver=versions[t]+(b?versions[t+1]:0);
 if(id&&slot_version[b][id-1]==ver){slot_frame[b][id-1]=frame_generation;return id-1;}
 /* Never overwrite an index generation already referenced by this frame. */
 if(!id||slot_frame[b][id-1]==frame_generation){
  unsigned s=used[b]++;
  if(s>=GIX_SLOTS){used[b]=GIX_SLOTS;return GIX_SLOTS;}
  id=s+1;slotmap[b][t]=id;
 }
 uint64_t begin=INDEX_NOW();
 unsigned s=id-1;unsigned char *dst=raw[b]+s*(b?64:32);memset(dst,0,b?64:32);
 for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++){
  unsigned m=morton(x,y),i=y*8+x;
  if(b)dst[m]=vram[t*32+i];else dst[m/2]|=((vram[t*32+i/2]>>((i&1)*4))&15)<<((m&1)*4);
 }
 slot_version[b][s]=ver;slot_frame[b][s]=frame_generation;updates[b][nupdates[b]++]=s;stats.uploads++;stats.bytes+=b?64:32;stats.ticks[1]+=INDEX_NOW()-begin;return s;
}
static void quad(struct GixBand *b,int x,int y,unsigned t,unsigned bits,unsigned bank,unsigned flip,unsigned layer,unsigned rank,unsigned semi,unsigned obj){
 if(x>=244||x+8<=-4||y>=160||y+8<=(int)b->first)return;
 if(f.nq==GIX_QUADS){f.fallback|=GIX_RESOURCE;return;}
 unsigned s=tile(t,bits);if(s==GIX_SLOTS){f.fallback|=GIX_RESOURCE;return;}
 f.quads[f.nq++]=(struct GixQuad){x,y,s,bank,bits,flip,layer,rank,semi,obj};b->nq++;
}
/* Exact unit affine transforms share the regular tile geometry. General
 * affine sampling is admitted separately once the PICA coordinate gate passes. */
static int unit_affine(unsigned attr1,int *fx,int *fy){
 unsigned m=(attr1>>9)&31;int pa=(int16_t)eswap16(oam_ram[m*16+3]),pb=(int16_t)eswap16(oam_ram[m*16+7]),pc=(int16_t)eswap16(oam_ram[m*16+11]),pd=(int16_t)eswap16(oam_ram[m*16+15]);
 if(pb||pc||(pa!=256&&pa!=-256)||(pd!=256&&pd!=-256))return 0;
 *fx=pa<0;*fy=pd<0;return 1;
}
static void build(struct GixBand *b){
 unsigned d=b->regs[0];if(d&128)return;
 for(unsigned bg=0;bg<4;bg++)if((d&(256u<<bg))&&!((d&7)==1&&bg==3)){
  unsigned c=b->regs[4+bg],bits=c&128?8:4,base=((c>>8)&31)*2048,chr=((c>>2)&3)*512;
  unsigned width=c&0x4000?512:256,height=c&0x8000?512:256;
  unsigned hx=b->regs[8+bg*2]&511,hy=b->regs[9+bg*2]&511;
  int y0=(int)b->first-((b->first+hy)&7);
  for(int y=y0;y<160;y+=8)for(int x=-(int)(hx&7)-8;x<248;x+=8){
   unsigned tx=((x+(int)hx)&(width-1))/8,ty=((y+(int)hy)&(height-1))/8;
   unsigned cell=(ty&31)*32+(tx&31)+((tx>>5)+(ty>>5)*(width/256))*1024;
   unsigned e=eswap16(address16(vram,(base+cell*2)&65535));
   quad(b,x,y,(chr+(e&1023)*(bits/4))&2047,bits,bits==8?32:e>>12,(e>>10)&3,bg,(c&3)*5+bg+2,0,255);
  }
 }
 if(!(d&4096))return;
 static const unsigned ws[3][4]={{8,16,32,64},{16,32,32,64},{8,8,16,32}},hs[3][4]={{8,16,32,64},{8,8,16,32},{16,32,32,64}};
 for(unsigned o=0;o<128;o++){
  unsigned a=eswap16(oam_ram[o*4]),bb=eswap16(oam_ram[o*4+1]),c=eswap16(oam_ram[o*4+2]);
  unsigned shape=a>>14,size=bb>>14,om=(a>>10)&3;if(shape==3||om==3||(a&0x300)==0x200)continue;
  unsigned w=ws[shape][size],h=hs[shape][size],bits=a&8192?8:4,step=bits/4;
  int ox=bb&511;if(ox>=256)ox-=512;int oy=a&255;if(oy>160)oy-=256;
  if(ox>=240||ox+(int)((a&0x300)==0x300?w*2:w)<=0||oy>=160||oy+(int)((a&0x300)==0x300?h*2:h)<=(int)b->first)continue;
  unsigned flip=(bb>>12)&3;int adjustx=0,adjusty=0;
  if(a&256){int fx,fy;if(!unit_affine(bb,&fx,&fy))continue;flip=fx|(fy<<1);
   if(a&512){adjustx=w/2;adjusty=h/2;}
   /* Negative unit matrices sample centre+extent at the left edge: one pixel
    * is transparent, and the opposite edge is clipped by the OBJ bounds. */
   if(fx)adjustx++;if(fy)adjusty++;
  }
  unsigned first=c&1023;if(bits==8)first&=~1u;
  for(unsigned y=0;y<h/8;y++)for(unsigned x=0;x<w/8;x++){
   unsigned sx=flip&1?w/8-1-x:x,sy=flip&2?h/8-1-y:y;
   quad(b,ox+adjustx+x*8,oy+adjusty+y*8,2048+(first+sy*(d&64?w/8*step:32)+sx*step)%1024,bits,bits==8?33:16+(c>>12),flip,om==2?5:4,(c>>10&3)*5+1,om==1,o);
  }
 }
}
static unsigned unsupported(void){
 unsigned d=read_ioreg(REG_DISPCNT),why=0;if(d&128)return 0;
 if((d&7)>=3)why|=GIX_BITMAP;
 if(((d&7)==2&&(d&0xc00))||((d&7)==1&&(d&0x400)))why|=GIX_AFFINE_BG;
 for(unsigned bg=0;bg<4;bg++)if((d&(256u<<bg))&&(read_ioreg(REG_BGxCNT(bg))&64)&&(read_ioreg(REG_MOSAIC)&255))why|=GIX_MOSAIC;
 if(d&0x8000)why|=GIX_OBJ_WINDOW;
 if(d&4096){unsigned cycles[160]={0};
 static const unsigned ws[3][4]={{8,16,32,64},{16,32,32,64},{8,8,16,32}},hs[3][4]={{8,16,32,64},{8,8,16,32},{16,32,32,64}};
 for(unsigned o=0;o<128;o++){unsigned a=eswap16(oam_ram[o*4]),b=eswap16(oam_ram[o*4+1]);if((a>>14)==3||(a&0x300)==0x200||((a>>10)&3)==3)continue;
  int x=(b&511);if(x>=256)x-=512;int y=a&255;if(y>160)y-=256;
  unsigned w=ws[a>>14][b>>14],h=hs[a>>14][b>>14];if(x>=240||x+(int)w<=0||y>=160||y+(int)h<=0)continue;
  if(a&256){int fx,fy;if(!unit_affine(b,&fx,&fy)||fx||fy)why|=GIX_AFFINE_OBJ;}
  if((a&4096)&&(read_ioreg(REG_MOSAIC)&0xff00))why|=GIX_MOSAIC;
  for(int row=MAX(y,0);row<MIN(y+(int)h,160);row++){if(cycles[row]>=(d&32?954:1210))why|=GIX_OBJ_BUDGET;cycles[row]+=w;}
 }}return why;
}
int gba_index_row(unsigned row,const int half[8],int stereo){
 if(!gba_index_enabled)return 0;
 uint64_t begin=INDEX_NOW(),upload_before=stats.ticks[1];
 unsigned why=damage?unsupported():last_why;last_why=why;
 unsigned was_fallback=frame_fallback;
 if(frame_fallback)f.fallback|=why;
 if(!frame_fallback&&(why||(f.nb==GIX_BANDS&&damage))){
  f.fallback|=why?why:GIX_RESOURCE;frame_fallback=1;f.prefix=row;
 }
 if(!frame_fallback&&damage){
  if(f.nb){struct GixBand *old=&f.bands[f.nb-1];old->last=row;
   unsigned n=old->firstq;for(unsigned i=old->firstq;i<old->firstq+old->nq;i++)if(f.quads[i].y<(int)row)f.quads[n++]=f.quads[i];old->nq=n-old->firstq;f.nq=n;
  }
  struct GixBand *b=&f.bands[f.nb++];memset(b,0,sizeof(*b));b->first=row;b->last=160;b->firstq=f.nq;
  for(unsigned i=0;i<48;i++)b->regs[i]=eswap16(io_registers[i]);
  for(unsigned i=0;i<512;i++)b->pal[i]=eswap16(palette_ram[i]);
  memcpy(b->pver,pver,sizeof(pver));
  for(unsigned i=0;i<8;i++)b->half[i]=stereo?half[i]:0;
  build(b);stats.bands++;stats.quads+=b->nq;damage=0;
  if(f.fallback){frame_fallback=1;f.prefix=row;b->last=row;}
 }
 if(frame_fallback&&f.nb)f.bands[f.nb-1].last=MIN(f.bands[f.nb-1].last,row);
 f.stereo=stereo;
 if(row==159){if(frame_fallback){stats.fallback++;for(unsigned i=0;i<7;i++)if(f.fallback&(1u<<i))stats.reasons[i]++;}else stats.gpu++;}
 stats.ticks[2]+=INDEX_NOW()-begin-(stats.ticks[1]-upload_before);
 return frame_fallback?(!was_fallback&&row&&gba_index_enabled==1?-1:0):gba_index_enabled==1;
}
/* Independent pixel oracle; this file's production common path stops above. */
static unsigned sample(const struct GixQuad *q,int x,int y){
 unsigned xx=x-q->x,yy=y-q->y;if(q->flip&1)xx=7-xx;if(q->flip&2)yy=7-yy;
 unsigned m=morton(xx,yy);const unsigned char *p=raw[q->bits==8]+q->slot*(q->bits==8?64:32);
 return q->bits==8?p[m]:(p[m/2]>>((m&1)*4))&15;
}
static int inwy(unsigned y,unsigned v){unsigned t=v>>8,b=v&255;if(t>227)return 0;if(b>227)return 1;return t>b?y>t||y<=b:y>=t&&y<b;}
static unsigned mask(const struct GixBand *b,int x,int y,int objwin){
 unsigned d=b->regs[0];if(!(d&0xe000))return 63;
 for(unsigned w=0;w<2;w++)if((d&(8192u<<w))&&inwy(y,b->regs[34+w])){unsigned l=b->regs[32+w]>>8,r=b->regs[32+w]&255;if(l!=r&&(l<r?(x>=(int)l&&x<(int)r):(x<(int)r||x>=(int)l)))return (b->regs[36]>>(8*w))&63;}
 return (b->regs[37]>>(objwin&&(d&0x8000)?8:0))&63;
}
void gba_index_raster(uint16_t *left,uint16_t *right,unsigned end){
 for(unsigned bi=0;bi<f.nb;bi++){const struct GixBand *b=&f.bands[bi];unsigned last=MIN(end,b->last);
 for(unsigned eye=0;eye<(right?2u:1u);eye++){uint16_t *out=eye?right:left;int es=eye?1:-1;
 static uint16_t ci[5][38400],oi[38400];static uint8_t cr[5][38400],ss[38400],ow[38400];
 memset(ci,0,sizeof(ci));memset(cr,99,sizeof(cr));memset(oi,255,sizeof(oi));memset(ss,0,sizeof(ss));memset(ow,0,sizeof(ow));
 for(unsigned qi=b->firstq;qi<b->firstq+b->nq;qi++){const struct GixQuad *q=&f.quads[qi];int shift=q->layer<4?b->half[q->layer]:q->layer==5?0:b->half[4+(q->rank-1)/5];int ox=q->x+shift*es;
  for(int y=MAX(q->y,(int)b->first);y<MIN(q->y+8,(int)last);y++)for(int x=MAX(ox,0);x<MIN(ox+8,240);x++){
   unsigned i=sample(q,x-shift*es,y),p=y*240+x;if(!i)continue;
   if(q->layer==5){ow[p]=1;continue;}unsigned l=q->layer;
   if(q->rank<cr[l][p]||(q->rank==cr[l][p]&&q->obj<oi[p])){ci[l][p]=(q->bank<32?q->bank*16:q->bank==33?256:0)+i;cr[l][p]=q->rank;if(l==4){ss[p]=q->semi;oi[p]=q->obj;}}
  }
 }
 for(unsigned y=b->first;y<last;y++)for(int x=0;x<240;x++){
  if(b->regs[0]&128){out[y*240+x]=65535;continue;}
  unsigned p=y*240+x,idx[5],rank[5],semi=ss[p],objwin=ow[p];for(unsigned i=0;i<5;i++){idx[i]=ci[i][p];rank[i]=cr[i][p];}
  unsigned m=mask(b,x,y,objwin),top=5,second=5,tr=22,sr=22;
  if(!(m&31))m|=32;
  for(unsigned l=0;l<5;l++)if((m&(1u<<l))&&rank[l]<tr){second=top;sr=tr;top=l;tr=rank[l];}else if((m&(1u<<l))&&rank[l]<sr){second=l;sr=rank[l];}
  unsigned c=b->pal[top==5?0:idx[top]],d=b->pal[second==5?0:idx[second]],bc=b->regs[40],eff=bc>>6&3;
  unsigned st=top==4&&semi,blend=(st||((m&32)&&eff==1&&(bc&(1u<<top))))&&(bc&(256u<<second));
  unsigned rgb[3];for(unsigned k=0;k<3;k++){unsigned a=(c>>(k*5))&31,bb=(d>>(k*5))&31,limit=k==1?63:31;if(k==1){a*=2;bb*=2;}
   if(blend)a=MIN(limit,(a*MIN(16,b->regs[41]&31)+bb*MIN(16,b->regs[41]>>8&31))>>4);
   else if((m&32)&&(bc&(1u<<top))&&!st){unsigned f=MIN(16,b->regs[42]&31);if(eff==2)a=((limit*f)>>4)+((a*(16-f))>>4);if(eff==3)a=(a*(16-f))>>4;}
   rgb[k]=a;
  }out[y*240+x]=(rgb[0]<<11)|(rgb[1]<<5)|rgb[2];
 }}}
}
