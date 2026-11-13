/* BreadBox shell side of the system interface: the registry of the app's
 * systems (generated from apps/<app>.mk SYSTEMS by tools/build/systems-table.py)
 * and the session control the menus, library and dev remote use. Systems
 * include bb_system.h only; nothing here is part of what a system implements. */
#ifndef BB_REGISTRY_H
#define BB_REGISTRY_H
#include "bb_system.h"

extern const bb_system_t *const bb_systems[];
extern const unsigned bb_system_count;
#define BB_MAX_SYSTEMS 16
/* Systems in menu order: release date newest first, ties by name. The one
 * place that orders system lists (shelf, Systems page, counts). Display only:
 * bb_systems[] indexes are persisted (cover ids, index records), never reorder
 * that array. with_games skips systems whose library is empty. */
unsigned bb_system_display_order(const bb_system_t **out,unsigned cap,bool with_games);
const bb_system_t *bb_system_for_path(const char *path);
const bb_system_t *bb_system_probe_path(const char *path, bb_entry *entry);
const bb_system_t *bb_system_folder(const char *name);
bool bb_system_extension(const bb_system_t *s,const char *path);
bool bb_session_active(void);
extern bool bb_session_stereo;          /* set through bb_session_set_stereo(), read by video.c */
bool bb_system_switch(const char *arg);
const char *bb_system_autostart(void);
extern int bb_remote_slot, bb_remote_stereo; /* dev remote load options; systems get them in bb_launch */
bool bb_system_init(void);
void bb_system_deinit(void);
bool bb_session_resume_requested(void);
void bb_system_resume(const char *path, bool enabled);
const char *bb_session_title(void);
int bb_system_total(void);
int bb_system_library_count(const bb_system_t *s);
void bb_system_counts(char *out, size_t cap);

#endif
