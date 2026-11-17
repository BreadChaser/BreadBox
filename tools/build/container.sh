#!/usr/bin/env bash
# Runs a command in the pinned build environment (tools/build/Dockerfile), with the
# repository mounted at /work. Without a command it opens a shell.
#
#   tools/build/container.sh make -j8
#   tools/build/container.sh bash tools/build/ci.sh all
#
# Uses Docker when the daemon is usable. Otherwise, if bubblewrap and unprivileged user
# namespaces are available (no root, no daemon), it unpacks the same image by digest from
# the registry (verified blob by blob) into ~/.cache/psx3d-build/<digest>, provisions it
# once with tools/build/provision.sh and runs the command in that. PSX3D_CONTAINER=docker
# or =bwrap forces one; PSX3D_ROOTFS overrides the unpack directory.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
image=$(awk '$1 == "image" { print $2 }' "$here/toolchain.lock")
digest=${image#*@sha256:}
engine=${PSX3D_CONTAINER:-auto}
if [[ $engine == auto ]]; then
	if docker info >/dev/null 2>&1; then engine=docker
	elif command -v bwrap >/dev/null 2>&1; then engine=bwrap
	else echo "container.sh: needs a usable Docker daemon or bubblewrap" >&2; exit 2; fi
fi
[[ $# -gt 0 ]] || set -- bash

# Computed here: a git worktree's .git file points outside the mounted directory.
export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-$(git -C "$root" log -1 --format=%ct 2>/dev/null || true)}
export PSX3D_COMMIT=${PSX3D_COMMIT:-$(git -C "$root" rev-parse HEAD 2>/dev/null || echo unknown)}
# The build ID the remote status reports (devcontrol.o); git cannot run inside the container.
export PSX3D_DESCRIBE=${PSX3D_DESCRIBE:-$(git -C "$root" describe --always --abbrev=12 --dirty 2>/dev/null || true)}

# In a linked git worktree `.git` is a file that points outside the mounted tree. Mount the
# worktree's git directory and the shared one (read-only, same paths) so git works inside.
gitdirs=()
if [[ -f $root/.git ]]; then
	wt=$(sed -n 's/^gitdir: //p' "$root/.git")
	if [[ -d $wt ]]; then gitdirs=("$wt" "$(cd "$wt" && cd "$(cat commondir)" && pwd)"); fi
fi
gitmounts=()
for d in "${gitdirs[@]}"; do gitmounts+=(-v "$d:$d:ro"); done

# CI settings a caller (or the workflow's env) may set; passed through only when set.
passenv=()
for var in BB_BUILD_JOBS BASELINE_BUILD_REFERENCE BASELINE_JOBS HOST_TEST_JOBS; do
	[[ -n ${!var+x} ]] && passenv+=("$var")
done

case $engine in
docker)
	tag=psx3d-build:$(cat "$here/Dockerfile" "$here/provision.sh" "$here/toolchain.lock" "$here/makerom-version-fix.patch" | sha256sum | cut -c1-12)
	docker image inspect "$tag" >/dev/null 2>&1 || docker build -t "$tag" -f "$here/Dockerfile" "$here"
	tty=(); [[ -t 0 && -t 1 ]] && tty=(-t)
	exec docker run --rm -i "${tty[@]}" --user "$(id -u):$(id -g)" -e HOME=/tmp -e SOURCE_DATE_EPOCH \
		-e PSX3D_COMMIT -e PSX3D_DESCRIBE "${passenv[@]/#/--env=}" "${gitmounts[@]}" -v "$root:/work" -w /work "$tag" "$@"
	;;
bwrap)
	rootfs=${PSX3D_ROOTFS:-${XDG_CACHE_HOME:-$HOME/.cache}/psx3d-build/$digest}
	sandbox=(bwrap --unshare-user --uid 0 --gid 0 --unshare-pid --unshare-ipc --unshare-uts --die-with-parent
		--bind "$rootfs" / --dev /dev --proc /proc --tmpfs /tmp --ro-bind /etc/resolv.conf /etc/resolv.conf
		--setenv HOME /root --setenv LANG en_US.UTF-8 --setenv DEVKITPRO /opt/devkitpro
		--setenv DEVKITARM /opt/devkitpro/devkitARM --setenv PSX3D_TOOLS /opt/psx3d-tools		--setenv PATH /opt/psx3d-tools/bin:/opt/devkitpro/tools/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin)
	if [[ ! -e $rootfs/.unpacked ]]; then
		rm -rf "$rootfs"
		mkdir -p "$(dirname "$rootfs")"
		python3 -I "$here/rootfs.py" "$image" "$rootfs"
		touch "$rootfs/.unpacked"
	fi
	provision_hash=$(cat "$here/provision.sh" "$here/toolchain.lock" "$here/makerom-version-fix.patch" | sha256sum | cut -d' ' -f1)
	if [[ ! -e $rootfs/.provisioned || $(cat "$rootfs/.provisioned") != "$provision_hash" ]]; then
		# A user namespace maps only one id, so the packages' chown calls to other ids fail.
		# Nothing here needs them: ignore those errors while installing, then restore the tools.
		"${sandbox[@]}" --ro-bind "$here" /mnt/build bash -c '
			for t in chown chgrp; do
				mv "/usr/bin/$t" "/usr/bin/$t.real"
				printf "#!/bin/sh\n/usr/bin/$t.real \"\$@\" || true\n" > "/usr/bin/$t"
				chmod +x "/usr/bin/$t"
			done
			status=0
			bash /mnt/build/provision.sh /mnt/build/toolchain.lock || status=$?
			for t in chown chgrp; do mv "/usr/bin/$t.real" "/usr/bin/$t"; done
			exit $status'
		printf '%s\n' "$provision_hash" > "$rootfs/.provisioned"
	fi
	sandbox+=(--setenv SOURCE_DATE_EPOCH "$SOURCE_DATE_EPOCH" --setenv PSX3D_COMMIT "$PSX3D_COMMIT" --setenv PSX3D_DESCRIBE "$PSX3D_DESCRIBE")
	for var in "${passenv[@]}"; do sandbox+=(--setenv "$var" "${!var}"); done
	for d in "${gitdirs[@]}"; do sandbox+=(--ro-bind "$d" "$d"); done
	exec "${sandbox[@]}" --bind "$root" /work --chdir /work "$@"
	;;
*)
	echo "container.sh: unknown PSX3D_CONTAINER=$engine" >&2; exit 2 ;;
esac
