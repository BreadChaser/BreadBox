#!/usr/bin/env bash
# Ledger, --check and refusal logic of tools/build/trunk-gate.sh and merge-to-trunk.sh, in a
# throwaway git repository with a stubbed CI command (no build runs here).
set -euo pipefail
here=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/trunk-gate.XXXXXX")
trap 'rm -rf "$work"' EXIT
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_SYSTEM=/dev/null
export GIT_AUTHOR_NAME=t GIT_AUTHOR_EMAIL=t@example.invalid GIT_COMMITTER_NAME=t GIT_COMMITTER_EMAIL=t@example.invalid
unset TRUNK_GATE_LEDGER TRUNK_GATE_CI TRUNK_BRANCH

repo=$work/repo
git init -q -b trunk "$repo"
cd "$repo"
mkdir -p tools/build
cp "$here/tools/build/trunk-gate.sh" "$here/tools/build/merge-to-trunk.sh" tools/build/
printf '/.trunk-gate.jsonl\n' > .gitignore
git add -A && git commit -qm base

fail() { echo "FAIL $*" >&2; exit 1; }
ok() { echo "ok   $*"; }
must_fail() { local what=$1; shift; if "$@" >/dev/null 2>&1; then fail "$what"; fi; ok "$what"; }
must_pass() { local what=$1; shift; "$@" >/dev/null 2>&1 || fail "$what"; ok "$what"; }
commit() { echo "$1" >> f.txt; git add f.txt; git commit -qm "$1"; }
gate() { TRUNK_GATE_CI=$1 bash tools/build/trunk-gate.sh; }
check() { bash tools/build/trunk-gate.sh --check "$1"; }

base=$(git rev-parse HEAD)
must_fail "a commit with no ledger line is not green" check "$base"
must_fail "an unknown commit is refused" check 0123456789abcdef0123456789abcdef01234567

must_pass "a green stub run succeeds" gate true
must_pass "it leaves a green line for HEAD" check "$base"
line=$(tail -n 1 .trunk-gate.jsonl)
for key in sha tree date result; do [[ $line == *"\"$key\":"* ]] || fail "ledger line has $key"; done
[[ $line == *"\"tree\":\"$(git rev-parse 'HEAD^{tree}')\""* ]] || fail "ledger tree is HEAD's tree"
ok "the ledger line holds sha, tree, date and result"
[[ -z $(git status --porcelain) ]] || fail "the ledger is gitignored"
ok "the ledger stays out of git"

echo dirty > scratch.txt
must_fail "a dirty tree is refused" gate true
rm -f scratch.txt

# A red run is recorded and does not count as green.
git checkout -q -b red-branch
commit red
red=$(git rev-parse HEAD)
must_fail "a red stub run fails" gate false
grep -Fq "\"sha\":\"$red\"" .trunk-gate.jsonl || fail "red run recorded"
grep -F "\"sha\":\"$red\"" .trunk-gate.jsonl | grep -Fq '"result":"red"' || fail "red result written"
must_fail "a red line is not green" check "$red"

# Merging: trunk is only fast-forwarded to a gated branch.
merge() { bash tools/build/merge-to-trunk.sh "$@"; }
must_fail "no argument is refused" merge
must_fail "a missing branch is refused" merge nope
git checkout -q red-branch
must_fail "merging while off trunk is refused" merge red-branch
git checkout -q trunk
must_fail "a branch with only a red line is refused" merge red-branch
must_fail "trunk as the branch is refused" merge trunk

git checkout -q -b green-branch trunk
commit green
git checkout -q trunk
must_fail "an ungated branch is refused" merge green-branch
git checkout -q green-branch
must_pass "the gate goes green on the branch head" gate true
git checkout -q trunk
echo dirty > scratch.txt
must_fail "a dirty trunk is refused" merge green-branch
rm -f scratch.txt
before=$(git rev-parse HEAD)
[[ $before == "$base" ]] || fail "trunk did not move on refusals"
must_pass "a gated fast-forward branch merges" merge green-branch
[[ $(git rev-parse HEAD) == "$(git rev-parse green-branch)" ]] || fail "trunk is at the branch head"
ok "trunk moved to the branch head"

# Gated but no longer a fast-forward: trunk moved on.
git checkout -q -b stale-branch "$base"
commit stale
must_pass "the stale branch is gated" gate true
git checkout -q trunk
commit later
must_fail "a gated branch that is not a fast-forward is refused" merge stale-branch

# A line for one commit never vouches for another (even with the same tree).
git checkout -q -b twin trunk
git commit -q --allow-empty -m "same tree, new commit"
must_fail "a same-tree commit needs its own line" merge twin
echo "trunk gate controls PASS"
