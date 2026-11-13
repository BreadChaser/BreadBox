/* Example third system, as it would live in systems/snes/sys_snes.c.
 * Only this file, its core blob and romfs/presets/snes are new; the app
 * makefile adds "snes" to SYSTEMS. No shared file changes. */
#include <string.h>
#include "bb_system.h"

static const char *const ext[] = { "sfc", "smc", "zip", NULL };
static const char *const hints[] = { "snes", "super nintendo", "super famicom", "sfc", NULL };
static const char *const scale[] = { "Original", "Fit", "Stretch", NULL };
static const bb_setting_desc settings[] = {
	{ "screen", "Screen size", "How the picture fills the top screen.", scale,
	  BB_SET_CHOICE, BB_CAT_DISPLAY, BB_MODEL_ALL, BB_SET_LIVE, 3, { 0, 0 } },
	{ "smooth", "Smoothing", "Soften the picture when scaled.", NULL,
	  BB_SET_TOGGLE, BB_CAT_DISPLAY, BB_MODEL_ALL, BB_SET_LIVE, 0, { 1, 1 } },
};

static bb_probe_result probe(const bb_probe_input *in, bb_entry *out)
{
	/* real module: LoROM/HiROM header score at 0x7FC0/0xFFC0 (+0x200 copier) */
	if(in->head_len <= 0x7fc0 || in->head[0x7fc0]!=0x5a)return BB_PROBE_NO;
	if (in->size < 0x8000 || in->size > (12u << 20)) return BB_PROBE_NO;
	strncpy(out->title, in->name, sizeof(out->title) - 1);
	return in->folder_match ? BB_PROBE_YES : BB_PROBE_MAYBE;
}
static bool identify(const char *path, const bb_entry *e, bb_content_id *id, char *err, size_t cap)
{
	(void)path; (void)err; (void)cap;
	strncpy(id->key, e->key[0] ? e->key : "SNES-UNKNOWN", sizeof(id->key) - 1);
	return true;
}
static bb_run_result run(const bb_launch *l, char *err, size_t cap) { (void)l; (void)err; (void)cap; return BB_RUN_QUIT; }

const bb_system_t bb_system_snes = {
	.abi = BB_SYSTEM_ABI, .id = "snes", .name = "Super Nintendo", .short_name = "SNES",
	.release_year = 1990, .release_month = 11,
	.models = BB_MODEL_ALL, .link = BB_LINK_STATIC, .stereo = BB_STEREO_LAYERS,
	.extensions = ext, .folder_hints = hints, .probe_bytes = 0x10200,
	.probe = probe, .identify = identify,
	.cover_db = "Nintendo - Super Nintendo Entertainment System",
	.settings = settings, .nsettings = sizeof(settings) / sizeof(settings[0]),
	.preset_dir = "romfs:/presets/snes",
	.budget = { [BB_OLD] = { 6144, 512, 2048, 0 }, [BB_NEW] = { 16384, 1024, 4096, 0 } },
	.run = run,
};
