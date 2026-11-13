#!/usr/bin/env python3
"""Generate the multi-system profile; fail closed if the PS1 template changes."""
from pathlib import Path
import re, sys
ROOT = Path(__file__).resolve().parents[2]

def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f'linker template expected one occurrence of {old!r}, got {text.count(old)}')
    return text.replace(old, new, 1)

def generate(script, libraries, systems=("gba", "ps1"), cores=("gba",)):
    """systems: every system of the app; cores: those with systems/<id>/core.mk."""
    script = replace_once(script, 'video.o(.text .text.*)',
        'video.o(.text.present_on_gpu_thread .text.video_snapshot '
        '.text.video_display_changed .text.video_hold .text.video_game_end '
        '.text.hold_on_gpu_thread .text.video_present_vsync)')
    script = replace_once(script, '__psx3d_frontend_text_end = .;',
        'video.o(.text .text.*)\n\t\t__psx3d_frontend_text_end = .;')
    # Optional measured system blocks precede all freely growing menu code.
    # Each system owns its selectors and cache-set offsets; PS1-only builds
    # use core-first.ld directly and acquire no padding from other systems.
    frame = ''.join((ROOT/'systems'/sid/'frame-text.ld').read_text()
                    for sid in systems if (ROOT/'systems'/sid/'frame-text.ld').exists())
    script = replace_once(script, '/* System frame blocks are inserted here by system-linker.py. */',
                          '/* Pinned system frame code. */\n' + frame)
    start = '\t\t/* Libraries: compiler/libc assembly, then the rest in link order. */'
    end = '\t\t/* Frontend: may grow freely. */'
    if script.count(start) != 1 or script.count(end) != 1:
        raise ValueError('linker library boundary changed')
    a, b = script.index(start), script.index(end)
    # Keep the template's established first library pin; do not duplicate its value.
    prefix = script[a:script.index('__psx3d_libs_text_start = .;', a)]
    script = script[:a] + prefix + '__psx3d_libs_text_start = .;\n' + libraries + '\n\t\t__psx3d_libs_text_end = .;\n\n' + script[b:]
    for anchor, sections in [('*(.roda)', 'rodata'), ('*(.gnu.linkonce.d*)', 'data'), ]:
        extra=''
        if sections=='rodata':
            for sid in cores:
                for kind in ('init','fini'):
                    extra+=f'\n        . = ALIGN(4); __bb_{sid}_{kind}_start = .;\n        KEEP(*(.sys.{sid}.{kind}_array .sys.{sid}.{kind}_array.*))\n        __bb_{sid}_{kind}_end = .;'
        script = replace_once(script, anchor, anchor + f'\n\t\t*(.sys.*.{sections} .sys.*.{sections}.*)' + extra)
    overlays=[]
    for system in sorted(systems):
        jit = f'__bb_{system}_jit_bytes' if system in cores else '0'
        if not re.fullmatch(r'[a-z][a-z0-9]{1,7}',system):raise ValueError('invalid overlay id')
        overlays.append(f"""
    .sys.exec.{system} ndrc_bss (INFO) :
    {{
        __bb_{system}_bss_start = .;
        *(.sys.{system}.bss .sys.{system}.bss.*)
        . = ALIGN(0x1000);
        __bb_{system}_bss_end = .;
    }} :NONE
    ASSERT(SIZEOF(.sys.exec.{system}) + {jit} + 0x2000 <= __bb_exec_size,
        "system statics and JIT exceed executable arena")
""")
    script = replace_once(script, '__bss_end__ = .;', """__bss_end__ = .;
    INCLUDE ../../../build/systems/arena-size.ld
""" + ''.join(f'    INCLUDE ../../../build/systems/{c}/jit-size.ld\n' for c in sorted(cores)) + """    __bb_image_end = .;
""" + ''.join(overlays) + """
    .sys.unclaimed __bb_image_end (INFO) : { *(.sys.*.bss .sys.*.bss.*) } :NONE
    ASSERT(SIZEOF(.sys.unclaimed) == 0, "unregistered system BSS overlay")
    . = __bb_image_end;""")
    return script

if __name__ == '__main__':
    # system-linker.py ID...: the app's SYSTEMS list (apps/<app>.mk)
    systems = sys.argv[1:]
    cores = [s for s in systems if (ROOT/'systems'/s/'core.mk').exists()]
    result = generate((ROOT/'build/3ds/core-first.ld').read_text(), (ROOT/'build/3ds/breadbox-library-text.ld').read_text(), systems, cores)
    out = ROOT/'build/3ds/core-first-breadbox.ld'
    if not out.exists() or out.read_text() != result:
        out.write_text(result)
