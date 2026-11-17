#!/usr/bin/env python3
"""Structured local CI receipts. Account-local files are not attestations."""
import datetime
import fcntl
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import sys

SCHEMA = 2
COMMAND = ['tools/build/container.sh', 'bash', 'tools/build/ci.sh', 'all']
CONTAINER_COMMAND = COMMAND[1:]
PHASES = ['toolchain', 'gates', 'build', 'layout', 'host-tests']


def git(*args):
    return subprocess.check_output(['git', *args], text=True).strip()


def digest(data):
    return hashlib.sha256(data).hexdigest()


def stamp():
    return datetime.datetime.now(datetime.timezone.utc).isoformat(timespec='microseconds')


def snapshot():
    return git('rev-parse', 'HEAD'), git('rev-parse', 'HEAD^{tree}'), git('status', '--porcelain')


def collect(out, sha):
    phases = (out / 'ci-phases.jsonl').read_text().splitlines()
    if phases != PHASES:
        raise ValueError('incomplete CI phases')
    host = json.loads((out / 'host-tests/summary.json').read_text())
    suites = subprocess.check_output(['bash', 'tools/build/host-tests.sh', '--list'], text=True).splitlines()
    if (not suites or host['suites'] != suites or not host['complete'] or
            host['passed'] != len(suites) or host['failed'] != 0 or host['skipped'] != 0):
        raise ValueError('incomplete host suites')
    epoch = int(git('log', '-1', '--format=%ct', sha))
    info = (out / 'BUILD-INFO.txt').read_text().splitlines()
    if info[:2] != ['commit ' + sha, 'SOURCE_DATE_EPOCH ' + str(epoch)]:
        raise ValueError('build source identity mismatch')
    artifacts = {}
    for name in ('BreadBox.elf', 'BreadBox.cxi', 'identity.txt'):
        data = (out / name).read_bytes()
        if not data:
            raise ValueError('empty build artifact: ' + name)
        artifacts[name] = digest(data)
    sums = dict((name, value) for value, name in
                (line.split() for line in (out / 'SHA256SUMS').read_text().splitlines()))
    if any(sums[name] != artifacts[name] for name in ('BreadBox.elf', 'BreadBox.cxi')):
        raise ValueError('artifact digest mismatch')
    return dict(host={key: host[key] for key in ('passed', 'failed', 'skipped')},
                layout='passed', phases=phases, epoch=epoch, artifacts=artifacts)


def validate(record, sha):
    if record.get('schema') != SCHEMA or record.get('authority') != 'ci':
        raise ValueError('no authoritative current-format receipt')
    if record.get('sha') != sha or record.get('result') != 'green':
        raise ValueError('latest receipt is not green')
    if record.get('command') not in (COMMAND, CONTAINER_COMMAND):
        raise ValueError('unexpected CI command')
    if record.get('tree') != git('rev-parse', sha + '^{tree}'):
        raise ValueError('tree does not match commit')
    lock = subprocess.check_output(['git', 'show', sha + ':tools/build/toolchain.lock'])
    if record.get('toolchain') != digest(lock):
        raise ValueError('toolchain lock does not match commit')
    start = datetime.datetime.fromisoformat(record['started'])
    end = datetime.datetime.fromisoformat(record['finished'])
    if start.tzinfo is None or end.tzinfo is None or end < start:
        raise ValueError('invalid receipt times')
    if record['epoch'] != int(git('log', '-1', '--format=%ct', sha)):
        raise ValueError('source epoch does not match commit')
    host = record['host']
    script = subprocess.check_output(['git', 'show', sha + ':tools/build/host-tests.sh'], text=True)
    expected = re.findall(r'^run\s+(\S+)', script, re.M)
    if (not expected or any(type(host[k]) is not int for k in ('passed', 'failed', 'skipped')) or
            host['passed'] != len(expected) or host['failed'] != 0 or host['skipped'] != 0):
        raise ValueError('host suites did not all pass')
    if record['layout'] != 'passed' or record['phases'] != PHASES:
        raise ValueError('required phases did not all pass')
    for name in ('BreadBox.elf', 'BreadBox.cxi', 'identity.txt'):
        value = record['artifacts'][name]
        if len(value) != 64 or any(c not in '0123456789abcdef' for c in value):
            raise ValueError('invalid artifact digest')


def ledgers():
    if 'TRUNK_GATE_LEDGER' in os.environ:
        return [Path(os.environ['TRUNK_GATE_LEDGER'])]
    return [Path(line[9:]) / '.trunk-gate.jsonl' for line in
            git('worktree', 'list', '--porcelain').splitlines() if line.startswith('worktree ')]


