#!/usr/bin/env python3
"""Fail-closed linker generation and section isolation contract."""
import importlib.util
from pathlib import Path
root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('linker', root/'tools/build/system-linker.py')
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
s = (root/'build/3ds/core-first.ld').read_text()
libs = (root/'build/3ds/breadbox-library-text.ld').read_text()
out = m.generate(s, libs)
for kind in ('text','rodata','data'):
    assert f'*(.sys.*.{kind} .sys.*.{kind}.*)' in out
for anchor in ('video.o(.text .text.*)', '__psx3d_frontend_text_end = .;', '*(.roda)', '*(.gnu.linkonce.d*)','__bss_end__ = .;', '/* System frame blocks are inserted here by system-linker.py. */'):
    for changed in (s.replace(anchor, ''), s + '\n' + anchor):
        try: m.generate(changed, libs)
        except ValueError: pass
        else: raise AssertionError(f'template drift accepted: {anchor}')
assert '\n .gba.text ' not in out
assert '.sys.exec.gba ndrc_bss (INFO)' in out
assert 'unregistered system BSS overlay' in out
assert '. = __bb_image_end;' in out
assert '__bb_gba_jit_bytes + 0x2000 <= __bb_exec_size' in out
assert 'SIZEOF(.sys.exec.ps1) + 0 + 0x2000' in out  # PS1 has no core.mk: no borrowed JIT
assert out.count('jit-size.ld') == 1
lone = m.generate(s, libs, ('ps1', 'nes'), ())  # a blob system without a JIT
assert 'jit-size.ld' not in lone and '.sys.exec.nes ndrc_bss (INFO)' in lone
print('system linker section placement + missing/duplicate anchors: PASS')

assert 'gba.o(.text .text.*)' in out
assert 'gba.o(.text .text.*)' not in lone
print('system-owned frame pins follow selected systems: PASS')
