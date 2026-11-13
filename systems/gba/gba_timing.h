/* GBA time follows its 16.777216 MHz clock, independently of LCD refresh.
 * Carry the fractional tick rather than rounding a frame or locking to 60 Hz. */
#ifndef GBA_TIMING_H
#define GBA_TIMING_H
#include <stdbool.h>
#include <stdint.h>
/* Keep the established fixed-rate storage and constant carry on ARM11.
 * The generic host clock supports variable ratios; GBA never changes rate. */
typedef struct {
    uint64_t next, whole;
    uint32_t remainder, fraction;
    unsigned consecutive;
} gba_clock;
static inline void gba_clock_reset(gba_clock *c, uint64_t now, uint32_t hz)
{
    uint64_t n=(uint64_t)hz*280896;
    *c=(gba_clock){.next=now,.whole=n/16777216,.remainder=n%16777216};
}
static inline bool gba_clock_skip(gba_clock *c, uint64_t now)
{
    /* At most two skipped pictures, then draw even under sustained load.
     * Never skip CPU/audio/backup work. No skip at full speed. */
    if(now>c->next+c->whole && c->consecutive<2) {c->consecutive++;return true;}
    c->consecutive=0;return false;
}
static inline bool gba_clock_advance(gba_clock *c, uint64_t now)
{
    c->next+=c->whole;c->fraction+=c->remainder;
    if(c->fraction>=16777216) {c->fraction-=16777216;c->next++;}
    /* Bound catch-up after a long SD stall; sleep/resume resets explicitly. */
    if(now>c->next+4*c->whole) {c->next=now;c->fraction=0;return true;}
    return false;
}
/* Rolling medians reject SD/scheduling spikes without retaining a peak forever. */
typedef struct { uint64_t values[60]; unsigned count, next; } gba_costs;
static inline void gba_cost_add(gba_costs *c, uint64_t value)
{
    c->values[c->next]=value;c->next=(c->next+1)%60;
    if(c->count<60) c->count++;
}
static inline uint64_t gba_cost_median(const gba_costs *c)
{
    uint64_t v[60];
    for(unsigned i=0;i<c->count;i++) {
        unsigned j=i;v[i]=c->values[i];
        while(j && v[j]<v[j-1]) {uint64_t t=v[j];v[j]=v[j-1];v[j-1]=t;j--;}
    }
    return c->count?v[c->count/2]:0;
}
/* A 30-picture pair includes one complete CPU/audio-only frame. */
static inline unsigned gba_stereo_mode(uint64_t mono, uint64_t extra,
    uint64_t present, uint64_t skipped, uint64_t frame, uint64_t margin)
{
    if(!mono) return 0;
    if(mono+extra+present+margin<frame) return 60;
    if(mono+extra+present+skipped+2*margin<2*frame) return 30;
    return 0;
}
/* Once stereo starts, budget pressure only lowers picture cadence. Latch the
 * lower cadence until an explicit user restart; never oscillate via mono. */
static inline unsigned gba_stereo_latch(unsigned current, unsigned predicted)
{
    return current==30 || predicted!=60 ? 30 : 60;
}
/* Only the picture cadence changes; the caller always runs retro_run.
 * Lateness cannot reject just the right eye: the left is displaced too. */
static inline bool gba_stereo_draw(unsigned mode, unsigned *phase,
    bool late, bool force, bool *skip)
{
    (void)late; (void)force;
    if(mode==30 && ((*phase)++&1)) *skip=true;
    return mode && !*skip;
}
#endif
