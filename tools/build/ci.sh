#!/usr/bin/env bash
# What CI runs, inside the pinned container (see .github/workflows/build.yml):
#
#   tools/build/container.sh bash tools/build/ci.sh [toolchain|build|baseline|layout|host-tests|all]
#
#   toolchain   the installed devkitPro packages match tools/build/toolchain.lock
#   build       3DSX, ELF and CIA, plus the identity record (section hashes, CXI hash)
#   layout      tools/layout/check.sh (a grown frontend must not move the emulator core) and
#               baseline-check.sh (frame code and core match the accepted build)
#   baseline    required public reference comparison (optional candidate ELF argument)
#   host-tests  tools/build/host-tests.sh: every check that needs no ROM, BIOS or console
#
# Needs no secrets and no game images. Results go to $CI_OUT (default ./artifacts).
set -euo pipefail
cd "$(dirname "$0")/../.."
step=${1:-all}
jobs=${BB_BUILD_JOBS:-3}
[[ $jobs =~ ^[1-9][0-9]*$ ]] || { echo 'ci.sh: BB_BUILD_JOBS must be a positive integer' >&2; exit 2; }
export BB_BUILD_JOBS=$jobs
name=BreadBox
out=${CI_OUT:-$PWD/artifacts}
export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-$(git log -1 --format=%ct)}   # fixes __DATE__/__TIME__
[[ $SOURCE_DATE_EPOCH =~ ^[0-9]+$ ]] || { echo "ci.sh: SOURCE_DATE_EPOCH is not set and git has no commit time" >&2; exit 1; }
commit=${PSX3D_COMMIT:-$(git rev-parse HEAD)}
mkdir -p "$out"

toolchain() {
	python3 tools/build/toolchain.py check
	# The Dockerfile pins the same image as the lock.
	lock=$(awk '$1 == "image" { print $2 }' tools/build/toolchain.lock)
	docker=$(sed -n 's/^FROM \(.*\)$/\1/p' tools/build/Dockerfile)
	[[ $lock == "$docker" ]] || { echo "Dockerfile FROM ($docker) differs from toolchain.lock ($lock)" >&2; exit 1; }
}

build() {
	make -j"$jobs"
	# The CIA contains a randomized ticket, so two CIAs of one build differ. The CXI (the
	# executable container inside it) is deterministic: compare that and the ELF sections.
	make -j"$jobs" cxi BANNERTOOL="$(command -v bannertool)" MAKEROM="$(command -v makerom)"
	cp build/3ds/$name.3dsx build/3ds/$name.cia build/3ds/$name.cxi build/3ds/$name.elf "$out/"
	python3 -I tools/build/elf-identity.py build/3ds/$name.elf > "$out/identity.txt"
	(cd "$out" && sha256sum "$name.3dsx" "$name.cia" "$name.elf" "$name.cxi" > SHA256SUMS)
	{ echo "commit $commit"; echo "SOURCE_DATE_EPOCH $SOURCE_DATE_EPOCH"; grep '^package' tools/build/toolchain.lock | awk '{print $2, $3}'; } > "$out/BUILD-INFO.txt"
	cat "$out/identity.txt"
}

baseline() { BASELINE_JOBS=${BASELINE_JOBS:-$jobs} bash tools/layout/baseline-check.sh "${1:-build/3ds/BreadBox.elf}"; }

layout() {
	baseline "${1:-build/3ds/BreadBox.elf}"
	# Core must not move under frontend growth, and the frame code must match the accepted
	# build (tools/layout/baseline.json; the pinned public archive is built when needed).
	# No step is optional.
	if [[ ${BB_LAYOUT_TEST:-0} != 1 ]]; then python3 tests/layout_baseline/check.py; fi
	bash tools/layout/check.sh
	python3 tools/layout/test-pinned.py build/3ds/BreadBox.elf build/3ds/BreadBox.elf build/3ds/build-breadbox
	python3 tools/layout/test-page-only.py
}
host_tests() { HOST_TEST_OUT="$out/host-tests" bash tools/build/host-tests.sh; }

case $step in
toolchain) toolchain ;;
build) build ;;
baseline) baseline "${2:-build/3ds/BreadBox.elf}" ;;
layout) layout "${2:-build/3ds/BreadBox.elf}" ;;
host-tests) host_tests ;;
all) toolchain; build; layout; host_tests ;;
*) echo "usage: ci.sh [toolchain|build|baseline|layout|host-tests|all]" >&2; exit 2 ;;
esac
