/* PS1 and GBA stand-ins with the fields the real adapters will fill. */
#include "bb_registry.h"
static bb_run_result run(const bb_launch *l, char *e, size_t c) { (void)l; (void)e; (void)c; return BB_RUN_QUIT; }
static const char *const ps1_ext[] = { "cue", "chd", "pbp", "m3u", "iso", NULL };
static const char *const gba_ext[] = { "gba", "zip", NULL };
const bb_system_t bb_system_ps1 = { .abi = BB_SYSTEM_ABI, .id = "ps1", .name = "PlayStation",
	.short_name = "PS1", .release_year = 1994, .release_month = 12, .models = BB_MODEL_ALL, .extensions = ps1_ext, .run = run };
const bb_system_t bb_system_gba = { .abi = BB_SYSTEM_ABI, .id = "gba", .name = "Game Boy Advance",
	.short_name = "GBA", .release_year = 2001, .release_month = 3, .models = BB_MODEL_ALL, .extensions = gba_ext, .run = run };
