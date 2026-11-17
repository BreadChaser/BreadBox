// Crash-safe saving: a file is written to <name>.tmp first and only then put
// in place, so a power cut, flat battery or crash during the write never
// leaves a cut-off memory card or state. FAT (the 3DS SD card) cannot rename
// over an existing file, so the old version is renamed away first.
//
// Memory cards, states and their thumbnails keep the previous good version as
// <name>.bak; loading falls back to it (then to a complete <name>.tmp) when
// <name> is missing or damaged. A damaged <name> is never deleted: it is kept
// as <name>.bad (.bad2 ... when taken) when it has to make room.
#ifndef SAFEFILE_H
#define SAFEFILE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <time.h>

typedef enum { SAFE_NONE, SAFE_CURRENT, SAFE_BACKUP, SAFE_TEMP } safe_source_t;

// What a good copy of a binary file is: exactly size bytes whose first head
// bytes pass valid() (NULL: any content) and, if given, whose whole content
// passes deep() (memory card checksums). A copy that passes valid() but not
// deep() is "weak": it is loaded only when no good copy exists, it never
// takes the place of a good backup, and it is kept as <name>.bad instead.
typedef struct {
	size_t size;
	size_t head;
	bool (*valid)(const void *head);
	bool (*deep)(const void *data, size_t size);
	// the backup about to be replaced (a version two saves old): keep it
	// somewhere else (history.c renames it away). NULL, or if it is still
	// there afterwards: it is deleted.
	void (*retire)(const char *bak, void *ctx);
	void *ctx;
} safe_format_t;

// One name on the SD card. Only "no such file" (ENOENT) proves ABSENT; any
// other metadata, open, read or close error is UNREADABLE: the file may be
// there and good, so nothing may take its place or start without it.
typedef enum { SAFE_NAME_ABSENT, SAFE_NAME_PRESENT, SAFE_NAME_UNREADABLE } safe_name_t;
safe_name_t safe_name(const char *path, long *size);

// What safe_load found. Only SAFE_ABSENT may start an empty save; SAFE_DAMAGED
// may ask the player; SAFE_UNREADABLE and SAFE_NO_MEMORY mean nothing is known.
typedef enum {
	SAFE_LOADED,       // a copy was returned (from, weak)
	SAFE_ABSENT,       // every copy tried answered "no such file"
	SAFE_DAMAGED,      // copies were read and none is usable
	SAFE_UNREADABLE,   // a copy could not be read, and none read is good
	SAFE_NO_MEMORY,    // no memory to read it
} safe_outcome_t;

typedef struct {
	safe_outcome_t outcome;
	safe_source_t from;   // where the returned copy came from
	bool rejected;        // a copy was there but damaged or unreadable
	bool weak;            // the returned copy fails deep(): there is no better one
	bool failed;          // outcome is SAFE_UNREADABLE or SAFE_NO_MEMORY (older callers)
} safe_info_t;

// Write size = fmt->size bytes. keep_backup: a good current version stays as
// <name>.bak; a damaged one is kept as <name>.bad instead.
bool safe_write(const char *path, const safe_format_t *fmt, const void *data, bool keep_backup);

// Text files: fprintf into the returned <name>.tmp, then safe_finish() puts it
// in place (ok = false or a write error throws it away). No .bak is kept.
FILE *safe_begin(const char *path);
bool safe_finish(FILE *f, const char *path, bool ok);
// Before reading a text file: bring back <name> if a replace was interrupted.
void safe_recover(const char *path);

// The best good copy, in a new buffer of fmt->size bytes (NULL: none). Tries
// <name>, then <name>.bak, then <name>.tmp (the first weak copy if none is
// good), minus the sources in skip (bits
// 1 << SAFE_CURRENT etc., for copies the caller could not use). A copy that
// could not be read is passed over only for a good copy after it; a weak copy
// is never chosen over one that could not be read. Nothing on the SD card
// changes. info->outcome says why nothing came back.
void *safe_load(const char *path, const safe_format_t *fmt, unsigned skip, safe_info_t *info);
// After loading from <name>.bak or <name>.tmp (from): make <name> a good copy
// again. The source copy is never rewritten or removed first: a .tmp is
// renamed into place, a .bak is copied. A damaged <name> becomes <name>.bad;
// one the SD card cannot read whole stays where it is (false: not repaired).
bool safe_repair(const char *path, const safe_format_t *fmt, const void *data, safe_source_t from);
// A save is there: <name> or its backup, or a good <name>.tmp (a first save
// cut off before it was put in place), or the SD card could not say (only "no
// such file" is no save). *mtime: its time, 0 when unknown.
bool safe_stat(const char *path, const safe_format_t *fmt, time_t *mtime);
// Delete <name>, its backup and any leftover temp file (a .bad stays)
void safe_remove(const char *path);

// A damaged file is set aside: renamed to the first free <file>.bad, .bad2
// ... .bad99; an existing .bad is never removed or written over. true: done,
// or nothing was there. false: it stays (no free name, or the SD card could
// not say whether a name is taken).
bool safe_park(const char *path);
// The player chose a good copy (from the backups) for a damaged <name>: the
// damaged .tmp/.bak are set aside, the copy is written as <name>.tmp and read
// back, then the damaged <name> is set aside and the copy put in place. A cut
// at any step leaves the damaged <name> (the question is asked again) or the
// complete copy as <name>.tmp (the next load takes it). false: not in place.
// aside (may be NULL: safe_park) moves <name> away instead, once the copy is
// read back (the save service keeps a smaller save in the backups).
bool safe_restore(const char *path, const safe_format_t *fmt, const void *data,
	bool (*aside)(const char *path, void *ctx), void *ctx);
// The player chose to start empty: <name>.tmp, <name>.bak, then <name> are
// set aside (safe_park). false: something stayed in place.
bool safe_set_aside(const char *path);

#ifdef SAFEFILE_TEST
// host tests: stop a replace after this many steps, as if the power went off
extern int safe_test_crash_after;
#endif

/* Optional serialized writer scheduler; unset between background permits. */
void safe_set_checkpoint(bool (*fn)(void *),void *ctx);
bool safe_checkpoint(void);
/* Optional diagnostic observer, bound to the same thread as the checkpoint.
 * Never called on the UI/other writers while an importer stack is suspended. */
void safe_set_io_observer(void (*fn)(const char *,const char *,bool));
void safe_observe_io(const char *,const char *,bool);

#endif
