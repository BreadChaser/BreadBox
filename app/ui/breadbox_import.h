/* Copy-only PSX3D migration. No source path is ever opened for writing. */
#ifndef BREADBOX_IMPORT_H
#define BREADBOX_IMPORT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
/* kinds: what was copied (PSX_IMPORT_* bits), for the welcome's "Brought from PSX3D" card */
enum { PSX_IMPORT_GAMES = 1, PSX_IMPORT_SAVES = 2, PSX_IMPORT_POSTCARDS = 4, PSX_IMPORT_SETTINGS = 8, PSX_IMPORT_CHEATS = 16, PSX_IMPORT_BIOS = 32 };
typedef struct { unsigned copied, unchanged, conflicts, rejected, failed, weak; bool interrupted, no_space; unsigned slots; /* numbered postcards copied */ unsigned kinds; } psx_import_result;
/* main.c: PSX3D's app settings were copied on this first start (config_import_psx3d) */
extern bool config_psx3d_imported;
/* Call only in the idle frontend, after settling the save writer. root paths
 * must be distinct, disjoint directories. progress may return false to pause. */
typedef bool (*psx_import_progress)(const char *file, void *ctx);
bool psx_import_recover(const char *dest);
bool psx_import_available(const char *source);
/* Metadata-only estimate, before validation/deduplication or conflict backups.
 * Never reads save bodies or creates destination files. */
typedef struct { unsigned files; uint64_t bytes; bool incomplete; } psx_import_estimate;
psx_import_estimate psx_import_size(const char *source);
psx_import_estimate psx_import_size_checked(const char *source, bool (*keep_going)(void *), void *ctx);
static inline void psx_import_format_size(char *out,size_t cap,uint64_t bytes)
{
 uint64_t unit=bytes<(1ULL<<20)?1ULL<<10:bytes<(1ULL<<30)?1ULL<<20:1ULL<<30;
 /* Round the remainder separately, avoiding overflow for large sources. */
 uint64_t tenths=(bytes%unit*10+unit/2)/unit;
 snprintf(out,cap,"%llu.%llu %s",(unsigned long long)(bytes/unit+tenths/10),
  (unsigned long long)(tenths%10),unit==(1ULL<<10)?"KiB":unit==(1ULL<<20)?"MiB":"GiB");
}

psx_import_result psx_import_run(const char *source,const char *dest,psx_import_progress,void *);
bool psx_import_continue_ready(const char *,const char *,const char *,unsigned *saved);
/* Worker-owned cursor: one filesystem operation or 64 KiB CRC per permit. A private suspended stack preserves
 * transaction locals; it runs only during LOW callbacks. Receipts survive
 * cancellation; finish persists the successful manifest. */
typedef struct psx_import_cursor psx_import_cursor;
typedef struct {uint64_t read_bytes,written_bytes,read_us,read_calls,max_read_us,dir_us,open_us,stat_us,close_us,seek_us,write_us,write_calls,max_write_us,flush_us,mutate_us,crc_us,crc_bytes,dir_calls,open_calls,stat_calls,close_calls,seek_calls,flush_calls,mutate_calls,metadata_calls;char last_op[64],last_path[1024];} psx_import_io;
psx_import_io psx_import_io_counts(const psx_import_cursor *);
psx_import_cursor *psx_import_open(const char *,const char *,bool force);
bool psx_import_step(psx_import_cursor *,psx_import_result *);
bool psx_import_finish(psx_import_cursor *);
/* Run final metadata transactions on the same yielding stack. fn/ctx must
 * remain stable until done. Checkpoints are cleared between worker turns. */
bool psx_import_task_step(psx_import_cursor *,bool (*fn)(void *),void *,bool *ok);
bool psx_import_changed(const psx_import_cursor *);
bool psx_import_cancel_step(psx_import_cursor *);
void psx_import_close(psx_import_cursor *);
typedef struct {char title[1024],save[1024],state[1024],rom[1024];unsigned saved;} psx_import_continue;
void psx_import_cursor_continue(const psx_import_cursor *,psx_import_continue *);
void psx_import_read_continue(const char *,const char *,psx_import_continue *);
void breadbox_import_init(void);
bool breadbox_import_busy(void);
void breadbox_import_start(bool first_boot);
void breadbox_import_shutdown(void);
void breadbox_import_poll(void);
bool breadbox_import_pause(void);
void breadbox_import_resume(void);
void breadbox_import_notice(void);
/* The import has run as far as it will this start (or cannot run); *kinds: what it copied
 * (PSX_IMPORT_* bits, the app settings file included). */
bool breadbox_import_settled(unsigned *kinds);
/* The welcome's PSX3D card is up: the "Brought your PSX3D ... over" notice is not shown beside it. */
void breadbox_import_notice_quiet(void);
#ifdef PSX_IMPORT_TEST
bool psx_import_test_identity_path(const char *);
extern int psx_import_stop_after; /* crash between durable publication steps */
#endif
#endif
