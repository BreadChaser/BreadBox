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
make -j3                # BreadBox 3dsx and ELF
make -j3 cia            # installable CIA (alias: make breadbox)
make cxi BANNERTOOL=/path/to/bannertool MAKEROM=/path/to/makerom
make BB_EXPERIMENTAL_NES=1   # adds the experimental QuickNES system
```

Outputs are `build/3ds/BreadBox.{3dsx,elf,cia}`; `make cxi` also builds a
deterministic executable container. CIA tickets are randomized. `make clean`
cleans the frontend, the shared PS1 core and all generated `build/systems` cores,
bindings and linker fragments. Frontend objects are isolated in
`build/3ds/build-breadbox`. The shared application lives in `app/`, `platform/3ds/`, `include/breadbox/`, `assets/`
and `build/3ds/`; vendored engines live in `core/`, `gba-core/` and
`systems/nes/core/`. `psx3d/vendor/` holds copies of citro2d's and citro3d's private `internal.h`
headers, which the renderer and batching code include.

`apps/breadbox.mk` selects packaging, object directories, the system list and the
version header. `include/breadbox/app_policy.h` supplies compile-time names, SD
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
`(16 + 16 * major + minor) * 1024 + patch * 16 + 15`: 0.9.0 is 25615,
0.9.1 is 25631, and 1.0.0 is 32783. Minor is 0..15, patch is 0..63, and
major is 0..2. The low four bits are 15 for releases; 0..14 are private
counters below that release. Pre-0.9.0 counters were private too. Runtime
displays (Share) recognize releases from 25615 onward; other counters display as
`test build N`. `tools/build/title-version.py` generates the portable
`include/breadbox/title_version.h` encoder/decoder/text (`header --check` verifies it). `VERSION=N` overrides the counter for
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
tools/build/container.sh bash tools/build/ci.sh layout  # build first; requires the qualified public reference
python3 tools/layout/compare.py /path/to/baseline.elf build/3ds/BreadBox.elf
python3 tools/layout/check-pinned.py /path/to/baseline.elf build/3ds/BreadBox.elf build/3ds/build-breadbox
python3 tools/architecture/ownership-guard.py --strict   # shared code owns no system policy
```

### Structure gates and the trunk gate

Four small checks keep the code from drifting. `ci.sh gates` runs the first three (they also
run inside `ci.sh all`); each has a host test in `tests/arch_gates/`. Every stored number is a plain
file in `tools/architecture/` that may only go down: after you shrink something, run the script
with `--update` to lock it in. `--update` refuses to raise a number or add an entry. `--list`
prints what is counted.

- **Size gate** (`size-gate.py`): first-party C files stay at or under 1,500 lines and functions
  at or under 150. An `.inc` file counts into the file that includes it. Today's offenders are
  frozen in `size-exceptions.json`. It fails when one grows, when a new one appears, and when a
  listed one no longer offends (delete it with `--update`). Vendored trees are not scanned.
- **Source-text ratchet** (`test-text-ratchet.py`): counts the Python tests that read a production
  source file and then slice or search its text (`.index(`, `.count(`, a `[a:b]` slice). The count
  (`text-ratchet.count`) must not rise; write a new test against the real code instead.
- **UI_METRIC ratchet** (`ui-metric-ratchet.py`): counts uses of numbered `UI_METRIC_<n>` names
  (`ui-metric.count`, now 0). The numbered names are gone; use a named style token instead.
- **Trunk gate** (`tools/build/trunk-gate.sh`): runs `ci.sh all` in the container for the checked-out,
  clean commit and appends `{sha, tree, date, result}` to the gitignored ledger `.trunk-gate.jsonl`
  (the run takes about an hour). `trunk-gate.sh --check <sha>` exits 0 only when that commit has a
  green line. `tools/build/merge-to-trunk.sh <branch>`, run on a clean trunk, refuses unless the
  branch is a fast-forward of trunk and its head has a green line, then fast-forwards.

### Accepted-build baseline

`tools/layout/baseline.json` preserves the original acceptance identity: CIA 2650,
commit `ffbe4132ae9099ef355b7db5703498f31def59a2`, ELF SHA-256
`84e79a1bea4043ce20bae23ea00d5ec50a1bb38f6a24f949326f2ea7334a7656`.
It was installed and checked on Old 3DS; this was not a speed acceptance and its
cold-start library stall was open. No new hardware acceptance is claimed here.

The executable reference is now deliberately qualified from public 0.9.0 source,
the `v0.9.0` tag commit `39148a3c0e2a84ec82ce4ff95a14e9369c1162bd`. Its pinned public archive,
source epoch and build recipe are in `baseline.json`. The first link is a recorded maintainer-side check of private artifacts, outside
public CI: qualification first checked the original 2650 ELF against the frozen release
ELF recorded by `qualification_target_elf_sha256`, using every unchanged exact
before/after decision in `accepted-2650.json`. All three gates then compared that
frozen target against the public reconstruction with **no allowances**: sizes,
cache offsets and instruction words match; only same-symbol literal addresses
move. This preserves that lineage without exporting private Git history. Historical human notes are provenance, not required test inputs.
`accepted-changes.json` now holds changes relative to this qualified public reference.
Frozen frontend/system lists retain all original names and add surviving functions
from the verified public reference's own objects (`freeze-public.py`). Their
checksums are bound to its metadata; qualification cannot drop an old pin.

