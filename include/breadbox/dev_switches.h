// Developer switches (switches.txt and remote load options)
#pragma once
#include <stdbool.h>

extern bool sw_gpu_flush, sw_gpu_async, sw_nogputhread;

extern bool sw_ff;
extern bool sw_prof, sw_stats, sw_o3ds, sw_fs1, sw_fs3, sw_noskip, sw_nosputhread, sw_acap;
extern int sw_state, sw_gldbg, sw_vramdump, sw_autosave_test;

extern int sw_pmu0, sw_pmu1;
extern char sw_clock[8], sw_cdra[8], sw_opt[8][48];

extern bool sw_pmuphase;

extern bool sw_pmuclass;

extern bool sw_wsbundle;
extern int sw_wsscreen;

extern float sw_quitturn;       // quitturn=<seconds>: the Quit's turn and fade (default 0.7)
extern int sw_room3d;           // room3d=30|60|off: the Old's room in 3D, 30 locked or 60 unlocked

extern float sw_slider;
extern int sw_stereo;

extern bool sw_sfxlog;          // sfxlog: one log line per menu sound played (the sound walk's event log)
extern bool sw_framelog;        // framelog: one log line per settings / pause card frame (what each presented frame showed)
extern bool sw_csnd, sw_nonet, sw_nosound, sw_noirrst, sw_noaa, sw_aa, sw_nolcd, sw_nocovers;
