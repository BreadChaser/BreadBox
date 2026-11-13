QuickNES core: https://github.com/libretro/QuickNES_Core
Pinned upstream commit: 26bb785c9deddb66a17717b21bb4e328f03ade32 (2026-10-28 download).

Upstream root LICENSE is GPL version 2. Individual emulator sources retain
Shay Green's LGPL-2.1-or-later notices. The repository supplies GPL version 2 at its root; no broader
licence grant is assumed for files without an individual notice.
All upstream notices and the root LICENSE are retained. Treat the combined
adapter/core under GPL version 2, with LGPL component obligations preserved.

Changes: replace build Makefile with a reviewed out-of-tree CTR adapter (original
retained as Makefile.upstream). Omit .git, .github and jni build metadata, and nes_emu/tools/proto
(an unused prebuilt x86-64 Linux executable without matching source).
All remaining upstream source files are unmodified.
No upstream scripts executed during import. Makefile.common and compile/archive
rules reviewed before building. No core implementation edits.

The LGPL-2.1 licence text is also supplied as COPYING.LESSER (the standard
verbatim text from the local libxdp licence package). This adds no core code.
