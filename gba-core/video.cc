/* gameplaySP
 *
 * Copyright (C) 2006 Exophase <exophase@gmail.com>
 * Copyright (C) 2023 David Guillen Fandos <david@davidgf.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

extern "C" {
  #include "common.h"
}

// Diagnostic scopes account exclusive time, including recursive OBJ windows.
#ifdef BBGBA_TIMING
extern "C" unsigned long long svcGetSystemTick(void);
static bool timing_on;
static u64 timing_ticks[6], timing_last;
static unsigned timing_calls[6];
static int timing_active=-1;
extern "C" void gba_timing_config(int on) { timing_on=on; timing_active=-1; memset(timing_ticks,0,sizeof(timing_ticks)); memset(timing_calls,0,sizeof(timing_calls)); }
extern "C" void gba_timing_read(u64 *t,unsigned *c) { memcpy(t,timing_ticks,sizeof(timing_ticks));memcpy(c,timing_calls,sizeof(timing_calls));memset(timing_ticks,0,sizeof(timing_ticks));memset(timing_calls,0,sizeof(timing_calls)); }
struct CpuTimer {
 int previous; bool enabled;
 CpuTimer(int category):previous(-1),enabled(timing_on) { if(enabled) { u64 now=svcGetSystemTick();previous=timing_active;if(previous>=0)timing_ticks[previous]+=now-timing_last;timing_active=category;timing_last=now;timing_calls[category]++; } }
 ~CpuTimer() { if(enabled) { u64 now=svcGetSystemTick();timing_ticks[timing_active]+=now-timing_last;timing_active=previous;timing_last=now; } }
};
#define CPU_TIME(category) CpuTimer cpu_timer(category)
#else
#define CPU_TIME(category) ((void)0)
#endif

// Derived palette products, never serialized. Guest stores and state restores
// invalidate them, including assembly stores and state loads.
extern "C" { u32 gba_palette_generation; }
static bool cpu_blend=true,cpu_bitmap=false;
extern "C" void gba_cpu_config(unsigned options) {cpu_blend=(options&2)!=0;cpu_bitmap=(options&16)!=0;gba_palette_generation++;}
u16* gba_screen_pixels = NULL;

/* Stereo is renderer-only state: never serialized or written to emulated OAM.
 * Depth is total eye separation in Q8 native pixels (positive = behind).
 * Observe a complete frame, debounce role changes, then slew to the new depth.
 * Neither transparency nor palette/scroll animation changes layer identity. */
static u16 *stereo_right;
static unsigned stereo_strength, stereo_hold, stereo_ramp;
static bool stereo_flat;
static unsigned stereo_frame_flags;
// A base BG has no underlying contributors: its already composed row can be
// shifted verbatim. Decode only the newly exposed edge in the second eye.
// Never cache across scanlines (VRAM, palettes and priorities may change).
static int stereo_eye, stereo_half[8];
struct StereoBaseSpan { unsigned start,end,layer,mode,offset,width; };
static StereoBaseSpan stereo_base_spans[8];
static unsigned stereo_base_count, stereo_base_bytes;
static u32 stereo_base_row[240];
static bool stereo_row_cache, stereo_frame_different;
static bool stereo_admitted=true;
// Frontend decides once, before emulation: a rejected frame is entirely mono.
// This does not reset the stable depth estimator or allocate/free eye buffers.
extern "C" void gba_stereo_admit(int allowed) { stereo_admitted=allowed!=0; }

static int stereo_obj_shift[5]; // OBJ-window mask (4) stays at the screen
static int stereo_depth[8], stereo_target[8], stereo_candidate[8];
static unsigned stereo_votes[4][4], stereo_obj_votes[4], stereo_candidate_frames;
#ifdef GBA_STEREO_DEBUG
static u16 stereo_debug_pixels[240*160];
static int stereo_debug_depth[160][8];
static bool stereo_debug_pass;
static unsigned stereo_debug_obj;
extern "C" const u16 *gba_stereo_debug_pixels(void) { return stereo_debug_pixels; }
extern "C" const int *gba_stereo_debug_depths(void) { return &stereo_debug_depth[0][0]; }
#endif
extern "C" void gba_stereo_config(u16 *right, unsigned strength) {
  if (!strength && !stereo_strength && right == stereo_right) return;
  if (right != stereo_right || !strength) {
    stereo_hold = stereo_ramp = stereo_candidate_frames = 0;
    memset(stereo_depth,0,sizeof(stereo_depth));
    memset(stereo_target,0,sizeof(stereo_target));
    memset(stereo_candidate,0,sizeof(stereo_candidate));
    memset(stereo_votes,0,sizeof(stereo_votes));
    memset(stereo_obj_votes,0,sizeof(stereo_obj_votes));
    stereo_flat = true;
  }
  stereo_right = right;
  stereo_strength = strength > 256 ? 256 : strength;
}
extern "C" unsigned gba_stereo_flags(void) { return stereo_frame_flags; }
extern "C" int gba_stereo_valid(void) { return stereo_right && stereo_strength && stereo_frame_different && stereo_admitted; }

static void stereo_frame_depths(void) {
  int prio[4], order[4], count=0, next[8]={0};
  for (int bg=0;bg<4;bg++) {
    unsigned best=0; prio[bg]=-1;
    for (int p=0;p<4;p++) if (stereo_votes[bg][p]>best) {
      best=stereo_votes[bg][p]; prio[bg]=p;
    }
  }
  // Front to back, same hardware tie break as order_layers.
  for(int p=0;p<4;p++) for(int bg=0;bg<4;bg++)
    if(prio[bg]==p) order[count++]=bg;
  // Main non-HUD OBJ priority estimates the playfield. Lower-numbered BGs
  // in front of it are overlays/foreground and stay near the screen, rather
  // than letting an enabled but transparent BG consume a whole depth plane.
  int actor=0;
  for(int p=1;p<4;p++) if(stereo_obj_votes[p]>stereo_obj_votes[actor] ||
      (!actor && stereo_obj_votes[p])) actor=p;
  int anchor=0;
  for(int rank=0;rank<count;rank++) {
    if(prio[order[rank]]<=actor) anchor=rank;
    if(prio[order[rank]]==actor) { anchor=rank; break; }
  }
  int depths[4]={0};
  for(int rank=0;rank<count;rank++) {
    if(rank>anchor) depths[rank]=8*(rank-anchor)/(count-1-anchor);
    next[order[rank]]=depths[rank]*256;
  }
  for(int p=0;p<4;p++) {
    if(p<=actor) { next[4+p]=(p==actor ? -2 : 0)*256; continue; }
    // OBJ wins ties against BG. Lower-priority OBJ scenery stays behind
    // foreground BGs; main actors are two pixels in front of their playfield.
    next[4+p]=(count ? depths[count-1] : 0)*256;
    for(int rank=0;rank<count;rank++) if(prio[order[rank]]>=p) {
      next[4+p]=(depths[rank] ? MAX(rank ? depths[rank-1] : -2,depths[rank]-2) : -2)*256; break;
    }
  }
  if(memcmp(next,stereo_candidate,sizeof(next))) {
    memcpy(stereo_candidate,next,sizeof(next)); stereo_candidate_frames=0;
  } else if(stereo_candidate_frames<12) {
    if(++stereo_candidate_frames==12) memcpy(stereo_target,next,sizeof(next));
  }
  for(int i=0;i<8;i++) {
    int delta=stereo_target[i]-stereo_depth[i];
    stereo_depth[i]+=MAX(-32,MIN(32,delta)); // <= 1/8 native pixel/frame
  }
  memset(stereo_votes,0,sizeof(stereo_votes));
  memset(stereo_obj_votes,0,sizeof(stereo_obj_votes));
}

#define get_screen_pixels()   gba_screen_pixels
#define get_screen_pitch()    GBA_SCREEN_PITCH

typedef struct {
  u16 attr0, attr1, attr2, attr3;
} t_oam;

typedef struct {
  u16 pad0[3];
  u16 dx;
  u16 pad1[3];
  u16 dmx;
  u16 pad2[3];
  u16 dy;
  u16 pad3[3];
  u16 dmy;
} t_affp;

typedef void (* bitmap_render_function)(
  u32 start, u32 end, void *dest_ptr, const u16 *pal);
typedef void (* tile_render_function)(
  u32 layer, u32 start, u32 end, void *dest_ptr, const u16 *pal);

typedef void (*render_function_u16)(
  u32 start, u32 end, u16 *scanline, u32 enable_flags);
typedef void (*render_function_u32)(
  u32 start, u32 end, u32 *scanline, u32 enable_flags);

typedef void (*window_render_function)(u16 *scanline, u32 start, u32 end);

static void render_scanline_conditional(
  u32 start, u32 end, u16 *scanline, u32 enable_flags = 0x3F);

typedef struct
{
  bitmap_render_function blit_render;
  bitmap_render_function scale_render;
  bitmap_render_function affine_render;
} bitmap_layer_render_struct;

typedef struct
{
  render_function_u16 fullcolor;
  render_function_u16 indexed_u16;
  render_function_u32 indexed_u32;
  render_function_u32 stacked;
} layer_render_struct;

// Object blending modes
#define OBJ_MOD_NORMAL     0
#define OBJ_MOD_SEMITRAN   1
#define OBJ_MOD_WINDOW     2
#define OBJ_MOD_INVALID    3

// BLDCNT color effect modes
#define COL_EFFECT_NONE   0x0
#define COL_EFFECT_BLEND  0x1
#define COL_EFFECT_BRIGHT 0x2
#define COL_EFFECT_DARK   0x3

// Background render modes
#define RENDER_NORMAL   0
#define RENDER_COL16    1
#define RENDER_COL32    2
#define RENDER_ALPHA    3


// Byte lengths of complete tiles and tile rows in 4bpp and 8bpp.

#define tile_width_4bpp   4
#define tile_size_4bpp   32
#define tile_width_8bpp   8
#define tile_size_8bpp   64

// Sprite rendering cycles
#define REND_CYC_MAX          32768   /* Theoretical max is 17920 */
#define REND_CYC_SCANLINE      1210
#define REND_CYC_REDUCED        954

// Generate bit mask (bits 9th and 10th) with information about the pixel
// status (1st and/or 2nd target) for later blending.
static inline u16 color_flags(u32 layer) {
#ifdef GBA_STEREO_DEBUG
  if (stereo_debug_pass) return ((layer==5 ? 8 : layer==4 ? 4+stereo_debug_obj : layer)<<12) |
    ((layer<4 ? read_ioreg(REG_BGxCNT(layer))&3 : layer==4 ? stereo_debug_obj&3 : 0)<<9);
#endif
  u32 bldcnt = read_ioreg(REG_BLDCNT);
  return (
    ((bldcnt >> layer) & 0x01) |            // 1st target
    ((bldcnt >> (layer + 7)) & 0x02)        // 2nd target
  ) << 9;
}

static const u32 map_widths[] = { 256, 512, 256, 512 };

typedef enum
{
  DUALCOLOR,  // Decode once, write RGB565 to both eye compositions
  FULLCOLOR,  // Regular rendering, output a 16 bit color
  INDXCOLOR,  // Rendering to indexed color, so we can later apply dark/bright
  STCKCOLOR,  // Stacks two indexed pixels (+flags) to apply blending
  PIXCOPY     // Special mode used for sprites, to allow for obj-window drawing
} rendtype;

static intptr_t stereo_dual_offset;
template<rendtype mode, typename T, typename C>
static inline void stereo_bg_store(T *dst, C color) {
  *dst=color;
  if (mode==DUALCOLOR)
    *(T*)((uintptr_t)dst+stereo_dual_offset)=color;
}

s32 affine_reference_x[2];
s32 affine_reference_y[2];

static inline s32 signext28(u32 value)
{
  s32 ret = (s32)(value << 4);
  return ret >> 4;
}

void video_reload_counters()
{
  /* This happens every Vblank */
  affine_reference_x[0] = signext28(read_ioreg32(REG_BG2X_L));
  affine_reference_y[0] = signext28(read_ioreg32(REG_BG2Y_L));
  affine_reference_x[1] = signext28(read_ioreg32(REG_BG3X_L));
  affine_reference_y[1] = signext28(read_ioreg32(REG_BG3Y_L));
}

// Renders non-affine tiled background layer.
// Will process a full or partial tile (start and end within 0..8) and draw
// it in either 8 or 4 bpp mode. Honors vertical and horizontal flip.

// tile contains the tile info (contains tile index, flip bits, pal info)
// hflip causes the tile pixels lookup to be reversed (from MSB to LSB
// If isbase is not set, color 0 is interpreted as transparent, otherwise
// we are drawing the base layer, so palette[0] is used (backdrop).

template<typename dtype, rendtype rdtype, bool is8bpp, bool isbase, bool hflip>
static inline void rend_part_tile_Nbpp(u32 bg_comb, u32 px_comb,
  dtype *dest_ptr, u32 start, u32 end, u16 tile,
  const u8 *tile_base, int vertical_pixel_flip, const u16 *paltbl
) {
  // Seek to the specified tile, using the tile number and size.
  // tile_base already points to the right tile-line vertical offset
  const u8 *tile_ptr = &tile_base[(tile & 0x3FF) * (is8bpp ? 64 : 32)];
  u16 bgcolor = paltbl[0];

  // On vertical flip, apply the mirror offset
  if (tile & 0x800)
    tile_ptr += vertical_pixel_flip;

  if (is8bpp) {
    // Each byte is a color, mapped to a palete. 8 bytes can be read as 64bit
    for (u32 i = start; i < end; i++, dest_ptr++) {
      // Honor hflip by selecting bytes in the correct order
      u32 sel = hflip ? (7-i) : i;
      u8 pval = tile_ptr[sel];
      // Alhpa mode stacks previous value (unless rendering the first layer)
      if (pval) {
        if (rdtype == FULLCOLOR || rdtype == DUALCOLOR)
          stereo_bg_store<rdtype>(dest_ptr, paltbl[pval]);
        else if (rdtype == INDXCOLOR)
          stereo_bg_store<rdtype>(dest_ptr, pval | px_comb);  // Add combine flags
        else if (rdtype == STCKCOLOR)
          // Stack pixels on top of the pixel value and combine flags
          stereo_bg_store<rdtype>(dest_ptr, pval | px_comb | ((isbase ? bg_comb : *dest_ptr) << 16));
      }
      else if (isbase) {
        if (rdtype == FULLCOLOR || rdtype == DUALCOLOR)
          stereo_bg_store<rdtype>(dest_ptr, bgcolor);
        else
          stereo_bg_store<rdtype>(dest_ptr, 0 | bg_comb);  // Add combine flags
      }
    }
  } else {
    // In 4bpp mode, the tile[15..12] bits contain the sub-palette number.
    u16 tilepal = (tile >> 12) << 4;
    u16 pxflg = px_comb | tilepal;
    const u16 *subpal = &paltbl[tilepal];
    // Read packed pixel data, skip start pixels
    u32 tilepix = eswap32(*(u32*)tile_ptr);
    if (hflip) tilepix <<= (start * 4);
    else       tilepix >>= (start * 4);
    // Only 32 bits (8 pixels * 4 bits)
    for (u32 i = start; i < end; i++, dest_ptr++) {
      u8 pval = hflip ? tilepix >> 28 : tilepix & 0xF;
      if (pval) {
        if (rdtype == FULLCOLOR || rdtype == DUALCOLOR)
          stereo_bg_store<rdtype>(dest_ptr, subpal[pval]);
        else if (rdtype == INDXCOLOR)
          stereo_bg_store<rdtype>(dest_ptr, pxflg | pval);
        else if (rdtype == STCKCOLOR)    // Stack pixels
          stereo_bg_store<rdtype>(dest_ptr, pxflg | pval | ((isbase ? bg_comb : *dest_ptr) << 16));
      }
      else if (isbase) {
        if (rdtype == FULLCOLOR || rdtype == DUALCOLOR)
          stereo_bg_store<rdtype>(dest_ptr, bgcolor);
        else
          stereo_bg_store<rdtype>(dest_ptr, 0 | bg_comb);
      }
      // Advance to next packed data
      if (hflip) tilepix <<= 4;
      else       tilepix >>= 4;
    }
  }
}

