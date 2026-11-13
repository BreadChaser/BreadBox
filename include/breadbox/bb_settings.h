#pragma once
#include <stdbool.h>
#include <stddef.h>
/* Menu-only model. Strings are borrowed until the next read; IDs are stable
 * within an adapter, never row positions persisted on disk. Adapter IDs must be
 * in [0, 9999]; the shell reserves larger IDs. No core is launched. */
typedef enum { BB_SOURCE_NONE, BB_SOURCE_SYSTEM, BB_SOURCE_RECOMMENDED,
 BB_SOURCE_CHANGED, BB_SOURCE_ALL } bb_setting_source;
typedef enum { BB_ROW_VALUE, BB_ROW_ACTION, BB_ROW_HEADING } bb_settings_kind;
typedef enum { BB_RESET_SCOPE, BB_RESET_CONTROLS, BB_RESET_RECOMMENDED } bb_settings_reset;
typedef struct {
 const char *path,*key,*title;
 bool game, running;
} bb_settings_context;
typedef struct {
 int id;
 const char *section,*label,*value,*help,*notice;
 bb_settings_kind kind;
 bb_setting_source source;
 bool system_changed,can_clear;
 const char *short_help; /* optional complete sentence for the two-line hang tag */
 bool advanced; /* the system's Advanced page (speed, compatibility, troubleshooting), not its main list */
} bb_settings_row;
typedef struct bb_settings_ops {
 bool (*open)(const bb_settings_context *);
 unsigned (*count)(void);
 bool (*read)(unsigned,bb_settings_row *);
 void (*change)(int id,int direction);
 void (*clear)(int id);
 void (*reset)(bb_settings_reset);
 void (*close)(void);
 /* Optional: an action requests that the caller resume its paused game. */
 bool (*resume_requested)(void);
} bb_settings_ops;
struct bb_system;
/* Caller retains context strings and backdrop userdata for this blocking menu. */
void bb_settings_run(const struct bb_system *,const bb_settings_context *,void (*top)(void *),void *ud);
void bb_settings_game(const struct bb_system *,const char *path,const char *key,const char *title,void (*top)(void *),void *ud);
const char *bb_settings_source_name(bb_setting_source);
/* App hotkey edits leave legacy game exceptions intact until their migration. */
int bb_settings_hotkey_get(int slot);
const char *bb_settings_hotkey_exception(int slot);
void bb_settings_hotkey_cycle(int slot,int direction);
void bb_settings_hotkey_default(int slot);
#include "ui_destination.h"
void bb_settings_room_run(const ui_destination *scene);

/* Borrow the current Settings scene for a child card; false outside Settings. */
bool bb_settings_child_top(void);
bool bb_settings_child_tag_visible(void);
