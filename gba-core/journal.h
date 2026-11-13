/* GPL-2.0-or-later. Write-side video damage, consumed at line boundaries. */
#ifndef GBA_JOURNAL_H
#define GBA_JOURNAL_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Assembly uses these separate, naturally aligned bitmaps. */
extern unsigned gba_journal_enabled,gba_journal_pending;
extern uint32_t gba_journal_vram[96],gba_journal_pal[16],gba_journal_oam[4],gba_journal_regs[3];
static inline void gba_journal_mark(uint32_t *bits,unsigned n) {bits[n>>5]|=1u<<(n&31);}
static inline void gba_journal_write(unsigned region,unsigned offset,unsigned bytes) {
 if(!gba_journal_enabled)return;
 gba_journal_pending=1;
 if(region==6){gba_journal_mark(gba_journal_vram,offset>>5);gba_journal_mark(gba_journal_vram,(offset+bytes-1)>>5);}
 if(region==5){offset&=1023;gba_journal_mark(gba_journal_pal,offset>>1);gba_journal_mark(gba_journal_pal,((offset+bytes-1)&1023)>>1);}
 if(region==7){offset&=1023;gba_journal_mark(gba_journal_oam,offset>>3);gba_journal_mark(gba_journal_oam,((offset+bytes-1)&1023)>>3);}
 if(region==4 && offset<0x60)gba_journal_mark(gba_journal_regs,offset>>1);
}
void gba_journal_vram_range(unsigned offset,unsigned last);
void gba_journal_config(unsigned mode,void *atlas);
void gba_journal_reset(void);
void gba_journal_row(unsigned row,int skipped);
void gba_journal_read(uint64_t *ticks,unsigned *counts);
#ifdef __cplusplus
}
#endif
#endif