From a clean clone of the public revision carrying these verification tools,
run the complete verification with no private files:

```sh
git clone https://github.com/BreadChaser/BreadBox.git
cd BreadBox
tools/build/container.sh env BB_BUILD_JOBS=3 bash tools/build/ci.sh all
```

`ci.sh layout` always runs the reference comparison first. Missing or wrong
references fail; there is no successful skip. By default `reference.py` fetches
the public archive, checks its SHA-256, builds it in
`artifacts/layout-reference/ref`, and verifies every loaded section plus the
address/size/symbol table against the pinned identity digest. Debug directory
paths are excluded. Identity tools and the reference build use `LC_ALL=C`:
GNU nm orders equal-address symbols using locale collation, so the locale must
be fixed too. No private commit resolution or home-directory artifact is
needed. Loaded assertion strings include absolute source paths, so reference
rebuilds use the recorded `/work/artifacts/layout-reference/ref` inside the pinned
container; they do not discard those loaded bytes from the identity. The only reference-source edit replaces the old driver's hardcoded
`-j4` with the requested limit; compiler inputs stay unchanged.

An existing reference is reused only after identity verification. To forbid
fetching/building, set `BASELINE_BUILD_REFERENCE=0`; a missing reference then
fails. `BREADBOX_BASELINE_ELF` supplies a prebuilt **qualified public** reference
and is verified identically. `LAYOUT_REFERENCE_OUT` can relocate an existing
verified cache; rebuilding requires the recorded container path.
`BASELINE_JOBS` defaults to `BB_BUILD_JOBS` (3). The standalone reference check
is `bash tools/build/ci.sh baseline [CANDIDATE.elf]`.

Local and CI builds default to three jobs in `ci.sh`; `BB_BUILD_JOBS=N` overrides
that limit. Direct `make -jN` propagates its jobserver into system-core makes.
The root build records `SOURCE_DATE_EPOCH`. When it changes, only
`core/frontend/main.o` and `build/3ds/build-breadbox/clip.o` are invalidated;
normal make dependencies rebuild their archives and links. Unrelated objects
remain reusable. A host regression inventories every timestamp-macro source,
so a new user must be added to this mapping.
By default a standalone `core-blob.py` uses at most three jobs and half the
available CPUs; `BB_BUILD_JOBS` can explicitly override that limit.
Caches are disposable; `make clean` removes owned core/frontend outputs, while
reference/download/CI artifacts under `artifacts/` remain explicit caches.

The required comparisons are:

- `compare.py`: core regions, symbols and core code words;
- `check-pinned.py`: size, I-cache set and instruction words of pinned frontend
  functions, including the frozen names in `pinned-names.json` and explicit
  absent-symbol explanations in `pinned-missing.json`;
- `check-system-pinned.py`: frame functions and core blobs, including frozen
  system/object names even when a candidate drops a pin row or object.

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

A hardware baseline may move only after fresh hardware acceptance. Preserve its
build/commit/ELF identity and limitations, qualify its public source recipe, and
review all frozen-name and per-function changes. Never re-record from an
unqualified candidate. `baseline_guard.py` continues to restrict `--record` to
the original accepted hardware ELF; public-source qualification did not grant
permission to erase the existing frozen name contract.

`make host-tests` requires no built ELF or reference download. Real-ELF
baseline negative controls belong to `ci.sh layout`, which always enforces the
reference comparison before its controls and stress builds. `BASELINE_META`
overrides require `BB_LAYOUT_TEST=1` and are for negative controls only.

Host suites need a C compiler, Python, FreeType, libpng, libcurl, zlib, OpenSSL,
ImageMagick, an ARM Linux cross compiler, and qemu-user. Logs go to
`HOST_TEST_OUT` or a temporary directory; each suite gets its own empty `TMPDIR`
and `XDG_CACHE_HOME` there and never writes to your home folder. Text widths are
measured with the bundled `tests/fonts/DejaVuSans.ttf`, not a system font. Skipped suites fail the gate; provide
missing tools instead of treating skips as passes. Tests use substitutes for SDK and
hardware services and cannot establish real GPU, audio, SD, or suspend behavior.
The executable settings contract in [settings.md](settings.md) is consumed by
the build and menu tests. Presets are validated by `tools/presets/check.py` and
versioned under `assets/romfs/presets`; defaults history and evidence pins must
remain consistent.

`ps1-messages` runs the PS1 core notice router and both libretro message callbacks:
routine BIOS and enabled-hack notices go to the diagnostic log, while actionable
warnings and game feedback still reach the OSD.

`ps1-pad` sends pad-library byte sequences to the core's pad code (with `ps1_pad.h`) and checks the
ID a game reads (0x41 digital, 0x73 analog): a probed pad stays digital until the
game or the player picks analog, and state loads keep the chosen mode.

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

The opt-in [remote developer interface](dev/dev-control.md) and
[recording tools](dev/dev-control.md#recording-and-exact-replay) support controlled tests. Never publish private
tokens or game/BIOS files. Keep generated evidence, plans, and internal reports
outside this repository. CI builds BreadBox and runs host and layout checks.
