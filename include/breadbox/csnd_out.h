// Sound without DSP firmware (csnd_out.c)
#pragma once
#include <3ds.h>
#include <stdbool.h>

// csnd_out.c - sound without DSP firmware
bool csnd_acquire(void);
void csnd_release(void);
int csnd_channel(int n);
double csnd_real_rate(u32 rate);
void csnd_setup(int chn, bool loop, u32 rate, float vol, float pan, const s16 *data, u32 bytes);
void csnd_set_volume(int chn, float vol, float pan);
