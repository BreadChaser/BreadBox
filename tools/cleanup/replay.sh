#!/usr/bin/env bash
# replay.sh <cleanup-ref>: replay the structure cleanup on the checked-out branch as NEW commits
# on top of it. Never replaces a whole file the branch already has: scripted steps rewrite only
# what their rules name, the remaining edits are applied as hunks (git apply --3way, checked
# first), and the plain commits are cherry-picked. Stops at the first conflict with the tree as
# git left it; nothing is committed for a step that did not apply.
#
#   git switch <target>            # clean tree, the branch to receive the cleanup
#   bash <(git show <cleanup-ref>:tools/cleanup/replay.sh) <cleanup-ref>
#
# Afterwards: build, the identity check (see README) and `make host-tests`.
set -euo pipefail
# Run independently after manual conflict resolution too: --check-assertions <cleanup-ref>.
# Compare AST assertions, not lines: formatting and splitting an `and` stay harmless.
check_cleanup_assertions() {
python3 - "$1" <<'PYASSERT'
import ast, collections, pathlib, subprocess, sys
ref=sys.argv[1]
def git(*args):return subprocess.check_output(['git',*args],text=True)
def assertions(source):
    result=collections.Counter()
    def add(node):
        if isinstance(node,ast.BoolOp) and isinstance(node.op,ast.And):
            for item in node.values:add(item)
        else:result[ast.dump(node,include_attributes=False)]+=1
    for node in ast.walk(ast.parse(source)):
        if isinstance(node,ast.Assert):add(node.test)
    return result
# Save ownership changes the loop anchor, while retaining the same release assertion.
old="'release_texture();' in s[s.index('while(aptMainLoop())'):]"
new="'release_texture();' in s[s.index('while(bb_save_runnable() && aptMainLoop())'):]"
key=lambda text:ast.dump(ast.parse(text,mode='eval').body,include_attributes=False)
errors=[];checked=0
for name in git('ls-tree','-r','--name-only',ref,'tests').splitlines():
    if not name.endswith('.py'):continue
    expected=assertions(git('show',ref+':'+name))
    if not expected:continue
    path=pathlib.Path(name)
    actual=assertions(path.read_text()) if path.exists() else collections.Counter()
    if name=='tests/retro_host/ctr_lifecycle.py' and not actual[key(old)] and actual[key(new)]:
        actual[key(old)]+=actual[key(new)]
    missing=expected-actual;checked+=sum(expected.values())
    if missing:errors.append(name+': lost assertions: '+str(dict(missing)))
if errors:sys.exit('replay: assertion preservation FAILED\n'+'\n'.join(errors))
print(f'replay: {checked} cleanup-source Python assertions preserved')
PYASSERT
}
if [ "${1:-}" = --check-assertions ]; then
  cd "$(git rev-parse --show-toplevel)"
  check_cleanup_assertions "${2:?usage: replay.sh --check-assertions <cleanup-ref>}"
  exit
fi
ref=${1:?usage: replay.sh <cleanup-ref>}
cd "$(git rev-parse --show-toplevel)"
[ -z "$(git status --porcelain --untracked-files=no)" ] || { echo "replay: the tree has uncommitted changes" >&2; exit 2; }
git rev-parse --verify -q "$ref^{commit}" >/dev/null || { echo "replay: unknown ref $ref" >&2; exit 2; }
[ -z "$(git ls-files tools/cleanup)" ] || { echo "replay: tools/cleanup is already tracked here (already replayed?)" >&2; exit 2; }
rm -rf tools/cleanup   # untracked leftovers only (e.g. __pycache__)

suites_before=$(grep -o '^run [a-z0-9-]*' tools/build/host-tests.sh | sort)

# the scripts, patches and map of the cleanup branch (a new folder: nothing is replaced)
git archive "$ref" tools/cleanup | tar -x
list=tools/cleanup/patches/series
step() { # step <title> <command...>: run a script, apply its hand-edit hunks, commit
  local title=$1 patch=tools/cleanup/patches/$2; shift 2
  "$@"
  git add -A   # the script's result is the base the hunks apply to
  if [ -s "$patch" ]; then
    if git apply --check "$patch" 2>/dev/null; then git apply "$patch"     # dry run first: clean
    else  # the branch changed those lines: 3-way merge, conflicts stop the replay
      git apply --3way "$patch" || { echo "replay: $patch conflicts (markers left); resolve, commit, then run the remaining steps by hand; finish with this replay script --check-assertions "$ref"" >&2; exit 3; }
    fi
  fi
  git add -A
  git commit -q -m "$title" -m "Replayed from $ref by tools/cleanup/replay.sh."
  echo "replay: committed '$title'"
}
step "Remove the always-true APP_BREADBOX guards and PSX3D-only code" step1.patch python3 tools/cleanup/unifdef_app.py
step "Split psx3d.h into one header per module" step2.patch python3 tools/cleanup/split_psx3d_h.py
step "Name the screen-size UI metrics" step3.patch python3 tools/cleanup/rename_ui_metrics.py
# the commits that are plain edits: cherry-picked (3-way, conflicts stop here)
for c in $(grep -v '^#' "$list"); do
  if ! git cherry-pick -x "$c" >/dev/null 2>&1; then
    # the reviewed-surface hash list is derived data: take the branch's and re-hash what this commit touched
    unmerged=$(git diff --name-only --diff-filter=U)
    if [ "$unmerged" = tests/ui_consistency/surfaces.json ]; then
      git checkout --ours tests/ui_consistency/surfaces.json
      python3 -c 'import sys,subprocess; sys.path.insert(0,"tools/cleanup"); import ratchets
ch=subprocess.check_output(["git","diff-tree","--no-commit-id","--name-only","-r",sys.argv[1]],text=True).split()
ratchets.rehash_surfaces(ch)' "$c"
      git add tests/ui_consistency/surfaces.json
      GIT_EDITOR=true git cherry-pick --continue >/dev/null
      echo "replay: surfaces.json re-hashed for $c"
    else
      echo "replay: cherry-pick $c conflicts in: $unmerged; resolve and 'git cherry-pick --continue', then the rest of $list" >&2; exit 3
    fi
  fi
  echo "replay: picked $(git log -1 --format=%s)"
done

suites_after=$(grep -o '^run [a-z0-9-]*' tools/build/host-tests.sh | sort)
lost=$(comm -23 <(echo "$suites_before") <(echo "$suites_after"))
[ -z "$lost" ] || { echo "replay: host suites no longer registered:"; echo "$lost"; exit 4; }
check_cleanup_assertions "$ref"
echo "replay: done; every host suite is registered and cleanup-source assertions are preserved"
