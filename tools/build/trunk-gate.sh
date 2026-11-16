#!/usr/bin/env bash
# The trunk gate: record that the full CI ran green on an exact commit.
#
#   tools/build/trunk-gate.sh            run `ci.sh all` in the container for HEAD and append
#                                        {sha, tree, date, result} to the ledger (.trunk-gate.jsonl)
#   tools/build/trunk-gate.sh --check S  exit 0 only if commit S has a green ledger line
#
# The tree must be clean, so the line describes the commit and nothing else. The ledger is
# gitignored; it is written in this checkout and read from every worktree of the repository, so a
# branch gated in its worktree can be merged from the trunk checkout (tools/build/merge-to-trunk.sh).
# A red run is recorded too (and does not satisfy --check).
#
# TRUNK_GATE_CI replaces the CI command (the host test uses a stub); TRUNK_GATE_LEDGER the ledger path.
# Slow: the real run is the whole build, layout gates and host suite (about an hour).
set -euo pipefail
cd "$(dirname "$0")/../.."
ledger_name=.trunk-gate.jsonl
ci=${TRUNK_GATE_CI:-tools/build/container.sh bash tools/build/ci.sh all}

ledgers() { # every ledger file of this repository's worktrees
	if [[ -n ${TRUNK_GATE_LEDGER:-} ]]; then echo "$TRUNK_GATE_LEDGER"; return; fi
	git worktree list --porcelain | sed -n 's/^worktree //p' | while read -r dir; do
		[[ -f $dir/$ledger_name ]] && echo "$dir/$ledger_name"
	done
	return 0
}

if [[ ${1:-} == --check ]]; then
	[[ $# -eq 2 ]] || { echo "usage: trunk-gate.sh --check <commit>" >&2; exit 2; }
	sha=$(git rev-parse --verify --quiet "$2^{commit}") || { echo "trunk-gate: unknown commit $2" >&2; exit 2; }
	while read -r file; do
		if grep -F "\"sha\":\"$sha\"" "$file" | grep -Fq '"result":"green"'; then
			echo "trunk-gate: $sha has a green line"; exit 0
		fi
	done < <(ledgers)
	echo "trunk-gate: no green line for $sha; run tools/build/trunk-gate.sh on it" >&2
	exit 1
fi
[[ $# -eq 0 ]] || { echo "usage: trunk-gate.sh [--check <commit>]" >&2; exit 2; }

[[ -z $(git status --porcelain) ]] || { echo "trunk-gate: the tree is not clean; commit or shelve first" >&2; exit 1; }
sha=$(git rev-parse HEAD)
tree=$(git rev-parse 'HEAD^{tree}')
result=green
bash -c "$ci" || result=red
file=${TRUNK_GATE_LEDGER:-$PWD/$ledger_name}
printf '{"sha":"%s","tree":"%s","date":"%s","result":"%s"}\n' "$sha" "$tree" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$result" >> "$file"
echo "trunk-gate: $sha $result"
[[ $result == green ]]
