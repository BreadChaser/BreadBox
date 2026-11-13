#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
out=$(mktemp -d);trap 'rm -rf "$out"' EXIT
cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -Wno-sign-compare -Wno-misleading-indentation \
 -Wl,--wrap=state_check -Wl,--wrap=fwrite -Wl,--wrap=mkdir -Wl,--wrap=lstat -Wl,--wrap=stat -Wl,--wrap=fopen -Wl,--wrap=opendir -Wl,--wrap=open -DPSX_IMPORT_TEST -fsanitize=address,undefined -Icore/include -Iapp/ui -Iinclude/breadbox -Iplatform/3ds \
 tests/psx3d_import/check.c tests/psx3d_import/strict_fs.c systems/ps1/breadbox_import.c \
 app/ui/history.c app/ui/safefile.c -lz -o "$out/check"
mkdir "$out/sd"
"$out/check" "$out/sd"
cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
 -Itests/app/host -Iapp/ui -Iinclude/breadbox -Iplatform/3ds -Icore/deps/libretro-common/include \
 tests/psx3d_import/ui.c app/ui/safefile.c -pthread -o "$out/ui"
mkdir "$out/ui-sd"
"$out/ui" "$out/ui-sd"

# Force global retention to evict a DIFFERENT game's entry, with the same
# production implementation and a small test-only history budget.
cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -Wno-sign-compare -Wno-misleading-indentation \
 -Wl,--wrap=state_check -Wl,--wrap=fwrite -Wl,--wrap=mkdir -Wl,--wrap=lstat -Wl,--wrap=stat -Wl,--wrap=fopen -Wl,--wrap=opendir -Wl,--wrap=open -DPSX_IMPORT_TEST -DHIST_BUDGET=8388608 -fsanitize=address,undefined \
 -Icore/include -Iapp/ui -Iinclude/breadbox -Iplatform/3ds tests/psx3d_import/check.c tests/psx3d_import/strict_fs.c systems/ps1/breadbox_import.c \
 app/ui/history.c app/ui/safefile.c -lz -o "$out/budget"
mkdir "$out/budget-sd"
"$out/budget" "$out/budget-sd" budget

# Review regressions: retry picture, deleted primary, slot label, weak/blank cards, full SD.
cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -Wno-sign-compare -Wno-misleading-indentation \
 -Wl,--wrap=state_check -Wl,--wrap=fwrite -Wl,--wrap=mkdir -Wl,--wrap=lstat -Wl,--wrap=stat -Wl,--wrap=fopen -Wl,--wrap=opendir -Wl,--wrap=open -DPSX_IMPORT_TEST -fsanitize=address,undefined \
 -Icore/include -Iapp/ui -Iinclude/breadbox -Iplatform/3ds tests/psx3d_import/review.c tests/psx3d_import/strict_fs.c systems/ps1/breadbox_import.c \
 app/ui/history.c app/ui/safefile.c -lz -o "$out/review"
for scenario in {1..6}; do
 mkdir "$out/review-$scenario"
 "$out/review" "$out/review-$scenario" "$scenario"
done
cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -Wno-misleading-indentation -Iapp/ui \
 tests/psx3d_import/path.c tests/psx3d_import/strict_fs.c \
 -Wl,--wrap=lstat -Wl,--wrap=stat -Wl,--wrap=fopen -Wl,--wrap=opendir -Wl,--wrap=open -o "$out/path"
"$out/path"

cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -Wno-sign-compare -Wno-misleading-indentation \
 -Wl,--wrap=state_check -Wl,--wrap=fread -Wl,--wrap=fwrite -Wl,--wrap=mkdir -Wl,--wrap=lstat -Wl,--wrap=stat -Wl,--wrap=fopen -Wl,--wrap=opendir -Wl,--wrap=open -DPSX_IMPORT_TEST -fsanitize=address,undefined \
 -Icore/include -Iapp/ui -Iinclude/breadbox -Iplatform/3ds tests/psx3d_import/perf.c tests/psx3d_import/strict_fs.c systems/ps1/breadbox_import.c \
 app/ui/history.c app/ui/safefile.c -lz -pthread -o "$out/perf"
mkdir "$out/perf-sd"
"$out/perf" "$out/perf-sd"

cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -Wno-sign-compare -Wno-misleading-indentation \
 -Wl,--wrap=state_check -Wl,--wrap=fwrite -Wl,--wrap=mkdir -Wl,--wrap=lstat -Wl,--wrap=stat -Wl,--wrap=fopen -Wl,--wrap=opendir -Wl,--wrap=open -DPSX_IMPORT_TEST -fsanitize=address,undefined \
 -Icore/include -Iapp/ui -Iinclude/breadbox -Iplatform/3ds -DPSX_IMPORT_NATIVE_CRC -DHIST_BUDGET=8388608 tests/psx3d_import/cursor_conflicts.c tests/psx3d_import/strict_fs.c systems/ps1/breadbox_import.c \
 app/ui/history.c app/ui/safefile.c -lz -pthread -o "$out/cursor-conflicts"
mkdir "$out/conflicts-sd"
"$out/cursor-conflicts" "$out/conflicts-sd"

cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -Wno-sign-compare -Wno-misleading-indentation \
 -Wl,--wrap=state_check -Wl,--wrap=malloc -Wl,--wrap=fwrite -Wl,--wrap=mkdir -Wl,--wrap=lstat -Wl,--wrap=stat -Wl,--wrap=fopen -Wl,--wrap=opendir -Wl,--wrap=open -DPSX_IMPORT_TEST -DPSX_IMPORT_METADATA_TEST -fsanitize=address,undefined \
 -Icore/include -Iapp/ui -Iinclude/breadbox -Iplatform/3ds tests/psx3d_import/memory.c tests/psx3d_import/strict_fs.c systems/ps1/breadbox_import.c \
 app/ui/history.c app/ui/safefile.c -lz -pthread -o "$out/memory"
mkdir "$out/memory-sd"
"$out/memory" "$out/memory-sd"

# Destination stdio and workspace growth failures must never become receipts.
cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -Wno-sign-compare -Wno-misleading-indentation \
 -Wl,--wrap=state_check -Wl,--wrap=memalign -Wl,--wrap=realloc -Wl,--wrap=fwrite -Wl,--wrap=mkdir -Wl,--wrap=lstat -Wl,--wrap=stat -Wl,--wrap=fopen -Wl,--wrap=opendir -Wl,--wrap=open -DPSX_IMPORT_TEST -DPSX_IMPORT_FAULT_TEST -fsanitize=address,undefined \
 -Icore/include -Iapp/ui -Iinclude/breadbox -Iplatform/3ds tests/psx3d_import/resource.c tests/psx3d_import/strict_fs.c systems/ps1/breadbox_import.c \
 app/ui/history.c app/ui/safefile.c -lz -pthread -o "$out/resource"
for scenario in {1..10}; do
 mkdir "$out/resource-$scenario"
 "$out/resource" "$out/resource-$scenario" "$scenario"
done

# Compile the actual native bulk-transfer branch with a POSIX-backed FS shim.
cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -Wno-sign-compare -Wno-misleading-indentation \
 -Wl,--wrap=state_check -Wl,--wrap=fwrite -Wl,--wrap=mkdir -Wl,--wrap=lstat -Wl,--wrap=stat -Wl,--wrap=fopen -Wl,--wrap=opendir -Wl,--wrap=open \
 -DPSX_IMPORT_TEST -DPSX_IMPORT_FSFILE_TEST -include tests/psx3d_import/native_fs.h -fsanitize=address,undefined \
 -Icore/include -Iapp/ui -Iinclude/breadbox -Iplatform/3ds tests/psx3d_import/native_fs.c tests/psx3d_import/strict_fs.c \
 systems/ps1/breadbox_import.c app/ui/history.c app/ui/safefile.c -lz -pthread -o "$out/native"
# Size/read are reached by streaming readback; cached source needs no input handle.
for scenario in 0 1 2 3 4 5 6; do
 mkdir "$out/native-$scenario"
 "$out/native" "$out/native-$scenario" "$scenario"
done

python3 tests/psx3d_import/mutations.py

cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
 -DBB_IMPORT_RECOVERY_TEST -DSAFE_RECOVERY_PROBE_PATH='"probe"' -Iapp/ui -Wl,--wrap=stat -Wl,--wrap=remove -Wl,--wrap=rename \
 tests/psx3d_import/recovery_ops.c app/ui/safefile.c -o "$out/recovery-ops"
mkdir "$out/recovery-sd"
"$out/recovery-ops" "$out/recovery-sd"
