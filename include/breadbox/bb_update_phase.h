/* Crash-safe update phase journal: updates/phase.txt, one fixed-size checksummed
 * line per phase, at most one small SD write per phase. The log is closed before
 * AM runs, so this file is the only record of the last steps of an install. A torn
 * or damaged line is skipped on read; every complete line before it survives. */
#ifndef BB_UPDATE_PHASE_H
#define BB_UPDATE_PHASE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define BB_UPDATE_PHASE_MAX 32
#define BB_UPDATE_PHASE_LINE 39 /* "BBUP1 SS PP RRRRRRRR TTTTTTTT HHHHHHHH\n" */
#define BB_UPDATE_PHASE_PATH APP_SD_ROOT "/updates/phase.txt"
typedef enum {
 BB_UP_FLUSH=1, BB_UP_LOG_CLOSE, BB_UP_SECOND_SHA, BB_UP_RECEIPT,
 BB_UP_AM_START, BB_UP_WRITES_DONE, BB_UP_GPU_ACK, BB_UP_FINISH,
 BB_UP_HANDLES_CLOSED, BB_UP_INSTALLER_JOIN, BB_UP_COVERS_EXIT,
 BB_UP_DEVNET_EXIT, BB_UP_ROMFS_EXIT, BB_UP_CHAINLOAD, BB_UP_HOME_RETURN,
 BB_UP_FAILURE, BB_UP_NEXT_BOOT,
 /* Appended, not inserted: a journal keeps its numbers across builds. */
 BB_UP_AUDIO_EXIT, BB_UP_GPU_EXIT, BB_UP_LAST=BB_UP_GPU_EXIT
} bb_update_phase;
typedef struct {unsigned sequence,phase;uint32_t result,stamp;} bb_update_phase_record;
/* Append one record (reset: start a new journal). Never retries, keeps no handle.
 * stamp is the caller's millisecond clock; result is phase-specific (1 ok, 0 failed,
 * or a service result code). False if the record could not be written. */
bool bb_update_phase_write(const char *path,unsigned phase,uint32_t result,uint32_t stamp,bool reset);
/* Valid records in order, at most max. Damaged or torn lines are skipped. */
unsigned bb_update_phase_load(const char *path,bb_update_phase_record *out,unsigned max);
const char *bb_update_phase_name(unsigned phase);
/* "name=result name=result ..." for the log and the developer status. */
void bb_update_phase_text(const bb_update_phase_record *r,unsigned n,char *out,size_t cap);
/* update.c: this process's own appends to BB_UPDATE_PHASE_PATH, stamped with its
 * monotonic millisecond clock. Best effort: a failure never stops an install. The
 * first record of an install (flush) starts a fresh journal. */
void bb_update_record(unsigned phase,uint32_t result);
/* Last phase this process recorded (0: none); for tests and status. */
unsigned bb_update_phase_last(void);
#endif
