#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
python3 "$ROOT/tests/app/dev/switch_file.py"
cc -D_GNU_SOURCE -std=gnu11 -Wall -Wextra -Werror -Wno-format-truncation -fsanitize=address,undefined \
  -I"$ROOT/core/deps/libretro-common/include" -g "$ROOT/tests/app/dev/check.c" -I"$ROOT/tests/app/host/skip" -I"$ROOT/app/ui" -I"$ROOT/include/breadbox" -I"$ROOT/platform/3ds" "$ROOT/systems/ps1/clip.c" "$ROOT/systems/ps1/benchstate.c" "$ROOT/tools/input-replay/mock-core.c" -lz -o "$TMP/check"
(cd "$TMP" && ./check)

cc -D_GNU_SOURCE -std=gnu11 -Wall -Wextra -Werror -Wno-format-truncation -fsanitize=address,undefined \
  -I"$ROOT/core/deps/libretro-common/include" -I"$ROOT/tests/app/dev" -g "$ROOT/tests/app/dev/transport.c" $(pkg-config --cflags --libs libpng) -pthread -I"$ROOT/tests/app/host/skip" -I"$ROOT/app/ui" -I"$ROOT/include/breadbox" -I"$ROOT/platform/3ds" "$ROOT/systems/ps1/clip.c" "$ROOT/systems/ps1/benchstate.c" "$ROOT/tools/input-replay/mock-core.c" -lz -lcrypto -o "$TMP/transport"
# The tracked client and benchmark driver also exercise the real TCP protocol.
HOST_TOOLS=${PSX3D_HOST_TOOLS:-$ROOT/tools/input-replay}
if [[ -f $HOST_TOOLS/psx3dctl.py && -f $HOST_TOOLS/fastbench.py ]]; then
  (cd "$TMP" && python3 "$ROOT/tests/app/dev/client_test.py" "$TMP/transport")
else
  echo "SKIP client_test.py: psx3dctl.py and fastbench.py not found in $HOST_TOOLS (set PSX3D_HOST_TOOLS)"
  exit 77
fi
cc -D_GNU_SOURCE -std=gnu11 -Wall -Wextra -Werror -Wno-format-truncation -fsanitize=address,undefined \
  -I"$ROOT/tests/app/dev" -I"$ROOT/tests/app/host/skip" -I"$ROOT/app/ui" -I"$ROOT/include/breadbox" -I"$ROOT/platform/3ds" -g "$ROOT/tests/app/dev/update.c" -lcrypto -o "$TMP/update-breadbox"
(cd "$TMP" && ./update-breadbox)
