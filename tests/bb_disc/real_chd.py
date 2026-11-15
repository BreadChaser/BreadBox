#!/usr/bin/env python3
"""The production CHD probe against the real vendored libchdr, both built the way the 3DS build
builds them: libchdr with the core's ctr USE_LIBRETRO_VFS setting (evaluated from
core/Makefile.libretro), app/ui/bb_disc.c with its own flags from build/3ds/Makefile. Issue #1:
the core read CHDs through libretro file streams while the probe passed a C FILE, so every CHD
was refused on the console. The discs are tiny synthetic uncompressed CHD v5 files; no game data."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import os, re, struct, subprocess, sys, tempfile

root = Path(__file__).resolve().parents[2]
core = root / 'core'
lc = core / 'deps/libretro-common'
chdr = core / 'deps/libchdr'


def core_setting(name):
    out = subprocess.run(['make', '-s', '-C', str(core), '-f', 'Makefile.libretro', 'platform=ctr', 'CTRULIB=/none',
                          '--eval', f'bb-print-setting: ; @echo BB_SETTING=$({name})', 'bb-print-setting'],
                         capture_output=True, text=True, check=True).stdout
    return re.findall(r'^BB_SETTING=(.*)$', out, re.M)[-1].strip()


def disc_flags():
    text = (root / 'build/3ds/Makefile').read_text()
    return [w for m in re.findall(r'^bb_disc\.o:\s*CFLAGS\s*\+=\s*(.*)$', text, re.M) for w in m.split()]


def chd(path, unit, hunk, sectors, pvd_at, system):
    """Uncompressed CHD v5: header, a raw map of 4-byte block numbers, then the hunks."""
    data = bytearray(unit * sectors)
    for s in range(sectors):
        if unit != 2048:
            base = s * unit
            data[base:base + 12] = b'\0' + b'\xff' * 10 + b'\0'
            data[base + 15] = 2
    p = 16 * unit + pvd_at
    data[p] = 1; data[p + 1:p + 6] = b'CD001'; data[p + 6] = 1
    data[p + 8:p + 40] = system.ljust(32, b' ')
    hunks = -(-len(data) // hunk)
    data += bytes(hunks * hunk - len(data))
    first = -(-(124 + 4 * hunks) // hunk)
    head = bytearray(124)
    head[0:8] = b'MComprHD'
    struct.pack_into('>II', head, 8, 124, 5)
    struct.pack_into('>QQQII', head, 32, unit * sectors, 124, 0, hunk, unit)
    blob = bytes(head) + b''.join(struct.pack('>I', first + i) for i in range(hunks))
    path.write_bytes(blob + bytes(first * hunk - len(blob)) + bytes(data))


def main():
    vfs = core_setting('USE_LIBRETRO_VFS') == '1'
    miniz = core_setting('USE_MINIZ') == '1'
    flags = disc_flags()
    # The contract issue #1 broke: the probe must open files the way the core's libchdr reads them.
    assert vfs == ('-DUSE_LIBRETRO_VFS' in flags), \
        f'core ctr USE_LIBRETRO_VFS={int(vfs)} but build/3ds/Makefile builds bb_disc.o with {flags}'
    lib = [f'{chdr}/src/libchdr_{n}.c' for n in ('bitstream', 'cdrom', 'chd', 'codec_cdfl', 'codec_cdlz', 'codec_cdzl',
           'codec_cdzs', 'codec_flac', 'codec_huff', 'codec_lzma', 'codec_zlib', 'codec_zstd', 'flac', 'huffman')]
    lib += [f'{chdr}/deps/lzma-26.02/src/LzmaDec.c', f'{chdr}/deps/zstd-1.5.7/zstddeclib.c']
    incs = ['-I' + str(chdr / 'include'), '-I' + str(lc / 'include')]
    defs = incs + ['-I' + str(chdr / 'deps/lzma-26.02/include')]
    if miniz:
        lib.append(f'{chdr}/deps/miniz-3.1.2/miniz.c')
        defs += ['-I' + str(chdr / 'deps/miniz-3.1.2'), '-DUSE_MINIZ', '-DMINIZ_NO_STDIO', '-DMINIZ_NO_DEFLATE_APIS',
                 '-DMINIZ_NO_ARCHIVE_APIS']
    else:
        defs.append('-DCHDR_SYSTEM_ZLIB')
    if vfs:
        defs.append('-DUSE_LIBRETRO_VFS')
        lib += [f'{lc}/{n}.c' for n in ('compat/compat_strl', 'compat/compat_posix_string', 'compat/fopen_utf8',
                'encodings/encoding_utf', 'file/file_path', 'file/file_path_io', 'file/retro_dirent',
                'streams/file_stream', 'streams/file_stream_transforms', 'string/stdstring', 'string/rstrtod',
                'time/rtime', 'vfs/vfs_implementation')]
    with tempfile.TemporaryDirectory() as tmp:
        t = Path(tmp)
        cc = 'cc'

        def build(i_src):
            i, src = i_src
            obj = t / f'{i}.o'
            subprocess.run([cc, '-std=gnu11', '-O1', '-w', *defs, '-c', src, '-o', str(obj)], check=True)
            return str(obj)
        with ThreadPoolExecutor(3) as pool:
            objs = list(pool.map(build, enumerate(lib)))
        main_c = t / 'main.c'
        main_c.write_text('''#include "bb_disc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void log_printf(const char *fmt, ...) { (void)fmt; }
int main(int argc, char **argv)
{
    for (int i = 1; i + 1 < argc; i += 2) {
        int got = (int)bb_disc_chd_kind(argv[i]);
        printf("%s %d\\n", argv[i], got);
        if (got != atoi(argv[i + 1])) return 1;
    }
    return 0;
}
''')
        san = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all']
        # Only the probe's own Makefile flags: the library's defines must not leak into it.
        subprocess.run([cc, '-std=c99', '-Wall', '-Wextra', '-Werror', '-g', *san, *flags, *incs,
                        '-I' + str(root / 'app/ui'), '-I' + str(root / 'include/breadbox'),
                        str(root / 'app/ui/bb_disc.c'), str(main_c), *objs, '-lm', '-o', str(t / 'probe')], check=True)
        cases = [('ps1-cd.chd', 2448, 19584, 'PLAYSTATION', 2),   # chdman createcd default geometry
                 ('ps1-raw.chd', 2352, 2352 * 4, 'PLAYSTATION', 2),
                 ('ps1-dvd.chd', 2048, 4096, 'PLAYSTATION', 2),     # createdvd: 2048-byte sectors
                 ('pc-cd.chd', 2448, 19584, 'PC', 0)]               # readable, not PS1: refused
        args = []
        for name, unit, hunk, system, want in cases:
            chd(t / name, unit, hunk, 32, 0 if unit == 2048 else 24, system.encode())
            args += [str(t / name), str(want)]
        (t / 'badheader.chd').write_bytes(b'MComprHD' + bytes(200))   # unreadable here: kept (1), never hidden
        args += [str(t / 'badheader.chd'), '1', str(t / 'missing.chd'), '1']
        r = subprocess.run([str(t / 'probe'), *args], capture_output=True, text=True)
        sys.stdout.write(r.stdout); sys.stderr.write(r.stderr)
        if r.returncode:
            sys.exit('bb_disc real libchdr: FAILED')
    print(f'bb_disc: real libchdr (ctr VFS={int(vfs)}) reads synthetic CHDs: PASS')


main()
