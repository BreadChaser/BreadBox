#!/bin/bash
# Host test of the crash-safe save files (memory cards, states, settings),
# with injected faults, plus mutations that each must make it fail.
set -euo pipefail
repo_root=$(cd "$(dirname "$0")/../../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -DSAFEFILE_TEST \
  -I"$repo_root/app/ui" -I"$repo_root/include/breadbox" -I"$repo_root/platform/3ds" "$repo_root/tests/app/save/check.c" \
  "$repo_root/app/ui/safefile.c" -o "$tmp/check"
cc -std=gnu11 -Wall -Wextra -Werror -Wno-format-truncation -fsanitize=address,undefined -g -DSAFEFILE_TEST \
  -include "$repo_root/tests/app/save/host.h" -I"$repo_root/tests/app/host" -I"$repo_root/app/ui" -I"$repo_root/include/breadbox" -I"$repo_root/platform/3ds" \
  -I"$repo_root/core/deps/libretro-common/include" "$repo_root/tests/app/save/cards.c" -Wl,--wrap=stat,--wrap=fopen \
  "$repo_root/systems/ps1/cards.c" "$repo_root/app/ui/bb_final.c" "$repo_root/app/ui/safefile.c" "$repo_root/app/ui/history.c" -o "$tmp/cards"
"$tmp/cards"
cc -std=gnu11 -O1 -Wall -Wextra -Wno-sign-compare -Werror -fsanitize=address,undefined -g \
  -I"$repo_root/core/include" "$repo_root/tests/app/save/states.c" -o "$tmp/states"
"$tmp/states"
cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -DSAFEFILE_TEST -DHIST_BUDGET=12000 \
  -I"$repo_root/app/ui" -I"$repo_root/include/breadbox" -I"$repo_root/platform/3ds" "$repo_root/tests/app/save/history.c" "$repo_root/app/ui/history.c" \
  "$repo_root/app/ui/safefile.c" -o "$tmp/history"
"$tmp/history"
python3 "$repo_root/tests/app/save/mutations.py"
"$tmp/check"
# SD errors on <name>/.bak/.tmp are never absence
cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -DSAFEFILE_TEST -Wl,--wrap=stat,--wrap=fopen \
  -I"$repo_root/app/ui" -I"$repo_root/include/breadbox" -I"$repo_root/platform/3ds" "$repo_root/tests/app/save/io_errors.c" \
  "$repo_root/app/ui/safefile.c" -o "$tmp/io-errors"
"$tmp/io-errors"
cc -std=gnu11 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  "$repo_root/tests/app/save/thumb.c" -o "$tmp/thumb"
"$tmp/thumb"

cc -std=gnu11 -Wall -Wextra -Werror -Wno-format-truncation -fsanitize=address,undefined -g -Wno-misleading-indentation -DSAFEFILE_TEST -DSAVE_WRITER_TEST -pthread -I"$repo_root/tests/gba_saves" \
 -include "$repo_root/tests/app/save/host.h" -I"$repo_root/tests/app/host" -I"$repo_root/app/ui" -I"$repo_root/include/breadbox" -I"$repo_root/platform/3ds" \
 -I"$repo_root/core/deps/libretro-common/include" "$repo_root/tests/app/save/cards_ownership.c" -Wl,--wrap=fopen,--wrap=stat,--wrap=malloc,--wrap=calloc \
 "$repo_root/systems/ps1/cards.c" "$repo_root/app/ui/bb_final.c" "$repo_root/app/ui/safefile.c" "$repo_root/app/ui/history.c" "$repo_root/app/ui/bb_save.c" "$repo_root/app/ui/save_writer.c" -lz -o "$tmp/final-cards"
TMPDIR="$tmp" "$tmp/final-cards"

# The final Continue controller is compiled verbatim from emu.c with a model core.
python3 - "$repo_root" "$tmp" <<'PYCODE'
from pathlib import Path
import sys
source=(Path(sys.argv[1])/'systems/ps1/emu.c').read_text()
start=source.index('typedef struct { char path[PATH_LEN], prefix[PATH_LEN], group[PATH_LEN];')
end=source.index('\n\n#define NO_SLOT',start)
(Path(sys.argv[2])/'continue.inc').write_text(source[start:end])
PYCODE
cc -std=gnu11 -Wall -Wextra -Werror -Wno-missing-field-initializers -fsanitize=address,undefined -g -DSAFEFILE_TEST \
 -I"$tmp" -I"$repo_root/app/ui" -I"$repo_root/include/breadbox" \
 "$repo_root/tests/app/save/continue.c" "$repo_root/app/ui/bb_final.c" "$repo_root/app/ui/safefile.c" \
 -Wl,--wrap=fopen,--wrap=malloc -o "$tmp/final-continue"
mkdir "$tmp/continue-sd"
"$tmp/final-continue" "$tmp/continue-sd"
