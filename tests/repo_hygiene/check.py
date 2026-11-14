#!/usr/bin/env python3
"""The public tree holds code and the docs a player or contributor needs, not internal work notes.

Three kinds of rule:
- file names and folders that look like work notes (research, archive, reports, reviews,
  plans, to-do lists, journals, logs, dated notes);
- in docs and other prose, process notes: owner or coordinator decisions, review
  questions, work rounds and phases, private branch names, status lines;
- in every authored text file, code included, private identifiers: home-directory
  paths, machine names, private network addresses, the private devtools folder,
  and AI model or agent names.
Vendored cores and shipped game data are not checked. Run with --self-test to prove
each rule fires and that normal code words (worker, journal, ownership) do not.
"""
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
VENDORED = ('core/', 'gba-core/', 'psx3d/vendor/', 'systems/nes/core/', 'assets/romfs/')
SELF = 'tests/repo_hygiene/'
# Data, not notes: candidate widescreen patches the catalog points at.
ALLOWED = ('tools/widescreen/research/',)
NOTE_TYPES = ('.md', '.txt', '.html', '.rst', '.org')
PROSE = ('.md', '.txt', '.rst', '.html', '.yml', '.yaml')
PROSE_NAMES = ('CREDITS', 'TESTING', 'README')
AUTHORED = PROSE + ('.c', '.h', '.inc', '.cc', '.cpp', '.s', '.S', '.py', '.sh', '.mk', '.ld',
                    '.json', '.js', '.ini', '.pica', '.def', '.tsv', '.cfg', '.rsf')

FOLDERS = re.compile(r'(^|/)(research|archive|notes|reports?|reviews?|journal|[^/]*-journal)/', re.I)
NAMES = [
    (re.compile(r'\.log$', re.I), 'log file'),
    (re.compile(r'report', re.I), 'report'),
    (re.compile(r'(^|[-_.])review', re.I), 'review'),
    (re.compile(r'^(plan|todo|notes?|status|handoff|morning|journal|gaps|readiness|decisions)([-_.]|$)', re.I),
     'plan or note'),
    (re.compile(r'journal', re.I), 'journal'),
    (re.compile(r'20\d\d-?[01]\d-?[0-3]\d'), 'dated note'),
]
PROCESS = [
    (re.compile(r'\bcoordinator\b', re.I), 'mentions the coordinator'),
    (re.compile(r'\(owner\b|\bowner[ ,:]+20\d\d-|\(user,? 20\d\d-|\bowner\'s (pick|choice|console)', re.I),
     'records an owner decision'),
    (re.compile(r'\bREVIEW Q\d|\bR\d+\'s\b|\breview round\b', re.I), 'cites a review'),
    (re.compile(r'\b(work |light |library |art )?round \d+[a-z]?\b(?! ?(mm|px|pixels?))', re.I), 'names a work round'),
    (re.compile(r'\bphase \d\b', re.I), 'names a work phase'),
    (re.compile(r'`t3/[\w./-]+`|\bplan [A-Z]\b|\bin progress\b'), 'private branch or plan'),
    (re.compile(r'^(Status|Base): ', re.M), 'status line'),
]
PRIVATE = [
    (re.compile(r'/home/[a-z_][\w-]*/'), 'home-directory path'),
    (re.compile(r'\b(100\.(6[4-9]|[7-9]\d|1[01]\d|12[0-7])|192\.168|10\.\d{1,3})\.\d{1,3}\.\d{1,3}\b'),
     'private network address'),
    (re.compile(r'(?<![\w/])devtools/'), 'points at the private devtools folder'),
    (re.compile(r'\b(Claude|Codex|Opus|Sonnet|Haiku|ChatGPT|GPT-?\d[\w.]*|Gemini|Copilot)\b'), 'AI model name'),
    (re.compile(r'\b(AI|coding|review|sub)[- ]?agents?\b|\bagents? (review|run|worker)', re.I), 'agent mention'),
]
TEXT_EXEMPT = ('CHANGELOG.md',)
# Machine and host names are private, so they are not listed here: put one per line in the file named by
# BB_PRIVATE_NAMES (outside the repository) to check for them too.
def private_names():
    path = os.environ.get('BB_PRIVATE_NAMES')
    if not path or not os.path.exists(path):
        return []
    names = [n.strip() for n in open(path, encoding='utf-8') if n.strip() and not n.startswith('#')]
    return [(re.compile(r'\b(' + '|'.join(map(re.escape, names)) + r')\b', re.I), 'private machine name')] if names else []




def kind(path):
    name = path.rsplit('/', 1)[-1]
    if name.endswith(PROSE) or name in PROSE_NAMES:
        return 'prose'
    if name.endswith(AUTHORED) or name in ('Makefile',):
        return 'code'
    return None


