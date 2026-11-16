"""Measured card-font advances and the production card's fixed text size."""
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
FONTS = json.loads(Path(__file__).with_name('card_font.json').read_text())['fonts']


def geometry(names=None):
    # Resolve the shared geometry through the C preprocessor, including expressions.
    import subprocess
    names = names or ['UI_CARD_X+UI_CARD_W-UI_CARD_RIGHT-UI_CARD_TEXT_X-UI_CARD_NUDGE', 'UI_CARD_ROW_SIZE']
    source = '#include "ui_style.h"\n' + '\n'.join(names)
    values = subprocess.check_output(['cc', '-E', '-P', '-I'+str(ROOT/'include/breadbox'), '-x', 'c', '-'], input=source, text=True).splitlines()[-len(names):]
    return tuple(eval(re.sub(r'(?<=\d)f\b', '', value), {'__builtins__': {}}, {}) for value in values)


def width(text, size):
    try:
        return max(sum(advance[c] for c in text) for advance in FONTS.values()) * size
    except KeyError as error:
        raise ValueError(f'unmeasured card character {error.args[0]!r}; use plain ASCII') from error


def validate(lines, release):
    if len(lines) > 6:
        raise ValueError(f'{release}: {len(lines)} highlights; at most 6 are allowed (move the rest under ### Details)')
    available, size = geometry()
    for line in lines:
        measured = width(line, size)
        if measured > available:
            raise ValueError(f'{release}: highlight {line!r} is {measured:.1f}px; one card line allows {available:g}px (shorten it or move it under ### Details)')