def check(sha, for_merge=False):
    candidates = []
    for path in ledgers():
        if not path.exists():
            continue
        latest = None
        for line in path.read_text().splitlines():
            try:
                record = json.loads(line)
                if record.get('sha') == sha:
                    # Legacy records have only date. They can supersede older records,
                    # but cannot authorize a new merge.
                    date = datetime.datetime.fromisoformat(record.get('finished', record.get('date', '')))
                    if date.tzinfo is None:
                        raise ValueError('receipt has no UTC offset')
                    latest = (date, len(candidates), record)
            except (ValueError, AttributeError):
                raise ValueError('malformed ledger: ' + str(path))
        if latest is not None:
            candidates.append(latest)
    if not candidates:
        raise ValueError('no receipt for ' + sha)
    record = max(candidates, key=lambda row: (row[0], row[1]))[2]
    if (not for_merge and set(record) == {'sha', 'tree', 'date', 'result'} and
            record['result'] == 'green' and record['tree'] == git('rev-parse', sha + '^{tree}') and
            subprocess.run(['git', 'merge-base', '--is-ancestor', sha, 'refs/heads/trunk'],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0):
        print('trunk-gate: historical green receipt for already-merged ' + sha)
        return
    validate(record, sha)
    print('trunk-gate: ' + sha + ' has an authoritative green receipt')


def run(inside):
    sha, tree, dirty = snapshot()
    if dirty:
        raise ValueError('the tree is not clean; commit or shelve first')
    stub = os.environ.get('TRUNK_GATE_CI')
    command = (CONTAINER_COMMAND if inside else COMMAND) if stub is None else ['bash', '-c', stub]
    record = dict(schema=SCHEMA, authority='ci' if stub is None else 'test', sha=sha, tree=tree,
                  command=command, toolchain=digest(Path('tools/build/toolchain.lock').read_bytes()),
                  started=stamp(), result='red', epoch=int(git('log', '-1', '--format=%ct', sha)),
                  host=dict(passed=0, failed=0, skipped=0), layout='not-run', phases=[], artifacts={})
    out = Path('artifacts')
    if stub is None:
        # These are generated outputs, never reuse a previous run's success.
        for name in ('ci-phases.jsonl', 'host-tests/summary.json', 'BUILD-INFO.txt', 'SHA256SUMS',
                     'BreadBox.elf', 'BreadBox.cxi', 'identity.txt'):
            (out / name).unlink(missing_ok=True)
    env = dict(os.environ, SOURCE_DATE_EPOCH=git('log', '-1', '--format=%ct', sha), PSX3D_COMMIT=sha)
    try:
        rc = subprocess.run(command, env=env).returncode
        if stub is None:
            if (out / 'ci-phases.jsonl').exists():
                record['phases'] = (out / 'ci-phases.jsonl').read_text().splitlines()
            record['layout'] = 'passed' if 'layout' in record['phases'] else 'not-passed'
            if (out / 'host-tests/summary.json').exists():
                summary = json.loads((out / 'host-tests/summary.json').read_text())
                record['host'] = {key: summary[key] for key in ('passed', 'failed', 'skipped')}
        if snapshot() != (sha, tree, ''):
            raise ValueError('HEAD, tree or cleanliness changed during CI')
        if rc:
            raise ValueError('CI exited ' + str(rc))
        if stub is None:
            record.update(collect(out, sha))
        record['result'] = 'green'
    except (ValueError, OSError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        record['failure'] = str(error)
        print('trunk-gate: ' + str(error), file=sys.stderr)
    record['finished'] = stamp()
    path = Path(os.environ.get('TRUNK_GATE_LEDGER', '.trunk-gate.jsonl'))
    with path.open('a') as ledger:
        fcntl.flock(ledger, fcntl.LOCK_EX)
        ledger.write(json.dumps(record, separators=(',', ':')) + '\n')
        ledger.flush()
        os.fsync(ledger.fileno())
    print('trunk-gate: ' + sha + ' ' + record['authority'] + ' ' + record['result'])
    return 0 if record['result'] == 'green' else 1


def main():
    if len(sys.argv) == 3 and sys.argv[1] in ('--check', '--check-for-merge'):
        sha = git('rev-parse', '--verify', '--end-of-options', sys.argv[2] + '^{commit}')
        check(sha, sys.argv[1] == '--check-for-merge')
        return 0
    if sys.argv[1:] not in ([], ['--inside-container']):
        raise ValueError('usage: trunk-gate.sh [--check COMMIT | --inside-container]')
    return run(bool(sys.argv[1:]))


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (ValueError, OSError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        print('trunk-gate: ' + str(error), file=sys.stderr)
        sys.exit(1)
