// Box art (covers.c)
#pragma once
#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>

// covers.c - box art
void covers_init(void);
bool covers_exit(void);   // false: a worker did not stop (it still runs when main returns)
bool covers_network_exit(u64 timeout);   // after a self-update's commit; false: a worker did not stop
// The cover's texture, or NULL while it loads. *sub is the part of it that
// holds the picture (its own aspect), in citro2d's flipped convention.
C3D_Tex *covers_get(int game, Tex3DS_SubTexture *sub);
C3D_Tex *covers_small(int game, Tex3DS_SubTexture *sub);

bool covers_full(int game);   // BreadBox: its full-size cover is shown (or none is coming)
void covers_reset(void);
// UI-thread transaction: drain workers before mutating library entries.
bool covers_library_begin(void);
void covers_library_end(void);
void covers_focus(int game);
void covers_pump(void);
void covers_stats_reset(void);   // developer diagnostics
extern bool covers_slow;
extern unsigned covers_sd_ms;
bool covers_busy(void);
bool covers_home_frame(void);   // a menu frame: HOME held back during cover downloads (true: it went HOME)
bool covers_stop(void);
bool covers_library_try_begin(void);   // covers_library_begin that never waits: false until the worker is idle
/* Failure retains every resource still reachable by background work. */
#define COVERS_STOP_DEADLINE_MS 65000
bool covers_stop_wait(void);
bool covers_wait_home(void); /* retain a rejected HOME press for the menu */
bool covers_stop_wait_apt(void); /* inside an APT hook: no recursive aptMainLoop */
const char *covers_wait_error(void);
const char *covers_stop_pending(void); /* NULL: paused and drained; otherwise the busy operation */
void covers_resume(void);

void covers_release_gpu(void);
