#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
python3 tests/update/packaging.py
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cc -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -DBB_UPDATE_HOST -fsanitize=address,undefined -g \
 -Iinclude/breadbox tests/update/check.c app/services/update_model.c -lcrypto -o "$tmp/check"
"$tmp/check" "$tmp/payload" "$tmp/missing"

# Crash-safe phase journal: torn/damaged files, bounds, resume by a later process.
cc -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -fsanitize=address,undefined -g -Iinclude/breadbox \
 tests/update/phase.c app/services/update_phase.c -o "$tmp/phase"
mkdir "$tmp/phase-dir" && "$tmp/phase" "$tmp/phase-dir"

# Diagnostic variants, plus both alternative endings of a committed install (HOME, launch by ID).
for diagnostic_flag in "" "-DBB_UPDATE_TEST_REMOTE" "-DBB_UPDATE_HANDOFF=0" "-DBB_UPDATE_HANDOFF=2"; do
cc -std=gnu11 -Wall -Wextra -Werror -Wno-misleading-indentation -Wno-unused-function -Wno-unused-variable \
 $diagnostic_flag -DBB_UPDATE_HOST -DBB_UPDATE_DEV_URL='"http://127.0.0.1/"' -fsanitize=address,undefined -g \
 -ffunction-sections -fdata-sections -Wl,--gc-sections -Wl,--wrap=fopen -Itests/app/host -Iapp/ui -Iinclude/breadbox -Iplatform/3ds \
 -Icore/deps/libretro-common/include tests/update/runtime.c app/services/update_model.c app/services/update_phase.c app/ui/safefile.c \
 $(pkg-config --cflags --libs libcurl) -lcrypto -lm -o "$tmp/runtime"
(cd "$tmp" && BB_DRAIN_REPRO=gate ./runtime http://127.0.0.1/ && BB_DRAIN_REPRO=1 ./runtime http://127.0.0.1/ && BB_ROBUST=1 python3 "$OLDPWD/tests/update/server.py" ./runtime && python3 "$OLDPWD/tests/update/server.py" ./runtime)
done

# Compile the actual release branches (no developer URL), preserving curl's HTTPS
# protocols and TLS checks while CONNECT_TO routes hosts to an ephemeral fixture.
cc -std=gnu11 -Wall -Wextra -Werror -Wno-misleading-indentation -Wno-unused-function -Wno-unused-variable \
 -DBB_UPDATE_RELEASE_TEST -DBB_UPDATE_HOST -fsanitize=address,undefined -g \
 -ffunction-sections -fdata-sections -Wl,--gc-sections -Wl,--wrap=fopen -Itests/app/host -Iapp/ui -Iinclude/breadbox -Iplatform/3ds \
 -Icore/deps/libretro-common/include tests/update/runtime.c app/services/update_model.c app/services/update_phase.c app/ui/safefile.c \
 $(pkg-config --cflags --libs libcurl) -lcrypto -lm -o "$tmp/release-runtime"
(cd "$tmp" && python3 "$OLDPWD/tests/update/release_server.py" ./release-runtime)
