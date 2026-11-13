/* Shell persistence, one active session. All entry points run on the core thread;
 * only immutable snapshots reach the asynchronous file writer. GPL-2.0-or-later. */
#ifndef BB_SAVE_H
#define BB_SAVE_H
#include "bb_system.h"
#include "bb_slots.h"
#include <stdint.h>
#include <time.h>
typedef struct bb_save_damage {
 bool clock;      /* the real-time clock file, not the game save */
 bool backup;     /* a backup of the game save is in Backups ... */
 time_t when;     /* ... written then */
 bool foreign;    /* its size is one another emulator writes for this cartridge: "Use it" may be offered */
} bb_save_damage;
enum { BB_DAMAGE_BACK, BB_DAMAGE_RESTORE, BB_DAMAGE_EMPTY, BB_DAMAGE_USE };
typedef struct {
 const bb_system_t *system;
 const char *key, *battery_path, *state_base;
 const char *history_key; /* optional legacy namespace; default <system>-<key> */
 uint32_t identity, content_bytes;
 int writer_core;
 void (*notice)(const char *);
 void (*restored)(void); /* forget queued audio, on successful load only */
 /* Optional, menus/pause only: the game's last picture as RGB565 rows, at most
  * BB_THUMB_MAX_W x BB_THUMB_MAX_H. Saved beside each state as its postcard. */
 bool (*picture)(uint16_t *rgb565,unsigned *w,unsigned *h);
 /* ...and the memory the service writes pictures from: BB_THUMB_BYTES, owned by
  * the caller (linear memory on the console, never the game's heap). NULL: none. */
 void *picture_buffer;
 uint32_t (*played)(void); /* optional: seconds played so far, for the postcard */
} bb_save_config;
/* The running system's question for a battery (clock) file that is damaged and has no good
 * copy, set by its adapter around a game (NULL: begin fails, as before). It asks the player
 * once: BB_DAMAGE_RESTORE (only when backup), _EMPTY, _USE (only when foreign: the original goes
 * into Backups first, then the file is padded to the core's size), or _BACK (begin fails, error "").
 * The damaged file is set aside as .bad first, never deleted. Outside the per-frame code. */
void bb_save_on_damage(int (*ask)(const bb_save_damage *));
#define BB_THUMB_MAX_W 256
#define BB_THUMB_MAX_H 240
#define BB_THUMB_BYTES (32+BB_THUMB_MAX_W*BB_THUMB_MAX_H*2)
/* One slot as the slot browser shows it, read once when it opens. */
typedef struct {
 bool used;            /* a state (or its backup) exists */
 uint32_t saved;       /* unix time, 0 = unknown */
 uint32_t played;      /* seconds of play time when it was saved, 0 = unknown */
 unsigned w,h;         /* picture size, 0 = none */
} bb_slot_info;
/* BreadBox: the last session's state base (room_walls.c), kept after it ended; "" none. */
const char *bb_save_last_base(void);
/* The running game's picture now (pause menus), false = none. */
bool bb_save_picture(uint16_t *px,unsigned *w,unsigned *h);
/* px (BB_THUMB_MAX_W*BB_THUMB_MAX_H, may be NULL) receives the picture. */
bool bb_save_slot_info(unsigned slot,bb_slot_info *info,uint16_t *px);
/* The slot goes to the backups (history) first; false keeps it. */
bool bb_save_delete(unsigned slot);
/* Worker read of the last durable snapshot; never joins the save writer. */
bool bb_save_continue_read(const bb_system_t *,const bb_content_id *,uint32_t *saved,bool (*cancel)(void *),void *arg);
bool bb_save_continue(const bb_system_t *,const bb_content_id *,uint32_t *saved);
bool bb_save_begin(const bb_save_config *,char *error,size_t cap);
void bb_save_invalidate(void);
bool bb_save_runnable(void); /* false after partial restore: stop core, no further frames/writes */
bool bb_save_queue(void);
bool bb_save_flush(void);
bool bb_save_failed(void);
bool bb_save_state(unsigned slot,bool save);
bool bb_save_exists(unsigned slot);
bool bb_save_absent(unsigned slot);   /* true only when the file is known to be missing (ENOENT) */
uint32_t bb_save_slots_present(void);   /* bit N: <base>.stateN exists (one pass over the folder) */
uint32_t bb_save_time(const char *path,const char *magic);
bool bb_save_time_write(const char *path,uint32_t crc,uint32_t saved);
bool bb_save_end(bool make_continue);
/* A game's end: the state made now; defer: its files written after the menu is back. */
bool bb_save_state_end(unsigned slot,bool defer);
/* Files written after the menu is back, in order, by one thread (bb_save_state_end,
 * the TV's last picture); bb_save_settle waits until they are all on the SD card. */
