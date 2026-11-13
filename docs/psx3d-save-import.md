# Bringing PSX3D saves into BreadBox

## For players

On its first start BreadBox copies everything worth keeping from `/3ds/PSX3D`:
memory cards, postcards (save states and Continue) and their pictures, per-game
settings, cheats (the enabled list and your own codes), BIOS files and play
history. When BreadBox has no settings file yet, PSX3D's app settings
(`config.ini`) come over too. A notice says how many files were copied (the
settings file included). It is a
copy, one way: PSX3D's folder is never changed, and play in BreadBox never
reaches PSX3D. PSX3D numbered postcards 0-9; BreadBox shows the same postcard
one higher (PSX3D slot 0 is BreadBox slot 1). When a file exists in both apps,
BreadBox keeps its own and archives PSX3D's copy (below). **Settings > Files >
Import again from PSX3D** copies anything new.

## How it works

BreadBox automatically copies PlayStation saves from `/3ds/PSX3D` on its LOW
worker and shows a completion notice. **Files > Import again from PSX3D** forces
a complete rescan. Import never writes into the PSX3D tree.

## What is copied

- Memory cards: `saves/*.srm`, `saves/*.mcd` and `saves/*.mcr`, including shared cards.
- State slots and Continue: `states/*.st0` through `.st9`, and `.sta`.
- Their matching `.thm` pictures.
- `games/*.ini` per-game preferences and `saves/*.id` ownership records.
- Play history from `saves/playlog.ini`, under BreadBox's `ps1:` namespace.
- Cheats: `cheats/*.on` (enabled list), `cheats/*.txt` and `cheats/*.cht` (the player's codes), byte for byte.
- BIOS files: every file directly in `bios/` (with or without an extension) up to 8 MiB,
  byte for byte; subfolders are not copied.
- App settings: on BreadBox's very first start only (no `config.ini` of its own), PSX3D's
  `config.ini` is read with the same rules as BreadBox's own (spaces, tabs, CRLF, `#`/`;`
  comments) and BreadBox writes its own file from the settings it understands
  (`config_import_psx3d`, before `config_load`). The four Continue keys, unknown keys, core
  options with unusual values and overlong lines stay behind; a file over 64 KiB is not read.

Save names, disc serials and region suffixes are preserved exactly. There is no
fuzzy game-title matching. The library root already has a separate legacy-root
migration; switches and credentials are never imported, and "Import again" does
not copy app settings again.

The importer reads a valid current file, otherwise its valid `.bak`, otherwise a
complete valid `.tmp`. It never repairs, renames, removes or writes anything in
the PSX3D tree. A backup state's picture must come from that backup as well.

## Conflicts and recovery

Conflicting imported versions are copied to a permanent, verified archive below
`/3ds/BreadBox/import/psx3d/source/`. Any existing BreadBox version that would
conflict is preserved below `import/psx3d/breadbox/`, even if damaged. These
archives are outside the normal backup retention budget. Before adding a conflict
to the ring, the importer also archives every existing BreadBox history file: normal
ring trimming or the global budget may retire a backup from a different game. They are deliberately
not automatically pruned: manual space management must retain any version the
player still needs.

For two valid states with known save times, the newer state's trailer
wins. The other valid state is copied into the game's normal ten-entry backup
ring, with its picture when available. A conflicting card is kept as a pinned
card backup. A verified blank BreadBox card yields to a strong PSX3D card with saves, after
archiving and pinning the blank card. Other cards, preferences, unknown-time states, ties and differing game
ownership records keep the existing BreadBox active file. Differing ownership
records also block publishing into empty slots for that name; these versions
are archived without offering another game's state in the ring.

The SD filesystem does not provide useful file modification times on the 3DS.
The importer therefore never guesses that an old card or preference file is
newer from `stat()`. It preserves both versions instead. Damaged source states
and unusable cards are skipped and counted in the result. Header-valid cards
with weak directory checksums are copied with a visible warning only after
checking for a stronger backup or temp copy.

A fresh file streams into a private temporary file with a running CRC-32, then
its closed destination is reread once and verified by full size and CRC before
publication. Existing archive collisions also require complete byte equality.
States also pass the actual PS1 loader's structural checks
and sealed-trailer CRC; cards require the exact size and header, with directory
checksums distinguishing strong copies from runtime-loadable weak copies. Pictures and
text have bounded format checks. Files over 8 MiB and names of 240 bytes or more
are rejected conservatively.

The staged replacement and parked original survive interruption between FAT
renames. A durable pending record preserves the pre-import history timestamp;
a subsequent startup restores parked BreadBox primary files before showing a
menu, then resumes the automatic import. Per-version completion receipts are
written last,
so retries finish partial work without importing completed versions again.
Pictures wait for their state's completed receipt. Failed-copy temps are removed;
successful replacements clear their parked original after verification, so a
subsequent player deletion stays deleted. Failed imports resume on a later
launch; manual import remains available.
An interrupted/short write leaves the original active file or its parked,
verified copy available. No transfer removes its source.

Play history merges timestamps and play seconds by maximum, never by adding a
second copy of the same history. Continue adopts the legacy config's exact game
only when its imported auto-state is the active verified copy, the ROM path is
available, and its timestamp beats pre-import BreadBox history and is at least
as recent as any subsequently recorded play. Import does not change Continue
to an older game or replace another system's history.

## Returning launches

CRC-protected per-file receipts record the source path, size and cheap identity.
A disposable combined index avoids opening every receipt separately. Missing or
corrupt indexes fall back to individual receipts. Changes invalidate the index
before modifying destination files, so interruption cannot conceal an unfinished
transaction.

Valid PSX3D trailer identity fields are used when present. Other large files use
six 1 KiB samples: first, last, size/5, 2size/5, 3size/5 and 4size/5. Every 128 KiB
memory card is fully hashed; small files and preferences are also fully hashed.
Matching receipts avoid full state fingerprints and all destination writes.
An equal-size state edit outside the samples, or one retaining the same trailer
identity, can remain undetected until **Import again** forces a full rescan.

The worker runs on LOW, requeues behind visible work, and checks an 8 ms deadline
between operations with 64 KiB transfer slices and at most 128 KiB per callback.
Individual blocking filesystem calls can exceed that deadline; elapsed-time
measurements determine acceptance, rather than the byte bound alone.

## Validation

`tests/psx3d_import/run.sh` exercises the production importer, PS1 state validator,
and backup ring with a fixture SD tree: fresh/partial imports, conflicts in both
time directions, corrupt states, backup recovery, interrupted publication,
short writes, repeat imports, source-tree integrity, aliases and identity conflicts.
Review regressions cover failed-state/picture retries, deleted primaries, actual
numbered-slot labels, weak and blank cards, full-SD temp cleanup, and Later after failure.
Opaque archive verification streams through a bounded buffer instead of allocating
another whole state; permanent archives and history snapshots remain necessary
for retention safety and consume extra SD space.
`tests/gba_saves/playlog.c` covers namespaced, idempotent history merging and write
failure. Console APT/FAT behavior remains a hardware acceptance check.

`tests/psx3d_import/perf.c` checks first-import reads <=2.2 times source bytes
and returning reads <= all card bytes + 1% of other source bytes, including
corrupt-index fallback, spread edits, full card hashes and forced rescans.