// Same as above, but optimized for full tiles. Skip comments here.
template<typename dtype, rendtype rdtype, bool is8bpp, bool isbase, bool hflip>
static inline void render_tile_Nbpp(
  u32 bg_comb, u32 px_comb, dtype *dest_ptr, u16 tile,
  const u8 *tile_base, int vertical_pixel_flip, const u16 *paltbl
) {
  const u8 *tile_ptr = &tile_base[(tile & 0x3FF) * (is8bpp ? 64 : 32)];
  u16 bgcolor = paltbl[0];

  if (tile & 0x800)
    tile_ptr += vertical_pixel_flip;

  if (is8bpp) {
    for (u32 j = 0; j < 2; j++) {
      u32 tilepix = eswap32(((u32*)tile_ptr)[hflip ? 1-j : j]);
      if (tilepix) {
        for (u32 i = 0; i < 4; i++, dest_ptr++) {
          u8 pval = hflip ? (tilepix >> (24 - i*8)) : (tilepix >> (i*8));
          if (pval) {
            if (rdtype == FULLCOLOR || rdtype == DUALCOLOR)
              stereo_bg_store<rdtype>(dest_ptr, paltbl[pval]);
            else if (rdtype == INDXCOLOR)
              stereo_bg_store<rdtype>(dest_ptr, pval | px_comb);  // Add combine flags
            else if (rdtype == STCKCOLOR)
              stereo_bg_store<rdtype>(dest_ptr, pval | px_comb | ((isbase ? bg_comb : *dest_ptr) << 16));
          }
          else if (isbase) {
            stereo_bg_store<rdtype>(dest_ptr, (rdtype == FULLCOLOR || rdtype == DUALCOLOR) ? bgcolor : 0 | bg_comb);
          }
        }
      } else {
        for (u32 i = 0; i < 4; i++, dest_ptr++)
          if (isbase)
            stereo_bg_store<rdtype>(dest_ptr, (rdtype == FULLCOLOR || rdtype == DUALCOLOR) ? bgcolor : 0 | bg_comb);
      }
    }
  } else {
    u32 tilepix = eswap32(*(u32*)tile_ptr);
    if (tilepix) {  // We can skip it all if the row is transparent
      u16 tilepal = (tile >> 12) << 4;
      u16 pxflg = px_comb | tilepal;
      const u16 *subpal = &paltbl[tilepal];
      for (u32 i = 0; i < 8; i++, dest_ptr++) {
        u8 pval = (hflip ? (tilepix >> ((7-i)*4)) : (tilepix >> (i*4))) & 0xF;
        if (pval) {
          if (rdtype == FULLCOLOR || rdtype == DUALCOLOR)
            stereo_bg_store<rdtype>(dest_ptr, subpal[pval]);
          else if (rdtype == INDXCOLOR)
            stereo_bg_store<rdtype>(dest_ptr, pxflg | pval);
          else if (rdtype == STCKCOLOR)
            stereo_bg_store<rdtype>(dest_ptr, pxflg | pval | ((isbase ? bg_comb : *dest_ptr) << 16));
        }
        else if (isbase) {
          stereo_bg_store<rdtype>(dest_ptr, (rdtype == FULLCOLOR || rdtype == DUALCOLOR) ? bgcolor : 0 | bg_comb);
        }
      }
    } else if (isbase) {
      // In this case we simply fill the pixels with background pixels
      for (u32 i = 0; i < 8; i++, dest_ptr++)
        stereo_bg_store<rdtype>(dest_ptr, (rdtype == FULLCOLOR || rdtype == DUALCOLOR) ? bgcolor : 0 | bg_comb);
    }
  }
}


template<typename stype, rendtype rdtype, bool isbase, bool is8bpp>
static void render_scanline_text_fast(u32 layer,
 u32 start, u32 end, void *scanline, const u16 * paltbl)
{
  u32 bg_control = read_ioreg(REG_BGxCNT(layer));
  u16 vcount = read_ioreg(REG_VCOUNT);
  u32 map_size = (bg_control >> 14) & 0x03;
  u32 map_width = map_widths[map_size];
  u32 hoffset = (start + read_ioreg(REG_BGxHOFS(layer))) % 512;
  u32 voffset = (vcount + read_ioreg(REG_BGxVOFS(layer))) % 512;
  stype *dest_ptr = ((stype*)scanline) + start;

  // Calculate combine masks. These store 2 bits of info: 1st and 2nd target.
  // If set, the current pixel belongs to a layer that is 1st or 2nd target.
  u32 bg_comb = color_flags(5), px_comb = color_flags(layer);

  // Background map data is in vram, at an offset specified in 2K blocks.
  // (each map data block is 32x32 tiles, at 16bpp, so 2KB)
  u32 base_block = (bg_control >> 8) & 0x1F;
  u16 *map_base = (u16 *)&vram[base_block * 2048];
  u16 *map_ptr, *second_ptr;

  end -= start;

  // Skip the top one/two block(s) if using the bottom half
  if ((map_size & 0x02) && (voffset >= 256))
    map_base += ((map_width / 8) * 32);

  // Skip the top tiles within the block
  map_base += (((voffset % 256) / 8) * 32);

  // we might need to render from two charblocks, store a second pointer.
  second_ptr = map_ptr = map_base;

  if(map_size & 0x01) {    // If background is 512 pixels wide
    if(hoffset >= 256) {
      // If we are rendering the right block, skip a whole charblock
      hoffset -= 256;
      map_ptr += (32 * 32);
    } else {
      // If we are rendering the left block, we might overrun into the right
      second_ptr += (32 * 32);
    }
  } else {
    hoffset %= 256;     // Background is 256 pixels wide
  }

  // Skip the left blocks within the block
  map_ptr += hoffset / 8;

  // Render a single scanline of text tiles
  u32 tilewidth = is8bpp ? tile_width_8bpp : tile_width_4bpp;
  u32 vert_pix_offset = (voffset % 8) * tilewidth;
  // Calculate the pixel offset between a line and its "flipped" mirror.
  // The values can be {56, 40, 24, 8, -8, -24, -40, -56}
  s32 vflip_off = is8bpp ?
       tile_size_8bpp - 2*vert_pix_offset - tile_width_8bpp :
       tile_size_4bpp - 2*vert_pix_offset - tile_width_4bpp;

  // The tilemap base is selected via bgcnt (16KiB chunks)
  u32 tilecntrl = (bg_control >> 2) & 0x03;
  // Account for the base offset plus the tile vertical offset
  u8 *tile_base = &vram[tilecntrl * 16*1024 + vert_pix_offset];
  // Number of pixels available until the end of the tile block
  u32 pixel_run = 256 - hoffset;

  u32 tile_hoff = hoffset % 8;
  u32 partial_hcnt = 8 - tile_hoff;

  if (tile_hoff) {
    // First partial tile, only right side is visible.
    u32 todraw = MIN(end, partial_hcnt); // [1..7]
    u32 stop = tile_hoff + todraw;   // Usually 8, unless short run.

    u16 tile = eswap16(*map_ptr++);
    if (tile & 0x400)   // Tile horizontal flip
      rend_part_tile_Nbpp<stype, rdtype, is8bpp, isbase, true>(
        bg_comb, px_comb, dest_ptr, tile_hoff, stop, tile, tile_base, vflip_off, paltbl);
    else
      rend_part_tile_Nbpp<stype, rdtype, is8bpp, isbase, false>(
        bg_comb, px_comb, dest_ptr, tile_hoff, stop, tile, tile_base, vflip_off, paltbl);

    dest_ptr += todraw;
    end -= todraw;
    pixel_run -= todraw;
  }

  if (!end)
    return;

  // Now render full tiles
  u32 todraw = MIN(end, pixel_run) / 8;

  for (u32 i = 0; i < todraw; i++, dest_ptr += 8) {
    u16 tile = eswap16(*map_ptr++);
    if (tile & 0x400)   // Tile horizontal flip
      render_tile_Nbpp<stype, rdtype, is8bpp, isbase, true>(
        bg_comb, px_comb, dest_ptr, tile, tile_base, vflip_off, paltbl);
    else
      render_tile_Nbpp<stype, rdtype, is8bpp, isbase, false>(
        bg_comb, px_comb, dest_ptr, tile, tile_base, vflip_off, paltbl);
  }

  end -= todraw * 8;
  pixel_run -= todraw * 8;

  if (!end)
    return;

  // Switch to the next char block if we ran out of tiles
  if (!pixel_run)
    map_ptr = second_ptr;

  todraw = end / 8;
  for (u32 i = 0; i < todraw; i++, dest_ptr += 8) {
    u16 tile = eswap16(*map_ptr++);
    if (tile & 0x400)   // Tile horizontal flip
      render_tile_Nbpp<stype, rdtype, is8bpp, isbase, true>(
        bg_comb, px_comb, dest_ptr, tile, tile_base, vflip_off, paltbl);
    else
      render_tile_Nbpp<stype, rdtype, is8bpp, isbase, false>(
        bg_comb, px_comb, dest_ptr, tile, tile_base, vflip_off, paltbl);
  }

  end -= todraw * 8;

  // Finalize the tile rendering the left side of it (from 0 up to "end").
  if (end) {
    u16 tile = eswap16(*map_ptr++);
    if (tile & 0x400)   // Tile horizontal flip
      rend_part_tile_Nbpp<stype, rdtype, is8bpp, isbase, true>(
        bg_comb, px_comb, dest_ptr, 0, end, tile, tile_base, vflip_off, paltbl);
    else
      rend_part_tile_Nbpp<stype, rdtype, is8bpp, isbase, false>(
        bg_comb, px_comb, dest_ptr, 0, end, tile, tile_base, vflip_off, paltbl);
  }
}

// A slow version of the above function that allows for mosaic effects
template<typename stype, rendtype rdtype, bool isbase, bool is8bpp>
static void render_scanline_text_mosaic(u32 layer,
 u32 start, u32 end, void *scanline, const u16 * paltbl)
{
  u32 bg_control = read_ioreg(REG_BGxCNT(layer));
  const u32 mosh = (read_ioreg(REG_MOSAIC) & 0xF) + 1;
  const u32 mosv = ((read_ioreg(REG_MOSAIC) >> 4) & 0xF) + 1;
  u16 vcount = read_ioreg(REG_VCOUNT);
  u32 map_size = (bg_control >> 14) & 0x03;
  u32 map_width = map_widths[map_size];
  u32 hoffset = (start + read_ioreg(REG_BGxHOFS(layer))) % 512;
  u16 vmosoff = vcount - vcount % mosv;
  u32 voffset = (vmosoff + read_ioreg(REG_BGxVOFS(layer))) % 512;
  stype *dest_ptr = ((stype*)scanline) + start;

  u32 bg_comb = color_flags(5), px_comb = color_flags(layer);

  u32 base_block = (bg_control >> 8) & 0x1F;
  u16 *map_base = (u16 *)&vram[base_block * 2048];
  u16 *map_ptr, *second_ptr;

  if ((map_size & 0x02) && (voffset >= 256))
    map_base += ((map_width / 8) * 32);

  map_base += (((voffset % 256) / 8) * 32);

  second_ptr = map_ptr = map_base;

  if(map_size & 0x01) {    // If background is 512 pixels wide
    if(hoffset >= 256) {
      // If we are rendering the right block, skip a whole charblock
      hoffset -= 256;
      map_ptr += (32 * 32);
    } else {
      // If we are rendering the left block, we might overrun into the right
      second_ptr += (32 * 32);
    }
  } else {
    hoffset %= 256;     // Background is 256 pixels wide
  }

  // Skip the left blocks within the block
  map_ptr += hoffset / 8;

  // Render a single scanline of text tiles
  u32 tilewidth = is8bpp ? tile_width_8bpp : tile_width_4bpp;
  u32 vert_pix_offset = (voffset % 8) * tilewidth;
  // Calculate the pixel offset between a line and its "flipped" mirror.
  // The values can be {56, 40, 24, 8, -8, -24, -40, -56}
  s32 vflip_off = is8bpp ?
       tile_size_8bpp - 2*vert_pix_offset - tile_width_8bpp :
       tile_size_4bpp - 2*vert_pix_offset - tile_width_4bpp;

  // The tilemap base is selected via bgcnt (16KiB chunks)
  u32 tilecntrl = (bg_control >> 2) & 0x03;
  // Account for the base offset plus the tile vertical offset
  u8 *tile_base = &vram[tilecntrl * 16*1024 + vert_pix_offset];

  u16 bgcolor = paltbl[0];

  // Iterate pixel by pixel, loading data every N pixels to honor mosaic effect
  u8 pval = 0;
  u32 mctr = 0;
  for (u32 i = 0; start < end; start++, i++, dest_ptr++) {
    u16 tile = eswap16(*map_ptr);

    // Counter-based mosaic (see render_affine_background for rationale).
    if (!mctr) {
      const u8 *tile_ptr = &tile_base[(tile & 0x3FF) * (is8bpp ? 64 : 32)];

      bool hflip = (tile & 0x400);
      if (tile & 0x800)
        tile_ptr += vflip_off;

      // Load byte or nibble with pixel data.
      if (is8bpp) {
        if (hflip)
          pval = tile_ptr[7 - hoffset % 8];
        else
          pval = tile_ptr[hoffset % 8];
      } else {
        if (hflip)
          pval = (tile_ptr[(7 - hoffset % 8) >> 1] >> (((hoffset & 1) ^ 1) * 4)) & 0xF;
        else
          pval = (tile_ptr[(hoffset % 8) >> 1] >> ((hoffset & 1) * 4)) & 0xF;
      }
      mctr = mosh;
    }
    mctr--;

    if (is8bpp) {
      if (pval) {
        if (rdtype == FULLCOLOR || rdtype == DUALCOLOR)
          stereo_bg_store<rdtype>(dest_ptr, paltbl[pval]);
        else if (rdtype == INDXCOLOR)
          stereo_bg_store<rdtype>(dest_ptr, pval | px_comb);  // Add combine flags
        else if (rdtype == STCKCOLOR)
          stereo_bg_store<rdtype>(dest_ptr, pval | px_comb | ((isbase ? bg_comb : *dest_ptr) << 16));
      }
      else if (isbase) {
        stereo_bg_store<rdtype>(dest_ptr, (rdtype == FULLCOLOR || rdtype == DUALCOLOR) ? bgcolor : 0 | bg_comb);
      }
    } else {
      u16 tilepal = (tile >> 12) << 4;
      u16 pxflg = px_comb | tilepal;
      const u16 *subpal = &paltbl[tilepal];
      if (pval) {
        if (rdtype == FULLCOLOR || rdtype == DUALCOLOR)
          stereo_bg_store<rdtype>(dest_ptr, subpal[pval]);
        else if (rdtype == INDXCOLOR)
          stereo_bg_store<rdtype>(dest_ptr, pxflg | pval);
        else if (rdtype == STCKCOLOR)
          stereo_bg_store<rdtype>(dest_ptr, pxflg | pval | ((isbase ? bg_comb : *dest_ptr) << 16));
      }
      else if (isbase) {
        stereo_bg_store<rdtype>(dest_ptr, (rdtype == FULLCOLOR || rdtype == DUALCOLOR) ? bgcolor : 0 | bg_comb);
      }
    }

    // Need to continue from the next charblock
    hoffset++;
    if (hoffset % 8 == 0)
      map_ptr++;
    if (hoffset >= 256) {
      hoffset = 0;
      map_ptr = second_ptr;
    }
  }
}

template<typename stype, rendtype rdtype, bool isbase>
static void render_scanline_text(u32 layer,
 u32 start, u32 end, void *scanline, const u16 * paltbl)
{
  CPU_TIME(3);
  // Tile mode has 4 and 8 bpp modes.
  u32 bg_control = read_ioreg(REG_BGxCNT(layer));
  bool is8bpp = (read_ioreg(REG_BGxCNT(layer)) & 0x80);
  const u32 mosamount = read_ioreg(REG_MOSAIC) & 0xFF;
  bool has_mosaic = (bg_control & 0x40) && (mosamount != 0);

  if (has_mosaic) {
    if (is8bpp)
      render_scanline_text_mosaic<stype, rdtype, isbase, true>(
        layer, start, end, scanline, paltbl);
    else
      render_scanline_text_mosaic<stype, rdtype, isbase, false>(
        layer, start, end, scanline, paltbl);
  } else {
    if (is8bpp)
      render_scanline_text_fast<stype, rdtype, isbase, true>(
        layer, start, end, scanline, paltbl);
    else
      render_scanline_text_fast<stype, rdtype, isbase, false>(
        layer, start, end, scanline, paltbl);
  }
}

