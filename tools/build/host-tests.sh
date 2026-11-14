#!/usr/bin/env bash
# Every host check that needs no console, game image, BIOS or private file (CI runs this).
# They build the real frontend/core sources with ASan/UBSan against small fakes of the
# 3DS SDK; they do not run on the 3DS and cannot certify GPU, SOC or APT timing, or the
# real worker threads. What each one executes is described in docs/building.md.
#
# Usage: bash tools/build/host-tests.sh [name-filter]
# Needs: cc, python3, pkg-config, FreeType, libpng, libcurl, zlib, OpenSSL,
# ImageMagick (`magick` or `convert`). Logs go to $HOST_TEST_OUT (default: a temp dir).
# Each group gets its own empty TMPDIR and XDG_CACHE_HOME under it and keeps its
# temporary files there (tests/common/test_tmpdir.h for C); none writes to the home
# folder. Text widths use the bundled tests/fonts/DejaVuSans.ttf, not a system font.
set -uo pipefail
cd "$(dirname "$0")/../.."
out=${HOST_TEST_OUT:-$(mktemp -d "${TMPDIR:-/tmp}/psx3d-host-tests.XXXXXX")}
mkdir -p "$out"
if ! command -v magick >/dev/null 2>&1; then   # ImageMagick 6 only ships `convert`
	mkdir -p "$out/bin"
	cat > "$out/bin/magick" <<'SH'
#!/bin/sh
case "${1:-}" in
identify) shift; exec identify "$@" ;;
convert) shift ;;
esac
exec convert "$@"
SH
	chmod +x "$out/bin/magick"
	export PATH="$out/bin:$PATH"
fi
filter=${1:-}
passed=0
failed=()
skipped=()
# Exit 77 is a missing prerequisite, never a pass.

# The gcc-12 AddressSanitizer in the pinned image crashes at random (an endless stream of
# "AddressSanitizer:DEADLYSIGNAL") on kernels with high-entropy ASLR (vm.mmap_rnd_bits=32:
# Ubuntu 24.04 runners, recent desktop kernels). Run the tests without ASLR where that is
# allowed. Docker's default seccomp profile refuses it; there the workflow lowers
# vm.mmap_rnd_bits on the host instead.
norandom=()
if setarch "$(uname -m)" -R true 2>/dev/null; then norandom=(setarch "$(uname -m)" -R); fi

run() { # run <name> <command...>
	local name=$1 start=$SECONDS
	shift
	[[ -z $filter || $name == *$filter* ]] || return 0
	# A crashing sanitizer handler can loop forever: cap the log (100 MB) instead of the disk.
	local scratch="$out/scratch/$name"
	rm -rf "$scratch"; mkdir -p "$scratch/tmp" "$scratch/cache"
	local rc=0
	(ulimit -f 204800; export TMPDIR="$scratch/tmp" XDG_CACHE_HOME="$scratch/cache"
		"${norandom[@]}" "$@") > "$out/$name.log" 2>&1 || rc=$?
	rm -rf "$scratch"
	if [[ $rc == 0 ]]; then
		passed=$((passed + 1))
		printf 'PASS %3ds  %s\n' $((SECONDS - start)) "$name"
	else
		if [[ $rc == 77 ]]; then
			skipped+=("$name")
			printf "SKIP %3ds  %s\n" $((SECONDS - start)) "$name"
			cat "$out/$name.log"
			return 0
		fi
		failed+=("$name")
		printf 'FAIL %3ds  %s\n' $((SECONDS - start)) "$name"
		tail -n 40 "$out/$name.log" | sed 's/^/     | /'
	fi
}

