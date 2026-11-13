#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
export BB_EXPERIMENTAL_NES=1
python3 tests/system_conformance/check.py "$@"
