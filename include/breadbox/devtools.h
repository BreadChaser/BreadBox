#ifndef PSX3D_DEVTOOLS_H
#define PSX3D_DEVTOOLS_H
#include "statsview.h"
#include <stddef.h>
#include <stdint.h>
/* Explicit switches.txt opt-in only. No sockets/threads/probes when disabled. */
extern bool dev_remote, dev_bench, dev_bench_home;
extern char dev_token[96], dev_update_url[256];
void dev_authorize(void);
bool dev_enabled(void);
bool dev_batch_active(void);
void dev_interrupt(void);
enum { DEV_UI_NONE, DEV_UI_LIBRARY, DEV_UI_PAUSE, DEV_UI_SETTINGS, DEV_UI_STATES, DEV_UI_RESUME, DEV_UI_CONTINUE,
	DEV_UI_WELCOME };   // the room: the first-launch welcome again (bb_welcome_open, as Settings > Help > Welcome)
int dev_ui_pending(void);
int dev_ui_take(void);
extern int dev_ui_slot;
bool dev_covers_blocked(void);
bool covers_refresh(int game);
void covers_debug(char *out, size_t len);
void dev_init(void);
void dev_shutdown(void);
bool dev_shutdown_timeout(uint64_t timeout); /* false: worker still running, nothing freed */
bool dev_tick(bool playing); /* main thread, after pica_thread_sync */
void dev_tick_keys(void);
bool dev_transition(void);
bool dev_exiting(void);
bool dev_next_game(char *path, char *save, char *title);
bool dev_remote_game(void);  /* current game came from a remote load/batch */
void dev_local_game(void);   /* before a local launch: drop remote run switches */
void dev_game_started(const char *title, bool state_ok);
void dev_game_ended(bool ok);
void dev_system_sample(const char *system, double elapsed, unsigned emulated, unsigned drawn, unsigned shown, double emu_ms, double present_ms, const char *audio, double guest_fps, const char *fields);
void dev_sample(const statsview_sample *s, const char *probe, int top_lum, double cycles, double ev0,
                double ev1);
void dev_reply(const char *json);
/* Main thread switch parser, shared by bench and remote load. */
bool switches_run(char *line, char *game, size_t len, char *error, size_t error_len);
void switches_local(void);   /* restore the startup switches after remote runs */
bool switches_set(const char *word);
bool emu_debug_state(int slot, bool save);
bool pmu_sample(double *cycles, double *ev0, double *ev1);
/* Network worker only; no emulation/GPU calls. */
bool dev_update(const char *args, char *result, size_t capacity);
#endif
