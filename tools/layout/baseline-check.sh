#!/usr/bin/env bash
# tools/layout/baseline-check.sh: BreadBox layout and hot-code gate against the accepted
# BreadBox build named in tools/layout/baseline.json (the replacement for the retired
# PSX3D byte-identity gate). Against the baseline, the new ELF must keep
#   * every core region, symbol and core code word (compare.py),
#   * the size, I-cache set and instruction words of every pinned per-frame frontend
#     function; a pinned name missing from either ELF fails unless listed with a reason
#     in tools/layout/pinned-missing.json (check-pinned.py),
#   * page offset, size and instruction words of every system frame function and of the
#     GBA core blob (check-system-pinned.py).
# Baseline source:
#   1. BREADBOX_BASELINE_ELF, else the recorded default_path, if that file exists:
#      it must have the recorded sha256;
#   2. otherwise skip unless BASELINE_BUILD_REFERENCE=1, which builds the whole
#      accepted commit from history (git archive + its own make), never the candidate.
#      The reference build is stamped with the commit and temporary output is removed.
# BASELINE_JOBS (default 3) sets reference-build parallelism.
# Usage: bash tools/layout/baseline-check.sh [NEW.elf]     (default build/3ds/BreadBox.elf)
# Moving the baseline forward: docs/building.md, "Accepted-build baseline".
set -euo pipefail
cd "$(dirname "$0")/../.."
ROOT=$PWD
new=${1:-build/3ds/BreadBox.elf}
meta=${BASELINE_META:-tools/layout/baseline.json}
field() { python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1]))[sys.argv[2]])' "$meta" "$1"; }
[[ -f $new ]] || { echo "baseline-check: $new missing (run make)" >&2; exit 2; }
# Diagnostic tracing must be an explicit build-only exception, never a release.
if [[ ${ALLOW_HT_TRACE:-0} != 1 ]] && "${DEVKITARM:-/opt/devkitpro/devkitARM}/bin/arm-none-eabi-nm" --defined-only "$new" | awk '$NF == "ht_mark" {found=1} END {exit !found}'; then
 echo "baseline-check: release ELF contains ht_mark (diagnostic builds require ALLOW_HT_TRACE=1)" >&2
 exit 2
fi
newbuild=$(dirname "$new")/build-breadbox
[[ -d $newbuild ]] || { echo "baseline-check: $newbuild missing (the pins read its objects)" >&2; exit 2; }

ref=$(field commit_full)
want=$(field sha256)
base=${BREADBOX_BASELINE_ELF:-$(field default_path)}
base=${base/#\~/$HOME}
if [[ ! -f $base && -z ${BREADBOX_BASELINE_ELF:-} && ${BASELINE_BUILD_REFERENCE:-0} != 1 ]]; then
	echo "baseline-check: skipped (BREADBOX_BASELINE_ELF is unset and no recorded artifact is available; set it to run the comparison)"
	exit 0
fi
# The recorded identities must agree before either baseline source is trusted.
[[ $(git rev-parse --verify -q "$(field commit)^{commit}" || true) == "$ref" ]] || { echo "baseline-check: baseline.json commit $(field commit) does not resolve to commit_full $ref" >&2; exit 2; }
if [[ -f $base ]]; then
	have=$(sha256sum "$base" | cut -d' ' -f1)
	[[ $have == "$want" ]] || { echo "baseline-check: $base is not the recorded baseline (sha256 $have, want $want)" >&2; exit 2; }
	echo "baseline: BreadBox build $(field build) ($(field commit)) artifact $want"
elif [[ -n ${BREADBOX_BASELINE_ELF:-} ]]; then
	echo "baseline-check: BREADBOX_BASELINE_ELF=$base does not exist" >&2; exit 2
else
	git cat-file -e "$ref^{commit}" 2>/dev/null || { echo "baseline-check: reference build was requested but commit $ref is not in this clone (fetch full history)" >&2; exit 2; }
	# The reference is the WHOLE accepted tree, built by its own Makefile: core, blobs,
	# generated linker scripts and frame pins all come from $ref, never from the candidate.
	# Its output path and make arguments are recorded with the accepted commit (a later
	# source move must not change how the historical tree is built). It costs a full
	# build; the hardware artifact avoids it.
	if [[ -n ${LAYOUT_REFERENCE_OUT:-} ]]; then out=$LAYOUT_REFERENCE_OUT; mkdir -p "$out"
	else out=$(mktemp -d "${TMPDIR:-/tmp}/bb-baseline.XXXXXX"); trap 'rm -rf "$out"' EXIT; fi
	dir=$out/ref; base=$dir/$(field reference_elf)
	# A cached reference is reused only when its stamp (commit, ELF digest, toolchain and
	# make arguments) still matches; anything else is rebuilt from scratch.
	toolchain=$( { "${DEVKITARM:-/opt/devkitpro/devkitARM}/bin/arm-none-eabi-gcc" --version | head -1; sha256sum tools/build/toolchain.lock | cut -d' ' -f1; field reference_make; } | sha256sum | cut -d' ' -f1)
	stamp_ok() { [[ -f $base && -f $dir/.stamp ]] && [[ $(cat "$dir/.stamp") == "$ref $(sha256sum "$base" | cut -d' ' -f1) $toolchain" ]]; }
	if ! stamp_ok; then
		rm -rf "$dir"; mkdir -p "$dir"
		git archive "$ref" | tar -x -C "$dir"
		make -C "$dir" -j"${BASELINE_JOBS:-3}" $(field reference_make) > "$out/ref-build.log" 2>&1 || { tail -30 "$out/ref-build.log" >&2; exit 2; }
		[[ -f $base ]] || { echo "baseline-check: reference build produced no $base" >&2; exit 2; }
		echo "$ref $(sha256sum "$base" | cut -d' ' -f1) $toolchain" > "$dir/.stamp"
	fi
	echo "baseline: BreadBox build $(field build) rebuilt from $ref (no artifact on this machine)"
	python3 -I tools/build/elf-identity.py "$base" | awk '$1 == ".text" || $1 == ".rodata.frontend" {print "  reference " $1, $4}'
fi
status=0
# Run every check even when one fails, so a report is complete.
python3 tools/layout/compare.py "$base" "$new" || status=1
python3 tools/layout/check-pinned.py "$base" "$new" "$newbuild" || status=1
python3 tools/layout/check-system-pinned.py "$base" "$new" "$newbuild" || status=1
exit $status
