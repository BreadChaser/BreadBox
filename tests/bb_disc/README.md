Run `bash tests/bb_disc/run.sh` from the repository root. This host-only test
links the production detector against a fake CHD API using the real libchdr
header, with ASan and UBSan. `real_chd.py` then builds the detector with its
flags from `build/3ds/Makefile` against the real vendored libchdr built as the
3DS core builds it (`USE_LIBRETRO_VFS` from `core/Makefile.libretro`, platform
ctr), and reads tiny synthetic uncompressed CHD v5 discs (issue #1: a C `FILE`
handed to the core's file-stream libchdr refused every CHD).

Results: PS1 (positive evidence), OTHER (readable, positively not PS1: hidden)
and UNREAD (could not be opened or checked: kept in the library, logged, and
left to the core to judge when it starts).

Coverage includes cooked 2048 and raw 2352/2448 Mode 1/2 PVD positions, the
usual 19584-byte CD hunk, PVD windows spanning hunks, hunk-zero reuse, exact
PVD identity, Sony license text, Sega signatures, optional old/new GD-ROM
metadata, missing metadata API, open/read/header failures, size/capacity
limits and overflow-resistant geometry checks. Every successful open must
close, and the fake rejects more than three hunk reads per call.

The detector deliberately reads only hunk zero plus the hunks covering the
64-byte PVD window at sector 16. License fallback searches only the portion
of hunk zero before sector 16. Metadata rejection is enabled only when
chd_get_metadata is externally available; it is a weak reference and does
not add that symbol to the shared core's export list. No ROMs are needed.
