#!/usr/bin/env bash
# Rebuild the room assets of assets/romfs/room from source.
#
#   tools/room/build.sh [--out DIR] [--hands] [--threads N] [--quick | --ref]
#
# Always: the Lamplight room (tools/room/lamplight/build.py: geometry, textures, the per-vertex bake of every room
# style in lamplight/lights.json, the clip check as a failing gate) -> DIR/romfs/room/room.bbr.
# --hands: also the hands and the boxes they hold -> hands.bbh, case-motion.bbh, props.bbr, CREDITS.md, from
# tools/room/source/ (hands/: the hand's mesh, rig and texture; hands-bake.json: its clips; props/: the boxes).
#
# --threads N: Blender's and Cycles' threads (1 or 2). The bake is bit-exact only with the sample counts in
# lights.json: every GI_* variable of the caller is dropped, except with --quick, which takes GI_SAMPLES and
# GI_AO_SAMPLES for a fast look and ships nothing (DIR/lamplight only). --ref: plain Cycles renders of each style's
# bake scene from Home (DIR/ref/ref-<style>.png), no room file.
# Needs Blender 5.2.3 (+ NumPy, bundled). Set BLENDER to the executable if it is not on PATH.
# Nothing in the source tree or in assets/romfs is touched: results go to DIR (default tools/room/build, ignored by
# git). Then run tools/room/check.py: it compares DIR/romfs/room with assets/romfs/room.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
src=$here/source
out=$here/build
threads=2
hands=0
mode=ship
while [ $# -gt 0 ]; do
	case $1 in
	--out) out=$(realpath -m "$2"); shift 2 ;;
	--hands) hands=1; shift ;;
	--threads) threads=$2; shift 2 ;;
	--quick) mode=quick; shift ;;
	--ref) mode=ref; shift ;;
	*) echo "usage: $0 [--out DIR] [--hands] [--threads N] [--quick | --ref]" >&2; exit 2 ;;
	esac
done
# the shipped room's sample counts are lights.json's: nothing from the caller's environment reaches the bake
for v in $(compgen -e | grep '^GI_' || :); do
	case $mode:$v in quick:GI_SAMPLES|quick:GI_AO_SAMPLES) ;; *) unset "$v" ;; esac
done

case $threads in
1|2) ;;
*) echo "build.sh: --threads must be 1 or 2" >&2; exit 2 ;;
esac

BLENDER=${BLENDER:-blender}
PINNED=5.2.3
have=$("$BLENDER" --version | head -1)
case $have in
"Blender $PINNED"*) ;;
*) echo "build.sh: need Blender $PINNED, found '$have'" >&2; exit 1 ;;
esac
mkdir -p "$out/lamplight" "$out/romfs/room"

# the room: one file, every room style a colour stream (and its atlas). Blender exits 0 on a Python error unless told
# otherwise, so its status is kept and the build counts only with build.py's OK marker (written last: the room file's
# SHA-256); outputs of an earlier run are removed first.
room() {   # room OUTDIR [build.py args...]: Blender's status, its log in OUTDIR/build.log
	local d=$1; shift
	rm -rf "$d"; mkdir -p "$d"
	PYTHONHASHSEED=0 "$BLENDER" -b --factory-startup --python-exit-code 1 -t "$threads" \
		--python "$here/lamplight/build.py" -- --out "$d" --threads "$threads" "$@" > "$d/build.log" 2>&1
}
fail() { echo "build.sh: $*" >&2; exit 1; }
rm -f "$out/romfs/room/room.bbr"
if [ $mode = ref ]; then
	room "$out/ref" --ref || fail "the reference renders failed ($out/ref/build.log)"
	grep -q '^REF-DONE' "$out/ref/build.log" && [ ! -e "$out/ref/room.bbr" ] || fail "--ref wrote a room file or did not finish"
	echo "build.sh: reference renders in $out/ref (no room file)"
	exit 0
fi
quick=()
[ $mode = quick ] && quick=(--quick)
room "$out/lamplight" "${quick[@]}" || { grep -E 'CLIP|FRAME|Error|Traceback|failed' "$out/lamplight/build.log" | tail -20 >&2; fail "the room did not build ($out/lamplight/build.log)"; }
grep -E '^(CLIP-CHECK|style|room\.bbr|ROOM-BUILD-OK)' "$out/lamplight/build.log" || :
[ -s "$out/lamplight/room.bbr" ] && [ -s "$out/lamplight/OK" ] || fail "no success marker: the room build stopped early ($out/lamplight/build.log)"
[ "$(sha256sum < "$out/lamplight/room.bbr" | cut -d' ' -f1)" = "$(cat "$out/lamplight/OK")" ] || fail "room.bbr is not the file this run wrote"
# the gates must still catch what they are for: a lamp in the bookcase, a desk sunk into the floor
for t in clip sink; do
	if room "$out/selftest-$t" --$t-selftest || [ -e "$out/selftest-$t/OK" ]; then fail "the $t self-test built a room: the clip gate is broken"; fi
	grep -q 'CLIP-CHECK FAIL' "$out/selftest-$t/build.log" || fail "the $t self-test failed for another reason ($out/selftest-$t/build.log)"
done
echo "build.sh: clip gate self-tests fail as they must (lamp in the bookcase, desk 5 cm into the floor)"
if [ $mode = quick ]; then
	echo "build.sh: --quick: $out/lamplight/room.bbr is a quick look (GI_SAMPLES=${GI_SAMPLES:-lights.json}), not shipped"
	exit 0
fi
cp "$out/lamplight/room.bbr" "$out/romfs/room/room.bbr"

if [ "$hands" = 1 ]; then
	# the boxes the hands hold (Python + NumPy, the pinned Blender's), then the hands from their standalone source
	"$BLENDER" -b --factory-startup --python-exit-code 1 --python "$src/props/build.py" -- "$out/props" > "$out/props.log" 2>&1 ||
		fail "props did not build ($out/props.log)"
	python3 -I "$src/pack-hands.py" --bake "$src/hands-bake.json" --hand "$src/hands" --textures room --out "$out/romfs/room" \
		--props "$out/props/props.bbr" --credits "$src/CREDITS.md"
	python3 -I "$here/strip-hand-mesh.py" "$out/romfs/room/hands.bbh" "$out/romfs/room/case-motion.bbh"
	mv "$out/romfs/room/MANIFEST.json" "$out/hands-manifest.json"
fi
echo "build.sh: done -> $out/romfs/room (check with: python3 tools/room/check.py --built $out/romfs/room)"
