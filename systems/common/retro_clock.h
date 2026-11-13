/* Exact rational guest clock, independent of LCD refresh. GPL-2.0-or-later. */
#ifndef BB_RETRO_CLOCK_H
#define BB_RETRO_CLOCK_H
#include <stdbool.h>
#include <stdint.h>
typedef struct {
 uint64_t next,whole,remainder,fraction,denominator;
 unsigned consecutive;
} bb_retro_clock;
static inline void bb_retro_clock_reset(bb_retro_clock *c,uint64_t now,uint64_t hz,uint64_t num,uint64_t den)
{
 uint64_t n=hz*den;
 *c=(bb_retro_clock){.next=now,.whole=n/num,.remainder=n%num,.denominator=num};
}
static inline bool bb_retro_clock_skip(bb_retro_clock *c,uint64_t now)
{
 /* At most two skipped pictures. CPU/audio/battery work always runs. */
 if(now>c->next+c->whole && c->consecutive<2){c->consecutive++;return true;}
 c->consecutive=0;return false;
}
static inline bool bb_retro_clock_advance(bb_retro_clock *c,uint64_t now)
{
 c->next+=c->whole;c->fraction+=c->remainder;
 if(c->fraction>=c->denominator){c->fraction-=c->denominator;c->next++;}
 if(now>c->next+4*c->whole){c->next=now;c->fraction=0;return true;}
 return false;
}
#endif
