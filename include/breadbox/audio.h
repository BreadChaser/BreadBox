// Audio output (audio.c)
#pragma once
#include <3ds.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// audio.c
bool audio_init(double sample_rate);
void audio_exit(void);
size_t audio_write(const int16_t *data, size_t frames);
void audio_pause(bool paused);
void audio_stats(char *buf, size_t len);
unsigned audio_occupancy(void);
bool audio_capture_start(void);
void audio_hash_start(u32 frames);
bool audio_test_tone_toggle(void);
