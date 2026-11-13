#!/usr/bin/env bash
# tools/layout/baseline-check.sh rejects missing/foreign explicit artifacts and skips a missing optional baseline.
set -u
cd "$(dirname "$0")/../.."
tmp=$(mktemp -d "${TMPDIR:-/tmp}/baseline-gate.XXXXXX"); trap 'rm -rf "$tmp"' EXIT
echo not-an-elf > "$tmp/new.elf"; mkdir "$tmp/build-breadbox"
BREADBOX_BASELINE_ELF="$tmp/absent.elf" bash tools/layout/baseline-check.sh "$tmp/new.elf" >"$tmp/out" 2>&1
[ $? -eq 2 ] && grep -q 'does not exist' "$tmp/out" || { echo 'missing baseline must fail'; cat "$tmp/out"; exit 1; }
echo foreign > "$tmp/foreign.elf"
BREADBOX_BASELINE_ELF="$tmp/foreign.elf" bash tools/layout/baseline-check.sh "$tmp/new.elf" >"$tmp/out" 2>&1
[ $? -eq 2 ] && grep -q 'not the recorded baseline' "$tmp/out" || { echo 'foreign baseline must fail'; cat "$tmp/out"; exit 1; }
bash tools/layout/baseline-check.sh "$tmp/nothing.elf" >"$tmp/out" 2>&1
[ $? -eq 2 ] && grep -q 'missing' "$tmp/out" || { echo 'missing new ELF must fail'; cat "$tmp/out"; exit 1; }
python3 -I - "$tmp" <<'PY'
import json,sys
d=json.load(open('tools/layout/baseline.json'))
d['default_path']=sys.argv[1]+'/nothing.elf'; d['commit_full']='0'*40
json.dump(d,open(sys.argv[1]+'/meta.json','w'))
PY
BASELINE_META="$tmp/meta.json" BASELINE_BUILD_REFERENCE=1 bash tools/layout/baseline-check.sh "$tmp/new.elf" >"$tmp/out" 2>&1
[ $? -eq 2 ] && grep -q 'does not resolve to commit_full' "$tmp/out" || { echo 'short commit must resolve to commit_full'; cat "$tmp/out"; exit 1; }
# The identity check comes before either baseline source is trusted (also with the artifact).
echo x > "$tmp/artifact.elf"
BASELINE_META="$tmp/meta.json" BREADBOX_BASELINE_ELF="$tmp/artifact.elf" bash tools/layout/baseline-check.sh "$tmp/new.elf" >"$tmp/out" 2>&1
[ $? -eq 2 ] && grep -q 'does not resolve to commit_full' "$tmp/out" || { echo 'commit identity must be checked with the artifact too'; cat "$tmp/out"; exit 1; }
# Reference cache: reused only for a matching stamp (commit, ELF digest, toolchain); a tampered ELF is rebuilt.
python3 -I - "$tmp" <<'PY'
import json,sys
d=json.load(open('tools/layout/baseline.json')); d['default_path']=sys.argv[1]+'/nothing.elf'
json.dump(d,open(sys.argv[1]+'/meta-ok.json','w'))
PY
env -u BREADBOX_BASELINE_ELF BASELINE_META="$tmp/meta-ok.json" bash tools/layout/baseline-check.sh "$tmp/new.elf" >"$tmp/out" 2>&1
[ $? -eq 0 ] && grep -q 'skipped (BREADBOX_BASELINE_ELF is unset' "$tmp/out" || { echo 'unset baseline must skip with a clear message'; cat "$tmp/out"; exit 1; }
mkdir -p "$tmp/shim" "$tmp/cache/ref/psx3d"; printf '#!/bin/sh\necho built >> "%s/make-called"\nexit 77\n' "$tmp" > "$tmp/shim/make"; chmod +x "$tmp/shim/make"
field() { python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1]))[sys.argv[2]])' "$tmp/meta-ok.json" "$1"; }
toolchain=$( { "${DEVKITARM:-/opt/devkitpro/devkitARM}/bin/arm-none-eabi-gcc" --version | head -1; sha256sum tools/build/toolchain.lock | cut -d' ' -f1; field reference_make; } | sha256sum | cut -d' ' -f1)
echo cached-reference > "$tmp/cache/ref/psx3d/BreadBox.elf"
echo "$(field commit_full) $(sha256sum "$tmp/cache/ref/psx3d/BreadBox.elf" | cut -d' ' -f1) $toolchain" > "$tmp/cache/ref/.stamp"
PATH="$tmp/shim:$PATH" BASELINE_META="$tmp/meta-ok.json" BASELINE_BUILD_REFERENCE=1 LAYOUT_REFERENCE_OUT="$tmp/cache" bash tools/layout/baseline-check.sh "$tmp/new.elf" >"$tmp/out" 2>&1
[ ! -e "$tmp/make-called" ] || { echo 'a matching stamp must reuse the cached reference'; cat "$tmp/out"; exit 1; }
echo tampered >> "$tmp/cache/ref/psx3d/BreadBox.elf"
PATH="$tmp/shim:$PATH" BASELINE_META="$tmp/meta-ok.json" BASELINE_BUILD_REFERENCE=1 LAYOUT_REFERENCE_OUT="$tmp/cache" bash tools/layout/baseline-check.sh "$tmp/new.elf" >"$tmp/out" 2>&1
[ $? -eq 2 ] && [ -e "$tmp/make-called" ] || { echo 'a tampered cached reference must be rebuilt'; cat "$tmp/out"; exit 1; }
[ -f build/3ds/BreadBox.elf ] && [ -d build/3ds/build-breadbox ] || { echo 'build first (make): the frame-gate controls need build/3ds/BreadBox.elf' >&2; exit 1; }
python3 tools/layout/test-pinned.py build/3ds/BreadBox.elf build/3ds/BreadBox.elf build/3ds/build-breadbox || exit 1
echo 'baseline gate fails closed PASS'