static inline u8 lookup_pix_8bpp(
  u32 px, u32 py, const u8 *tile_base, const u8 *map_base, u32 map_size
) {
  // Pitch represents the log2(number of tiles per row) (from 16 to 128)
  u32 map_pitch = map_size + 4;
  // Given coords (px,py) in the background space, find the tile.
  u32 mapoff = (px / 8) + ((py / 8) << map_pitch);
  // Each tile is 8x8, so 64 bytes each.
  const u8 *tile_ptr = &tile_base[map_base[mapoff] * tile_size_8bpp];
  // Read the 8bit color within the tile.
  return tile_ptr[(px % 8) + ((py % 8) * 8)];
}


template<typename dsttype, rendtype rdtype, bool isbase>
static inline void rend_pix_8bpp(
  dsttype *dest_ptr, u8 pval, u32 bg_comb, u32 px_comb, const u16 *pal
) {
  // Alhpa mode stacks previous value (unless rendering the first layer)
  if (pval) {
    if (rdtype == FULLCOLOR || rdtype == DUALCOLOR)
      stereo_bg_store<rdtype>(dest_ptr, pal[pval]);
    else if (rdtype == INDXCOLOR)
      stereo_bg_store<rdtype>(dest_ptr, pval | px_comb);  // Add combine flags
    else if (rdtype == STCKCOLOR)
      // Stack pixels. If base, stack the base pixel.
      stereo_bg_store<rdtype>(dest_ptr, pval | px_comb | ((isbase ? bg_comb : *dest_ptr) << 16));
  }
  else if (isbase) {
    // Transparent pixel, but we are base layer, so render background.
    if (rdtype == FULLCOLOR || rdtype == DUALCOLOR)
      stereo_bg_store<rdtype>(dest_ptr, pal[0]);
    else
      stereo_bg_store<rdtype>(dest_ptr, 0 | bg_comb);  // Just backdrop color and combine flags
  }
}

template<typename dsttype, rendtype rdtype>
static inline void render_bdrop_pixel_8bpp(
  dsttype *dest_ptr, u32 bg_comb, u16 bgcol) {
  // Alhpa mode stacks previous value (unless rendering the first layer)
  if (rdtype == FULLCOLOR || rdtype == DUALCOLOR)
    stereo_bg_store<rdtype>(dest_ptr, bgcol);
  else
    stereo_bg_store<rdtype>(dest_ptr, 0 | bg_comb);
}

typedef void (*affine_render_function) (
  u32 layer, u32 start, u32 cnt, const u8 *map_base,
  u32 map_size, const u8 *tile_base, void *dst_ptr,
  const u16* pal);

// Affine background rendering logic.
// wrap extends the background infinitely, otherwise transparent/backdrop fill
// rotate indicates if there's any rotation (optimized version for no-rotation)
// mosaic applies to horizontal mosaic (vertical is adjusted via affine ref)
template <typename dtype, rendtype rdtype,
          bool isbase, bool wrap, bool rotate, bool mosaic>
static inline void render_affine_background(
  u32 layer, u32 start, u32 cnt, const u8 *map_base,
  u32 map_size, const u8 *tile_base, void *dst_ptr_raw,
  const u16* pal) {

  dtype *dst_ptr = (dtype*)dst_ptr_raw;
  // Backdrop and current layer combine bits.
  u32 bg_comb = color_flags(5);
  u32 px_comb = color_flags(layer);

  s32 dx = (s16)read_ioreg(REG_BGxPA(layer));
  s32 dy = (s16)read_ioreg(REG_BGxPC(layer));

  s32 source_x = affine_reference_x[layer - 2] + (start * dx);
  s32 source_y = affine_reference_y[layer - 2] + (start * dy);

  // Maps are squared, four sizes available (128x128 to 1024x1024)
  u32 width_height = 128 << map_size;

  // Horizontal mosaic effect.
  const u32 mosh = (mosaic ? (read_ioreg(REG_MOSAIC)) & 0xF : 0) + 1;

  if (wrap) {
    // In wrap mode the entire space is covered, since it "wraps" at the edges
    u8 pval = 0;
    u32 mctr = 0;
    if (rotate) {
      for (u32 i = 0; cnt; i++, cnt--) {
        u32 pix_x = (u32)(source_x >> 8) & (width_height-1);
        u32 pix_y = (u32)(source_y >> 8) & (width_height-1);

        // Lookup pixel and draw it (only every Nth if mosaic is on).  The
        // counter form avoids a divl per pixel: mosh is loop-invariant but
        // runtime-valued, so gcc/clang do not strength-reduce 'i % mosh'
        // into a multiply-by-magic-constant sequence.  When mosaic == false
        // the counter machinery is dead-code-eliminated at compile time.
        if (!mosaic || !mctr) {
          pval = lookup_pix_8bpp(pix_x, pix_y, tile_base, map_base, map_size);
          mctr = mosh;
        }
        rend_pix_8bpp<dtype, rdtype, isbase>(dst_ptr++, pval, bg_comb, px_comb, pal);
        if (mosaic) mctr--;

        source_x += dx; source_y += dy;  // Move to the next pixel
      }
    } else {
      // Y coordinate stays contant across the walk.
      const u32 pix_y = (u32)(source_y >> 8) & (width_height-1);
      for (u32 i = 0; cnt; i++, cnt--) {
        u32 pix_x = (u32)(source_x >> 8) & (width_height-1);
        if (!mosaic || !mctr) {
          pval = lookup_pix_8bpp(pix_x, pix_y, tile_base, map_base, map_size);
          mctr = mosh;
        }
        rend_pix_8bpp<dtype, rdtype, isbase>(dst_ptr++, pval, bg_comb, px_comb, pal);
        if (mosaic) mctr--;
        source_x += dx;  // Only moving in the X direction.
      }
    }
  } else {
    u16 bgcol = pal[0];
    if (rotate) {
      // Draw backdrop pixels if necessary until we reach the background edge.
      while (cnt) {
        // Draw backdrop pixels if they lie outside of the background.
        u32 pix_x = (u32)(source_x >> 8), pix_y = (u32)(source_y >> 8);

        // Stop once we find a pixel that is actually *inside* the map.
        if (pix_x < width_height && pix_y < width_height)
          break;

        // Draw a backdrop pixel if we are the base layer.
        if (isbase)
          render_bdrop_pixel_8bpp<dtype, rdtype>(dst_ptr, bg_comb, bgcol);

        dst_ptr++;
        source_x += dx; source_y += dy;
        cnt--;
      }

      // Draw background pixels by looking them up in the map
      u8 pval = 0;
      u32 mctr = 0;
      for (u32 i = 0; cnt; i++, cnt--) {
        u32 pix_x = (u32)(source_x >> 8), pix_y = (u32)(source_y >> 8);

        // Check if we run out of background pixels, stop drawing.
        if (pix_x >= width_height || pix_y >= width_height)
          break;

        // Lookup pixel and draw it.
        if (!mosaic || !mctr) {
          pval = lookup_pix_8bpp(pix_x, pix_y, tile_base, map_base, map_size);
          mctr = mosh;
        }
        rend_pix_8bpp<dtype, rdtype, isbase>(dst_ptr++, pval, bg_comb, px_comb, pal);
        if (mosaic) mctr--;

        // Move to the next pixel, update coords accordingly
        source_x += dx; source_y += dy;
      }
    } else {
      // Specialized version for scaled-only backgrounds
      u8 pval = 0;
      const u32 pix_y = (u32)(source_y >> 8);
      if (pix_y < width_height) {  // Check if within Y-coord range
        // Draw/find till left edge
        while (cnt) {
          u32 pix_x = (u32)(source_x >> 8);
          if (pix_x < width_height)
            break;

          if (isbase)
            render_bdrop_pixel_8bpp<dtype, rdtype>(dst_ptr, bg_comb, bgcol);

          dst_ptr++;
          source_x += dx;
          cnt--;
        }
        // Draw actual background
        u32 mctr = 0;
        for (u32 i = 0; cnt; i++, cnt--) {
          u32 pix_x = (u32)(source_x >> 8);
          if (pix_x >= width_height)
            break;

          if (!mosaic || !mctr) {
            pval = lookup_pix_8bpp(pix_x, pix_y, tile_base, map_base, map_size);
            mctr = mosh;
          }
          rend_pix_8bpp<dtype, rdtype, isbase>(dst_ptr++, pval, bg_comb, px_comb, pal);
          if (mosaic) mctr--;

          source_x += dx;
        }
      }
    }

    // Complete the line on the right, if we ran out over the bg edge.
    // Only necessary for the base layer, otherwise we can safely finish.
    if (isbase)
      while (cnt--)
        render_bdrop_pixel_8bpp<dtype, rdtype>(dst_ptr++, bg_comb, bgcol);
  }
}


// Renders affine backgrounds. These differ substantially from non-affine
// ones. Tile maps are byte arrays (instead of 16 bit), limiting the map to
// 256 different tiles (with no flip bits and just one single 256 color pal).
// Optimize for common cases: wrap/non-wrap, scaling/rotation.
template<typename dsttype, rendtype rdtype, bool isbase>
static void render_scanline_affine(u32 layer,
 u32 start, u32 end, void *scanline, const u16 *pal)
{
  CPU_TIME(3);
  u32 bg_control = read_ioreg(REG_BGxCNT(layer));
  u32 map_size = (bg_control >> 14) & 0x03;

  // Char block base pointer
  u32 base_block = (bg_control >> 8) & 0x1F;
  u8 *map_base = &vram[base_block * 2048];
  // The tilemap base is selected via bgcnt (16KiB chunks)
  u32 tilecntrl = (bg_control >> 2) & 0x03;
  u8 *tile_base = &vram[tilecntrl * 16*1024];

  dsttype *dest_ptr = ((dsttype*)scanline) + start;
  const u32 mosamount = read_ioreg(REG_MOSAIC) & 0xFF;

  bool has_mosaic = (bg_control & 0x40) && (mosamount != 0);
  bool has_rotation = read_ioreg(REG_BGxPC(layer)) != 0;
  bool has_wrap = (bg_control >> 13) & 1;

  // Number of pixels to render
  u32 cnt = end - start;

  // Four specialized versions for faster rendering on specific cases like
  // scaling only or non-wrapped backgrounds.
  u32 fidx = (has_wrap     ? 0x4 : 0) |
             (has_rotation ? 0x2 : 0) |
             (has_mosaic   ? 0x1 : 0);

  static const affine_render_function rdfns[8] = {
    render_affine_background<dsttype, rdtype, isbase, false, false, false>,
    render_affine_background<dsttype, rdtype, isbase, false, false, true>,
    render_affine_background<dsttype, rdtype, isbase, false, true,  false>,
    render_affine_background<dsttype, rdtype, isbase, false, true,  true>,
    render_affine_background<dsttype, rdtype, isbase, true,  false, false>,
    render_affine_background<dsttype, rdtype, isbase, true,  false, true>,
    render_affine_background<dsttype, rdtype, isbase, true,  true,  false>,
    render_affine_background<dsttype, rdtype, isbase, true,  true,  true>,
  };

  rdfns[fidx](layer, start, cnt, map_base, map_size, tile_base, dest_ptr, pal);
}

// Effect pixels keep the ordinary target/OBJ flags. Bit 12 names BG2's
// direct colour in this row, outside the 512-entry palette index space.
// A bitmap has only one BG, so the same sidecar supplies either stack slot.
static u16 bitmap_effect_colors[240];

template<rendtype rdmode, typename buftype, unsigned mode, typename pixfmt>
static inline void bitmap_pixel_write(
  buftype *dst_ptr, pixfmt val, const u16 * palptr, u16 px_attr, u32 x
) {
  if (mode != 4) {
    if (rdmode == FULLCOLOR)
      *dst_ptr = convert_palette(val);
    else {
      bitmap_effect_colors[x] = convert_palette(val);
      u32 pixel = 0x1000 | px_attr;
      *dst_ptr = rdmode == STCKCOLOR ? pixel | ((*dst_ptr) << 16) : pixel;
    }
  }
  else if (val) {
    if (rdmode == FULLCOLOR)
      *dst_ptr = palptr[val];
    else if (rdmode == INDXCOLOR)
      *dst_ptr = val | px_attr;  // Add combine flags
    else if (rdmode == STCKCOLOR)
      *dst_ptr = val | px_attr | ((*dst_ptr) << 16);  // Stack pixels
  }
}


typedef enum
{
  BLIT,     // The bitmap has no scaling nor rotation on the X axis
  SCALED,   // The bitmap features some scaling (on the X axis) but no rotation
  ROTATED   // Bitmap has rotation (and perhaps scaling too)
} bm_rendmode;

// Renders a bitmap honoring the pixel mode and any affine transformations.
// There's optimized versions for bitmaps without scaling / rotation.

template<rendtype rdtype, typename dsttype, // Rendering target type and format
         unsigned mode,
         typename pixfmt,                   // Bitmap source pixel format (8/16)
         unsigned width, unsigned height,   // Bitmap size (not screen!)
         bm_rendmode rdmode,                // Rendering mode optimization.
         bool mosaic>                       // Whether mosaic effect is used.
