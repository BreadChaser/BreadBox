#!/usr/bin/env python3
"""new-system.py ID "Name" "Short" [--core DIR] [--ext gb,gbc,zip] [--hint "game boy"]

Start a system folder: systems/<id>/sys_<id>.c (the descriptor, with every hook a
new system must fill marked TODO) and, with --core, systems/<id>/core.mk for a
vendored libretro core. Nothing outside systems/<id>/ changes: add the id to
SYSTEMS in apps/breadbox.mk yourself. Steps after this: docs/dev/new-system.md."""
import argparse, json, pathlib, re, sys

root = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
p.add_argument('id'); p.add_argument('name'); p.add_argument('short')
p.add_argument('--core', help='vendored libretro core folder, relative to the repository root')
p.add_argument('--ext', default='zip', help='comma-separated file extensions, lower case, without dots')
p.add_argument('--hint', action='append', default=[], help='folder name that marks this system (repeatable)')
p.add_argument('--root', default=str(root), help=argparse.SUPPRESS)
a = p.parse_args()
if not re.fullmatch(r'[a-z][a-z0-9]{1,7}', a.id):
    sys.exit('id must be 2-8 lower-case letters/digits, starting with a letter')
# Reject make syntax rather than trying to quote it in generated core.mk.
if a.core and (not re.fullmatch(r'[A-Za-z0-9_./-]+', a.core) or pathlib.PurePosixPath(a.core).is_absolute() or '..' in pathlib.PurePosixPath(a.core).parts):
    p.error('--core must be a repository-relative path without make metacharacters')
for text in (a.name, a.short, *a.hint):
    if any(ord(c) < 32 or ord(c) > 126 for c in text) or '*/' in text:
        p.error('names and hints must be printable ASCII without */')
extensions = [e.strip().lstrip('.').lower() for e in a.ext.split(',') if e.strip()]
if not extensions or any(not re.fullmatch(r'[a-z0-9]+', e) for e in extensions):
    p.error('--ext must contain comma-separated alphanumeric extensions')
# Escape C strings after validation; comments use the original printable text.
a.name_c = json.dumps(a.name)[1:-1]
a.short_c = json.dumps(a.short)[1:-1]
folder = pathlib.Path(a.root) / 'systems' / a.id
if folder.exists():
    sys.exit('%s already exists' % folder)