# Frontend code against a fake SDK: menus, settings, input, config, presets, widescreen, clips.
run auto-update bash tests/update/run.sh
run share-room python3 tests/share_room/check.py
run art-smdh24 python3 tests/art/smdh24.py
run shelf-input python3 tests/room/input.py
run shelf-preload python3 tests/room/preload.py
run shelf-identity python3 tests/room/identity.py
run room-spines python3 tests/room/spines.py
run shelf-camera python3 tests/room/camera.py
run room-restore python3 tests/room/restore.py
run room-tv python3 tests/room/tv.py
run room-stops python3 tests/room/stops.py
run room-build-sh bash tests/room/build-sh.sh
run room-missing python3 tests/room/missing.py
run room-pace python3 tests/room/pace.py
run room-loaders python3 tests/room/loaders.py
run room-refine python3 tests/room/refine.py
run room-light-wiring python3 tests/room/light-wiring.py
run ui-fit bash -c 'cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Iinclude/breadbox tests/ui_fit/check.c -o "$1/ui-fit" && "$1/ui-fit"' _ "$out"
run ui-strings python3 tests/ui_fit/strings.py
run welcome bash -c 'cc -D_GNU_SOURCE -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Iinclude/breadbox -Iapp/ui tests/welcome/check.c app/ui/welcome.c -o "$1/welcome" && "$1/welcome"' _ "$out"
run ui-sound python3 tests/ui_sound/check.py
run postcard-text python3 tests/postcards/text_fit.py
run continue-card python3 tests/postcards/continue_card.py
run place-cards python3 tests/place_cards/check.py
run card-headers python3 tests/ui_consistency/card_headers.py
run update-lines     python3 tests/ui_fit/update_lines.py
run room-refresh bash -c 'cc -Wall -Wextra -Werror -Iapp/ui tests/room/refresh.c -o "$1/room-refresh" && "$1/room-refresh"' _ "$out"
run ui-mark bash -c 'python3 tools/ui/mark_masks.py --check && cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Iinclude/breadbox tests/ui_mark/check.c -lm -o "$1/ui-mark" && "$1/ui-mark"' _ "$out"
run ui-paper bash -c 'cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Iinclude/breadbox tests/ui_paper/check.c -lm -o "$1/ui-paper" && "$1/ui-paper"' _ "$out"
run text-baseline bash tests/text_baseline/run.sh
run play-controls    bash tests/play_controls/run.sh
run ps1-messages     python3 tests/ps1_messages/check.py
run ps1-save-osd     python3 tests/ps1_messages/osd_saved.py
run ps1-card-close   python3 tests/ps1_card_question/check.py
run parity           bash tests/parity/run.sh
run retro-quicknes   bash tests/retro_host/quicknes.sh
run retro-tooling    python3 tests/retro_host/tooling.py
run retro-host       bash tests/retro_host/run.sh
run shell-guard      python3 tools/architecture/shell-guard.py --strict
run ownership-guard  python3 tools/architecture/ownership-guard.py --strict
run ownership-guard-controls python3 tests/ownership/check.py
run main-lifecycle python3 tests/main_lifecycle/check.py
run docs-current     python3 tests/docs_current/check.py
run build-contract   python3 tests/layout_baseline/build_contract.py
run repo-hygiene     python3 tests/repo_hygiene/check.py
run system-interface bash tests/system_iface/run.sh
run system-conformance env BB_EXPERIMENTAL_NES=1 bash tests/system_conformance/run.sh
run arena            bash tests/arena/run.sh
run system-build     python3 tests/system_build/check.py
run app-targets      bash tests/app_targets/run.sh
run ui-consistency   python3 tests/ui_consistency/check.py
run pause-shared     python3 tests/ui_consistency/pause_shared.py
run ps1-continue     python3 tests/save_service/ps1_continue_question.py
run ps1-back         python3 tests/save_service/ps1_back.py
run ui-depth         python3 tests/ui_consistency/depth.py
run ui-list-input    python3 tests/ui_consistency/input.py
run system-3d        bash -c 'cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined tests/system_3d/check.c -o "$0/system-3d" && "$0/system-3d"' "$out"
run psx3d-import bash tests/psx3d_import/run.sh
run save-service bash tests/save_service/run.sh
run save-slots   bash tests/save_slots/run.sh
run slot-map      bash tests/slot_map/run.sh
run log-ring       bash tests/log_ring/run.sh
run system-switch bash tests/system_switch/run.sh
run gba-save-safety  python3 tests/gba_saves/run.py
run gba-backup-bound bash tests/gba_history/run.sh
run gba-runtime      python3 tests/gba/run.py
run gba-speed        bash -c 'cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Itests/app/host -Iapp/ui -Iinclude/breadbox -Iplatform/3ds -Isystems/gba -Isystems/common -Icore/deps/libretro-common/include tests/gba_speed/check.c -o "$0/gba-speed" && "$0/gba-speed"' "$out"
run gba-presentation python3 tests/gba/presentation.py
run gba-latch       python3 tests/gba/latch.py
run gba-alternation python3 tests/gba/alternation.py
run gba-upload       bash tests/gba_upload/run.sh
run gba-indexed      bash tests/gba_indexed/run.sh
run gba-journal      bash tests/gba_journal/run.sh
run gba-bitmap       bash tests/gba_bitmap/run.sh
run gba-bitmap-effects bash tests/gba_bitmap_effects/run.sh
run gba-blend        python3 tests/gba_blend/check.py
run gba-jit          python3 tests/gba/jit.py
run gba-prefetch     python3 tests/gba/prefetch.py
run gba-vram-smc     python3 tests/gba/vram-smc.py
run gba-core-fixes  python3 tests/gba/core-fixes.py
run gba-stereo       bash tests/gba_stereo/run.sh
run gba-observation  python3 tests/gba/hw1.py
run gba-remote       python3 tests/gba/remote.py
run shared-library   bash tests/bb_library/run.sh
run gba-cart         bash tests/gba_cart/run.sh
run gba-identity     bash tests/gba_identity/run.sh
run bb-disc          bash tests/bb_disc/run.sh
run shared-index     bash tests/bb_index/run.sh
run bb-formats       bash tests/bb_formats/run.sh
run ui-format        bash -c 'cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Iinclude/breadbox tests/ui_format/check.c -o "$0/ui-format" && "$0/ui-format"' "$out"
run folder-picker    bash -c 'cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Iinclude/breadbox tests/ui_format/folder_peek.c app/ui/folder_peek.c -o "$0/folder-picker" && "$0/folder-picker"' "$out"
run config-reset     bash tests/config_reset/run.sh
run controls-rows    bash tests/bb_controls_rows/run.sh
run credits          python3 tests/bb_credits/check.py
run gba-covers       bash tests/gba_covers/run.sh
run gba-library      python3 tests/gba_library/run.py
run frontend-ui      env PSX3D_MOCKUPS="$out/mockups" bash tests/app/host/run.sh
run theme-scenes     python3 tests/themes/run.py
run theme-values     python3 tests/themes/values.py
run shelf-carton     bash -c 'cc -std=c11 -Wall -Wextra -Werror tests/shelf_carton/check.c -lm -o "$0/shelf-carton" && "$0/shelf-carton"' "$out"
run shelf            bash tests/shelf/run.sh
run scene-batch      bash tests/scene_batch/run.sh
run room             bash -c 'cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Iapp/ui -Iinclude/breadbox -Iplatform/3ds tests/room/check.c app/ui/room_bbr.c -lm -o "$0/room" && "$0/room" && python3 tests/room/transition.py && python3 tests/room/swap-sites.py && python3 tests/room/box-light.py' "$out"
run room-walls       bash -c 'cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Iapp/ui -Iinclude/breadbox -Iplatform/3ds tests/room/walls.c app/ui/room_board.c app/ui/room_bbr.c -lm -o "$0/room-walls" && "$0/room-walls"' "$out"
run hands            bash -c 'cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Iapp/ui -Iinclude/breadbox -Iplatform/3ds tests/room/hands.c app/ui/hands.c -lm -o "$0/hands" && "$0/hands" && python3 tests/room/hands-sleeve.py' "$out"
run turn-loop        bash -c 'cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -Iapp/ui -Iinclude/breadbox -Iplatform/3ds tests/room/turn_loop.c app/ui/hands.c -lm -o "$0/turn-loop" && "$0/turn-loop"' "$out"
run settings-b       bash tests/settings_b/run.sh
run settings-ui      env PSX3D_SETTINGS_OUTPUT="$out/settings" python3 tests/settings/run.py
run settings-spec    env SPEC_DUMP="$out/settings-menu.tsv" bash tests/settings/spec.sh
run renderer-config  bash tests/renderer_config.sh
run widescreen       bash tests/widescreen/run.sh
run presets          bash tests/presets/run.sh
run presets-update   bash tests/app/presetupdate/run.sh
run statsview        bash -c 'cc -std=c99 -Wall -Wno-unused-parameter -Wno-unused-function -fsanitize=address,undefined -g -Itests/app/host/skip -Iinclude/breadbox tests/statsview/check.c -o "$0/statsview" && "$0/statsview" > /dev/null' "$out"
run stereo-flat     bash -c 'cc -std=gnu11 -Wall -Wextra -fsanitize=address,undefined -g -Iapp/ui -Iinclude/breadbox -Iplatform/3ds tests/stereo/flat.c systems/ps1/gpustereo_core.c -lm -o "$0/stereo-flat" && "$0/stereo-flat"' "$out"
run stereo-exact    bash -c 'cc -std=gnu11 -Wall -Wextra -fsanitize=address,undefined -g -Iapp/ui -Iinclude/breadbox -Iplatform/3ds tests/stereo/exact.c systems/ps1/gpustereo_core.c -lm -o "$0/stereo-exact" && "$0/stereo-exact"' "$out"
run audio-resampler  bash -c 'cc -std=gnu11 -Wall -Wextra -fsanitize=address,undefined -g tests/audio_resample_test.c platform/3ds/audio_resample.c platform/3ds/audio_tsm.c -lm -o "$0/audio-resampler" && "$0/audio-resampler"' "$out"
run input-replay     env BUILD_DIR="$out/clips" bash tools/input-replay/check.sh
run frameskip        bash tools/frameskip/check.sh
# Developer server/updater protocol and the cover downloader worker.
run dev-server       bash tests/app/dev/run.sh
run sd-throughput    bash tests/sd_bench/run.sh
run covers           bash tests/app/covers/run.sh
run boxart           bash tests/app/boxart/run.sh
run library          bash tests/app/library/run.sh
run save-safety      bash tests/app/save/run.sh
run save-names       bash tests/app/saveid/run.sh
run c3d-abi          bash tests/c3d_abi/run.sh
run covers-worker    python3 tests/app/covers/worker.py
run covers-rebuild   bash tests/app/covers/rebuild.sh
run core-microtests bash tests/core/run.sh
run title-version   python3 tests/title_version/check.py
# Core/renderer/video code extracted from the sources and run on the host.
run video-scanout    python3 tests/app/vout/check.py
run display-fit      bash tests/app/display/run.sh
run video-teardown   python3 tools/frontend-overhead/library_check.py
run gpu-reference    env BUILD_DIR="$out/gpu" bash tools/gpu-compare/regress.sh
run pica-layout      env PICA_LAYOUT_CHECK_OUT="$out/pica-layout" bash tools/pica-layout/check.sh

echo
echo "$passed passed, ${#failed[@]} failed, ${#skipped[@]} skipped (logs: $out)"
(( passed + ${#failed[@]} + ${#skipped[@]} )) || { echo "no host test group matches '$filter'" >&2; exit 2; }
[[ ${#failed[@]} -eq 0 ]] || { echo "failed: ${failed[*]}"; exit 1; }

if (( ${#skipped[@]} )); then
 echo "skipped suites fail this run: ${skipped[*]}" >&2
 exit 1
fi