static inline void render_scanline_bitmap(
  u32 start, u32 end, void *scanline, const u16 * palptr
) {
  CPU_TIME(3);
  s32 dx = (s16)read_ioreg(REG_BG2PA);
  s32 dy = (s16)read_ioreg(REG_BG2PC);
  s32 source_x = affine_reference_x[0] + (start * dx); // Always BG2
  s32 source_y = affine_reference_y[0] + (start * dy);

  // Premature abort render optimization if bitmap out of Y coordinate.
  if ((rdmode != ROTATED) && ((u32)(source_y >> 8)) >= height)
    return;

  // Modes 4 and 5 feature double buffering.
  bool second_frame = (mode >= 4) && (read_ioreg(REG_DISPCNT) & 0x10);
  pixfmt *src_ptr = (pixfmt*)&vram[second_frame ? 0xA000 : 0x0000];
  dsttype *dst_ptr = ((dsttype*)scanline) + start;
  u16 px_attr = color_flags(2);   // Always BG2

  const u32 mosh = (mosaic ? (read_ioreg(REG_MOSAIC)) & 0xF : 0) + 1;

  if (rdmode == BLIT) {
    // We just blit pixels (copy) from buffer to buffer.
    const u32 pixel_y = (u32)(source_y >> 8);
    if (source_x < 0) {
      // The bitmap starts somewhere after "start", skip those pixels.
      u32 delta = (-source_x + 255) >> 8;
      dst_ptr += delta;
      start += delta;
      source_x = 0;
    }

    u32 pixel_x = (u32)(source_x >> 8);
    // A window can begin to the right of mode 5's 160-pixel bitmap.
    // Do not let unsigned width-pixel_x wrap and sample the next row.
    if (mode == 5 && (pixel_x >= width || start >= end))
      return;
    u32 pixcnt = MIN(end - start, width - pixel_x);
    pixfmt *valptr = &src_ptr[pixel_x + (pixel_y * width)];
    // Direct-color BLIT spans can convert two pixels in separate halfwords.
    // Restrict word transfers to aligned little-endian 16-bit buffers; the
    // original scalar path handles mosaic, indexed color and unaligned tails.
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__ && !defined(USE_XBGR1555_FORMAT)
    if (cpu_bitmap && rdtype == FULLCOLOR && mode != 4 && !mosaic && sizeof(dsttype) == 2 && sizeof(pixfmt) == 2 &&
        (((uintptr_t)valptr | (uintptr_t)dst_ptr) & 3) == 0) {
      while (pixcnt >= 2) {
        u32 pair;
        memcpy(&pair, __builtin_assume_aligned(valptr, 4), sizeof(pair));
        u32 converted = ((pair & 0x001f001fu) << 11) |
                        ((pair & 0x03e003e0u) << 1) |
                        ((pair >> 10) & 0x001f001fu);
        memcpy(__builtin_assume_aligned(dst_ptr, 4), &converted, sizeof(converted));
        valptr += 2;
        dst_ptr += 2;
        pixcnt -= 2;
      }
    }
#endif
    pixfmt val = 0;
    u32 mctr = 0;
    for (u32 i = 0; pixcnt; i++, pixcnt--, valptr++) {
      // Pretty much pixel copier (counter-based mosaic: see render_affine_background)
      if (!mosaic || !mctr) {
        val = sizeof(pixfmt) == 2 ? eswap16(*valptr) : *valptr;
        mctr = mosh;
      }
      bitmap_pixel_write<rdtype, dsttype, mode, pixfmt>(dst_ptr++, val, palptr, px_attr, start + i);
      if (mosaic) mctr--;
    }
  }
  else if (rdmode == SCALED) {
    // Similarly to above, but now we need to sample pixels instead.
    const u32 pixel_y = (u32)(source_y >> 8);

    // Find the "inside" of the bitmap
    while (start < end) {
      u32 pixel_x = (u32)(source_x >> 8);
      if (pixel_x < width)
        break;
      source_x += dx;
      start++;
      dst_ptr++;
    }

    u32 cnt = end - start;
    pixfmt val = 0;
    u32 mctr = 0;
    for (u32 i = 0; cnt; i++, cnt--) {
      u32 pixel_x = (u32)(source_x >> 8);
      if (pixel_x >= width)
        break;  // We reached the end of the bitmap

      if (!mosaic || !mctr) {
        pixfmt *valptr = &src_ptr[pixel_x + (pixel_y * width)];
        val = sizeof(pixfmt) == 2 ? eswap16(*valptr) : *valptr;
        mctr = mosh;
      }

      bitmap_pixel_write<rdtype, dsttype, mode, pixfmt>(dst_ptr++, val, palptr, px_attr, start + i);
      if (mosaic) mctr--;
      source_x += dx;
    }
  } else {
    // Look for the first pixel to be drawn.
    while (start < end) {
      u32 pixel_x = (u32)(source_x >> 8), pixel_y = (u32)(source_y >> 8);
      if (pixel_x < width && pixel_y < height)
        break;
      start++;
      dst_ptr++;
      source_x += dx;  source_y += dy;
    }

    pixfmt val = 0;
    u32 mctr = 0;
    for (u32 i = 0; start < end; start++, i++) {
      u32 pixel_x = (u32)(source_x >> 8), pixel_y = (u32)(source_y >> 8);

      // Check if we run out of background pixels, stop drawing.
      if (pixel_x >= width || pixel_y >= height)
        break;

      // Lookup pixel and draw it.  Counter-based mosaic (see
      // render_affine_background).  Before this fix the loop counter
      // 'i' was declared but never incremented; gcc constant-folded
      // 'i % mosh' to !0 and the mosaic skip never triggered, so the
      // rotated-bitmap mode-3/4/5 paths sampled every pixel instead of
      // every Nth.  Matches the SCALED-bitmap and affine-object paths,
      // which both honor BG2 horizontal mosaic correctly.
      if (!mosaic || !mctr) {
        pixfmt *valptr = &src_ptr[pixel_x + (pixel_y * width)];
        val = sizeof(pixfmt) == 2 ? eswap16(*valptr) : *valptr;
        mctr = mosh;
      }

      bitmap_pixel_write<rdtype, dsttype, mode, pixfmt>(dst_ptr++, val, palptr, px_attr, start);
      if (mosaic) mctr--;

      // Move to the next pixel, update coords accordingly
      source_x += dx;
      source_y += dy;
    }
  }
}

// Object/Sprite rendering logic

static const u8 obj_dim_table[3][4][2] = {
  { {8, 8}, {16, 16}, {32, 32}, {64, 64} },
  { {16, 8}, {32, 8}, {32, 16}, {64, 32} },
  { {8, 16}, {8, 32}, {16, 32}, {32, 64} }
};

static u8 obj_priority_list[5][160][128];
static u8 obj_priority_count[5][160];
static u8 obj_alpha_count[160];

typedef struct {
  s32 obj_x, obj_y;
  s32 obj_w, obj_h;
  u32 attr1, attr2;
  bool is_double;
} t_sprite;

// Renders a tile row (8 pixels) for a regular (non-affine) object/sprite.
// tile_offset points to the VRAM offset where the data lives.
template<typename dsttype, rendtype rdtype, bool is8bpp, bool hflip>
static inline void render_obj_part_tile_Nbpp(
  u32 px_comb, dsttype *dest_ptr, u32 start, u32 end,
  u32 tile_offset, u16 palette, const u16 *pal
) {
  // Note that the last VRAM bank wrap around, hence the offset aliasing
  const u8* tile_ptr = &vram[0x10000 + (tile_offset & 0x7FFF)];
  u32 px_attr = px_comb | palette | 0x100;  // Combine flags + high palette bit

  if (is8bpp) {
    // Each byte is a color, mapped to a palete.
    for (u32 i = start; i < end; i++, dest_ptr++) {
      // Honor hflip by selecting bytes in the correct order
      u32 sel = hflip ? (7-i) : i;
      u8 pval = tile_ptr[sel];
      // Alhpa mode stacks previous value
      if (pval) {
        if (rdtype == FULLCOLOR)
          *dest_ptr = pal[pval];
        else if (rdtype == INDXCOLOR)
          *dest_ptr = pval | px_attr;  // Add combine flags
        else if (rdtype == STCKCOLOR) {
          // Stack pixels on top of the pixel value and combine flags
          // We do not stack OBJ on OBJ, rather overwrite the previous object
          if (*dest_ptr & 0x100)
            *dest_ptr = pval | px_attr | ((*dest_ptr) & 0xFFFF0000);
          else
            *dest_ptr = pval | px_attr | ((*dest_ptr) << 16);
        }
        else if (rdtype == PIXCOPY)
          *dest_ptr = dest_ptr[240];
      }
    }
  } else {
    // Only 32 bits (8 pixels * 4 bits)
    for (u32 i = start; i < end; i++, dest_ptr++) {
      u32 selb = hflip ? (3-i/2) : i/2;
      u32 seln = hflip ? ((i & 1) ^ 1) : (i & 1);
      u8 pval = (tile_ptr[selb] >> (seln * 4)) & 0xF;
      const u16 *subpal = &pal[palette];
      if (pval) {
        if (rdtype == FULLCOLOR)
          *dest_ptr = subpal[pval];
        else if (rdtype == INDXCOLOR)
          *dest_ptr = pval | px_attr;
        else if (rdtype == STCKCOLOR) {
          if (*dest_ptr & 0x100)
            *dest_ptr = pval | px_attr | ((*dest_ptr) & 0xFFFF0000);
          else
            *dest_ptr = pval | px_attr | ((*dest_ptr) << 16);  // Stack pixels
        }
        else if (rdtype == PIXCOPY)
          *dest_ptr = dest_ptr[240];
      }
    }
  }
}

// Same as above but optimized for full tiles
template<typename dsttype, rendtype rdtype, bool is8bpp, bool hflip>
static inline void render_obj_tile_Nbpp(u32 px_comb,
  dsttype *dest_ptr, u32 tile_offset, u16 palette, const u16 *pal
) {
  const u8* tile_ptr = &vram[0x10000 + (tile_offset & 0x7FFF)];
  u32 px_attr = px_comb | palette | 0x100;  // Combine flags + high palette bit

  if (is8bpp) {
    for (u32 j = 0; j < 2; j++) {
      u32 tilepix = eswap32(((u32*)tile_ptr)[hflip ? 1-j : j]);
      if (tilepix) {
        for (u32 i = 0; i < 4; i++, dest_ptr++) {
          u8 pval = hflip ? (tilepix >> (24 - i*8)) : (tilepix >> (i*8));
          if (pval) {
            if (rdtype == FULLCOLOR)
              *dest_ptr = pal[pval];
            else if (rdtype == INDXCOLOR)
              *dest_ptr = pval | px_attr;  // Add combine flags
            else if (rdtype == STCKCOLOR) {
              if (*dest_ptr & 0x100)
                *dest_ptr = pval | px_attr | ((*dest_ptr) & 0xFFFF0000);
              else
                *dest_ptr = pval | px_attr | ((*dest_ptr) << 16);
            }
            else if (rdtype == PIXCOPY)
              *dest_ptr = dest_ptr[240];
          }
        }
      } else
        dest_ptr += 4;
    }
  } else {
    u32 tilepix = eswap32(*(u32*)tile_ptr);
    if (tilepix) {   // Can skip all pixels if the row is just transparent
      for (u32 i = 0; i < 8; i++, dest_ptr++) {
        u8 pval = (hflip ? (tilepix >> ((7-i)*4)) : (tilepix >> (i*4))) & 0xF;
        const u16 *subpal = &pal[palette];
        if (pval) {
          if (rdtype == FULLCOLOR)
            *dest_ptr = subpal[pval];
          else if (rdtype == INDXCOLOR)
            *dest_ptr = pval | px_attr;
          else if (rdtype == STCKCOLOR) {  // Stack background, replace sprite
            if (*dest_ptr & 0x100)
              *dest_ptr = pval | px_attr | ((*dest_ptr) & 0xFFFF0000);
            else
              *dest_ptr = pval | px_attr | ((*dest_ptr) << 16);
          }
          else if (rdtype == PIXCOPY)
            *dest_ptr = dest_ptr[240];
        }
      }
    }
  }
}

// Renders a regular sprite (non-affine) row to screen.
// delta_x is the object X coordinate referenced from the window start.
// cnt is the maximum number of pixels to draw, honoring window, obj width, etc.
template <typename stype, rendtype rdtype, bool is8bpp, bool hflip>
static void render_object(
  s32 delta_x, u32 cnt, stype *dst_ptr, u32 tile_offset, u32 px_comb,
  u16 palette, const u16* palptr
) {
  // Tile size in bytes for each mode
  const u32 tile_bsize = is8bpp ? tile_size_8bpp : tile_size_4bpp;
  // Number of bytes to advance (or rewind) on the tile map
  const s32 tile_size_off = hflip ? -tile_bsize : tile_bsize;

  if (delta_x < 0) {      // Left part is outside of the screen/window.
    u32 offx = -delta_x;  // How many pixels did we skip from the object?
    s32 block_off = offx / 8;
    u32 tile_off = offx % 8;

    // Skip the first object tiles (skips in the flip direction)
    tile_offset += block_off * tile_size_off;

    // Render a partial tile to the left
    if (tile_off) {
      u32 residual = 8 - tile_off;   // Pixel count to complete the first tile
      u32 maxpix = MIN(residual, cnt);
      render_obj_part_tile_Nbpp<stype, rdtype, is8bpp, hflip>(
        px_comb, dst_ptr, tile_off, tile_off + maxpix, tile_offset, palette, palptr);

      // Move to the next tile
      tile_offset += tile_size_off;
      // Account for drawn pixels
      cnt -= maxpix;
      dst_ptr += maxpix;
    }
  } else {
    // Render object completely from the left. Skip the empty space to the left
    dst_ptr += delta_x;
  }

  // Render full tiles to the scan line.
  s32 num_tiles = cnt / 8;
  while (num_tiles--) {
    // Render full tiles
    render_obj_tile_Nbpp<stype, rdtype, is8bpp, hflip>(
      px_comb, dst_ptr, tile_offset, palette, palptr);
    tile_offset += tile_size_off;
    dst_ptr += 8;
  }

  // Render any partial tile on the end
  cnt = cnt % 8;
  if (cnt)
    render_obj_part_tile_Nbpp<stype, rdtype, is8bpp, hflip>(
      px_comb, dst_ptr, 0, cnt, tile_offset, palette, palptr);
}

// A slower version of the version above, that renders objects pixel by pixel.
// This allows proper mosaic effects whenever necessary.
template <typename stype, rendtype rdtype, bool is8bpp, bool hflip>
static void render_object_mosaic(
  s32 delta_x, u32 cnt, stype *dst_ptr, u32 base_tile_offset,
  u32 mosh, u32 px_comb, u16 palette, const u16* pal
) {
  const u32 tile_bsize = is8bpp ? tile_size_8bpp : tile_size_4bpp;
  const s32 tile_size_off = hflip ? -tile_bsize : tile_bsize;

  u32 offx = 0;
  if (delta_x < 0) {      // Left part is outside of the screen/window.
    offx = -delta_x;  // Number of skipped pixels
  } else {
    dst_ptr += delta_x;
  }

  u32 px_attr = px_comb | palette | 0x100;  // Combine flags + high palette bit

  u8 pval = 0;
  u32 mctr = 0;
  for (u32 i = 0; i < cnt; i++, offx++, dst_ptr++) {
    // Counter-based mosaic (see render_affine_background for rationale).
    if (!mctr) {
      // Load tile pixel color.
      u32 tile_offset = base_tile_offset + (offx / 8) * tile_size_off;
      const u8* tile_ptr = &vram[0x10000 + (tile_offset & 0x7FFF)];

      // Lookup for each mode and flip value.
      if (is8bpp) {
        if (hflip)
          pval = tile_ptr[7 - offx % 8];
        else
          pval = tile_ptr[offx % 8];
      } else {
        if (hflip)
          pval = (tile_ptr[(7 - offx % 8) >> 1] >> (((offx & 1) ^ 1) * 4)) & 0xF;
        else
          pval = (tile_ptr[(offx % 8) >> 1] >> ((offx & 1) * 4)) & 0xF;
      }
      mctr = mosh;
    }
    mctr--;

    // Write the pixel value as required
    const u16 *subpal = &pal[palette];
    if (pval) {
      if (rdtype == FULLCOLOR)
        *dst_ptr = is8bpp ? pal[pval] : subpal[pval];
      else if (rdtype == INDXCOLOR)
        *dst_ptr = pval | px_attr;  // Add combine flags
      else if (rdtype == STCKCOLOR) {
        if (*dst_ptr & 0x100)
          *dst_ptr = pval | px_attr | ((*dst_ptr) & 0xFFFF0000);
        else
          *dst_ptr = pval | px_attr | ((*dst_ptr) << 16);
      }
      else if (rdtype == PIXCOPY)
        *dst_ptr = dst_ptr[240];
    }
  }
}


