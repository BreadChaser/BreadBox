#!/usr/bin/env python3
"""core-blob.py ID...: build each system's libretro core into one namespaced object.

For every system that has systems/<id>/core.mk (PS1 is the app's base core and
has none) this runs the core's own libretro Makefile into build/systems/<id>/,
links the archive into build/systems/<id>/<id>_core_blob.o with every global
symbol prefixed and every section renamed .sys.<id>.*, so the system linker
script (system-linker.py) places its BSS in the shared executable arena and
nothing clashes with the PS1 core. It also writes jit-size.ld: the bytes of
translation cache the core takes from that arena (0 without a JIT).

core.mk keys (make syntax, prefixed with the id; see docs/dev/new-system.md):
  <id>_CORE_DIR       vendored core source, relative to the repository root
  <id>_CORE_MAKE      arguments for its libretro Makefile (platform=ctr ...)
  <id>_CORE_ARCHIVE   the static library that build produces
  <id>_CORE_PREFIX    prefix for every global symbol of the core
  <id>_CORE_JIT_HEADER, <id>_CORE_JIT_DEFINES, <id>_CORE_JIT_EXPR
                      optional: a header of the core, the defines its CTR build
                      uses, and the C expression of its translation-cache bytes
build/3ds/Makefile reads <id>_CORE_INCLUDES and <id>_CORE_LIBS from the same file."""
from pathlib import Path
import os, re, shlex, subprocess, sys, tempfile

ROOT = Path(__file__).resolve().parents[2]
env = dict(os.environ)
arm = Path(env.get('DEVKITARM', '/opt/devkitpro/devkitARM')) / 'bin' / 'arm-none-eabi-'
env.setdefault('CTRULIB', '/opt/devkitpro/libctru')


def settings(system):
    """The <id>_CORE_* assignments of systems/<id>/core.mk, without the prefix."""
    out = {}
    for line in (ROOT / 'systems' / system / 'core.mk').read_text().splitlines():
        m = re.match(r'\s*%s_CORE_(\w+)\s*:?=\s*(.*?)\s*(?:#.*)?$' % re.escape(system), line)
        if m:
            out[m[1]] = m[2]
    for key in ('DIR', 'MAKE', 'ARCHIVE', 'PREFIX'):
        if key not in out:
            sys.exit('systems/%s/core.mk: missing %s_CORE_%s' % (system, system, key))
    return out


def build(system):
    cfg = settings(system)
    if cfg.get('PREPARE'):
        subprocess.run(shlex.split(cfg['PREPARE']), cwd=ROOT, env=env, check=True)
    core = ROOT / cfg['DIR']
    out = ROOT / 'build' / 'systems' / system
    out.mkdir(parents=True, exist_ok=True)
    subprocess.run(['make', '-C', str(out), '-f', str(core / 'Makefile'), 'CORE_DIR=' + str(core),
                    *cfg['MAKE'].split(), '-j4', 'all'], env=env, check=True)
    raw = out / (system + '.raw.o')
    blob = out / (system + '_core_blob.o')
    archive = out / cfg['ARCHIVE']
    newest = max(archive.stat().st_mtime_ns, Path(__file__).stat().st_mtime_ns,
                 (ROOT / 'systems' / system / 'core.mk').stat().st_mtime_ns)
    if cfg.get('OBJECT_ORDER'):
        newest = max(newest, (ROOT/cfg['OBJECT_ORDER']).stat().st_mtime_ns)
    if not blob.exists() or newest > blob.stat().st_mtime_ns:
        if cfg.get('OBJECT_ORDER'):
            # Archive updates append new members: without a manifest an
            # incremental build and a fresh build can have different I-cache sets.
            names = subprocess.check_output([str(arm)+'ar','t',str(archive)],text=True).splitlines()
            if len(names)!=len(set(names)):
                sys.exit('duplicate archive member basename: '+str(archive))
            order = (ROOT/cfg['OBJECT_ORDER']).read_text().splitlines()
            if len(order)!=len(set(order)) or set(order)!=set(names):
                sys.exit('core object-order manifest differs from archive: '+system)
            with tempfile.TemporaryDirectory(dir=out) as td:
                ordered=[]
                for name in order:
                    path=Path(td)/name
                    path.write_bytes(subprocess.check_output([str(arm)+'ar','p',str(archive),name]))
                    ordered.append(str(path))
                subprocess.run([str(arm)+'ld','-r',*ordered,'-o',str(raw)],check=True)
        else:
            subprocess.run([str(arm) + 'ld', '-r', '--whole-archive', str(archive), '--no-whole-archive', '-o', str(raw)], check=True)
        symbols = subprocess.check_output([str(arm) + 'nm', '-g', '--defined-only', str(raw)], text=True)
        rename = out / 'symbols.txt'
        rename.write_text(''.join('%s %s%s\n' % (line.split()[-1], cfg['PREFIX'], line.split()[-1])
                                  for line in symbols.splitlines() if line.split()))
        sections = subprocess.check_output([str(arm) + 'objdump', '-h', str(raw)], text=True)
        args = [str(arm) + 'objcopy', '--redefine-syms=' + str(rename)]
        for line in sections.splitlines():
            words = line.split()
            if len(words) >= 3 and words[0].isdigit():
                name = words[1]
                if any(name == p or name.startswith(p + '.') for p in ('.text', '.rodata', '.data', '.bss', '.init_array', '.fini_array')):
                    args += ['--rename-section', '%s=.sys.%s%s' % (name, system, name)]
        subprocess.run(args + [str(raw), str(blob)], check=True)
    subprocess.run([sys.executable, str(ROOT / 'tools/build/retro-bindings.py'),
                    system, cfg['PREFIX'], str(out / ('retro_bindings_' + system + '.c')), cfg.get('HOST_OMIT','')], check=True)
    jit = 0
    if 'JIT_EXPR' in cfg:
        # Ask the same preprocessor that compiles the core for its cache sizes.
        expr = subprocess.check_output(
            [str(arm) + 'gcc', '-E', '-P', *cfg.get('JIT_DEFINES', '').split(), '-I' + str(core), '-xc', '-'],
            input='#include "%s"\n%s\n' % (cfg['JIT_HEADER'], cfg['JIT_EXPR']), text=True).strip().splitlines()[-1]
        # Only arithmetic emitted by the trusted cache-size definitions is accepted.
        if not re.fullmatch(r'[0-9xXa-fA-F() +*\t-]+', expr):
            sys.exit('unexpected JIT size expression: ' + expr)
        jit = eval(expr, {'__builtins__': {}}, {})
    (out / 'jit-size.ld').write_text('__bb_%s_jit_bytes = %d;\n' % (system, jit))


if __name__ == '__main__':
    for system in sys.argv[1:]:
        build(system)
