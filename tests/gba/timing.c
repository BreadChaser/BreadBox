#include <assert.h>
#include <stdio.h>
#include "../../systems/gba/gba_timing.h"
int main(void)
{
    gba_costs samples={0};
    assert(gba_cost_median(&samples)==0);
    for(int i=0;i<60;i++)gba_cost_add(&samples,i<55?14000:33000);
    assert(gba_cost_median(&samples)==14000); // spikes do not poison admission
    assert(gba_stereo_mode(14000,4600,500,8000,16743,1000)==30);
    assert(gba_stereo_mode(10000,2000,500,8000,16743,1000)==60);
    assert(gba_stereo_mode(14000,12000,500,8000,16743,1000)==0);
    assert(gba_stereo_mode(0,2000,500,8000,16743,1000)==0);
    for(int i=0;i<60;i++)gba_cost_add(&samples,9000);
    assert(gba_cost_median(&samples)==9000); // scene recovery, ring replacement

    unsigned phase=0, pictures=0, cpu_frames=0;
    for(unsigned i=0;i<600;i++) {
        bool skip=false;
        bool stereo=gba_stereo_draw(30,&phase,true,false,&skip);
        assert(stereo==!skip);pictures+=stereo;cpu_frames++;
    }
    assert(pictures==300 && cpu_frames==600);
    bool skip=false;
    assert(gba_stereo_draw(60,&phase,true,false,&skip) && !skip);
    assert(gba_stereo_draw(60,&phase,true,true,&skip));
    skip=true;assert(!gba_stereo_draw(60,&phase,false,true,&skip));
    skip=false;assert(!gba_stereo_draw(0,&phase,false,false,&skip) && !skip);

    /* Scene estimates may cross every threshold for a minute without ever
     * presenting mono or re-promoting a latched 30-picture session. */
    unsigned mode=60, mono=0, transitions=0, last=60;
    for(unsigned i=0;i<3600;i++) {
        mode=gba_stereo_latch(mode,i%3==0?60:i%3==1?30:0);
        bool dropped=false;
        bool pair=gba_stereo_draw(mode,&phase,i%2,false,&dropped);
        mono+=!pair && !dropped;
        transitions+=mode!=last;last=mode;
    }
    assert(!mono && transitions==1 && mode==30);
    assert(gba_stereo_latch(0,0)==30);
    assert(gba_stereo_latch(0,60)==60);

    const uint32_t hz=268123480;
    gba_clock c;gba_clock_reset(&c,123,hz);
    /* More than a day of frames: no accumulating rounding drift. */
    for(uint64_t n=1;n<=6000000;n++) {
        assert(!gba_clock_skip(&c,c.next));
        assert(!gba_clock_advance(&c,c.next));
        assert(c.next==123+(uint64_t)((__uint128_t)n*hz*280896/16777216));
    }
    gba_clock_reset(&c,100,hz);
    assert(!gba_clock_skip(&c,100+c.whole));
    assert(gba_clock_skip(&c,101+c.whole));
    assert(gba_clock_skip(&c,101+c.whole));
    assert(!gba_clock_skip(&c,101+c.whole));
    assert(!gba_clock_skip(&c,100));
    assert(gba_clock_advance(&c,100+8*c.whole));
    assert(!gba_clock_skip(&c,c.next));
    gba_clock_reset(&c,1ULL<<48,hz); /* suspend/resume starts a fresh deadline */
    assert(!gba_clock_skip(&c,1ULL<<48));
    puts("GBA exact rate for 6M frames, behind-only bounded skip, stall/resume: PASS");
}