// Renders an affine sprite row to screen.
// They support 4bpp and 8bpp modes. 1D and 2D tile mapping modes.
// Their render area is limited to their size (and optionally double size)
template <typename stype, rendtype rdtype, bool mosaic, bool is8bpp, bool rotate>
static void render_affine_object(
  const t_sprite *obji, const t_affp *affp, bool is_double,
  u32 start, u32 end, stype *dst_ptr, u32 mosv, u32 mosh,
  u32 base_tile, u32 pxcomb, u16 palette, const u16 *palptr
) {
  // Tile size in bytes for each mode
  const u32 tile_bsize = is8bpp ? tile_size_8bpp : tile_size_4bpp;
  const u32 tile_bwidth = is8bpp ? tile_width_8bpp : tile_width_4bpp;

  // Affine params
  s32 dx = (s16)eswap16(affp->dx);
  s32 dy = (s16)eswap16(affp->dy);
  s32 dmx = (s16)eswap16(affp->dmx);
  s32 dmy = (s16)eswap16(affp->dmy);

  // Object dimensions and boundaries
  u32 obj_dimw = obji->obj_w;
  u32 obj_dimh = obji->obj_h;
  s32 middle_x = is_double ? obji->obj_w : (obji->obj_w / 2);
  s32 middle_y = is_double ? obji->obj_h : (obji->obj_h / 2);
  s32 obj_width  = is_double ? obji->obj_w * 2 : obji->obj_w;
  s32 obj_height = is_double ? obji->obj_h * 2 : obji->obj_h;

  s32 vcount = read_ioreg(REG_VCOUNT);
  if (mosaic)
    vcount -= vcount % mosv;
  s32 y_delta = vcount - (obji->obj_y + middle_y);

  if (obji->obj_x < (signed)start)
    middle_x -= (start - obji->obj_x);
  s32 source_x = (obj_dimw << 7) + (y_delta * dmx) - (middle_x * dx);
  s32 source_y = (obj_dimh << 7) + (y_delta * dmy) - (middle_x * dy);

  // Early optimization if Y-coord is out completely for this line.
  // (if there's no rotation Y coord remains identical throughout the line).
  if (!rotate && ((u32)(source_y >> 8)) >= (u32)obj_height)
    return;

  u32 d_start = MAX((signed)start, obji->obj_x);
  u32 d_end   = MIN((signed)end,   obji->obj_x + obj_width);
  u32 cnt = d_end - d_start;
  dst_ptr += d_start;

  bool obj1dmap = read_ioreg(REG_DISPCNT) & 0x40;
  const u32 tile_pitch = obj1dmap ? (obj_dimw / 8) * tile_bsize : 1024;
  u32 px_attr = pxcomb | palette | 0x100;  // Combine flags + high palette bit

  // Skip pixels outside of the sprite area, until we reach the sprite "inside"
  while (cnt) {
    u32 pixel_x = (u32)(source_x >> 8), pixel_y = (u32)(source_y >> 8);

    // Stop once we find a pixel that is actually *inside* the map.
    if (pixel_x < obj_dimw && pixel_y < obj_dimh)
      break;

    dst_ptr++;
    source_x += dx;
    if (rotate)
      source_y += dy;
    cnt--;
  }

  // Draw sprite pixels by looking them up first. Lookup address is tricky!
  u8 pixval = 0;
  u32 mctr = 0;
  for (u32 i = 0; i < cnt; i++) {
    u32 pixel_x = (u32)(source_x >> 8), pixel_y = (u32)(source_y >> 8);

    // Check if we run out of the sprite, then we can safely abort.
    if (pixel_x >= obj_dimw || pixel_y >= obj_dimh)
      return;

    // For mosaic, we "remember" the last looked up pixel.  Counter form
    // avoids a per-pixel divl (see render_affine_background).
    if (!mosaic || !mctr) {
      // Lookup pixel and draw it.
      if (is8bpp) {
        // We lookup the byte directly and render it.
        const u32 tile_off =
          base_tile +                        // Character base
          ((pixel_y >> 3) * tile_pitch) +    // Skip vertical blocks
          ((pixel_x >> 3) * tile_bsize) +    // Skip horizontal blocks
          ((pixel_y & 0x7) * tile_bwidth) +  // Skip vertical rows to the pixel
          (pixel_x & 0x7);                   // Skip the horizontal offset

        pixval = vram[0x10000 + (tile_off & 0x7FFF)];   // Read pixel value!
      } else {
        const u32 tile_off =
          base_tile +                        // Character base
          ((pixel_y >> 3) * tile_pitch) +    // Skip vertical blocks
          ((pixel_x >> 3) * tile_bsize) +    // Skip horizontal blocks
          ((pixel_y & 0x7) * tile_bwidth) +  // Skip vertical rows to the pixel
          ((pixel_x >> 1) & 0x3);            // Skip the horizontal offset

        u8 pixpair = vram[0x10000 + (tile_off & 0x7FFF)]; // Read 2 pixels @4bpp
        pixval = ((pixel_x & 1) ? pixpair >> 4 : pixpair & 0xF);
      }
      mctr = mosh;
    }
    if (mosaic) mctr--;

    // Render the pixel value
    if (pixval) {
      if (rdtype == FULLCOLOR)
        *dst_ptr = palptr[pixval | palette];
      else if (rdtype == INDXCOLOR)
        *dst_ptr = pixval | px_attr;  // Add combine flags
      else if (rdtype == STCKCOLOR) {
        // Stack pixels on top of the pixel value and combine flags
        if (*dst_ptr & 0x100)
          *dst_ptr = pixval | px_attr | ((*dst_ptr) & 0xFFFF0000);
        else
          *dst_ptr = pixval | px_attr | ((*dst_ptr) << 16);  // Stack pixels
      }
      else if (rdtype == PIXCOPY)
        *dst_ptr = dst_ptr[240];
    }

    // Move to the next pixel, update coords accordingly
    dst_ptr++;
    source_x += dx;
    if (rotate)
      source_y += dy;
  }
}

// Renders a single sprite on the current scanline.
// This function calls the affine or regular renderer depending on the sprite.
// Will calculate whether sprite has certain effects (flip, rotation ...) to
// use an optimized renderer function.
template <typename stype, rendtype rdtype, bool is8bpp, bool mosaic>
inline static void render_sprite(
  const t_sprite *obji, bool is_affine, u32 start, u32 end, stype *scanline,
  u32 pxcomb, const u16* palptr
) {
  s32 vcount = read_ioreg(REG_VCOUNT);
  bool obj1dmap = read_ioreg(REG_DISPCNT) & 0x40;
  const u32 msk = is8bpp && !obj1dmap ? 0x3FE : 0x3FF;
  const u32 base_tile = (obji->attr2 & msk) * 32;

  const u32 mosv = (mosaic ? (read_ioreg(REG_MOSAIC) >> 12) & 0xF : 0) + 1;
  const u32 mosh = (mosaic ? (read_ioreg(REG_MOSAIC) >>  8) & 0xF : 0) + 1;

  // Render the object scanline using the correct mode.
  // (in 4bpp mode calculate the palette number)
  // Objects use the higher palette part
  u16 pal = (is8bpp ? 0 : ((obji->attr2 >> 8) & 0xF0));

  if (is_affine) {
    u32 pnum = (obji->attr1 >> 9) & 0x1f;
    const t_affp *affp_base = (t_affp*)oam_ram;
    const t_affp *affp = &affp_base[pnum];

    if (affp->dy == 0)     // No rotation happening (just scale)
      render_affine_object<stype, rdtype, mosaic, is8bpp, false>(
        obji, affp, obji->is_double, start, end, scanline, mosv, mosh,
        base_tile, pxcomb, pal, palptr);
    else                   // Full rotation and scaling
      render_affine_object<stype, rdtype, mosaic, is8bpp, true>(
        obji, affp, obji->is_double, start, end, scanline, mosv, mosh,
        base_tile, pxcomb, pal, palptr);
  } else {
    // The object could be out of the window, check and skip.
    if (obji->obj_x >= (signed)end || obji->obj_x + obji->obj_w <= (signed)start)
      return;

    // Non-affine objects can be flipped on both edges.
    bool hflip = obji->attr1 & 0x1000;
    bool vflip = obji->attr1 & 0x2000;

    // Calulate the vertical offset (row) to be displayed. Account for vflip.
    u32 voffset = vflip ? obji->obj_y + obji->obj_h - vcount - 1
                        : vcount - obji->obj_y;
    if (mosaic)
      voffset -= voffset % mosv;

    // Calculate base tile for the object (points to the row to be drawn).
    u32 tile_bsize  = is8bpp ? tile_size_8bpp : tile_size_4bpp;
    u32 tile_bwidth = is8bpp ? tile_width_8bpp : tile_width_4bpp;
    u32 obj_pitch = obj1dmap ? (obji->obj_w / 8) * tile_bsize : 1024;
    u32 hflip_off = hflip ? ((obji->obj_w / 8) - 1) * tile_bsize : 0;

    // Calculate the pointer to the tile.
    const u32 tile_offset =
      base_tile +                    // Char offset
      (voffset / 8) * obj_pitch +    // Select tile row offset
      (voffset % 8) * tile_bwidth +  // Skip tile rows
      hflip_off;                     // Account for horizontal flip

    // Make everything relative to start
    s32 obj_x_offset  = obji->obj_x - start;
    u32 clipped_width = obj_x_offset >= 0 ? obji->obj_w : obji->obj_w + obj_x_offset;
    u32 max_range = obj_x_offset >= 0 ? end - obji->obj_x : end - start;
    u32 max_draw = MIN(max_range, clipped_width);

    if (mosaic && mosh > 1) {
      if (hflip)
        render_object_mosaic<stype, rdtype, is8bpp, true>(
          obj_x_offset, max_draw, &scanline[start], tile_offset, mosh, pxcomb, pal, palptr);
      else
        render_object_mosaic<stype, rdtype, is8bpp, false>(
          obj_x_offset, max_draw, &scanline[start], tile_offset, mosh, pxcomb, pal, palptr);
    } else {
      if (hflip)
        render_object<stype, rdtype, is8bpp, true>(
          obj_x_offset, max_draw, &scanline[start], tile_offset, pxcomb, pal, palptr);
      else
        render_object<stype, rdtype, is8bpp, false>(
          obj_x_offset, max_draw, &scanline[start], tile_offset, pxcomb, pal, palptr);
    }
  }
}

// Renders objects on a scanline for a given priority.
// This function assumes that order_obj has been called to prepare the objects.
template <typename stype, rendtype rdtype>
void render_scanline_objs(
  u32 priority, u32 start, u32 end, void *raw_ptr, const u16* palptr
) {
  CPU_TIME(1);
  stype *scanline = (stype*)raw_ptr;
  s32 vcount = read_ioreg(REG_VCOUNT);
  s32 objn;
  u32 objcnt = obj_priority_count[priority][vcount];
  u8 *objlist = obj_priority_list[priority][vcount];

  // Render all the visible objects for this priority (back to front)
  for (objn = objcnt-1; objn >= 0; objn--) {
    // Objects in the list are pre-filtered and sorted in the appropriate order
    u32 objoff = objlist[objn];
    const t_oam *oamentry = &((t_oam*)oam_ram)[objoff];

    u16 obj_attr0 = eswap16(oamentry->attr0);
    u16 obj_attr1 = eswap16(oamentry->attr1);
    u16 obj_shape = obj_attr0 >> 14;
    u16 obj_size = (obj_attr1 >> 14);
    bool is_affine = obj_attr0 & 0x100;
    bool is_trans = ((obj_attr0 >> 10) & 0x3) == OBJ_MOD_SEMITRAN;

    t_sprite obji = {
      .obj_x = ((s32)(obj_attr1 << 23) >> 23) + stereo_obj_shift[priority],
      .obj_y = obj_attr0 & 0xFF,
      .obj_w = obj_dim_table[obj_shape][obj_size][0],
      .obj_h = obj_dim_table[obj_shape][obj_size][1],
      .attr1 = obj_attr1,
      .attr2 = eswap16(oamentry->attr2),
      .is_double = (obj_attr0 & 0x200) != 0,
    };

    s32 obj_maxw = (is_affine && obji.is_double) ? obji.obj_w * 2 : obji.obj_w;

    // The object could be out of the window, check and skip.
    if (obji.obj_x >= (signed)end || obji.obj_x + obj_maxw <= (signed)start)
      continue;

    // ST-OBJs force 1st target bit (forced blending)
    bool forcebld = is_trans && rdtype != FULLCOLOR;

    if (obji.obj_y > 160)
      obji.obj_y -= 256;

    // In PIXCOPY mode, we have already some stuff rendered (winout) and now
    // we render the "win-in" area for this object. The PIXCOPY function will
    // copy (merge) the two pixels depending on the result of the sprite render
    // The temporary buffer is rendered on the next scanline area.
    if (rdtype == PIXCOPY) {
      u32 sec_start = MAX((signed)start, obji.obj_x);
      u32 sec_end   = MIN((signed)end, obji.obj_x + obj_maxw);
      u32 obj_enable = read_ioreg(REG_WINOUT) >> 8;

      // Render at the next scanline!
      u16 *tmp_ptr = (u16*)&scanline[GBA_SCREEN_PITCH];
      render_scanline_conditional(sec_start, sec_end, tmp_ptr, obj_enable);
    }

    // Calculate combine masks. These store 2 bits of info: 1st and 2nd target.
    // If set, the current pixel belongs to a layer that is 1st or 2nd target.
    // For ST-objs, we set an extra bit, for later blending.
#ifdef GBA_STEREO_DEBUG
    stereo_debug_obj = priority;
#endif
    u32 pxcomb = (forcebld ? 0x800 : 0) | color_flags(4);

    bool emosaic = (obj_attr0 & 0x1000) != 0;
    bool is_8bpp = (obj_attr0 & 0x2000) != 0;

    // Some games enable mosaic but set it to size 0 (1), so ignore.
    const u32 mosreg = read_ioreg(REG_MOSAIC) & 0xFF00;

    if (emosaic && mosreg) {
      if (is_8bpp)
        render_sprite<stype, rdtype, true, true>(
          &obji, is_affine, start, end, scanline, pxcomb, palptr);
      else
        render_sprite<stype, rdtype, false, true>(
          &obji, is_affine, start, end, scanline, pxcomb, palptr);
    } else {
      if (is_8bpp)
        render_sprite<stype, rdtype, true, false>(
          &obji, is_affine, start, end, scanline, pxcomb, palptr);
      else
        render_sprite<stype, rdtype, false, false>(
          &obji, is_affine, start, end, scanline, pxcomb, palptr);
    }
  }
}


// Goes through the object list in the OAM (from #127 to #0) and adds objects
// into a sorted list by priority for the current row.
// Invisible objects are discarded. ST-objects are flagged. Cycle counting is
// performed to discard excessive objects (to match HW capabilities).
static void order_obj(u32 video_mode)
{
  CPU_TIME(4);
  u32 obj_num;
  u32 row;
  t_oam *oam_base = (t_oam*)oam_ram;
  u16 rend_cycles[160];

  bool hblank_free = read_ioreg(REG_DISPCNT) & 0x20;
  u16 max_rend_cycles = !sprite_limit ? REND_CYC_MAX :
                         hblank_free  ? REND_CYC_REDUCED :
                                        REND_CYC_SCANLINE;

  memset(obj_priority_count, 0, sizeof(obj_priority_count));
  memset(obj_alpha_count, 0, sizeof(obj_alpha_count));
  memset(rend_cycles, 0, sizeof(rend_cycles));

  for(obj_num = 0; obj_num < 128; obj_num++)
  {
    t_oam *oam_ptr = &oam_base[obj_num];
    u16 obj_attr0 = eswap16(oam_ptr->attr0);

    // Bit 9 disables regular sprites (that is, non-affine ones).
    if ((obj_attr0 & 0x0300) == 0x0200)
      continue;

    u16 obj_shape = obj_attr0 >> 14;
    u32 obj_mode = (obj_attr0 >> 10) & 0x03;

    // Prohibited shape and mode
    if ((obj_shape == 0x3) || (obj_mode == OBJ_MOD_INVALID))
      continue;

    u16 obj_attr2 = eswap16(oam_ptr->attr2);

    // On bitmap modes, objs 0-511 are not usable, ingore them.
    if ((video_mode >= 3) && (!(obj_attr2 & 0x200)))
      continue;

    u16 obj_attr1 = eswap16(oam_ptr->attr1);
    // Calculate object size (from size and shape attr bits)
    u16 obj_size = (obj_attr1 >> 14);
    s32 obj_height = obj_dim_table[obj_shape][obj_size][1];
    s32 obj_width  = obj_dim_table[obj_shape][obj_size][0];
    s32 obj_y = obj_attr0 & 0xFF;

    if(obj_y > 160)
      obj_y -= 256;

    // Double size for affine sprites with double bit set
    if(obj_attr0 & 0x200)
    {
      obj_height *= 2;
      obj_width *= 2;
    }

    if(((obj_y + obj_height) > 0) && (obj_y < 160))
    {
      s32 obj_x = (s32)(obj_attr1 << 23) >> 23;

      if(((obj_x + obj_width) > 0) && (obj_x < 240))
      {
        u32 obj_priority = (obj_attr2 >> 10) & 0x03;
        bool is_affine = obj_attr0 & 0x100;
        // Clip Y coord and height to the 0..159 interval
        u32 starty = MAX(obj_y, 0);
        u32 endy   = MIN(obj_y + obj_height, 160);

        // Calculate needed cycles to render the sprite
        u16 cyccnt = is_affine ? (10 + obj_width * 2) : obj_width;

        switch (obj_mode) {
        case OBJ_MOD_SEMITRAN:
          for(row = starty; row < endy; row++)
          {
            if (rend_cycles[row] < max_rend_cycles) {
              u32 cur_cnt = obj_priority_count[obj_priority][row];
              obj_priority_list[obj_priority][row][cur_cnt] = obj_num;
              obj_priority_count[obj_priority][row] = cur_cnt + 1;
              rend_cycles[row] += cyccnt;
              // Mark the row as having semi-transparent objects
              obj_alpha_count[row] = 1;
            }
          }
          break;
        case OBJ_MOD_WINDOW:
          obj_priority = 4;
          /* fallthrough */
        case OBJ_MOD_NORMAL:
          // Add the object to the list.
          for(row = starty; row < endy; row++)
          {
            if (rend_cycles[row] < max_rend_cycles) {
              u32 cur_cnt = obj_priority_count[obj_priority][row];
              obj_priority_list[obj_priority][row][cur_cnt] = obj_num;
              obj_priority_count[obj_priority][row] = cur_cnt + 1;
              rend_cycles[row] += cyccnt;
            }
          }
          break;
        };
      }
    }
  }
}

