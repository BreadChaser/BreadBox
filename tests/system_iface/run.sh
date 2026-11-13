#!/bin/sh
# Host check of the draft system interface: header compiles warning-free,
# the registry is generated from a list. Real shell/index coverage follows.
set -e
here=$(cd "$(dirname "$0")" && pwd); root=$here/../..
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
python3 "$root/tools/build/systems-table.py" "$tmp/systems_table.c" ps1 gba snes
if grep -nE '\b(gba|ps1|snes)\b' "$here/shell.c" | grep -v '"snes"\|"n64"' | grep -v '^\s*/\?\*'; then
	echo "FAIL: shell names a system"; exit 1; fi
cc -std=c11 -Wall -Wextra -Werror -I"$root/app/ui" -I"$root/include/breadbox" -I"$root/platform/3ds" -I"$root/systems/gba" -o "$tmp/shell" \
	"$here/shell.c" "$here/stubs.c" "$here/sys_snes_example.c" "$tmp/systems_table.c"
"$tmp/shell"
python3 "$root/tools/build/systems-table.py" "$tmp/systems_table.c" ps1 gba
cc -std=c11 -Wall -Wextra -Werror -I"$root/app/ui" -I"$root/include/breadbox" -I"$root/platform/3ds" -I"$root/systems/gba" -o "$tmp/session" \
 "$here/session.c" "$root/app/ui/bb_session.c" "$root/app/ui/bb_order.c" "$tmp/systems_table.c"
"$tmp/session"

# The real production scanner and routing/session controller compile with the
# extra adapter; no stand-in shell provides those services.
python3 "$here/real-shell.py"

python3 "$here/continue.py"

python3 "$here/continue-io.py"
