#!/usr/bin/env bash
# Run fixed full CI and record its inputs/results; --check requires an authoritative receipt.
# TRUNK_GATE_CI produces test receipts only. TRUNK_GATE_LEDGER selects a private ledger.
set -euo pipefail
cd "$(dirname "$0")/../.."
exec python3 tools/build/gate-record.py "$@"
