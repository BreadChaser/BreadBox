#!/usr/bin/env bash
# Required exact-CIA Library/Settings walk on both models, private and muted.
# Needs a local emulator and ROM fixture; host-tests stays ROM-free.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
host_tools=${PSX3D_HOST_TOOLS:?set PSX3D_HOST_TOOLS to the host support directory}
cia=$(realpath "${1:?Usage: smoke-menu.sh CIA [OUT]}")
out=${2:-$host_tools/breadbox/menu-smoke}
mkdir -p "$out/tmp"
export TMPDIR=$out/tmp
sha256sum "$cia" > "$out/cia.sha256"
for model in old new; do
 flock "$dev/fastloop/lock.heavy" env NOFILM=1 python3 -u "$dev/breadbox/integrate-r3/capture-rc.py" \
  "$cia" "$model" "$root/tests/settings_b/smoke-actions.jsonl" "$out/$model" > "$out/$model.log" 2>&1
 test "$(tail -n 1 "$out/$model.log")" = DONE
done
