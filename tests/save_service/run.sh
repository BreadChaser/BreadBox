#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
out=$(mktemp -d);trap 'rm -rf "$out"' EXIT
for fault in normal fault; do
extra=(); [[ $fault != fault ]] || extra=(-DSAFEFILE_TEST -Wno-unused-function)
cc "${extra[@]}" -std=gnu11 -g -Wall -Wextra -Werror -Wno-misleading-indentation \
 -DSAVE_WRITER_TEST -fsanitize=address,undefined -pthread -DSLOT_SCAN_TEST -Wl,--wrap=readdir,--wrap=stat \
 -Iapp/ui -Iinclude/breadbox -Iplatform/3ds -Itests/gba_saves tests/save_service/check.c \
 app/ui/bb_save.c app/ui/bb_final.c app/ui/save_writer.c app/ui/safefile.c app/ui/history.c -lz -o "$out/check"
mkdir "$out/$fault"
"$out/check" "$out/$fault"
done
# the exit's play-log retry and kept markers: the real choke point (bb_platform_saves, taken out
# of bb_platform.c), the real later thread, play log and safe files
awk '/^static bool closing\(void\)/{on=1} on{print} on && /^ return !go/{getline;print;exit}' platform/3ds/bb_platform.c > "$out/choke.inc"
grep -q 'playlog_flush' "$out/choke.inc"
cc -std=gnu11 -g -Wall -Wextra -Werror -Wno-misleading-indentation -DSAVE_WRITER_TEST \
 -fsanitize=address,undefined -pthread -Wl,--wrap=safe_begin,--wrap=realloc \
 -I"$out" -Itests/app/host -Iapp/ui -Iinclude/breadbox -Iplatform/3ds -Isystems/gba -Itests/gba_saves -Icore/deps/libretro-common/include \
 -include tests/gba_saves/host.h tests/save_service/exit_playlog.c \
 app/ui/bb_save.c app/ui/bb_final.c app/ui/save_writer.c app/ui/safefile.c app/ui/history.c app/ui/playlog.c -lz -o "$out/exit"
mkdir "$out/exit-sd"
"$out/exit" "$out/exit-sd"
"$out/exit" "$out/exit-sd" dialog

cc -std=gnu11 -g -Wall -Wextra -Werror -Wno-misleading-indentation \
 -DSAVE_WRITER_TEST -fsanitize=address,undefined -pthread \
 -Iapp/ui -Iinclude/breadbox -Iplatform/3ds -Itests/gba_saves tests/save_service/ownership.c \
 app/ui/bb_save.c app/ui/bb_final.c app/ui/save_writer.c app/ui/safefile.c app/ui/history.c -lz -o "$out/ownership"
for mode in sync partial continue uncaptured end-continue; do
 mkdir "$out/$mode"
 "$out/ownership" "$out/$mode" "$mode"
done
cc -std=gnu11 -g -Wall -Wextra -Werror -Wno-unused-function -Wno-misleading-indentation \
 -DSAVE_WRITER_TEST -DSAFEFILE_TEST -fsanitize=address,undefined -pthread \
 -Wl,--wrap=fopen,--wrap=malloc,--wrap=calloc \
 -Iapp/ui -Iinclude/breadbox -Iplatform/3ds -Itests/gba_saves tests/save_service/faults.c \
 app/ui/bb_save.c app/ui/bb_final.c app/ui/save_writer.c app/ui/safefile.c app/ui/history.c -lz -o "$out/faults"
mkdir "$out/faults-sd"
"$out/faults" "$out/faults-sd"
# The pause's Continue point and the unanswered Continue question, real later thread
cc -std=gnu11 -g -Wall -Wextra -Werror -Wno-unused-function -Wno-misleading-indentation \
 -DSAVE_WRITER_TEST -fsanitize=address,undefined -pthread \
 -Iapp/ui -Iinclude/breadbox -Iplatform/3ds -Itests/gba_saves tests/save_service/continue.c \
 app/ui/bb_save.c app/ui/bb_final.c app/ui/save_writer.c app/ui/safefile.c app/ui/history.c -lz -o "$out/continue-point"
mkdir "$out/continue-point-sd"
"$out/continue-point" "$out/continue-point-sd"
# The one writer of Continue points (allocation failure, overlap, late failure, Start over errors)
cc -std=gnu11 -g -Wall -Wextra -Werror -Wno-unused-function -Wno-misleading-indentation \
 -DSAVE_WRITER_TEST -fsanitize=address,undefined -pthread -Wl,--wrap=malloc,--wrap=calloc,--wrap=fopen,--wrap=stat,--wrap=rename \
 -Iapp/ui -Iinclude/breadbox -Iplatform/3ds -Itests/gba_saves tests/save_service/continue_writer.c \
 app/ui/bb_save.c app/ui/bb_final.c app/ui/save_writer.c app/ui/safefile.c app/ui/history.c -lz -o "$out/continue-writer"
