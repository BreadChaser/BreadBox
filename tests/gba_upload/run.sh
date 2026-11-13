#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
bb_upload_tmp=$(mktemp -d)
trap 'rm -rf "$bb_upload_tmp"' EXIT
cc -O2 -Wall -Wextra -fsanitize=address,undefined tests/gba_upload/check.c -o "$bb_upload_tmp/check"
"$bb_upload_tmp/check"
