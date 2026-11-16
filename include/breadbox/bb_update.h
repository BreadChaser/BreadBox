#ifndef BB_UPDATE_H
#define BB_UPDATE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#define BB_UPDATE_TITLE UINT64_C(0x000400000B0B0B00)
#define BB_UPDATE_MAX (64u*1024u*1024u)
typedef struct {
 uint16_t version;
 uint32_t size;
 char display[32],url[512],sha256[65];
} bb_update_manifest;
typedef enum {BB_UPDATE_IDLE,BB_UPDATE_DOWNLOAD,BB_UPDATE_VERIFY,BB_UPDATE_READY,
 BB_UPDATE_INSTALL,BB_UPDATE_RELAUNCH,BB_UPDATE_FAILED} bb_update_state;
typedef struct {_Atomic bb_update_state state;} bb_update_machine;
typedef struct {
 bool (*download)(void *),(*verify)(void *),(*flush)(void *),(*install)(void *);
 void *ctx;
} bb_update_ops;
/* One phase per invocation. No work at all while a game is active. */
void bb_update_step(bb_update_machine *,const bb_update_ops *,bool idle);
bool bb_update_manifest_parse(const char *,size_t,bb_update_manifest *);
bool bb_update_release_asset(const char *,size_t,char *,size_t);
bool bb_update_newer(uint64_t title,unsigned candidate,unsigned installed);
bool bb_update_known_answer(void);
typedef enum {BB_UPDATE_HASH_OK,BB_UPDATE_HASH_MISMATCH,BB_UPDATE_HASH_IO} bb_update_hash_result;
bb_update_hash_result bb_update_hash_check_cancel(const char *,const bb_update_manifest *,bool (*)(void *),void *);
bb_update_hash_result bb_update_hash_check(const char *,const bb_update_manifest *);
bool bb_update_hash(const char *path,const bb_update_manifest *);
/* Main thread only; worker handoffs are internal. tick is called only by Home. */
bool bb_update_tick(void); /* true: AM committed; bypass scene cleanup and shut down immediately */
/* Shared menu frame: continue staging only; the room alone installs. */
void bb_update_menu_tick(void);
void bb_update_draw_notice(void);
bool bb_update_installing(void);
bool bb_update_committed(void);
void bb_update_install_frame(void);
void bb_update_shutdown(void);
#define BB_UPDATE_JOIN_NS UINT64_C(5000000000) /* installer tail and developer worker after commit */
/* False if the installer thread is still running after timeout (nothing is freed). */
bool bb_update_shutdown_timeout(uint64_t timeout);
/* After a committed install, with every worker and service down: settle, then
 * request the relaunch only if clean; otherwise clear it so APT returns to HOME.
 * True if the relaunch was requested. */
bool bb_update_handoff(bool clean);
bool bb_update_net_busy(void);
bool bb_update_net_stop(void);
const char *bb_update_preference(void);
const char *bb_update_glance(void);   /* "Up to date", "Update ready", "Update found", "Updates off"; NULL: not known */
void bb_update_settings(void);
#endif
