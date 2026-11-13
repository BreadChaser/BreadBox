# Building and testing

The repository builds BreadBox from one shared frontend, the vendored GPU-only
PCSX-ReARMed core and the other system cores. No submodules or patch regeneration
are required. PSX3D is retired (frozen at v0.5.2, `9cb4f7ad` in the PSX3D repo):
there is no `APP` switch, and `APP=psx3d` is rejected. Reproduce v0.5.2 from its
own frozen revision, not from this tree.

## Toolchain

Use the devkitARM image and packages pinned in `tools/build/toolchain.lock`.
The build checks the installed SDK and citro3d private ABI before compiling.
Set `DEVKITPRO=/opt/devkitpro` and `DEVKITARM=/opt/devkitpro/devkitARM`, and add
`/opt/devkitpro/tools/bin` to PATH. `SDK_CHECK=0` is only for deliberate SDK
upgrades; changing the compiler or SDK requires layout and hardware checks.

The container helper uses Docker or bubblewrap and provisions pinned bannertool
and makerom versions:

```sh
tools/build/container.sh bash tools/build/ci.sh all
```

## Build targets

```sh
make -j4                # BreadBox 3dsx and ELF
make -j4 cia            # installable CIA (alias: make breadbox)
make cxi BANNERTOOL=/path/to/bannertool MAKEROM=/path/to/makerom
make BB_EXPERIMENTAL_NES=1   # adds the experimental QuickNES system
```

Outputs are `build/3ds/BreadBox.{3dsx,elf,cia}`; `make cxi` also builds a
deterministic executable container. CIA tickets are randomized. `make clean`
cleans the frontend and the shared core. Frontend objects are isolated in
`build/3ds/build-breadbox`. The shared application lives in `app/`, `platform/3ds/`, `include/breadbox/`, `assets/`
and `build/3ds/`; `psx3d/vendor` keeps its name until the vendor step.

`apps/breadbox.mk` selects packaging, object directories, the system list and the
version header. `app/ui/app_policy.h` supplies compile-time names, SD
roots, title IDs, release URLs and the PS1 preset feed. The PS1 preset pack is
still published on the frozen PSX3D release page. On first start, BreadBox
imports compatible settings, memory cards, and save states from
`sdmc:/3ds/PSX3D`; it leaves the original files in place.

BreadBox packages its PNG artwork directly from `apps/breadbox/`; the `cia` rule
writes the hand-made 24 px icon into the SMDH with `tools/art/smdh24.py`.
See [CREDITS](../CREDITS) and `tools/art/CREDITS.md` for artwork provenance.
Legacy placeholder SVGs have been removed; do not regenerate the release PNGs
from old sketches.

## Title versions and packaging

BreadBox has its own version sequence in `apps/breadbox/version.h`
(`BB_VERSION`, `BB_BUILD`; a build script may still pass the old
`-DPSX3D_BUILD=...`, which is accepted). The counter is
`(major * 16 + minor) * 1024 + patch + 3`. `VERSION=N` overrides the counter for
private test packages; it must fit uint16. Choose test versions at session time
above the installed version and below the next planned release. Never downgrade
or reuse a stale test counter for a live session.

Use makerom with `tools/build/makerom-version-fix.patch`, as the container does.
Stock makerom can drop version bits. Packaging parses the resulting TMD and
checks the requested version and the BreadBox title ID. Always run makerom from
the repository root: RSF RomFS paths are relative to that directory.

## Tests and layout

```sh
make host-tests
bash tools/layout/check.sh
bash tools/layout/baseline-check.sh     # core + pinned frame functions against the accepted BreadBox build (tools/layout/baseline.json)
python3 tools/layout/compare.py /path/to/baseline.elf build/3ds/BreadBox.elf
python3 tools/layout/check-pinned.py /path/to/baseline.elf build/3ds/BreadBox.elf build/3ds/build-breadbox
python3 tools/architecture/ownership-guard.py --strict   # shared code owns no system policy
```

### Accepted-build baseline

`tools/layout/baseline.json` names the BreadBox build every change is measured
against: CIA 2650 (`ffbe4132`), the room + pause build installed and checked on
the Old (hardware-tested, not a performance acceptance: its cold-start library
stall was still open) and the SHA-256 of its `BreadBox.elf`. The ELF is a
hardware artifact kept outside the repository; `baseline-check.sh` finds it
through `BREADBOX_BASELINE_ELF` or the recorded path and refuses any other file.
If neither source is available, the gate prints a clear skip unless
`BASELINE_BUILD_REFERENCE=1` requests a reference rebuild from Git history. That
rebuild uses the recorded output path and make arguments: core, blobs, linker
scripts and pins all come from the accepted commit, not the candidate. A cached reference is reused only when its
stamp (commit, ELF digest, toolchain, make arguments) still matches. `BASELINE_JOBS`
(default 3) sets its parallelism; a temporary reference is removed on exit. The
recorded short `commit` must resolve to `commit_full` before either source is
trusted. The gate never skips. It runs

