#!/usr/bin/env bash
# Exercise the production gate/merge tools against a disposable command/output fixture.
set -euo pipefail
here=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/trunk-gate.XXXXXX")
trap 'rm -rf "$work"' EXIT
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_SYSTEM=/dev/null
export GIT_AUTHOR_NAME=t GIT_AUTHOR_EMAIL=t@example.invalid GIT_COMMITTER_NAME=t GIT_COMMITTER_EMAIL=t@example.invalid
unset TRUNK_GATE_LEDGER TRUNK_GATE_CI TRUNK_BRANCH
export PYTHONDONTWRITEBYTECODE=1
repo=$work/repo
git init -q -b trunk "$repo"
cd "$repo"
mkdir -p tools/build
cp "$here/tools/build/"{trunk-gate.sh,merge-to-trunk.sh,gate-record.py} tools/build/
printf '/.trunk-gate.jsonl\n/artifacts/\n' > .gitignore
printf 'fixture toolchain\n' > tools/build/toolchain.lock
# The fixed production command runs this repository-local fixture. TRUNK_GATE_CI
# is never used to generate the authoritative positive control.
cat > tools/build/container.sh <<'EOF'
#!/bin/bash
exec "$@"
EOF
chmod +x tools/build/container.sh
cat > tools/build/host-tests.sh <<'EOF'
#!/bin/bash
if [[ $1 == --list ]]; then printf 'one\ntwo\n';exit 0;fi
# These declarations are the fixture's complete suite manifest.
run one true
run two true
EOF
cat > tools/build/ci.sh <<'EOF'
#!/bin/bash
set -eu
[[ $1 == all ]]
mkdir -p artifacts/host-tests
printf 'toolchain\ngates\nbuild\nlayout\nhost-tests\n' > artifacts/ci-phases.jsonl
printf '{"passed":2,"failed":0,"skipped":0,"complete":true,"suites":["one","two"]}' > artifacts/host-tests/summary.json
printf 'commit %s\nSOURCE_DATE_EPOCH %s\n' "$PSX3D_COMMIT" "$SOURCE_DATE_EPOCH" > artifacts/BUILD-INFO.txt
for name in BreadBox.elf BreadBox.cxi identity.txt; do printf '%s fixture\n' "$name" > "artifacts/$name"; done
(cd artifacts && sha256sum BreadBox.elf BreadBox.cxi > SHA256SUMS)
case ${FAULT:-} in
 missing) rm artifacts/BreadBox.elf ;;
 hashes) echo changed >> artifacts/BreadBox.cxi ;;
 epoch) echo wrong > artifacts/BUILD-INFO.txt ;;
 host) printf '{"passed":2,"failed":0,"skipped":0,"complete":true,"suites":["one"]}' > artifacts/host-tests/summary.json ;;
 skip) printf '{"passed":1,"failed":0,"skipped":1,"complete":true,"suites":["one","two"]}' > artifacts/host-tests/summary.json ;;
 layout) sed -i '/layout/d' artifacts/ci-phases.jsonl ;;
 dirty) echo dirty >> f.txt ;;
 index) echo changed > f.txt; git add f.txt ;;
 head) git commit -q --allow-empty -m moved ;;
 false) exit 1 ;;
esac
EOF
: > f.txt
git add -A && git commit -qm base
fail() { echo "FAIL $*" >&2; exit 1; }
ok() { echo "ok   $*"; }
must_fail() { local what=$1; shift; if "$@" >"$work/result" 2>&1; then cat "$work/result";fail "$what";fi;ok "$what"; }
must_pass() { local what=$1; shift; "$@" >"$work/result" 2>&1 || { cat "$work/result";fail "$what"; };ok "$what"; }
commit() { echo "$1" >> f.txt; git add f.txt; git commit -qm "$1"; }
gate() { bash tools/build/trunk-gate.sh; }
check() { bash tools/build/trunk-gate.sh --check "$1"; }
merge() { bash tools/build/merge-to-trunk.sh "$@"; }
base=$(git rev-parse HEAD)
must_fail 'no receipt' check "$base"
must_fail 'unknown commit' check 0123456789abcdef0123456789abcdef01234567
must_pass 'test command succeeds as a test receipt' env TRUNK_GATE_CI=true bash tools/build/trunk-gate.sh
must_fail 'old true-command bypass is refused' check "$base"
python3 - <<'PY'
import json
r=json.loads(open('.trunk-gate.jsonl').readlines()[-1]);assert r['authority']=='test' and r['result']=='green'
PY
must_pass 'fixed full command and complete outputs' gate
must_pass 'complete authoritative receipt' check "$base"
cp .trunk-gate.jsonl "$work/green"
for field in tree toolchain epoch command authority host layout phases artifacts started finished; do
 cp "$work/green" .trunk-gate.jsonl
 FIELD=$field python3 - <<'PY'
