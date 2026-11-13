/* Boktai GPIO light counter. BreadBox implementation, GPL-2.0-or-later.
 * Cartridge protocol: CS (bit 2) low, reset (bit 1), rising clock (bit 0),
 * comparator output (bit 3). Bright light reaches the threshold sooner.
 * See GBATEK cartridge solar sensor; no upstream implementation copied. */
#ifndef GPSP_SOLAR_H
#define GPSP_SOLAR_H
struct solar_sensor { unsigned count, threshold, previous; };
static unsigned solar_threshold(unsigned level)
{
  static const unsigned attenuation[11]={0,6,14,24,32,42,56,72,96,120,152};
  return 0xe8 - attenuation[level>10?10:level];
}
static unsigned solar_pins(struct solar_sensor *s,unsigned pins,unsigned direction,unsigned level)
{
  if (!(pins&4)) {
    if(pins&2) { s->count=0; s->threshold=solar_threshold(level); }
    else if((pins&1) && !(s->previous&1) && s->count<256) ++s->count;
    if(!(direction&8)) pins=(pins&~8u) | (s->count>=s->threshold?8:0);
  }
  s->previous=pins;
  return pins;
}
#endif