- `compare.py`: core regions, symbols and core code words;
- `check-pinned.py`: size, I-cache set and instruction words of every pinned
  frontend frame function. Required names are the NEW objects' plus the frozen
  `pinned-names.json`, so a baseline function that vanished from NEW fails; a name
  absent from an image needs a reason in `pinned-missing.json`;
- `check-system-pinned.py`: page offset, size and instruction words of every
  system frame function and of the GBA core blob. Required systems, objects and
  names are the union of the candidate's `frame-pins.txt` and the frozen
  `system-pinned-names.json`: dropping a row, an object or a function fails.

Words count as relocations only when the value lands inside the same symbol's
extent in both images, or in the same unnamed read-only block (same gap between
the same neighboring symbols, or identical content); an instruction word is
never inside an image section, so it can not pass as a pointer (`hotcode.py`).

**Intentional code changes** (an anim-calm style UI change in a pinned function) are
accepted one function at a time in `tools/layout/accepted-changes.json`, never by
moving the baseline or re-recording names. Print a stub with
`python3 tools/layout/accept-change.py --gate frontend|system BASELINE.elf NEW.elf NAME...`
and fill in `reason` and `evidence` (what was reviewed and measured). An entry pins
the exact before/after (size and normalized code fingerprint of the baseline and
the candidate function); it fails when the baseline function is not the recorded
one, when the candidate drifts from the accepted code, or when the function no
longer changes. The page offset stays pinned unless the entry sets
`new_page_offset` with a `page_reason`. Everything not listed keeps its comparison.

CI runs the gate in the layout step, with `tools/layout/test-pinned.py` (controls:
instruction/size flips, deleted baseline function, deleted pin row, pointer
look-alike, GBA same-size flips, stale and drifted accepted changes, candidate
re-records).

Move the baseline forward only after the new build passed on hardware:

1. copy its `BreadBox.elf` next to the old one and take its `sha256sum`;
2. edit `tools/layout/baseline.json`: `build`, **both** `commit` (short) and
   `commit_full` (`git rev-parse <commit>`), `sha256`, `default_path`,
   `reference_elf` and `reference_make` (how that commit builds), `note`;
3. re-record the frozen names **from the accepted ELF with its own objects**
   (`check-pinned.py` / `check-system-pinned.py BASELINE.elf build/3ds/BreadBox.elf
   BUILD_DIR --record`, run on the accepted commit's tree; they refuse any other
   ELF) and review the name-list diff; never record from a candidate;
4. empty `accepted-changes.json` (its entries are relative to the old baseline) and
   trim `pinned-missing.json` to what the new pair needs.

Host suites need a C compiler, Python, FreeType, libpng, libcurl, zlib, OpenSSL,
DejaVu fonts, ImageMagick, an ARM Linux cross compiler, and qemu-user. Logs go to
`HOST_TEST_OUT` or a temporary directory. Skipped suites fail the gate; provide
missing tools instead of treating skips as passes. `HOST_TEST_ALLOW_SKIPS=1` is
an explicit local troubleshooting opt-out. Tests use substitutes for SDK and
hardware services and cannot establish real GPU, audio, SD, or suspend behavior.
The executable settings contract in [settings.md](settings.md) is consumed by
the build and menu tests. Presets are validated by `tools/presets/check.py` and
versioned under `assets/romfs/presets`; defaults history and evidence pins must
remain consistent.

`ps1-messages` runs the PS1 core notice router and both libretro message callbacks:
routine BIOS and enabled-hack notices go to the diagnostic log, while actionable
warnings and game feedback still reach the OSD.

Old 3DS speed depends on code and data addresses. Keep `core-first.ld`, core
archive/library order, core string isolation, the pinned frontend, and
`NDRC_LAYOUT_PAD=3492` for the current core body. Layout checks compare core
instructions and addresses, and verify frontend cache offsets. A new core or
library must not be inserted ahead of those protected regions. Intentional
core changes require an explained address comparison and renewed hardware
qualification.

Menu-only changes need builds, affected host tests, layout checks, and silent
emulator screenshots. Per-frame frontend changes add representative correctness
checks; renderer or dynarec changes need full scene/HLE gates. Speed claims
require Old 3DS A/B/A measurements using the 13-heavy-scene mean, with individual
regressions investigated. Simulator timing is not hardware FPS.

The opt-in [remote developer interface](dev-control.md) and
[recording tools](recorder.md) support controlled tests. Never publish private
tokens or game/BIOS files. Keep generated evidence, plans, and internal reports
outside this repository. CI builds BreadBox and runs host and layout checks.
