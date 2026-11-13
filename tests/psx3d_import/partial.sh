#!/usr/bin/env bash
# Optional private fixture test: copy an Old PSX3D tree and its partial
# BreadBox destination, resume, then reopen. Never writes the provided trees.
set -euo pipefail
[[ $# == 3 ]] || { echo "usage: $0 PSX3D PARTIAL_BREADBOX EXPECTED_REMAINING_COPIES"; exit 2; }
source_tree=$(realpath "$1")
partial_tree=$(realpath "$2")
remaining=$3
cd "$(dirname "$0")/../.."
out=$(mktemp -d);trap 'rm -rf "$out"' EXIT
cp -a "$source_tree" "$out/PSX3D"
cp -a "$partial_tree" "$out/BreadBox"
python3 - "$out/PSX3D" "$out/source.json" <<'PY'
import hashlib,json,sys
from pathlib import Path
p=Path(sys.argv[1]);Path(sys.argv[2]).write_text(json.dumps({str(f.relative_to(p)):hashlib.sha256(f.read_bytes()).hexdigest() for f in p.rglob('*') if f.is_file()},sort_keys=True))
PY
cc -std=gnu11 -O1 -g -Wno-sign-compare -Wno-misleading-indentation \
 -Wl,--wrap=state_check -Wl,--wrap=fwrite -Wl,--wrap=mkdir -Wl,--wrap=lstat -Wl,--wrap=stat -Wl,--wrap=fopen -Wl,--wrap=opendir -Wl,--wrap=open \
 -DPSX_IMPORT_TEST -DPSX_IMPORT_NATIVE_CRC -fsanitize=address,undefined \
 -Icore/include -Iapp/ui -Iinclude/breadbox -Iplatform/3ds tests/psx3d_import/partial.c tests/psx3d_import/strict_fs.c \
 systems/ps1/breadbox_import.c app/ui/history.c app/ui/safefile.c -lz -o "$out/resume"
"$out/resume" "$out/PSX3D" "$out/BreadBox" "$remaining"
python3 - "$out/PSX3D" "$out/source.json" <<'PY'
import hashlib,json,sys
from pathlib import Path
p=Path(sys.argv[1]);assert json.loads(Path(sys.argv[2]).read_text())=={str(f.relative_to(p)):hashlib.sha256(f.read_bytes()).hexdigest() for f in p.rglob('*') if f.is_file()}
print('All source files byte-identical after partial resume and reopen: PASS')
PY
