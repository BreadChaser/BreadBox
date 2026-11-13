GBA box art uses the same cover worker, pause/drain transaction, HTTPS driver,
Wi-Fi backoff, 14-day miss state and RGB565 256x256 `.cv2` textures as PS1.
A single shared worker handles both systems. Namespaced GBA cache files are in
`3ds/BreadBox/covers/gba/`; small thumbnails share `covers/thumbs.pk`.

The UI queues the selection first, then up to eight neighbors each side,
including covers for list rows, before the carousel draws. It does no network,
ROM, PNG decode or filesystem work. Sorting and incremental GBA publication
use `covers_library_begin/end`, which waits for every worker borrow before
changing indexes or freeing strings. The 1044 PS1 library boundary remains.

Matching tries the ROM filename without its extension, replacing libretro's
reserved characters (`&*/:` plus backtick, angle brackets, question mark,
backslash, pipe and quote) with underscores. ZIPs try the single inner `.gba`
name first (central directory only), then the outer filename. Cartridge header
code (0xAC, already present in the shared index) supplies No-Intro fallback
names. Header titles are not treated as unique identifiers. The shared driver
uses 15-second connect / 30-second overall timeouts and retries transient
failures; only confirmed 404 misses enter the 14-day cache.

A PNG next to the ROM wins, followed by the ROM directory's `covers/<stem>.png`,
then `3ds/BreadBox/covers/gba/<sanitized stem>.png`. Those overrides are checked
before cached downloads, including when the network is disabled. Removing or
changing an override refreshes its converted texture on the next library open.

The cartridge-code table `assets/romfs/covers/gba-codes.txt` is adapted from
[libretro-database's No-Intro GBA DAT](https://github.com/libretro/libretro-database/blob/master/metadat/no-intro/Nintendo%20-%20Game%20Boy%20Advance.dat),
version 2026.10.07, retrieved 2026-10-27. Credit: No-Intro and libretro-database
contributors. The database and this adapted table are licensed under
[Creative Commons Attribution-ShareAlike 4.0](https://creativecommons.org/licenses/by-sa/4.0/).
The adaptation retains four-byte serials and names, removes other fields,
deduplicates and sorts rows. Regenerate with:

```
python3 tools/boxart/gba-table.py 'Nintendo - Game Boy Advance.dat' assets/romfs/covers/gba-codes.txt
```

`bash tests/gba_covers/run.sh` tests filename punctuation, Classic NES names,
ZIP inner names, and cartridge-code fallback. `tests/gba_covers/audit.py` checks
the real C matcher against a previously host-downloaded thumbnail index; it
never contacts a console. The cover rebuild suite runs the same threaded
publication regression with both PS1 and GBA IDs.