u32 layer_order[16];
u32 layer_count;

// Sorts active BG/OBJ layers and generates an ordered list of layers.
// Things are drawn back to front, so lowest priority goes first.
static void order_layers(u32 layer_flags, u32 vcnt)
{
  CPU_TIME(4);
  bool obj_enabled = (layer_flags & 0x10);
  s32 priority;

  // Pre-fetch the 4 layer priorities once instead of reading io_registers
  // four times per priority (16 reads -> 4 reads per scanline).  The
  // compiler can't CSE the read across the outer loop because io_registers
  // is global and could in principle be modified by JIT-translated code
  // between iterations; that doesn't actually happen here (we are inside
  // update_scanline which runs to completion before returning to the JIT),
  // but stating it explicitly costs nothing.
  const u32 layer_prio[4] = {
    (u32)(read_ioreg(REG_BGxCNT(0)) & 0x03),
    (u32)(read_ioreg(REG_BGxCNT(1)) & 0x03),
    (u32)(read_ioreg(REG_BGxCNT(2)) & 0x03),
    (u32)(read_ioreg(REG_BGxCNT(3)) & 0x03),
  };

  layer_count = 0;

  for(priority = 3; priority >= 0; priority--)
  {
    bool anyobj = obj_priority_count[priority][vcnt] > 0;
    s32 lnum;

    for(lnum = 3; lnum >= 0; lnum--)
    {
      if(((layer_flags >> lnum) & 1) &&
         (layer_prio[lnum] == (u32)priority))
      {
        layer_order[layer_count++] = lnum;
      }
    }

    if(obj_enabled && anyobj)
      layer_order[layer_count++] = priority | 0x04;
  }
}


// Blending is performed by separating an RGB value into 0G0R0B (32 bit)
// Since blending factors are at most 16, mult/add operations do not overflow
// to the neighbouring color and can be performed much faster than separatedly

// Here follow the mask value to separate/expand the color to 32 bit,
// the mask to detect overflows in the blend operation and

#define BLND_MSK (SATR_MSK | SATG_MSK | SATB_MSK)

#ifdef USE_XBGR1555_FORMAT
  #define OVFG_MSK 0x04000000
  #define OVFR_MSK 0x00008000
  #define OVFB_MSK 0x00000020
  #define SATG_MSK 0x03E00000
  #define SATR_MSK 0x00007C00
  #define SATB_MSK 0x0000001F
#else
  #define OVFG_MSK 0x08000000
  #define OVFR_MSK 0x00010000
  #define OVFB_MSK 0x00000020
  #define SATG_MSK 0x07E00000
  #define SATR_MSK 0x0000F800
  #define SATB_MSK 0x0000001F
#endif

// GBA brightness adds/subtracts a truncated delta in native 5-bit channels.
// Multiplying by (16-y) instead rounds the final colour the wrong way.
static inline u16 effect_brightness(u16 color, unsigned y, bool brighten) {
#ifdef USE_XBGR1555_FORMAT
  const u32 mask = 0x03e07c1f;
#else
  const u32 mask = 0x07c0f81f;
#endif
  u32 pixel = (color | ((u32)color << 16)) & mask;
  u32 delta = ((((brighten ? mask-pixel : pixel) * y) >> 4) & mask);
  pixel = brighten ? pixel+delta : pixel-delta;
  return (pixel >> 16) | pixel;
}

static u32 blend_products[2][512];
#if defined(ARM_ARCH) && !defined(__aarch64__)
static u32 blend_generation;
#else
static u16 blend_palette[512];
#endif
static u32 blend_coefficients=~0u;
static void prepare_blend_products(u32 a,u32 b) {
  u32 coefficients=a|(b<<8);
#if defined(ARM_ARCH) && !defined(__aarch64__)
  if(blend_generation==gba_palette_generation && blend_coefficients==coefficients)return;
#else
  if(blend_coefficients==coefficients && !memcmp(blend_palette,palette_ram_converted,sizeof(blend_palette)))return;
#endif
  for(unsigned i=0;i<512;i++) {
    u32 p=palette_ram_converted[i];p=(p|(p<<16))&BLND_MSK;
    blend_products[0][i]=p*a;blend_products[1][i]=p*b;
  }
#if defined(ARM_ARCH) && !defined(__aarch64__)
  blend_generation=gba_palette_generation;
#else
  memcpy(blend_palette,palette_ram_converted,sizeof(blend_palette));
#endif
  blend_coefficients=coefficients;
}

typedef enum
{
  OBJ_BLEND,    // No effects, just blend forced-blend pixels (ie. ST objects)
  BLEND_ONLY,   // Just alpha blending (if the pixels are 1st and 2nd target)
  BLEND_BRIGHT, // Perform alpha blending if appropiate, and brighten otherwise
  BLEND_DARK,   // Same but with darken effecg
} blendtype;

// Applies blending (and optional brighten/darken) effect to a bunch of
// color-indexed pixel pairs. Depending on the mode and the pixel target
// number, blending, darken/brighten or no effect will be applied.
// Bits 0-8 encode the color index (paletted colors)
// Bit 9 is set if the pixel belongs to a 1st target layer
// Bit 10 is set if the pixel belongs to a 2nd target layer
// Bit 11 is set if the pixel belongs to a ST-object
template <blendtype bldtype, bool st_objs>
static void merge_blend(u32 start, u32 end, u16 *dst, u32 *src) {
  CPU_TIME(2);
  u32 bldalpha = read_ioreg(REG_BLDALPHA);
  u32 brightf = MIN(16, read_ioreg(REG_BLDY) & 0x1F);
  u32 blend_a = MIN(16, (bldalpha >> 0) & 0x1F);
  u32 blend_b = MIN(16, (bldalpha >> 8) & 0x1F);

  bool can_saturate = blend_a + blend_b > 16;
  if(cpu_blend)prepare_blend_products(blend_a,blend_b);

  if (can_saturate) {
    // If blending can result in saturation, we need to clamp output values.
    while (start < end) {
      u32 pixpair = src[start];
      // If ST-OBJ, force blending mode (has priority over other effects).
      // If regular blending mode, blend if 1st/2nd bits are set respectively.
      // Otherwise, apply other color effects if 1st bit is set.
      bool force_blend = (pixpair & 0x04000800) == 0x04000800;
      bool do_blend    = (pixpair & 0x04000200) == 0x04000200;
      if ((st_objs && force_blend) || (do_blend && bldtype == BLEND_ONLY)) {
        // Top pixel is 1st target, pixel below is 2nd target. Blend!
        u32 sum;
        if(cpu_blend) sum=blend_products[0][pixpair&0x1ff]+blend_products[1][(pixpair>>16)&0x1ff];
        else {
          u16 p1=palette_ram_converted[pixpair&0x1ff],p2=palette_ram_converted[(pixpair>>16)&0x1ff];
          sum=(((p1|(p1<<16))&BLND_MSK)*blend_a)+(((p2|(p2<<16))&BLND_MSK)*blend_b);
        }
        u32 pfe = (sum >> 4);

        // If the overflow bit is set, saturate (set) all bits to one.
        if (pfe & (OVFR_MSK | OVFG_MSK | OVFB_MSK)) {
          if (pfe & OVFG_MSK)
            pfe |= SATG_MSK;
          if (pfe & OVFR_MSK)
            pfe |= SATR_MSK;
          if (pfe & OVFB_MSK)
            pfe |= SATB_MSK;
        }
        pfe &= BLND_MSK;
        dst[start++] = (pfe >> 16) | pfe;
      }
      else if ((bldtype == BLEND_DARK || bldtype == BLEND_BRIGHT) &&
               (pixpair & 0x200) == 0x200) {
        // Top pixel is 1st-target, can still apply bright/dark effect.
        u16 pidx = palette_ram_converted[pixpair & 0x1FF];
        u32 epixel = (pidx | (pidx << 16)) & BLND_MSK;
        u32 pa = bldtype == BLEND_DARK ? 0 : ((BLND_MSK * brightf) >> 4) & BLND_MSK;
        u32 pb = ((epixel * (16 - brightf)) >> 4) & BLND_MSK;
        epixel = (pa + pb) & BLND_MSK;
        dst[start++] = (epixel >> 16) | epixel;
      }
      else {
        dst[start++] = palette_ram_converted[pixpair & 0x1FF];   // No effects
      }
    }
  } else {
    while (start < end) {
      u32 pixpair = src[start];
      bool do_blend    = (pixpair & 0x04000200) == 0x04000200;
      bool force_blend = (pixpair & 0x04000800) == 0x04000800;
      if ((st_objs && force_blend) || (do_blend && bldtype == BLEND_ONLY)) {
        // Top pixel is 1st target, pixel below is 2nd target. Blend!
        u32 sum;
        if(cpu_blend) sum=blend_products[0][pixpair&0x1ff]+blend_products[1][(pixpair>>16)&0x1ff];
        else {
          u16 p1=palette_ram_converted[pixpair&0x1ff],p2=palette_ram_converted[(pixpair>>16)&0x1ff];
          sum=(((p1|(p1<<16))&BLND_MSK)*blend_a)+(((p2|(p2<<16))&BLND_MSK)*blend_b);
        }
        u32 pfe = (sum >> 4) & BLND_MSK;
        dst[start++] = (pfe >> 16) | pfe;
      }
      else if ((bldtype == BLEND_DARK || bldtype == BLEND_BRIGHT) &&
               (pixpair & 0x200) == 0x200) {
        // Top pixel is 1st-target, can still apply bright/dark effect.
        u16 pidx = palette_ram_converted[pixpair & 0x1FF];
        u32 epixel = (pidx | (pidx << 16)) & BLND_MSK;
        u32 pa = bldtype == BLEND_DARK ? 0 : ((BLND_MSK * brightf) >> 4) & BLND_MSK;
        u32 pb = ((epixel * (16 - brightf)) >> 4) & BLND_MSK;
        epixel = (pa + pb) & BLND_MSK;
        dst[start++] = (epixel >> 16) | epixel;
      }
      else {
        dst[start++] = palette_ram_converted[pixpair & 0x1FF];   // No effects
      }
    }
  }
}

// Applies brighten/darken effect to a bunch of color-indexed pixels.
template <blendtype bldtype>
static void merge_brightness(u32 start, u32 end, u16 *srcdst) {
  CPU_TIME(2);
  u32 brightness = MIN(16, read_ioreg(REG_BLDY) & 0x1F);

  while (start < end) {
    u16 spix = srcdst[start];
    u16 pixcol = palette_ram_converted[spix & 0x1FF];

    if ((spix & 0x200) == 0x200) {
      // Pixel is 1st target, can apply color effect.
      u32 epixel = (pixcol | (pixcol << 16)) & BLND_MSK;
      u32 pa = bldtype == BLEND_DARK ? 0 : ((BLND_MSK * brightness) >> 4) & BLND_MSK; // B/W
      u32 pb = ((epixel * (16 - brightness)) >> 4) & BLND_MSK;  // Pixel color
      epixel = (pa + pb) & BLND_MSK;
      pixcol = (epixel >> 16) | epixel;
    }

    srcdst[start++] = pixcol;
  }
}

// Fills a segment using the backdrop color (in the right mode).
template<rendtype rdmode, typename dsttype>
void fill_line_background(u32 start, u32 end, dsttype *scanline) {
  dsttype bgcol = palette_ram_converted[0];
  u16 bg_comb = color_flags(5);
  while (start < end)
    if (rdmode == FULLCOLOR)
      scanline[start++] = bgcol;
    else
      scanline[start++] = 0 | bg_comb;
}

// Renders the backdrop color (ie. whenever no layer is active) applying
// any effects that might still apply (usually darken/brighten).
static void render_backdrop(u32 start, u32 end, u16 *scanline) {
  u16 bldcnt = read_ioreg(REG_BLDCNT);
  u16 pixcol = palette_ram_converted[0];
  u32 effect = (bldcnt >> 6) & 0x03;
  u32 bd_1st_target = ((bldcnt >> 0x5) & 0x01);

  if (bd_1st_target && effect == COL_EFFECT_BRIGHT) {
    u32 brightness = MIN(16, read_ioreg(REG_BLDY) & 0x1F);

    // Unpack 16 bit pixel for fast blending operation
    u32 epixel = (pixcol | (pixcol << 16)) & BLND_MSK;
    u32 pa = ((BLND_MSK * brightness)      >> 4) & BLND_MSK;  // White color
    u32 pb = ((epixel * (16 - brightness)) >> 4) & BLND_MSK;  // Pixel color
    epixel = (pa + pb) & BLND_MSK;
    pixcol = (epixel >> 16) | epixel;
  }
  else if (bd_1st_target && effect == COL_EFFECT_DARK) {
    u32 brightness = MIN(16, read_ioreg(REG_BLDY) & 0x1F);
    u32 epixel = (pixcol | (pixcol << 16)) & BLND_MSK;
    epixel = ((epixel * (16 - brightness)) >> 4) & BLND_MSK;  // Pixel color
    pixcol = (epixel >> 16) | epixel;
  }

  // Fill the line with that color
  while (start < end)
    scanline[start++] = pixcol;
}

// The base layer's pixel representation is identical in both eyes, including
// backdrop target flags and stacked/indexed modes. Windows and mosaic restart
// sampling at segment boundaries. Cache each fixed WIN0/1 span separately;
// OBJ windows and mosaic retain the original renderer.
template<typename T>
static void stereo_render_base(tile_render_function fn, unsigned layer,
    u32 start, u32 end, T *dst, unsigned mode) {
  int delta = -2 * stereo_half[layer];
  if (stereo_eye == 1) for(unsigned n=0;n<stereo_base_count;n++) {
    const StereoBaseSpan &span=stereo_base_spans[n];
    if(span.layer!=layer || span.mode!=mode || span.start!=start || span.end!=end || span.width!=sizeof(T)) continue;
    if(end-start <= (unsigned)(delta<0?-delta:delta)) break;
    unsigned lo=start+(delta<0?-delta:0);
    unsigned hi=end-(delta>0?delta:0);
    if(lo>=hi) break;
    memcpy(dst+lo, (T*)((u8*)stereo_base_row+span.offset)+lo-start+delta, (hi-lo)*sizeof(T));
    if(lo>start) fn(layer,start,lo,dst,palette_ram_converted);
    if(hi<end) fn(layer,hi,end,dst,palette_ram_converted);
    return;
  }
  fn(layer,start,end,dst,palette_ram_converted);
  stereo_base_bytes=(stereo_base_bytes+3)&~3u;
  if(stereo_eye==-1 && stereo_base_count<8 &&
      stereo_base_bytes+(end-start)*sizeof(T)<=sizeof(stereo_base_row)) {
    memcpy((u8*)stereo_base_row+stereo_base_bytes,dst+start,(end-start)*sizeof(T));
    stereo_base_spans[stereo_base_count++]={start,end,layer,mode,stereo_base_bytes,sizeof(T)};
    stereo_base_bytes+=(end-start)*sizeof(T);
  }
}

