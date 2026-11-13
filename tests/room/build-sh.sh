#!/usr/bin/env bash
# tools/room/build.sh fails closed (review M2): a fake Blender plays a Python crash that exits 0, a run without
# the OK marker, a marker that names another file, a broken clip gate, and a good run. Only the good run may
# leave a room.bbr in the output, and a room file of an earlier run never passes for a new one. V3 fix round: no GI_*
# variable of the caller reaches the bake (only GI_SAMPLES / GI_AO_SAMPLES with --quick, which ships nothing), the
# thread count reaches build.py, and --ref renders without a room file.
set -u
root=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/room-build-sh.XXXXXX"); trap 'rm -rf "$tmp"' EXIT
cat > "$tmp/blender" <<'EOF'
#!/usr/bin/env bash
[ "$1" = --version ] && { echo "Blender 5.2.3 (fake)"; exit 0; }
out=; self=0; ref=0; all="$*"
while [ $# -gt 0 ]; do case $1 in --out) out=$2; shift ;; --*-selftest) self=1 ;; --ref) ref=1 ;; esac; shift; done
[ -n "$out" ] || exit 3
echo "$all" > "$out/args.txt"; env | grep '^GI_' | sort > "$out/env.txt" || :
if [ $ref = 1 ]; then echo png > "$out/ref-day.png"; echo REF-DONE; exit 0; fi
case $FAKE in
crash) echo "CLIP-CHECK PASS pairs 47"; echo '{"fails": []}' > "$out/check.json"; echo "Traceback (most recent call last):"; exit 0 ;;   # Blender exits 0 on a Python error
nomarker) echo "CLIP-CHECK PASS pairs 47"; echo room > "$out/room.bbr"; exit 0 ;;
wrongfile) echo room > "$out/room.bbr"; echo 0000 > "$out/OK"; exit 0 ;;
esac
if [ $self = 1 ] && [ "$FAKE" != gate-broken ]; then echo "CLIP-CHECK FAIL 1 pairs 46"; exit 1; fi
echo "CLIP-CHECK PASS pairs 47"; echo "room-$FAKE" > "$out/room.bbr"
sha256sum < "$out/room.bbr" | cut -d' ' -f1 > "$out/OK"; echo ROOM-BUILD-OK
EOF
chmod +x "$tmp/blender"
fails=0
run() {   # run MODE WANT(0 ok, 1 fail)
	local o=$tmp/out-$1
	mkdir -p "$o/romfs/room" "$o/lamplight"; echo stale > "$o/romfs/room/room.bbr"; echo stale > "$o/lamplight/room.bbr"
	FAKE=$1 BLENDER=$tmp/blender bash "$root/tools/room/build.sh" --out "$o" > "$o.log" 2>&1
	local got=$?
	if [ "$2" = 0 ]; then
		[ $got = 0 ] && [ "$(cat "$o/romfs/room/room.bbr")" = "room-$1" ] || { echo "FAIL $1: a good run did not ship its room"; fails=$((fails+1)); }
	else
		[ $got != 0 ] && [ ! -e "$o/romfs/room/room.bbr" ] || { echo "FAIL $1: exit $got, room.bbr $(cat "$o/romfs/room/room.bbr" 2>/dev/null)"; cat "$o.log"; fails=$((fails+1)); }
	fi
}
run crash 1; run nomarker 1; run wrongfile 1; run gate-broken 1; run ok 0
# the environment and the modes (FAKE=ok: a good build)
mode() {   # mode NAME [build.sh args...]: GI_* set in the caller's environment
	local o=$tmp/mode-$1; shift
	mkdir -p "$o/romfs/room"; echo stale > "$o/romfs/room/room.bbr"
	GI_SAMPLES=7 GI_AO_SAMPLES=3 GI_REF_ONLY=1 GI_THREADS=9 FAKE=ok BLENDER=$tmp/blender bash "$root/tools/room/build.sh" --out "$o" "$@" > "$o.log" 2>&1
}
want() { "$@" || { echo "FAIL: $*"; fails=$((fails+1)); }; }
mode ship; o=$tmp/mode-ship
want [ ! -s "$o/lamplight/env.txt" ]
want grep -q -- '--threads 2' "$o/lamplight/args.txt"
want grep -qx room-ok "$o/romfs/room/room.bbr"
mode quick --quick --threads 1; o=$tmp/mode-quick
want [ "$(cat "$o/lamplight/env.txt")" = "$(printf 'GI_AO_SAMPLES=3\nGI_SAMPLES=7')" ]
want grep -q -- '--threads 1 --quick' "$o/lamplight/args.txt"
want [ ! -e "$o/romfs/room/room.bbr" ]
mode ref --ref; o=$tmp/mode-ref
want [ -s "$o/ref/ref-day.png" ]
want [ ! -e "$o/romfs/room/room.bbr" ]
want [ ! -s "$o/ref/env.txt" ]
[ $fails = 0 ] && echo "room build.sh fails closed: crash (exit 0), no marker, wrong file, broken gate; good run ships; GI_* dropped (--quick: samples only, not shipped), threads passed, --ref no room: PASS"
exit $fails
