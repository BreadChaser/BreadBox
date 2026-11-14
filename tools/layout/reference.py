#!/usr/bin/env python3
"""Resolve a digest-pinned reference from the public source archive, never private Git."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
ARM = Path(os.environ.get('DEVKITARM', '/opt/devkitpro/devkitARM')) / 'bin/arm-none-eabi-'


def identity(elf):
    # Debug paths may vary. Bind every loaded section, address, size and symbol;
    # executable changes and missing pins cannot hide in an otherwise valid ELF.
    # GNU nm uses locale collation for symbols sharing an address. Bind its
    # byte order explicitly; Docker and inherited host locales can differ.
    env = dict(os.environ, LC_ALL='C')
    rows = subprocess.check_output([sys.executable, '-I', str(ROOT / 'tools/build/elf-identity.py'), str(elf)], env=env, text=True)
    rows = '\n'.join(row for row in rows.splitlines() if not row.startswith(elf.stem + '.3dsx'))
    symbols = subprocess.check_output([str(ARM) + 'nm', '-n', '-S', str(elf)], env=env, text=True)
    return hashlib.sha256((rows + '\n' + symbols).encode()).hexdigest()


def verify(elf, meta):
    have = identity(elf)
    if have != meta['identity_sha256']:
        raise ValueError(f'{elf} is not the qualified public reference (identity {have}, want {meta["identity_sha256"]})')


def unpack(archive, target):
    # The pinned SDK image predates tarfile's filter= API. Accept only regular
    # files/directories with safe relative names; never extract links/devices.
    with tarfile.open(archive) as tar:
        for entry in tar.getmembers():
            name = Path(entry.name)
            parts = name.parts
            if name.is_absolute() or '..' in parts or not (entry.isfile() or entry.isdir()):
                raise ValueError('unsafe public source archive member: ' + entry.name)
            if len(parts) < 2:
                continue
            entry.name = str(Path(*parts[1:]))
            tar.extract(entry, target)


def build_environment(meta, target):
    env = dict(os.environ)
    for key in ('MAKEFLAGS', 'MFLAGS', 'MAKELEVEL', 'BB_EXPERIMENTAL_NES', 'EXTRA_DEFS', 'VERSION', 'PICA_LAYOUT'):
        env.pop(key, None)
    # A ceiling excludes parent discovery only when Git starts below it. The
    # public recipe also runs git -C at the archive root, so explicitly select
    # its absent .git directory. This forces the pinned metadata fallback even
    # inside a real clone, and overrides any inherited repository selection.
    env.update(SOURCE_DATE_EPOCH=str(meta['source_date_epoch']), PSX3D_COMMIT=meta['commit'],
               PSX3D_DESCRIBE=meta['commit'][:12], PYTHONDONTWRITEBYTECODE='1',
               GIT_DIR=str(target / '.git'), LC_ALL='C')
    return env


def require_build_root(meta, target):
    # Loaded assertion strings contain absolute __FILE__ paths, unlike debug
    # info. Rebuild at the qualified container path; prebuilt ELFs can move.
    if str(target) != meta['build_root']:
        raise ValueError('public reference build requires ' + meta['build_root']
                         + '; run the pinned container at the default cache path, or supply BREADBOX_BASELINE_ELF')


def resolve(meta):
    explicit = os.environ.get('BREADBOX_BASELINE_ELF')
    if explicit:
        elf = Path(explicit).expanduser().resolve()
        if not elf.is_file():
            raise ValueError(f'BREADBOX_BASELINE_ELF={elf} does not exist')
        verify(elf, meta)
        return elf
    out = Path(os.environ.get('LAYOUT_REFERENCE_OUT', ROOT / 'artifacts/layout-reference')).resolve()
    elf = out / 'ref' / meta['elf']
    if elf.is_file():
        verify(elf, meta)  # Tampering fails closed; never bless a cache's own hash.
        return elf
    if os.environ.get('BASELINE_BUILD_REFERENCE', '1') != '1':
        raise ValueError('accepted reference missing; enable BASELINE_BUILD_REFERENCE=1 or set BREADBOX_BASELINE_ELF')
    out.mkdir(parents=True, exist_ok=True)
    archive = out / 'source.tar.gz'
    if not archive.is_file():
        request = urllib.request.Request(meta['url'], headers={'User-Agent': 'BreadBox-layout-check'})
        with urllib.request.urlopen(request, timeout=60) as response, archive.open('wb') as dest:
            shutil.copyfileobj(response, dest)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != meta['archive_sha256']:
        raise ValueError(f'public reference source archive has wrong digest {digest}')
    target = out / 'ref'
    require_build_root(meta, target)
    if target.exists():
        shutil.rmtree(target)
    target.mkdir()
    unpack(archive, target)
    # The public 0.9.0 build driver forces -j4. This exact build-only recipe
    # correction limits its nested make without changing any compiler input.
    blob = target / 'tools/build/core-blob.py'
    original = blob.read_text()
    needle = "'-j4', 'all'"
    if original.count(needle) != 1:
        raise ValueError('public source build recipe differs from the qualified recipe')
    jobs = os.environ.get('BASELINE_JOBS', os.environ.get('BB_BUILD_JOBS', '3'))
    if not jobs.isdecimal() or int(jobs) < 1:
        raise ValueError('BASELINE_JOBS must be a positive integer')
    blob.write_text(original.replace(needle, f"'-j{int(jobs)}', 'all'"))
    env = build_environment(meta, target)
    with (out / 'build.log').open('w') as log:
        result = subprocess.run(['make', '-j' + jobs], cwd=target, env=env, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        raise ValueError(f'public reference build failed; see {out / "build.log"}')
    verify(elf, meta)
    return elf


def main():
    try:
        if 'BASELINE_META' in os.environ and os.environ.get('BB_LAYOUT_TEST') != '1':
            raise ValueError('BASELINE_META requires BB_LAYOUT_TEST=1 (test-only override)')
        meta = json.loads(Path(os.environ.get('BASELINE_META', ROOT / 'tools/layout/baseline.json')).read_text())
        public = meta['public_reference']
        if (meta['build'] != 2650 or meta['commit'] != 'ffbe4132'
                or meta['commit_full'] != 'ffbe4132ae9099ef355b7db5703498f31def59a2'):
            raise ValueError('original build-2650 acceptance identity changed')
        if (meta['sha256'] != '84e79a1bea4043ce20bae23ea00d5ec50a1bb38f6a24f949326f2ea7334a7656'
                or public['url'] != 'https://codeload.github.com/BreadChaser/BreadBox/tar.gz/' + public['commit']
                or public['commit'] != '39148a3c0e2a84ec82ce4ff95a14e9369c1162bd'):
            raise ValueError('reference acceptance/source identity changed')
        # Qualification binds the public tree to the retained exact technical
        # before/after decisions. Private human notes are not a runtime input.
        manifest = ROOT / 'tools/layout/accepted-2650.json'
        if hashlib.sha256(manifest.read_bytes()).hexdigest() != public['qualification_sha256']:
            raise ValueError('build-2650 qualification manifest identity changed')
        pins = public['frozen_names']
        if set(pins) != {'pinned-names.json', 'system-pinned-names.json'}:
            raise ValueError('public reference frozen pin identities missing')
        for name, digest in pins.items():
            if hashlib.sha256((manifest.parent / name).read_bytes()).hexdigest() != digest:
                raise ValueError('public reference frozen pin identity changed: ' + name)
        print(resolve(public))
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError, tarfile.TarError) as error:
        sys.exit('baseline-check: ' + str(error))


if __name__ == '__main__':
    main()