import json,os
p='.trunk-gate.jsonl';r=json.loads(open(p).readlines()[-1]);r[os.environ['FIELD']]=None
open(p,'w').write(json.dumps(r)+'\n')
PY
 must_fail "invalid $field" check "$base"
done
cp "$work/green" .trunk-gate.jsonl
must_fail 'later failed run' env FAULT=false bash tools/build/trunk-gate.sh
must_fail 'newer red supersedes green' check "$base"
must_pass 'new green supersedes red' gate
python3 - <<'PYCODE'
import json
p='.trunk-gate.jsonl';r=json.loads(open(p).readlines()[-1]);r['result']='red';r['finished']='2001-01-01T00:00:00+00:00'
with open(p,'a') as f:f.write(json.dumps(r)+'\n')
PYCODE
must_fail 'last line wins even with an older clock' check "$base"
for fault in missing hashes epoch host skip layout dirty index head; do
 cp "$work/green" .trunk-gate.jsonl
 must_fail "run refuses $fault" env FAULT=$fault bash tools/build/trunk-gate.sh
 must_fail "receipt refuses $fault" check "$base"
 git reset --hard -q "$base"
done
cp "$work/green" .trunk-gate.jsonl
echo dirty > scratch.txt
must_fail 'dirty before run' gate
rm scratch.txt
# Legacy receipts remain historical evidence for commits already on trunk only.
legacy() { printf '{"sha":"%s","tree":"%s","date":"2099-01-01T00:00:00Z","result":"green"}\n' "$1" "$(git rev-parse "$1^{tree}")" > .trunk-gate.jsonl; }
legacy "$base"
must_pass 'merged legacy commit remains historical' check "$base"
must_fail 'legacy never authorizes merge' bash tools/build/trunk-gate.sh --check-for-merge "$base"
git checkout -q -b feature
commit feature
head=$(git rev-parse HEAD)
legacy "$head"
must_fail 'unmerged legacy receipt' check "$head"
git checkout -q trunk
must_fail 'legacy merge bypass' merge feature
must_fail 'no merge argument' merge
must_fail 'missing branch' merge missing
must_fail 'trunk itself' merge trunk
git checkout -q feature
must_fail 'off trunk' merge feature
must_pass 'feature test receipt' env TRUNK_GATE_CI=true bash tools/build/trunk-gate.sh
git checkout -q trunk
must_fail 'test receipt cannot merge' merge feature
: > .trunk-gate.jsonl
git checkout -q feature
must_pass 'authoritative feature receipt' gate
git checkout -q trunk
echo dirty > scratch.txt
must_fail 'dirty trunk' merge feature
rm scratch.txt
must_pass 'authoritative fast-forward' merge feature
[[ $(git rev-parse HEAD) == "$head" ]] || fail 'trunk head'
git checkout -q -b stale "$base"
commit stale
must_pass 'stale branch receipt' gate
git checkout -q trunk
must_fail 'non fast-forward' merge stale
git checkout -q -b twin
git commit -q --allow-empty -m twin
twin=$(git rev-parse HEAD)
git checkout -q trunk
must_fail 'same-tree new commit needs its own receipt' merge twin
must_fail 'same-tree check is refused' check "$twin"
# A newer red in another worktree is also decisive.
cp .trunk-gate.jsonl "$work/previous"
git worktree add -q "$work/other" feature
(cd "$work/other" && FAULT=false bash tools/build/trunk-gate.sh >/dev/null 2>&1) && fail 'red run'
must_fail 'newest receipt across worktrees wins' check "$head"
echo 'trunk gate controls PASS'