// Renders all the available and enabled layers (in tiled mode).
// Walks the list of layers in visibility order and renders them in the
// specified mode (taking into consideration the first layer, etc).
template<rendtype bgmode, rendtype objmode, typename dsttype>
void tile_render_layers(u32 start, u32 end, dsttype *dst_ptr, u32 enabled_layers) {
  u32 lnum;
  u32 base_done = 0;
  u16 dispcnt = read_ioreg(REG_DISPCNT);
  u16 video_mode = dispcnt & 0x07;
  bool obj_enabled = (enabled_layers & 0x10);   // Objects are visible

  bool objlayer_is_1st_tgt = ((read_ioreg(REG_BLDCNT) >> 4) & 1) != 0;
  bool has_trans_obj = obj_alpha_count[read_ioreg(REG_VCOUNT)];

  for (lnum = 0; lnum < layer_count; lnum++) {
    u32 layer = layer_order[lnum];
    bool is_obj = layer & 0x4;
    if (is_obj && obj_enabled) {
      bool can_skip_blend = !has_trans_obj && !objlayer_is_1st_tgt;

      // If it's the first layer, make sure to fill with backdrop color.
      if (!base_done)
        fill_line_background<bgmode, dsttype>(start, end, dst_ptr);

      // Optimization: skip blending mode if no blending can happen to this layer
      if (objmode == STCKCOLOR && can_skip_blend)
        render_scanline_objs<dsttype, INDXCOLOR>(
          layer & 0x3, start, end, dst_ptr, &palette_ram_converted[0x100]);
      else
        render_scanline_objs<dsttype, objmode>(
          layer & 0x3, start, end, dst_ptr, &palette_ram_converted[0x100]);

      base_done = 1;
    }
    else if (!is_obj && ((1 << layer) & enabled_layers)) {
      bool layer_is_1st_tgt = ((read_ioreg(REG_BLDCNT) >> layer) & 1) != 0;
      bool can_skip_blend = !has_trans_obj && !layer_is_1st_tgt;

      bool is_affine = (video_mode >= 1) && (layer >= 2);
      u32 fnidx = (base_done) | (is_affine ? 2 : 0);

      // Can optimize rendering if no blending can really happen.
      // If stack mode, no blending and not base layer, we might speed up a bit
      if (bgmode == STCKCOLOR && can_skip_blend) {
        static const tile_render_function rdfns[4] = {
          render_scanline_text<dsttype, INDXCOLOR, true>,
          render_scanline_text<dsttype, INDXCOLOR, false>,
          render_scanline_affine<dsttype, INDXCOLOR, true>,
          render_scanline_affine<dsttype, INDXCOLOR, false>,
        };
        if (stereo_row_cache && !base_done)
          stereo_render_base(rdfns[fnidx],layer,start,end,dst_ptr,INDXCOLOR);
        else rdfns[fnidx](layer, start, end, dst_ptr, palette_ram_converted);
      } else {
        static const tile_render_function rdfns[4] = {
          render_scanline_text<dsttype, bgmode, true>,
          render_scanline_text<dsttype, bgmode, false>,
          render_scanline_affine<dsttype, bgmode, true>,
          render_scanline_affine<dsttype, bgmode, false>,
        };
        if (stereo_row_cache && !base_done)
          stereo_render_base(rdfns[fnidx],layer,start,end,dst_ptr,bgmode);
        else rdfns[fnidx](layer, start, end, dst_ptr, palette_ram_converted);
      }

      base_done = 1;
    }
  }

  // Render background if we did not render any active layer.
  if (!base_done)
    fill_line_background<bgmode, dsttype>(start, end, dst_ptr);
}


// Renders all layers honoring color effects (blending, brighten/darken).
// It uses different rendering routines depending on the coloring effect
// requirements, speeding up common cases where no effects are used.

// No effects use NORMAL mode (RBB565 color is written on the buffer).
// For blending, we use BLEND mode to record the two top-most pixels.
// For other effects we use COLOR16, which records an indexed color in the
// buffer (used for darken/brighten effects at later passes) or COLOR32,
// which similarly uses an indexed color for rendering but recording one
// color for the background and another one for the object layer.

static void render_w_effects(
  u32 start, u32 end, u16* scanline, u32 enable_flags,
  const layer_render_struct *renderers
) {
  bool effects_enabled = enable_flags & 0x20;   // Window bit for effects.
  bool obj_blend = obj_alpha_count[read_ioreg(REG_VCOUNT)] > 0;
  u16 bldcnt = read_ioreg(REG_BLDCNT);

  // If the window bits disable effects, default to NONE
  u32 effect_type = effects_enabled ? ((bldcnt >> 6) & 0x03)
                                    : COL_EFFECT_NONE;

  switch (effect_type) {
  case COL_EFFECT_BRIGHT:
    {
      // If no layers are 1st target, no effect will really happen.
      bool some_1st_tgt = (read_ioreg(REG_BLDCNT) & 0x3F) != 0;
      // If the factor is zero, it's the same as "regular" rendering.
      bool non_zero_blend = (read_ioreg(REG_BLDY) & 0x1F) != 0;
      if (some_1st_tgt && non_zero_blend) {
        if (obj_blend) {
          u32 tmp_buf[240];
          renderers->indexed_u32(start, end, tmp_buf, enable_flags);
          merge_blend<BLEND_BRIGHT, true>(start, end, scanline, tmp_buf);
        } else {
          renderers->indexed_u16(start, end, scanline, enable_flags);
          merge_brightness<BLEND_BRIGHT>(start, end, scanline);
        }
        return;
      }
    }
    break;

  case COL_EFFECT_DARK:
    {
      // If no layers are 1st target, no effect will really happen.
      bool some_1st_tgt = (read_ioreg(REG_BLDCNT) & 0x3F) != 0;
      // If the factor is zero, it's the same as "regular" rendering.
      bool non_zero_blend = (read_ioreg(REG_BLDY) & 0x1F) != 0;
      if (some_1st_tgt && non_zero_blend) {
        if (obj_blend) {
          u32 tmp_buf[240];
          renderers->indexed_u32(start, end, tmp_buf, enable_flags);
          merge_blend<BLEND_DARK, true>(start, end, scanline, tmp_buf);
        } else {
          renderers->indexed_u16(start, end, scanline, enable_flags);
          merge_brightness<BLEND_DARK>(start, end, scanline);
        }
        return;
      }
    }
    break;

  case COL_EFFECT_BLEND:
    {
      // If no layers are 1st or 2nd target, no effect will really happen.
      bool some_1st_tgt = (read_ioreg(REG_BLDCNT) & 0x003F) != 0;
      bool some_2nd_tgt = (read_ioreg(REG_BLDCNT) & 0x3F00) != 0;
      // If 1st target is 100% opacity and 2nd is 0%, just render regularly.
      bool non_trns_tgt = (read_ioreg(REG_BLDALPHA) & 0x1F1F) != 0x001F;
      if (some_1st_tgt && some_2nd_tgt && non_trns_tgt) {
        u32 tmp_buf[240];
        renderers->stacked(start, end, tmp_buf, enable_flags);
        if (obj_blend)
          merge_blend<BLEND_ONLY, true>(start, end, scanline, tmp_buf);
        else
          merge_blend<BLEND_ONLY, false>(start, end, scanline, tmp_buf);
        return;
      }
    }
    break;

  case COL_EFFECT_NONE:
    // Default case, see below.
    break;
  };

  // Default rendering mode, without layer effects (except perhaps sprites).
  if (obj_blend) {
    u32 tmp_buf[240];
    renderers->stacked(start, end, tmp_buf, enable_flags);
    merge_blend<OBJ_BLEND, true>(start, end, scanline, tmp_buf);
  } else {
    renderers->fullcolor(start, end, scanline, enable_flags);
  }
}

#define bitmap_layer_render_functions(rdmode, dsttype, mode, ttype, w, h) {   \
 {                                                                            \
   render_scanline_bitmap<rdmode, dsttype, mode, ttype, w, h, BLIT, false>,   \
   render_scanline_bitmap<rdmode, dsttype, mode, ttype, w, h, SCALED, false>, \
   render_scanline_bitmap<rdmode, dsttype, mode, ttype, w, h, ROTATED, false>,\
 },                                                                           \
 {                                                                            \
   render_scanline_bitmap<rdmode, dsttype, mode, ttype, w, h, BLIT, true>,    \
   render_scanline_bitmap<rdmode, dsttype, mode, ttype, w, h, SCALED, true>,  \
   render_scanline_bitmap<rdmode, dsttype, mode, ttype, w, h, ROTATED, true>, \
 }                                                                            \
}

static const bitmap_layer_render_struct idx32_bmrend[3][2] =
{
  bitmap_layer_render_functions(INDXCOLOR, u32, 3, u16, 240, 160),
  bitmap_layer_render_functions(INDXCOLOR, u32, 4, u8,  240, 160),
  bitmap_layer_render_functions(INDXCOLOR, u32, 5, u16, 160, 128)
};

// Render the BG and OBJ in a bitmap scanline from start to end ONLY if
// enable_flag allows that layer/OBJ.

template<rendtype bgmode, rendtype objmode, typename dsttype>
static void bitmap_render_layers(
  u32 start, u32 end, dsttype *scanline, u32 enable_flags)
{
  u16 dispcnt = read_ioreg(REG_DISPCNT);
  bool has_trans_obj = obj_alpha_count[read_ioreg(REG_VCOUNT)];
  bool objlayer_is_1st_tgt = (read_ioreg(REG_BLDCNT) & 0x10) != 0;
  bool bg2_is_1st_tgt = (read_ioreg(REG_BLDCNT) & 0x4) != 0;

  // Fill in the renderers for a layer based on the mode type,
  static const bitmap_layer_render_struct renderers[3][2] =
  {
    bitmap_layer_render_functions(bgmode, dsttype, 3, u16, 240, 160),
    bitmap_layer_render_functions(bgmode, dsttype, 4, u8,  240, 160),
    bitmap_layer_render_functions(bgmode, dsttype, 5, u16, 160, 128)
  };

  const u32 mosamount = read_ioreg(REG_MOSAIC) & 0xFF;
  u32 bg_control = read_ioreg(REG_BG2CNT);
  u32 mmode = ((bg_control & 0x40) && (mosamount != 0)) ? 1 : 0;

  unsigned modeidx = (dispcnt & 0x07) - 3;
  const bitmap_layer_render_struct *mode_rend = &renderers[modeidx][mmode];
  const bitmap_layer_render_struct *idxm_rend = &idx32_bmrend[modeidx][mmode];

  u32 current_layer;
  u32 layer_order_pos;

  fill_line_background<bgmode, dsttype>(start, end, scanline);

  for(layer_order_pos = 0; layer_order_pos < layer_count; layer_order_pos++)
  {
    current_layer = layer_order[layer_order_pos];
    if(current_layer & 0x04)
    {
      if (enable_flags & 0x10) {
        bool can_skip_blend = !has_trans_obj && !objlayer_is_1st_tgt;

        // Optimization: skip blending mode if no blending can happen to this layer
        if (objmode == STCKCOLOR && can_skip_blend)
          render_scanline_objs<dsttype, INDXCOLOR>(
            current_layer & 3, start, end, scanline, &palette_ram_converted[0x100]);
        else
          render_scanline_objs<dsttype, objmode>(
            current_layer & 3, start, end, scanline, &palette_ram_converted[0x100]);
      }
    }
    else
    {
      if(enable_flags & 0x04) {
        s32 dx = (s16)read_ioreg(REG_BG2PA);
        s32 dy = (s16)read_ioreg(REG_BG2PC);

        // Optimization: Skip stack mode if there's no blending happening.
        bool can_skip_blend = !has_trans_obj && !bg2_is_1st_tgt;
        const bitmap_layer_render_struct *rd =
          (bgmode == STCKCOLOR && can_skip_blend) ? idxm_rend : mode_rend;

        if (dy)
          rd->affine_render(start, end, scanline, palette_ram_converted);
        else if (dx == 256)
          rd->blit_render(start, end, scanline, palette_ram_converted);
        else
          rd->scale_render(start, end, scanline, palette_ram_converted);
      }
    }
  }
}


static const layer_render_struct tile_mode_renderers = {
  .fullcolor   = tile_render_layers<FULLCOLOR, FULLCOLOR, u16>,
  .indexed_u16 = tile_render_layers<INDXCOLOR, INDXCOLOR, u16>,
  .indexed_u32 = tile_render_layers<INDXCOLOR, STCKCOLOR, u32>,
  .stacked     = tile_render_layers<STCKCOLOR, STCKCOLOR, u32>,
};

static const layer_render_struct bitmap_mode_renderers = {
  .fullcolor   = bitmap_render_layers<FULLCOLOR, FULLCOLOR, u16>,
  .indexed_u16 = bitmap_render_layers<INDXCOLOR, INDXCOLOR, u16>,
  .indexed_u32 = bitmap_render_layers<INDXCOLOR, STCKCOLOR, u32>,
  .stacked     = bitmap_render_layers<STCKCOLOR, STCKCOLOR, u32>,
};

// Modes 3/5 have one direct-colour BG plus paletted OBJ/backdrop. Only
// effect spans enter this path; the original full-colour loops stay intact.
static void render_bitmap_effects(u32 start, u32 end, u16 *scanline,
                                  u32 enable_flags) {
  u32 control = read_ioreg(REG_BLDCNT);
  unsigned effect = enable_flags & 0x20 ? (control >> 6) & 3 : 0;
  bool translucent = obj_alpha_count[read_ioreg(REG_VCOUNT)] > 0;
  if (!effect && !translucent) {
    bitmap_mode_renderers.fullcolor(start,end,scanline,enable_flags);
    return;
  }
  u32 pixels[240];
  bitmap_mode_renderers.stacked(start,end,pixels,enable_flags);
  unsigned alpha = read_ioreg(REG_BLDALPHA);
  unsigned a = MIN(16, alpha & 31), b = MIN(16, (alpha >> 8) & 31);
  unsigned y = MIN(16, read_ioreg(REG_BLDY) & 31);
  for (unsigned x=start; x<end; x++) {
    u32 pair = pixels[x], top = pair & 65535, below = pair >> 16;
    u16 c = top & 0x1000 ? bitmap_effect_colors[x] : palette_ram_converted[top & 511];
    bool blend = (below & 0x400) && ((top & 0x800) || (effect == 1 && (top & 0x200)));
    if (blend) {
      u16 d = below & 0x1000 ? bitmap_effect_colors[x] : palette_ram_converted[below & 511];
#ifdef USE_XBGR1555_FORMAT
      const unsigned shifts[3] = {0,5,10};
#else
      const unsigned shifts[3] = {0,6,11};
#endif
      unsigned result=0;
      for (unsigned i=0;i<3;i++) {
        unsigned shift=shifts[i];
        unsigned value=(((c>>shift)&31)*a+((d>>shift)&31)*b)>>4;
        result |= MIN(31,value)<<shift;
      }
      c=result;
    } else if ((top & 0x200) && effect >= 2) {
      c=effect_brightness(c,y,effect == 2);
    }
    scanline[x]=c;
  }
}

// Renders a full scanline, given an enable_flags mask (for which layers and
// effects are enabled).
static void render_scanline_conditional(
  u32 start, u32 end, u16 *scanline, u32 enable_flags)
{
  u16 dispcnt = read_ioreg(REG_DISPCNT);
  u32 video_mode = dispcnt & 0x07;
#ifdef GBA_STEREO_DEBUG
  if(stereo_debug_pass) {
    if(layer_count && (enable_flags&0x1f))
      tile_render_layers<INDXCOLOR, INDXCOLOR, u16>(start,end,scanline,enable_flags);
    else for(u32 x=start;x<end;x++) scanline[x]=8<<12;
    return;
  }
#endif

  // Check if any layer is actually active.
  if (layer_count && (enable_flags & 0x1F)) {
    // Direct bitmap colours use a row sidecar during effect composition.
    if(video_mode < 3)
      render_w_effects(start, end, scanline, enable_flags, &tile_mode_renderers);
    else
      render_bitmap_effects(start, end, scanline, enable_flags);
  }
  else
    // Render the backdrop color, since no layers are enabled/visible.
    render_backdrop(start, end, scanline);
}

// Renders the are outside of all active windows
static void render_windowout_pass(u16 *scanline, u32 start, u32 end)
{
  u32 winout = read_ioreg(REG_WINOUT);
  u32 wndout_enable = winout & 0x3F;

  render_scanline_conditional(start, end, scanline, wndout_enable);
}

// Renders window-obj. This is a pixel-level windowing effect, based on sprites
// (objects) with a special rendering mode (the sprites are not themselves
// visible but rather "enable" other pixels to be rendered conditionally).
static void render_windowobj_pass(u16 *scanline, u32 start, u32 end)
{
  u32 winout = read_ioreg(REG_WINOUT);
  u32 wndout_enable = winout & 0x3F;

  // First we render the "window-out" segment.
  render_scanline_conditional(start, end, scanline, wndout_enable);

  // Now we render the objects in "copy" mode. This renders the scanline in
  // WinObj-mode to a temporary buffer and performs a "copy-mode" render.
  // In this mode, we copy pixels from the temp buffer to the final buffer
  // whenever an object pixel is rendered.
  render_scanline_objs<u16, PIXCOPY>(4, start, end, scanline, NULL);

  // TODO: Evaluate whether it's better to render the whole line and copy,
  // or render subsegments and copy as we go (depends on the pixel/obj count)
}

