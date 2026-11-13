#!/usr/bin/env python3
"""Source coverage for input/present loops, not a substitute for swap films.

Every discovered loop must own the shared key gate or have a specific reviewed
exception below. Presentation wrappers are discovered within each source file;
gates must remain in the input loop itself. Literal-dead/zero-mask gates do not
count; UI_SWAP_MENU (a menu-to-menu loop: no dim, no wait) does.
Masks: every gate passes UI_SWAP_MENU unless it is a reviewed game hand-over in
HANDOVERS below (launch, resume, quit), the only swaps that dim (owner 2026-11-11). Share and Shelf require independent branch gates. This is a source
presence check, not proof of exit masks, all control-flow paths, or swap quality.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
# These are behavior exceptions, not a baseline of unfixed swaps.
EXCEPTIONS = {
    ('systems/ps1/emu.c', 'run_game'): 'Gameplay consumes held input; the pause menu owns screen swaps.',
    ('systems/gba/gba.c', 'gba_run'): 'Gameplay consumes held/touch input; bb_pause_run owns screen swaps. The bottom-only retained-target submission is gameplay presentation.',
}

# Game hand-overs: the keys in these masks start, resume or leave a game. Everything else is a menu.
HANDOVERS = {
    ('app/ui/home.c', 'UI_SWAP_REMOTE'): 'Continue from the room launches its game.',
    ('app/ui/shelf.c', 'UI_SWAP_REMOTE'): 'The shelf launch, after its pull-out motion.',
    ('app/ui/browser.c', 'UI_SWAP_REMOTE | (launches_on_a ? KEY_A : 0)'): 'A on a game file launches it.',
    ('app/ui/bb_pause.c', 'KEY_A'): 'The Continue question: A plays on.',
    ('app/ui/bb_pause.c', 'KEY_START'): 'The postcard board closes into the game after a save or load.',
    ('app/ui/bb_pause.c', 'swaps'): 'GBA pause: B resumes; A on Resume, Undo, Quit; remote leave.',
    ('systems/ps1/emu.c', 'swaps'): 'PS1 pause: B resumes; A on Resume, Undo, Disc, Fast forward, Exit; remote leave.',
    ('systems/ps1/savestates.c', 'pick_plays(&b)?KEY_A:UI_SWAP_MENU'): 'PS1 board: A loads/saves into the game.',
    ('systems/ps1/savestates.c', 'KEY_A'): 'PS1 Continue question: A plays on.',
}
# Kept as they were on purpose (not menus a player reaches).
UNREACHABLE = {
    ('systems/gba/gba.c', 'KEY_B|KEY_Y'): 'settings_menu: GBA settings open through its settings_model (bb_pause.c settings_run '
                                          'returns before config.settings); kept byte-identical so the GBA frame functions after '
                                          'it keep their page offsets (system layout gate).',
}
MASK = re.compile(r'\bui_swap_(?:keys|confirm_keys)\s*\(')


def mask_audit():
    found, failures = set(), []
    for path in sorted([*(ROOT/'app').rglob('*.c'), *(ROOT/'systems').rglob('*.c'), *(ROOT/'systems').rglob('*.inc'), *(ROOT/'platform/3ds').glob('*.c')]):
        rel = path.relative_to(ROOT).as_posix()
        s = scrub(path.read_text())
        for m in MASK.finditer(s):
            opening = m.end() - 1
            end = end_pair(s, opening, '(', ')')
            depth, comma = 0, None
            for at in range(opening + 1, end - 1):
                depth += (s[at] == '(') - (s[at] == ')')
                if s[at] == ',' and not depth:
                    comma = at
            mask = re.sub(r'\s+', ' ', s[comma + 1:end - 1]).strip()
            if mask == 'UI_SWAP_MENU' or (rel == 'app/ui/ui.c' and mask in ('swap_mask', 'u32 swap_mask')):
                continue
            if (rel, mask) in HANDOVERS or (rel, mask) in UNREACHABLE:
                found.add((rel, mask))
            else:
                failures.append(f'{rel}: ui_swap mask "{mask}" dims a menu swap (use UI_SWAP_MENU or list the game hand-over)')
    failures += [f'stale hand-over {k}' for k in (set(HANDOVERS) | set(UNREACHABLE)) - found]
    return failures


def scrub(s):
    # Keep byte/line offsets while removing braces and fake calls inside comments/literals.
    return re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                  lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0]), s, flags=re.S)

def breadbox_branch(s):
    active = [True]
    out = []
    for line in s.splitlines(keepends=True):
        directive = re.match(r'\s*#\s*(ifdef|ifndef|if|elif|else|endif)\b(.*)', line)
        if directive:
            kind, expression = directive.groups()
            if kind in ('if', 'ifdef', 'ifndef'):
                selected = expression.strip() != '0'
                if 'APP_BREADBOX' in expression:
                    selected = kind != 'ifndef' and '!defined' not in expression
                active.append(active[-1] and selected)
            elif kind == 'else':
                active[-1] = active[-2] and not active[-1]
            elif kind == 'elif':
                active[-1] = False
            else:
                active.pop()
            out.append(''.join('\n' if c == '\n' else ' ' for c in line))
        else:
            out.append(line if active[-1] else ''.join('\n' if c == '\n' else ' ' for c in line))
    return ''.join(out)

def end_pair(s, start, opening='{', closing='}'):
    depth = 1
    for p in range(start + 1, len(s)):
        depth += (s[p] == opening) - (s[p] == closing)
        if not depth:
            return p + 1
    raise AssertionError(f'unbalanced {opening} at {start}')

def functions(s):
    # Functions start at brace depth zero. Declarations and preprocessor branches
    # do not affect depth; multiline attributes/signatures are permitted.
    depth = 0
    for m in re.finditer(r'[{}]', s):
        if m[0] == '{':
            if depth == 0:
                header = s[max(s.rfind(';', 0, m.start()), s.rfind('}', 0, m.start())) + 1:m.start()]
                names = re.findall(r'\b(\w+)\s*\([^;{}]*\)\s*$', header, re.S)
                if names:
                    yield names[-1], m.end(), end_pair(s, m.start()) - 1
            depth += 1
        else:
            depth -= 1

INPUT = re.compile(r'\b(?:input_poll|input_keys_down|input_keys_held|hidScanInput|hidKeysDown|hidKeysHeld)\s*\(')
PRESENT = re.compile(r'\bvideo_present_\w+\s*\(')
GATE = re.compile(r'\bui_swap_(?:keys|confirm_keys)\s*\(')


def blank(s):
    return ''.join('\n' if c == '\n' else ' ' for c in s)


def constant_zero(expression):
    # This source check rejects syntactically zero masks. A variable/conditional
    # mask still needs behavioral review; presence does not prove the right keys.
    expression = re.sub(r'\bUI_METRIC_0\b|\bfalse\b', '0', expression)
    expression = re.sub(r'(?<=\d)[uUlL]+\b', '', expression)
    expression = expression.replace('(', '').replace(')', '').strip()
    return bool(re.fullmatch(r'(?:0[xX]0+|0+)(?:\s*[|+]\s*(?:0[xX]0+|0+))*', expression))


def statement_end(s, start):
    while start < len(s) and s[start].isspace():
        start += 1
    if s[start] == '{':
        return end_pair(s, start)
    # Sufficient for an unbraced gate assignment, including nested call args.
    depth = 0
    for at in range(start, len(s)):
        depth += (s[at] == '(') - (s[at] == ')')
        if s[at] == ';' and not depth:
            return at + 1
    raise AssertionError('unterminated statement')


def live_code(s):
    # Exclude literal-false C branches as well as preprocessor #if0 branches.
    # Preserve offsets so gate positions stay tied to their original branch.
    for m in list(re.finditer(r'\b(?:if|while)\s*\(', s)):
        opening = m.end() - 1
        if s[opening] != '(':  # an enclosing dead branch was already blanked
            continue
        after = end_pair(s, opening, '(', ')')
        if constant_zero(s[opening + 1:after - 1]):
            start = after
            while start < len(s) and s[start].isspace():
                start += 1
            if start < len(s) and s[start] != ';':
                end = statement_end(s, start)
                s = s[:start] + blank(s[start:end]) + s[end:]
    return s


def gate_calls(s):
    calls = []
    for m in GATE.finditer(s):
        opening = m.end() - 1
        end = end_pair(s, opening, '(', ')')
        depth = 0
        comma = None
        for at in range(opening + 1, end - 1):
            depth += (s[at] == '(') - (s[at] == ')')
            if s[at] == ',' and not depth:
                comma = at
                break
        assert comma is not None, 'ui_swap_keys needs two arguments'
        if not constant_zero(s[comma + 1:end - 1]):
            calls.append((m.start(), end, comma))
    return calls


def conditional_blocks(s, condition, require_else=False):
    for m in re.finditer(r'\bif\s*\(', s):
        opening = m.end() - 1
        after = end_pair(s, opening, '(', ')')
        if re.sub(r'\s+', '', s[opening + 1:after - 1]) != condition:
            continue
        start = after
        while s[start].isspace():
            start += 1
        end = statement_end(s, start)
        otherwise = re.match(r'\s*else\s*', s[end:])
        if require_else and not otherwise:
            continue
        return (start, end), ((end + otherwise.end(), statement_end(s, end + otherwise.end())) if otherwise else None)
    return None, None


def branch_gates(rel, name, loop, calls):
    positions = [call[0] for call in calls]
    if rel == 'app/services/share.c' and name == 'share_about_run' and 'share_qr' in loop:
        qr, listing = conditional_blocks(loop, 'share_qr', require_else=True)
        return bool(qr and listing and all(any(a <= p < b for p in positions) for a, b in (qr, listing)))
    if rel == 'app/ui/shelf.c' and name == 'shelf_run':
        launch, _ = conditional_blocks(loop, 'launching')
        return bool(launch and any(launch[0] <= p < launch[1] for p in positions)
                    and any(not launch[0] <= p < launch[1] for p in positions))
    return bool(calls)


def inventory(overrides=None):
    rows = []
    for path in sorted([*(ROOT/'app/ui').glob('*.c'), *(ROOT/'app/settings').glob('*.c'), *(ROOT/'app/services').glob('*.c'), *(ROOT/'platform/3ds').glob('*.c'), *(ROOT/'systems').glob('*/*.c'), *(ROOT/'systems').glob('*/*.inc')]):
        rel = path.relative_to(ROOT).as_posix()
        s = breadbox_branch(scrub((overrides or {}).get(rel, path.read_text())))
        if not PRESENT.search(s):
            continue
        s = live_code(s)
        definitions = list(functions(s))
        # Discover local presentation wrappers transitively, including newly
        # introduced helper names. Calls into other files remain a review limit.
        presenters = {name for name, a, b in definitions if PRESENT.search(s[a:b])}
        while True:
            expanded = presenters | {name for name, a, b in definitions
                                      if any(re.search(r'\b' + re.escape(helper) + r'\s*\(', s[a:b]) for helper in presenters)}
            if expanded == presenters:
                break
            presenters = expanded
        if not presenters:
            continue
        present = re.compile(r'\b(?:video_present_\w+|' + '|'.join(map(re.escape, presenters)) + r')\s*\(')
        for name, first, last in definitions:
            body = s[first:last]
            if not INPUT.search(body):
                continue
            openings = []
            for m in re.finditer(r'\b(?:while|for)\s*\(', body):
                after = end_pair(s, first + m.end() - 1, '(', ')')
                brace = after
                while brace < last and s[brace].isspace():
                    brace += 1
                if brace < last and s[brace] == '{':
                    openings.append(brace)
            openings += [first + m.end() - 1 for m in re.finditer(r'\bdo\s*{', body)]
            for brace in sorted(set(openings)):
                end = end_pair(s, brace)
                loop = s[brace:end]
                if not INPUT.search(loop) or not present.search(loop):
                    continue
                line = s.count('\n', 0, brace) + 1
                unreachable_share = (rel == 'app/services/share.c' and name == 'share_about_run'
                                     and re.search(r'return;\s*in_game\s*=', s[first:brace]))
                reason = ('Legacy PSX3D Share fallback is unreachable after BreadBox return; verified source prefix.'
                          if unreachable_share else None)
                rows.append((rel, name, line, branch_gates(rel, name, loop, gate_calls(loop)), reason))
    return rows


def negative_controls(rows):
    count = 0
    def fails(file, source):
        return any(r[0] == file and not r[3] and not r[4] and (r[0], r[1]) not in EXCEPTIONS
                   for r in inventory({file: source}))
    for file in sorted({row[0] for row in rows if row[3]}):
        assert fails(file, GATE.sub('removed_swap_keys(', (ROOT/file).read_text())), file
        count += 1
    # Each independent branch must fail when its one gate is removed, masked
    # with0, or hidden behind if(0), even though the other branch keeps its gate.
    for file in ['app/services/share.c', 'app/ui/shelf.c', 'app/ui/bb_feature_ui.c']:
        source = (ROOT/file).read_text()
        for start, end, comma in gate_calls(scrub(source)):
            stmt_start = max(source.rfind(';', 0, start), source.rfind('{', 0, start), source.rfind('}', 0, start)) + 1
            stmt_end = statement_end(source, stmt_start)
            mutations = [source[:start] + 'removed_' + source[start:],
                         source[:comma + 1] + '0' + source[end - 1:],
                         source[:stmt_start] + 'if(0){' + source[stmt_start:stmt_end] + '}' + source[stmt_end:]]
            for mutation in mutations:
                assert fails(file, mutation), (file, start, mutation[start:end])
                count += 1
    # Synthetic discovery controls: a new helper name, transitive helper call,
    # do/while presentation loop, zero mask and unbraced/braced dead C branches.
    sample = 'static void inner(void){video_present_vsync(0,0,0,0);} static void outer(void){inner();} void test(void){do{input_poll();k=ui_swap_keys(k,KEY_B);outer();}while(alive);}'
    file = 'app/ui/bb_feature_ui.c'
    assert not fails(file, sample)
    for replacement in ['ui_swap_keys(k,0)', 'ui_swap_keys(k,(0u))', 'ui_swap_keys(k,UI_METRIC_0)']:
        assert fails(file, sample.replace('ui_swap_keys(k,KEY_B)', replacement))
        count += 1
    for replacement in ['if(0)k=ui_swap_keys(k,KEY_B);', 'if((0u)){k=ui_swap_keys(k,KEY_B);}']:
        assert fails(file, sample.replace('k=ui_swap_keys(k,KEY_B);', replacement))
        count += 1
    # A cache update may inspect QR state before the paired input branches.
    # It must not hide either branch's independent gate from discovery.
    cached = 'if(share_qr)make_qr();if(share_qr){k=ui_swap_keys(k,KEY_A);}else{k=ui_swap_keys(k,KEY_B);}'
    share = ('app/services/share.c', 'share_about_run')
    assert branch_gates(*share, cached, gate_calls(cached))
    for gate in ('ui_swap_keys(k,KEY_A)', 'ui_swap_keys(k,KEY_B)'):
        missing = cached.replace(gate, 'removed_gate(k)')
        assert not branch_gates(*share, missing, gate_calls(missing))
        count += 1
    return count

def main():
    failures = []
    rows = inventory()
    assert rows, 'source discovery found no input/present loops'
    used = set()
    for file, name, line, gated, proof in rows:
        key = (file, name)
        if key in EXCEPTIONS:
            used.add(key)
        reason = 'shared keys' if gated else proof or EXCEPTIONS.get(key)
        print(f'{file}:{line} {name}: {reason or "MISSING ui_swap_keys"}')
        if not reason:
            failures.append(f'{file}:{line} ({name})')
    # No silent catch-all exceptions or stale entries.
    stale = set(EXCEPTIONS) - used
    if stale:
        failures.append(f'stale exceptions: {sorted(stale)}')
    failures += mask_audit()
    controls = negative_controls(rows) if not failures else 0
    if failures:
        print('Swap source coverage FAIL: ' + ', '.join(failures), file=sys.stderr)
        return 1
    print(f'Swap source gate presence PASS: {len(rows)} input/present loops, {len(HANDOVERS)} game hand-over masks (all other gates UI_SWAP_MENU), {len(used)} reasoned exceptions, {controls} negative controls; masks and swap behavior require separate review')
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
