#!/usr/bin/env bash
# Fast-forward trunk to a branch, but only a branch whose head passed the trunk gate.
#
#   tools/build/merge-to-trunk.sh <branch>     run from the main checkout, on a clean trunk
#
# Refuses unless: the current branch is trunk and clean, the branch exists, trunk is an ancestor of
# the branch (a fast-forward; rebase the branch first otherwise), and the branch head has a green
# line from tools/build/trunk-gate.sh. Then it runs `git merge --ff-only`. It does not delete the
# branch or its worktree.
#
# TRUNK_BRANCH overrides the trunk name.
set -euo pipefail
cd "$(dirname "$0")/../.."
trunk=${TRUNK_BRANCH:-trunk}
[[ $# -eq 1 ]] || { echo "usage: merge-to-trunk.sh <branch>" >&2; exit 2; }
branch=$1
refuse() { echo "merge-to-trunk: $*" >&2; exit 1; }

[[ $(git rev-parse --abbrev-ref HEAD) == "$trunk" ]] || refuse "not on $trunk"
[[ -z $(git status --porcelain) ]] || refuse "$trunk is not clean"
[[ $branch != "$trunk" ]] || refuse "the branch is $trunk itself"
head=$(git rev-parse --verify --quiet "refs/heads/$branch^{commit}") || refuse "no branch $branch"
git merge-base --is-ancestor "$trunk" "$head" || refuse "$branch is not a fast-forward of $trunk; rebase it first (and gate it again)"
bash tools/build/trunk-gate.sh --check "$head" || refuse "$branch head $head has no green trunk-gate line"
git merge --ff-only "$branch"
echo "merge-to-trunk: $trunk is now at $(git rev-parse --short HEAD)"
