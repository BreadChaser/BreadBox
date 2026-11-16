#!/usr/bin/env python3
"""Build the running release's notes from the public changelog."""
import argparse
import json
from pathlib import Path
import re
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tests/ui_fit"))
from card_lines import validate


def release_text(changelog, version):
    lines = changelog.splitlines()
    start = next((i + 1 for i, line in enumerate(lines)
                  if re.fullmatch(r'## v?' + re.escape(version) + r'(?:\s+.*)?', line)), None)
    if start is None:
        return ''
    end = next((i for i in range(start, len(lines)) if lines[i].startswith('## ')), len(lines))
    highlights = []
    for line in lines[start:end]:
        line = line.strip()
        if not line:
            if highlights:
                break
            continue
        if line.startswith('#'):
            break
        if not line.startswith('- '):
            raise ValueError(f'{version}: highlights must be Markdown list items')
        highlights.append(line[2:])
    validate(highlights, version)
    return '\n'.join(highlights)



def generate(changelog, version_header):
    version = re.search(r'^#define BB_VERSION "([^"\n]+)"', version_header, re.M).group(1)
    for heading in re.findall(r'^## (.+)$', changelog, re.M):
        release = heading.split()[0].removeprefix('v')
        if release == 'Unreleased' or re.fullmatch(r'\d+\.\d+\.\d+', release):
            release_text(changelog, release)
    text = release_text(changelog, version)
    return ('/* Release notes from CHANGELOG.md; rebuilt by make. */\n#pragma once\n'
            f'#define BB_CHANGELOG_VERSION {json.dumps(version)}\n'
            f'#define BB_CHANGELOG_TEXT {json.dumps(text, ensure_ascii=False)}\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('changelog', type=Path)
    parser.add_argument('version', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    try:
        result = generate(args.changelog.read_text(), args.version.read_text())
    except ValueError as error:
        parser.exit(1, f'Changelog: {error}\n')
    if not args.output.exists() or args.output.read_text() != result:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(result)


if __name__ == '__main__':
    main()
