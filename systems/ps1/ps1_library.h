// The PS1 library's private parts, shared by library.c (the list, cache and
// index import), ps1_disc_groups.c and ps1_library_import.c.
#ifndef BREADBOX_PS1_LIBRARY_H
#define BREADBOX_PS1_LIBRARY_H

#include <stdbool.h>
#include <stddef.h>
#include "app_paths.h"
#include "library.h"

#define MAX_GAMES 2048
#define MAX_DISCS 8
#define MAX_DEPTH 3
#define MAX_ENTRIES 4096    // disc files and folders read per folder
#define MAX_CANDS 8192      // disc files held between the two scan passes
#define MAX_REFS 8192       // files named by .cue and .m3u files
#define REF_BYTES (16 * 1024)   // read of one .cue or .m3u
#define MAX_PARTS 128       // folders in a path being compared

// what the last scan left out, by reason
typedef struct {
	int files;     // folder or library over its limit, or a path too long
	int depth;     // folders below MAX_DEPTH, not opened
	int folders;   // folders that could not be opened
	int games;     // over MAX_GAMES
	int discs;     // over MAX_DISCS for one game
	int nomem;     // out of memory
	int refs;      // .cue/.m3u names not read: list too long (extra files may show)
	int notps1;    // disc images of other systems (logged; not counted as omitted)
} ps1_library_omissions;

typedef struct { char *path; u32 dir_id;
 bool probed, valid, added;
} cand_t;

// library.c
extern game_t *ps1_library_games;
extern int ps1_library_ngames;
extern ps1_library_omissions ps1_library_omit;
void ps1_library_game_free(game_t *g);

// ps1_disc_groups.c
void ps1_library_add_disc(u32 dir_id, const char *path);
void ps1_library_fix_article(game_t *g);
void ps1_library_disambiguate(void);
int ps1_library_cmp_games(const void *a, const void *b);
void ps1_library_sort_discs(game_t *g);

// ps1_library_import.c
extern cand_t *ps1_library_cands;
extern int ps1_library_ncands;
extern char **ps1_library_refs;   // sorted
extern int ps1_library_nrefs;
extern bool ps1_library_refs_prepared;
extern const void *ps1_library_refs_data;
extern size_t ps1_library_refs_size;
const char *ps1_library_ext_of(const char *name);
char *ps1_library_trim(char *s);
void ps1_library_norm_path(const char *dir, const char *ref, char *out, size_t len);
int ps1_library_cmp_ref(const void *a, const void *b);
void ps1_library_scan_free(void);
void ps1_library_note_file(u32 dir_id, const char *dir, const char *name);
bool ps1_library_image_at(const char *path, int depth);

#endif