mkdir "$out/continue-writer-sd"
"$out/continue-writer" "$out/continue-writer-sd"
# The final files' metadata lock sleeps when busy (one core, strict priorities: Old 3DS)
cc -std=gnu11 -g -Wall -Wextra -Werror -fsanitize=address,undefined -pthread -Wl,--wrap=nanosleep \
 -Iapp/ui -Iinclude/breadbox tests/save_service/final_lock.c app/ui/bb_final.c -o "$out/final-lock"
"$out/final-lock"
python3 tests/save_service/continue_question.py
# A damaged battery/clock with no good copy: ask, put a backup back or start empty, keep .bad
cc -std=gnu11 -g -Wall -Wextra -Werror -Wno-unused-function -Wno-misleading-indentation \
 -DSAVE_WRITER_TEST -fsanitize=address,undefined -pthread \
 -Iapp/ui -Iinclude/breadbox -Iplatform/3ds -Isystems/gba -Itests/gba_saves tests/save_service/damaged.c \
 app/ui/bb_save.c app/ui/bb_final.c app/ui/save_writer.c app/ui/safefile.c app/ui/history.c -lz -o "$out/damaged"
mkdir "$out/damaged-sd"
"$out/damaged" "$out/damaged-sd"
# ... with a power cut at every step of Restore and a full SD card
cc -std=gnu11 -g -Wall -Wextra -Werror -Wno-unused-function -Wno-misleading-indentation \
 -DSAVE_WRITER_TEST -DSAFEFILE_TEST -fsanitize=address,undefined -pthread \
 -Iapp/ui -Iinclude/breadbox -Iplatform/3ds -Isystems/gba -Itests/gba_saves tests/save_service/damaged.c \
 app/ui/bb_save.c app/ui/bb_final.c app/ui/save_writer.c app/ui/safefile.c app/ui/history.c -lz -o "$out/damaged-cuts"
mkdir "$out/damaged-cuts-sd"
"$out/damaged-cuts" "$out/damaged-cuts-sd"
# merge/v1-fixes: the damage question and the Continue question in one start (Back/Restore, sleep/Close, pause, Start over)
cc -std=gnu11 -g -Wall -Wextra -Werror -Wno-unused-function -Wno-misleading-indentation \
 -DSAVE_WRITER_TEST -fsanitize=address,undefined -pthread \
 -Iapp/ui -Iinclude/breadbox -Iplatform/3ds -Itests/gba_saves tests/save_service/combined.c \
 app/ui/bb_save.c app/ui/bb_final.c app/ui/save_writer.c app/ui/safefile.c app/ui/history.c -lz -o "$out/combined"
mkdir "$out/combined-sd"
"$out/combined" "$out/combined-sd"
# "Use it" for a smaller save, the process killed at every file step, four real sizes
cc -std=gnu11 -g -Wall -Wextra -Werror -Wno-unused-function -Wno-misleading-indentation \
 -DSAVE_WRITER_TEST -fsanitize=address,undefined -pthread \
 -Wl,--wrap=fopen,--wrap=fwrite,--wrap=fclose,--wrap=rename,--wrap=remove \
 -Iapp/ui -Iinclude/breadbox -Iplatform/3ds -Isystems/gba -Itests/gba_saves tests/save_service/foreign_cuts.c \
 app/ui/bb_save.c app/ui/bb_final.c app/ui/save_writer.c app/ui/safefile.c app/ui/history.c -lz -o "$out/foreign-cuts"
mkdir "$out/foreign-cuts-sd"
"$out/foreign-cuts" "$out/foreign-cuts-sd"
# SD read errors on the battery/backup/sole tmp/clock are never "no save"
cc -std=gnu11 -g -Wall -Wextra -Werror -Wno-unused-function -Wno-misleading-indentation \
 -DSAVE_WRITER_TEST -fsanitize=address,undefined -pthread -Wl,--wrap=stat,--wrap=fopen,--wrap=fread,--wrap=fclose,--wrap=rename \
 -Iapp/ui -Iinclude/breadbox -Iplatform/3ds -Itests/gba_saves tests/save_service/read_errors.c \
 app/ui/bb_save.c app/ui/bb_final.c app/ui/save_writer.c app/ui/safefile.c app/ui/history.c -lz -o "$out/read-errors"
mkdir "$out/read-errors-sd"
"$out/read-errors" "$out/read-errors-sd"
