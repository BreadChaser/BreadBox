/* GPL-2.0-or-later. Shared native-index palette encoding. */
#ifndef BB_GBA_PALETTE_LUT_H
#define BB_GBA_PALETTE_LUT_H
/* PICA LUT words: unsigned Q0.12 base in bits0..11, signed-magnitude
 * interpolation delta in bits12..23. Palette plateaus use delta=0.
 * Seven Q0.12 units bias toward the interior of the desired 8-bit bin,
 * selected by the real New2040 byte sweep (0..254 exact).
 * Zero stays exactly zero; 255 saturates to the legal maximum, never0x1000.
 * GBA palette/effect values are multiples of4 and at most252. 255 is
 * outside that domain; saturated4095 yields254 on hardware, identical to255
 * after RGB565/RGB555 storage. Never overflow into delta bits. */
static inline unsigned gba_palette_lut_entry(unsigned value){
 if(!value)return 0;
 unsigned encoded=(value*4096u+254u)/255u+7u;
 return encoded>4095u?4095u:encoded;
}
#endif
