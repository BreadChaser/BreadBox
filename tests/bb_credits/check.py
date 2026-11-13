#!/usr/bin/env python3
"""Source checks on the in-app credits (app/ui/breadbox_credits.c): pages that each fit the dialog whole, American English, a list of
names and licenses that must stay on a page, and the HOME > Close guards after each caller. This does not check
that the notices are complete or legally sufficient (that is the CREDITS file and a human review), nor how the
text wraps: the character and line budgets are only a proxy; the wrapped pages are the Azahar screenshots
(the private credits validation report). The caller guard is a source-order check; the runtime test of the
close paths is tests/shelf (shelf_run) and tests/config_reset."""
import pathlib, re, sys
root = pathlib.Path(__file__).resolve().parents[2]
src = (root / 'app/ui/breadbox_credits.c').read_text()
block = re.search(r'credits_pages\[\]\s*=\s*\{(.*?)\n\};', src, re.S).group(1)
block = block.replace('" PSX3D_VERSION "', '0.0.0')
pages = re.findall(r'^\s*"((?:[^"\\]|\\.)*)",?\s*$', block, re.M)
errors = []
if not 6 <= len(pages) <= 12: errors.append(f'expected 6 to 12 credits pages, found {len(pages)}')
install_pages = re.findall(r'^\s*"((?:[^"\\]|\\.)*)",?\s*$', re.search(r'install_pages\[\]\s*=\s*\{(.*?)\n\};', src, re.S).group(1), re.M)
for i, text in enumerate(pages + install_pages, 1):   # each page fits the dialog whole: tests/ui_fit/strings.py (compiled pages)
    if not text.rstrip().endswith(('.', ')')): errors.append(f'page {i} does not end a sentence')
whole = ' '.join(pages)
for need in ['GPL-2.0-or-later', 'PCSX-ReARMed', 'notaz', 'Exophase', 'gpSP', 'Normmatt', 'VBA', 'system files',
             'devkitPro', 'libctru', 'citro', 'zoogie', 'Dreamstale', 'Flaticon', 'Michroma', 'OFL',
             'scribbletoad', 'CC BY 4.0', 'modified', 'libretro', 'creativecommons.org', 'CC BY-SA 4.0', 'Blender',
             'Joseph Sardin', 'BigSoundBank', 'Kenney', 'OwlishMedia', 'CC0']:   # the menu sounds as in CREDITS
    if need.lower() not in whole.lower(): errors.append(f'credits lack "{need}"')
for bad in ['licence', 'colour', 'docs/third-party', 'PSX3D only', 'PS1 only']:   # BIOS / HLE may be named: credits are attribution prose
    if bad.lower() in whole.lower(): errors.append(f'credits contain "{bad}"')
for needed in ['bb_credits_run', 'bb_install_help_run', 'aptMainLoop()']:
    if needed not in src: errors.append(f'breadbox_credits.c lacks {needed} (HOME-close guard / entry points)')
# both entry points must stop drawing after HOME > Close
for caller in ['app/settings/settings_ui.c', 'app/services/share.c']:
    text = (root / caller).read_text()
    for m in re.finditer(r'(bb_credits_run|bb_install_help_run)\(\);', text):
        tail = text[m.end():m.end() + 160]
        if '!aptMainLoop()' not in tail: errors.append(f'{caller}: {m.group(1)} is not followed by the aptMainLoop() guard')
install = re.search(r'install_pages\[\]\s*=\s*\{(.*?)\n\};', src, re.S).group(1)
if 'BreadBox.cia' not in install or 'SD card' not in install: errors.append('the install help must say sharing needs BreadBox.cia on the SD card')
# HOME > Close during library_load: the first-run loop must not draw after aptMainLoop() went false (home.c)
home = (root / 'app/ui/home.c').read_text()
loop = home[home.index('int home_empty_library_run'):]
draw = loop.index('video_present_vsync(false, empty_top, empty_bottom')
guard = loop.rfind('!aptMainLoop()', 0, draw)
if guard < 0 or loop.rfind('ui_begin();', 0, draw) < guard: errors.append('home_empty_library_run draws after library_load without an aptMainLoop() guard')
print('\n'.join(errors) if errors else f'credits source checks: {len(pages)} pages, the listed names and licenses are on a page')
sys.exit(1 if errors else 0)