folder.mkdir(parents=True)
ext = ', '.join('".%s"' % e.strip().lstrip('.').lower() for e in a.ext.split(',') if e.strip())
hints = ', '.join(json.dumps(h.lower()) for h in [a.id, a.name.lower(), *a.hint])
source = '''/* %(name)s for BreadBox: the system descriptor (include/breadbox/bb_system.h).
 * Everything this system owns lives in systems/%(id)s/; the shell reaches it only
 * through bb_system_%(id)s. Checklist: docs/dev/new-system.md. */
#include <stdio.h>
#include <string.h>
#include "bb_system.h"

static const char *const extensions[] = { %(ext)s, NULL };
static const char *const hints[] = { %(hints)s, NULL };

/* Runs on the index worker thread: decide from the file name, size and the
 * first probe_bytes bytes whether a file is a %(short)s game. No UI, no GPU. */
static bb_probe_result probe(const bb_probe_input *in, bb_entry *out)
{
	/* TODO: check the header (in->head, in->head_len); fill out->key with the
	 * game's product code when the header has one. */
	snprintf(out->title, sizeof(out->title), "%%s", in->name);
	return in->folder_match ? BB_PROBE_YES : BB_PROBE_MAYBE;
}

/* At launch: confirm the game and fill its save key (saves/%(id)s/<key>.*). */
static bool identify(const char *path, const bb_entry *e, bb_content_id *id, char *error, size_t cap)
{
	(void)path; (void)error; (void)cap;
	snprintf(id->key, sizeof(id->key), "%%s", e && e->key[0] ? e->key : "%(upper)s-UNKNOWN");
	return true;
}

/* Owns the game session: load the core, run frames until the player quits,
 * write saves, return. TODO: until the shared libretro host exists, copy the
 * session from systems/gba/gba.c (see docs/dev/new-system.md). */
static bb_run_result run(const bb_launch *l, char *error, size_t cap)
{
	(void)l;
	snprintf(error, cap, "%(name)s is not ready yet");
	return BB_RUN_FAILED;
}

const bb_system_t bb_system_%(id)s = {
	.abi = BB_SYSTEM_ABI, .id = "%(id)s", .name = "%(name)s", .short_name = "%(short)s",
	.release_year = 0, .release_month = 0, /* TODO: first release worldwide; menus list systems newest first */
	.models = BB_MODEL_ALL, .link = BB_LINK_STATIC, .stereo = BB_STEREO_NONE,
	.extensions = extensions, .folder_hints = hints,
	.probe_bytes = 0, /* TODO: header bytes probe() needs */
	.probe = probe, .identify = identify, .run = run,
	.cover_db = "%(name)s", /* TODO: the libretro-thumbnails folder name */
	/* TODO: measured worst-game memory per console (heap, linear, exec, keep=0) */
	.budget = { [BB_OLD] = { 0, 0, 0, 0 }, [BB_NEW] = { 0, 0, 0, 0 } },
};
''' % dict(id=a.id, name=a.name_c, short=a.short_c, upper=a.id.upper(), ext=ext, hints=hints)
if a.core:
    source = source.replace('#include \"bb_system.h\"', '#include \"app_policy.h\"\n#include \"retro_ctr.h\"\nextern const bb_retro_core bb_retro_'+a.id+';\nextern const bb_system_t bb_system_'+a.id+';\nextern unsigned char __bb_'+a.id+'_bss_start[], __bb_'+a.id+'_bss_end[];')
    start = source.index('/* Owns the game session:')
    end = source.index('const bb_system_t', start)
    source = source[:start] + '''/* TODO: set the core's maximum geometry and joypad map. */
static bb_run_result run(const bb_launch *l, char *error, size_t cap)
{
    const bb_retro_desc d = { .system=&bb_system_%(id)s, .core=&bb_retro_%(id)s,
        .system_dir=APP_SD_ROOT \"/bios\", .max_width=256, .max_height=256,
        .screen_size=1, .keys={ [RETRO_DEVICE_ID_JOYPAD_A]=KEY_A,
            [RETRO_DEVICE_ID_JOYPAD_B]=KEY_B, [RETRO_DEVICE_ID_JOYPAD_START]=KEY_START,
            [RETRO_DEVICE_ID_JOYPAD_SELECT]=KEY_SELECT, [RETRO_DEVICE_ID_JOYPAD_UP]=KEY_UP,
            [RETRO_DEVICE_ID_JOYPAD_DOWN]=KEY_DOWN, [RETRO_DEVICE_ID_JOYPAD_LEFT]=KEY_LEFT,
            [RETRO_DEVICE_ID_JOYPAD_RIGHT]=KEY_RIGHT } };
    return bb_retro_ctr_run(l, &d, error, cap);
}

''' % dict(id=a.id) + source[end:]
    source = source.replace('.cover_db =', '.save = &bb_retro_ctr_save_ops,\n\t.cover_db =')
    source = source.replace('.probe = probe, .identify = identify, .run = run,',
        '.probe = probe, .identify = identify, .run = run,\n    .arena_start=__bb_'+a.id+'_bss_start, .arena_end=__bb_'+a.id+'_bss_end,')
(folder / ('sys_%s.c' % a.id)).write_text(source)
if a.core:
    (folder / 'core.mk').write_text('''# How BreadBox builds and links the %(name)s core (tools/build/core-blob.py reads
# this file; build/3ds/Makefile includes it). Every key starts with the system id.
%(id)s_CORE_DIR := %(core)s
%(id)s_CORE_MAKE := platform=ctr
%(id)s_CORE_ARCHIVE := TODO_libretro_ctr.a
%(id)s_CORE_PREFIX := %(id)s_core_
# optional: translation cache bytes taken from the shared executable arena
# %(id)s_CORE_JIT_HEADER := config.h
# %(id)s_CORE_JIT_DEFINES :=
# %(id)s_CORE_JIT_EXPR := CACHE_SIZE
%(id)s_CORE_INCLUDES := %(core)s/libretro-common/include
%(id)s_CORE_LIBS :=
%(id)s_CORE_LICENSE := TODO
''' % dict(id=a.id, name=a.name, core=a.core))
print('created %s; next: add %s to SYSTEMS in apps/breadbox.mk and follow docs/dev/new-system.md' % (folder, a.id))