// If the window Y coordinates are out of the window range we can skip
// rendering the inside of the window.
inline bool in_window_y(u32 vcount, u32 top, u32 bottom) {
  // TODO: check if these are reversed when top-bottom are also reversed.
  if (top > 227)     // This causes the window to be invisible
    return false;
  if (bottom > 227)  // This makes it all visible
    return true;

  if (top > bottom)  /* Reversed: if not in the "band" */
    return vcount > top || vcount <= bottom;

  return vcount >= top && vcount < bottom;
}

// Renders window 0/1. Checks boundaries and divides the segment into
// subsegments (if necessary) rendering each one in their right mode.
// outfn is called for "out-of-window" rendering.
template<window_render_function outfn, unsigned winnum>
static void render_window_n_pass(u16 *scanline, u32 start, u32 end)
{
  u32 vcount = read_ioreg(REG_VCOUNT);
  // Check the Y coordinates to check if they fall in the right row
  u32 win_top = read_ioreg(REG_WINxV(winnum)) >> 8;
  u32 win_bot = read_ioreg(REG_WINxV(winnum)) & 0xFF;
  // Check the X coordinates and generate up to three segments
  // Clip the coordinates to the [start, end) range.
  u32 win_lraw = read_ioreg(REG_WINxH(winnum)) >> 8;
  u32 win_rraw = read_ioreg(REG_WINxH(winnum)) & 0xFF;
  u32 win_l = MAX(start, MIN(end, win_lraw));
  u32 win_r = MAX(start, MIN(end, win_rraw));
  bool goodwin = win_lraw < win_rraw;

  if (!in_window_y(vcount, win_top, win_bot) || (win_lraw == win_rraw))
    // WindowN is completely out, just render all out.
    outfn(scanline, start, end);
  else {
    // Render window withtin the clipped range
    // Enable bits for stuff inside the window (and outside)
    u32 winin = read_ioreg(REG_WININ);
    u32 wndn_enable = (winin >> (8 * winnum)) & 0x3F;

    // If the window is defined upside down, the areas are inverted.
    if (goodwin) {
      // Render [start, win_l) range (which is outside the window)
      if (win_l != start)
        outfn(scanline, start, win_l);
      // Render the actual window0 pixels
      render_scanline_conditional(win_l, win_r, scanline, wndn_enable);
      // Render the [win_l, end] range (outside)
      if (win_r != end)
        outfn(scanline, win_r, end);
    } else {
      // Render [0, win_r) range (which is "inside" window0)
      if (win_r != start)
        render_scanline_conditional(start, win_r, scanline, wndn_enable);
      // The actual window is now outside, render recursively
      outfn(scanline, win_r, win_l);
      // Render the [win_l, 240] range ("inside")
      if (win_l != end)
        render_scanline_conditional(win_l, end, scanline, wndn_enable);
    }
  }
}

// Renders a full scaleline, taking into consideration windowing effects.
// Breaks the rendering step into N steps, for each windowed region.
static void render_scanline_window(u16 *scanline)
{
  u16 dispcnt = read_ioreg(REG_DISPCNT);
  u32 win_ctrl = (dispcnt >> 13);

  // Priority decoding for windows
  switch (win_ctrl) {
  case 0x0: // No windows are active.
    render_scanline_conditional(0, 240, scanline);
    break;

  case 0x1: // Window 0
    render_window_n_pass<render_windowout_pass, 0>(scanline, 0, 240);
    break;

  case 0x2: // Window 1
    render_window_n_pass<render_windowout_pass, 1>(scanline, 0, 240);
    break;

  case 0x3: // Window 0 & 1
    render_window_n_pass<render_window_n_pass<render_windowout_pass, 1>, 0>(scanline, 0, 240);
    break;

  case 0x4: // Window Obj
    render_windowobj_pass(scanline, 0, 240);
    break;

  case 0x5: // Window 0 & Obj
    render_window_n_pass<render_windowobj_pass, 0>(scanline, 0, 240);
    break;

  case 0x6: // Window 1 & Obj
    render_window_n_pass<render_windowobj_pass, 1>(scanline, 0, 240);
    break;

  case 0x7: // Window 0, 1 & Obj
    render_window_n_pass<render_window_n_pass<render_windowobj_pass, 1>, 0>(scanline, 0, 240);
    break;
  }
}

// Fast plain-row compositor: walk priority order once. Each BG decode writes
// the same source color to both destinations at its fixed disparity. Only
// nonoverlapping eye edges decode again. OBJ keeps its proven per-eye renderer.
// No masks or blends are approximated: those rows use render_scanline_window.
static void stereo_plain_row(u16 *left, u16 *right, unsigned mode,
    const u16 *scroll, const s32 *ax, const s32 *ay, const int *half) {
  bool base=false;
  for (unsigned n=0;n<layer_count;n++) {
    unsigned layer=layer_order[n];
    if (layer&4) {
      if(!base) {
        fill_line_background<FULLCOLOR,u16>(0,240,left);
        memcpy(right,left,480);
      }
      unsigned p=layer&3;
      stereo_obj_shift[p]=-half[4+p];
      render_scanline_objs<u16,FULLCOLOR>(p,0,240,left,&palette_ram_converted[0x100]);
      stereo_obj_shift[p]=half[4+p];
      render_scanline_objs<u16,FULLCOLOR>(p,0,240,right,&palette_ram_converted[0x100]);
      stereo_obj_shift[p]=0;
    } else {
      bool affine=mode && layer>=2;
      unsigned idx=base|(affine?2:0);
      static const tile_render_function dual[4]={
        render_scanline_text<u16,DUALCOLOR,true>,render_scanline_text<u16,DUALCOLOR,false>,
        render_scanline_affine<u16,DUALCOLOR,true>,render_scanline_affine<u16,DUALCOLOR,false>};
      static const tile_render_function single[4]={
        render_scanline_text<u16,FULLCOLOR,true>,render_scanline_text<u16,FULLCOLOR,false>,
        render_scanline_affine<u16,FULLCOLOR,true>,render_scanline_affine<u16,FULLCOLOR,false>};
      int h=half[layer], d=2*h;
      unsigned lo=d<0?-d:0, hi=d>0?240-d:240;
      write_ioreg(REG_BGxHOFS(layer),(scroll[layer]+h)&511);
      if(affine) {
        affine_reference_x[layer-2]=ax[layer-2]+h*(s16)read_ioreg(REG_BGxPA(layer));
        affine_reference_y[layer-2]=ay[layer-2]+h*(s16)read_ioreg(REG_BGxPC(layer));
      }
      stereo_dual_offset=(intptr_t)((uintptr_t)right-(uintptr_t)left)+d*2;
      dual[idx](layer,lo,hi,left,palette_ram_converted);
      if(lo) single[idx](layer,0,lo,left,palette_ram_converted);
      if(hi<240) single[idx](layer,hi,240,left,palette_ram_converted);
      write_ioreg(REG_BGxHOFS(layer),(scroll[layer]-h)&511);
      if(affine) {
        affine_reference_x[layer-2]=ax[layer-2]-h*(s16)read_ioreg(REG_BGxPA(layer));
        affine_reference_y[layer-2]=ay[layer-2]-h*(s16)read_ioreg(REG_BGxPC(layer));
      }
      if(d>0) single[idx](layer,0,d,right,palette_ram_converted);
      if(d<0) single[idx](layer,240+d,240,right,palette_ram_converted);
    }
    base=true;
  }
  if(!base) {
    fill_line_background<FULLCOLOR,u16>(0,240,left);memcpy(right,left,480);
  }
}

static const u8 active_layers[] = {
  0x1F,   // Mode 0, Tile BG0-3 and OBJ
  0x17,   // Mode 1, Tile BG0-2 and OBJ
  0x1C,   // Mode 2, Tile BG2-3 and OBJ
  0x14,   // Mode 3, BMP  BG2 and OBJ
  0x14,   // Mode 4, BMP  BG2 and OBJ
  0x14,   // Mode 5, BMP  BG2 and OBJ
  0,      // Unused
  0,
};

void update_scanline(void)
{
  CPU_TIME(0);
  u32 pitch = get_screen_pitch();
  u16 dispcnt = read_ioreg(REG_DISPCNT);
  u32 vcount = read_ioreg(REG_VCOUNT);
  u16 *screen_offset = get_screen_pixels() + (vcount * pitch);
  u32 video_mode = dispcnt & 0x07;

  if(gba_journal_enabled)gba_journal_row(vcount,skip_next_frame);
  if(skip_next_frame)
    return;

  // If OAM has been modified since the last scanline has been updated then
  // reorder and reprofile the OBJ lists.
  if(reg[OAM_UPDATED])
  {
    order_obj(video_mode);
    reg[OAM_UPDATED] = 0;
  }

  order_layers((dispcnt >> 8) & active_layers[video_mode], vcount);

  const bool stereo = stereo_right && stereo_strength;
  if (stereo) {
    if (!vcount) {
      stereo_frame_depths();
      stereo_frame_different=false;
      stereo_frame_flags=0;
      if(stereo_hold) --stereo_hold;
      stereo_flat = stereo_hold != 0;
      if(!stereo_flat && stereo_ramp<60) ++stereo_ramp;
    }
    if(dispcnt&0x80) stereo_frame_flags|=1;
    if(video_mode>=3) stereo_frame_flags|=2;
    // Forced blank may be used for a single visible scanline during VRAM
    // updates. Flatten only that white row, not the following 30 pictures.
    // Bitmap mode changes still use the conservative scene transition ramp.
    if (video_mode>=3) {
      stereo_flat=true; stereo_hold=30; stereo_ramp=0;
    }
    for(unsigned bg=0;bg<4;bg++) if(((dispcnt>>8)&active_layers[video_mode])&(1<<bg))
      ++stereo_votes[bg][read_ioreg(REG_BGxCNT(bg))&3];
    if(dispcnt&0x1000) for(int p=0;p<4;p++) stereo_obj_votes[p]+=obj_priority_count[p][vcount];
  }

  bool gpu_owns=false;
  if(gba_index_enabled) {
    int gpu_half[8]={0};
    if(stereo && !stereo_flat && stereo_admitted)for(int i=0;i<8;i++) {
      int scaled=stereo_depth[i]*(int)stereo_strength*(int)stereo_ramp;
      gpu_half[i]=scaled>=0?(scaled+3932160)/7864320:-((-scaled+3932160)/7864320);
    }
    int gpu_result=gba_index_row(vcount,gpu_half,stereo && stereo_admitted);
    gpu_owns=gpu_result>0;
    if(gpu_result<0) {
      gba_index_raster(get_screen_pixels(),stereo && stereo_admitted?stereo_right:NULL,vcount);
    }
    if(gpu_owns) {
      for(unsigned i=0;i<8;i++)if(gpu_half[i])stereo_frame_different=true;
    }
  }
  if(!gpu_owns) {
  if (!stereo || stereo_flat || !stereo_admitted || (dispcnt & 0x80)) {
    if(dispcnt & 0x80) memset(screen_offset,0xff,240*sizeof(u16));
    else render_scanline_window(screen_offset);
    if(stereo && stereo_admitted)
      memcpy(stereo_right+vcount*240,screen_offset,240*sizeof(u16));
  } else {
    u16 scroll[4]; s32 ax[2], ay[2]; int *half=stereo_half;
    memcpy(ax,affine_reference_x,sizeof(ax));
    memcpy(ay,affine_reference_y,sizeof(ay));
    for(unsigned bg=0;bg<4;bg++) scroll[bg]=read_ioreg(REG_BGxHOFS(bg));
    for(int i=0;i<8;i++) {
      int scaled=stereo_depth[i]*(int)stereo_strength*(int)stereo_ramp;
      // Symmetric, bounded to -1..+4 pixels per eye at full slider.
      half[i]=scaled>=0 ? (scaled+3932160)/7864320 : -((-scaled+3932160)/7864320);
    }
    bool different=false;
    unsigned enabled=(dispcnt>>8)&active_layers[video_mode];
    for(unsigned bg=0;bg<4;bg++) {
      if ((enabled&(1<<bg)) && half[bg]) different=true;
      if ((enabled&16) && obj_priority_count[bg][vcount] && half[4+bg]) different=true;
    }
    // No disparity on any enabled contributor: render once, retain r2 pixels.
    stereo_base_count=stereo_base_bytes=0;
    stereo_row_cache=!(dispcnt&0x8000);
    for(unsigned bg=0;bg<4;bg++) if ((enabled&(1<<bg)) &&
        (read_ioreg(REG_BGxCNT(bg))&0x40)) stereo_row_cache=false;
    bool plain=stereo_row_cache && !(dispcnt&0x6000) && !(read_ioreg(REG_BLDCNT)&0xc0) &&
        !obj_alpha_count[vcount];
    if(plain && different) stereo_plain_row(screen_offset,stereo_right+vcount*240,
        video_mode,scroll,ax,ay,half);
    for(int eye=-1;(!plain || !different) && eye<=1;eye+=2) {
      stereo_eye=eye;
      if (eye==1 && !different) {
        memcpy(stereo_right+vcount*240,screen_offset,240*sizeof(u16));
        break;
      }
      for(unsigned bg=0;bg<4;bg++) {
        int shift=half[bg]*eye;
        write_ioreg(REG_BGxHOFS(bg),(scroll[bg]-shift)&511);
        if(bg>=2) {
          affine_reference_x[bg-2]=ax[bg-2]-shift*(s16)read_ioreg(REG_BGxPA(bg));
          affine_reference_y[bg-2]=ay[bg-2]-shift*(s16)read_ioreg(REG_BGxPC(bg));
        }
        stereo_obj_shift[bg]=half[4+bg]*eye;
      }
      render_scanline_window(eye<0 ? screen_offset : stereo_right+vcount*240);
    }
    stereo_frame_different |= different;
    stereo_eye=0; stereo_row_cache=false;
    memset(stereo_obj_shift,0,sizeof(stereo_obj_shift));
    for(unsigned bg=0;bg<4;bg++) write_ioreg(REG_BGxHOFS(bg),scroll[bg]);
    memcpy(affine_reference_x,ax,sizeof(ax));
    memcpy(affine_reference_y,ay,sizeof(ay));
  }
  } // CPU path: fallback or validation mode
#ifdef GBA_STEREO_DEBUG
  // True unshifted top-source ownership. Blended pixels show their top
  // contributor; the production eye passes still resolve both contributors.
  stereo_debug_pass=true;
  if(video_mode<3 && !(dispcnt&0x80)) render_scanline_window(stereo_debug_pixels+vcount*240);
  else for(int x=0;x<240;x++) stereo_debug_pixels[vcount*240+x]=8<<12;
  stereo_debug_pass=false;
  for(int i=0;i<8;i++) stereo_debug_depth[vcount][i]=stereo_flat?0:stereo_depth[i];
#endif

  // Mode 0 does not use any affine params at all.
  if (video_mode) {
    // Account for vertical mosaic effect, by correcting affine references.
    const u32 bgmosv = ((read_ioreg(REG_MOSAIC) >> 4) & 0xF) + 1;

    if (read_ioreg(REG_BG2CNT) & 0x40) {   // Mosaic enabled for this BG
      if ((vcount % bgmosv) == bgmosv-1) { // Correct after the last line
        affine_reference_x[0] += (s16)read_ioreg(REG_BG2PB) * bgmosv;
        affine_reference_y[0] += (s16)read_ioreg(REG_BG2PD) * bgmosv;
      }
    } else {
      affine_reference_x[0] += (s16)read_ioreg(REG_BG2PB);
      affine_reference_y[0] += (s16)read_ioreg(REG_BG2PD);
    }

    if (read_ioreg(REG_BG3CNT) & 0x40) {
      if ((vcount % bgmosv) == bgmosv-1) {
        affine_reference_x[1] += (s16)read_ioreg(REG_BG3PB) * bgmosv;
        affine_reference_y[1] += (s16)read_ioreg(REG_BG3PD) * bgmosv;
      }
    } else {
      affine_reference_x[1] += (s16)read_ioreg(REG_BG3PB);
      affine_reference_y[1] += (s16)read_ioreg(REG_BG3PD);
    }
  }
}