def problems(files):
    """files: {path: text or None}. Returns a list of 'path: why' strings."""
    out = []
    for path, text in sorted(files.items()):
        if path.startswith(VENDORED) or path.startswith(ALLOWED) or path.startswith(SELF):
            continue
        if FOLDERS.search(path):
            out.append(f'{path}: internal-notes folder')
            continue
        name = path.rsplit('/', 1)[-1]
        if name.lower().endswith(NOTE_TYPES) or name.lower().endswith('.log'):
            for rule, why in NAMES:
                if rule.search(name):
                    out.append(f'{path}: {why}')
                    break
        if text is None:
            continue
        rules = PRIVATE + private_names() + (PROCESS if kind(path) == 'prose' and path not in TEXT_EXEMPT else [])
        scan = text.replace('AGENTS.md', '')
        for rule, why in rules:
            m = rule.search(scan)
            if m:
                line = scan.count('\n', 0, m.start()) + 1
                out.append(f'{path}:{line}: {why} ({m.group(0)!r})')
    return out


def self_test():
    planted = {
        'research/gba/design.md': '', 'docs/archive/old.md': '', 'tools/x/notes/a.md': '',
        'docs/V1-REPORT.md': '', 'docs/review-r2.md': '', 'PLAN.md': '', 'TODO.txt': '',
        'docs/gpu-journal.md': '', 'build.log': '', 'docs/2026-11-12-notes.md': '', 'DECISIONS.md': '',
        'docs/a.md': 'Asked the coordinator first.', 'docs/b.md': 'Day style (owner 2026-11-06).',
        'docs/c.md': 'See devtools/breadbox/x.', 'docs/d.md': '## Round 4 results\n',
        'docs/e.md': 'Status: done\n',
        # the notes a review found surviving the first cleanup
        'docs/f.md': 'to be re-measured as part of defaults v1 (REVIEW Q6)',
        'docs/g.md': "R2's event-thread claim does not match libctru",
        'docs/h.md': 'Hidden on both consoles per Round 3 decision.',
        'docs/i.md': 'pitch-preserving version in progress (`t3/audio-pitch`)',
        'docs/j.md': 'Phase 2 checks that it only appears with the switch.',
        'docs/k.md': 'The room poster is round 9 pick 01.',
        'docs/l.md': 'the save service in plan B replaces it',
        # private identifiers, in prose and in code
        'docs/m.md': 'Built in /home/someone/projects/app.',
        'app/o.c': '/* server 100.64.0.5:8091 */', 'tools/p.sh': 'ssh 192.168.0.10',
        'docs/q.md': 'Checked by Claude Opus.', 'tools/r.json': '{"reason": "the review (GPT-6.1 Sol) cleared"}',
        'docs/s.md': 'An agent review found it.', 'app/t.h': '// see devtools/fastloop/lock.old',
    }
    found = {p.split(':')[0] for p in problems(planted)}
    missed = sorted(set(planted) - found)
    assert not missed, f'rules missed: {missed}'
    clean = {
        'docs/dev/save-service.md': 'The worker owns the file. Ownership records stay.',
        'tests/gba_journal/check.c': '/* the write journal */', 'tests/parity/review.py': None,
        'tools/widescreen/research/X.ws': None, 'core/notes/a.md': 'owner 2026-01-01',
        'CHANGELOG.md': '## v0.5.0 (2026-10-21)', 'app/u.c': 'curl_easy_setopt(c, CURLOPT_USERAGENT, "BreadBox");',
        'docs/v.md': 'A round screen flag; see AGENTS.md; tests/settings/fixtures/v1-r2/ holds it.',
        'assets/romfs/covers/gba-codes.txt': 'BWVE\tWinX Club - Quest for the Codex (USA)',
        '.gitignore': '/devtools/', 'docs/w.md': 'Version 10.5 of the SDK; 2.7.0 pinned.',
        'docs/x.md': 'Rounded to whole percents; round 2 mm corners.',
    }
    assert not problems(clean), problems(clean)


def main():
    self_test()
    if '--self-test' in sys.argv:
        print('repo hygiene self-test PASS')
        return
    tracked = subprocess.check_output(['git', 'ls-files'], cwd=ROOT, text=True).splitlines()
    files = {}
    for path in tracked:
        full = ROOT / path
        if kind(path) and full.is_file() and full.stat().st_size < 4_000_000:
            files[path] = full.read_text(errors='replace')
        else:
            files[path] = None
    found = problems(files)
    if found:
        sys.exit('internal work notes or private details in the tracked tree '
                 '(keep them in the private devtools folder):\n' + '\n'.join(found))
    print(f'repo hygiene PASS: {len(tracked)} tracked files, no internal notes or private details')


if __name__ == '__main__':
    main()
