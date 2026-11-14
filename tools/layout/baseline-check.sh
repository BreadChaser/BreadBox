#!/usr/bin/env bash
# Required public-source reference comparison. Missing/unverified references fail.
# Reference source, digest, qualification and original hardware identity: baseline.json.
# BREADBOX_BASELINE_ELF supplies a prebuilt qualified reference. Otherwise the public
# source is fetched/rebuilt in LAYOUT_REFERENCE_OUT (default artifacts/layout-reference).
# BASELINE_BUILD_REFERENCE=0 forbids fetching/building and requires an existing reference.
set -euo pipefail
cd "$(dirname "$0")/../.."
new=${1:-build/3ds/BreadBox.elf}
[[ -f $new ]] || { echo "baseline-check: $new missing (run make)" >&2; exit 2; }
newbuild=$(dirname "$new")/build-breadbox
[[ -d $newbuild ]] || { echo "baseline-check: $newbuild missing (the pins read its objects)" >&2; exit 2; }
if [[ ${ALLOW_HT_TRACE:-0} != 1 ]] && "${DEVKITARM:-/opt/devkitpro/devkitARM}/bin/arm-none-eabi-nm" --defined-only "$new" | awk '$NF == "ht_mark" {found=1} END {exit !found}'; then
 echo 'baseline-check: release ELF contains ht_mark (diagnostic builds require ALLOW_HT_TRACE=1)' >&2
 exit 2
fi
base=$(python3 -I tools/layout/reference.py) || exit 2
printf 'baseline: verified public 0.9.0 reference %s (build-2650 acceptance lineage retained)\n' "$base"
status=0
python3 tools/layout/compare.py "$base" "$new" || status=1
python3 tools/layout/check-pinned.py "$base" "$new" "$newbuild" || status=1
python3 tools/layout/check-system-pinned.py "$base" "$new" "$newbuild" || status=1
exit "$status"