void bb_later(void (*fn)(void *),void *arg);
bool bb_save_settle_timeout(uint64_t timeout); /* false retains worker and its file ownership */
void bb_save_settle(void);
/* Nonblocking menu check before the import worker takes save/history ownership. */
bool bb_save_background_idle(void);
/* Shared install/relaunch gate: save writer or background import owns files. */
bool bb_save_writers_busy(void);
void bb_later_place(int core,int prio);   /* the later thread's core and priority (main.c, at start) */
/* A whole file read by that thread into buf (size bytes; buf NULL: the thread finds the size
 * and memalign()s it, the caller frees it); state goes QUEUED, READING, then DONE or FAILED.
 * wait: done now (a read not started yet is done here); cancel: before buf goes. */
enum { BB_LATER_IDLE, BB_LATER_QUEUED, BB_LATER_READING, BB_LATER_DONE, BB_LATER_FAILED };
typedef struct bb_later_file { const char *path; void *buf; size_t size; int state;
 void (*after)(struct bb_later_file *); /* optional, on that thread after a good read (a cache flush) */ } bb_later_file;
void bb_later_read(bb_later_file *r);
int bb_later_state(bb_later_file *r);
bool bb_later_wait(bb_later_file *r);
void bb_later_cancel(bb_later_file *r);
/* Work for that thread: fn(arg) there; finish: done now (run here if it has not started) */
typedef struct { void (*fn)(void *); void *arg; int state; } bb_later_task;
void bb_later_do(bb_later_task *t);
bool bb_later_done(bb_later_task *t);
void bb_later_finish(bb_later_task *t);
/* That state file is still being written (its time in *saved): it is there for the menus. */
bool bb_save_writing(const char *path,uint32_t *saved);
/* The last game's final battery write failed (sticky; its snapshot kept): retry it (menu thread),
 * or let it go (the player chose to). bb_save_begin refuses while it is pending. */
bool bb_save_final_failed(void);
bool bb_save_final_retry(void);
/* No retained bytes: acknowledge separately, preserving retryable failures. */
bool bb_save_final_uncaptured(void);
void bb_save_final_acknowledge(void);
void bb_save_final_discard(void);
/* The one choke point (the shell: before a game starts, at exit, each menu frame): settle when
 * asked, then while the final battery write is failed and kept, ask() decides; a closing()
 * system gets one more try. False: the app is closing, stop now (no GPU work, no game). */
enum { BB_SAVE_RETRY, BB_SAVE_LATER, BB_SAVE_LOSE, BB_SAVE_ACK };
bool bb_save_resolve(bool settle,bool (*closing)(void),int (*ask)(void *),void *ctx);
/* Settle everything queued after a game before HOME / sleep (bb_save_apt_init: the shell's hook). */
void bb_save_barrier(void);
void bb_save_apt_init(void);
/* No GPU/audio calls: safe after relinquishing GSP, before arena release. */
bool bb_save_checkpoint(bool make_continue);
/* The Continue point of a pause, so a crash, a flat battery or a stalled game after a pause loses nothing:
 * take: made now on the core thread from the paused game (no SD access: the card opens at once); put: its
 * files written by the later thread (after the card's first frame), the session going on, failures kept
 * for the retry like the end's. While the game stays paused the end and HOME/sleep make no second one;
 * over: it runs again (resume, reset; a load says so itself). */
bool bb_save_pause_take(void);
void bb_save_pause_put(void);
void bb_save_pause_over(void);
/* The Continue question is unanswered, or the player went back from it: HOME, sleep, Close, power-off
 * and the end make no Continue point (the boot state never replaces the player's). */
void bb_save_continue_hold(bool hold);
/* Start over: the old Continue point and its backup go to the backups; false: they stay (and the hold). */
bool bb_save_continue_aside(void);
#endif
