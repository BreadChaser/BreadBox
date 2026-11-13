/* GPL-2.0-or-later. Journal-backed native-index composition protocol. */
#ifndef GBA_INDEXED_H
#define GBA_INDEXED_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define GIX_SLOTS 4096
#define GIX_QUADS 12288
/* At most one immutable band begins at each visible scanline. */
#define GIX_BANDS 160
/* No colour-expanded tile storage. Slot data is PICA Morton L4/L8. */
struct GixQuad {int16_t x,y;uint16_t slot;uint8_t bank,bits,flip,layer,rank,semi,obj;};
struct GixBand {uint16_t regs[48],pal[512];uint32_t pver[34];int8_t half[8];uint16_t first,last;uint32_t firstq,nq;};
struct GixFrame {struct GixBand bands[GIX_BANDS];struct GixQuad quads[GIX_QUADS];unsigned nb,nq,fallback,prefix,stereo;};
enum GixReason {GIX_BITMAP=1,GIX_AFFINE_BG=2,GIX_AFFINE_OBJ=4,GIX_MOSAIC=8,GIX_OBJ_BUDGET=16,GIX_RESOURCE=32,GIX_OBJ_WINDOW=64};
struct GixStats {uint64_t frames,gpu,fallback,reasons[7],dirty,uploads,bytes,quads,bands,palette_entries,journal_words,ticks[3];};
/* Zero when the optional compositor is inactive; skip scanline setup then. */
extern unsigned gba_index_enabled;
void gba_index_config(unsigned,void *raw4,void *raw8);
void gba_index_reset(void);
void gba_index_damage(unsigned row);
int gba_index_row(unsigned row,const int half[8],int stereo);
const struct GixFrame *gba_index_frame(void);
const unsigned char *gba_index_raw(unsigned bits);
const uint16_t *gba_index_updates(unsigned bits,unsigned *count);
struct GixStats gba_index_stats(void);
/* Test oracle and rare late-frame fallback only. Never used on a common GPU frame. */
void gba_index_raster(uint16_t *left,uint16_t *right,unsigned end);
#ifdef __cplusplus
}
#endif
#endif
