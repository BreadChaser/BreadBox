Run `bash tests/bb_disc/run.sh` from the repository root. This host-only test
links the production detector against a fake CHD API using the real libchdr
header, with ASan and UBSan.

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
