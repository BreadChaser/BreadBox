// Rounded post-vblank gaps at the 3DS 59.83 Hz display clock.
#ifndef ROOM_REFRESH_H
#define ROOM_REFRESH_H
#include <stdint.h>
static inline unsigned room_refresh_gap(uint64_t dt, uint64_t hz)
{
 return (unsigned)((dt * 5983 + hz * 50) / (hz * 100));
}
static inline unsigned room_refresh_lost(unsigned gap, unsigned expected)
{
 return gap > expected ? gap - expected : 0;
}
#endif
