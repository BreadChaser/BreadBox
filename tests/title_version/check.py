#!/usr/bin/env python3
"""Protect CIA field boundaries and catch makerom's version macro collision."""
import importlib.util
from pathlib import Path
import struct
import subprocess
import os
import tempfile
import sys
sys.dont_write_bytecode = True
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('title_version', ROOT / 'tools/build/title-version.py')
v = importlib.util.module_from_spec(spec)
spec.loader.exec_module(v)


class Versions(unittest.TestCase):
    def test_psx3d_released_mapping(self):
        # PSX3D's released titles (another title ID, frozen at 0.5.2) keep their numbers.
        for version, counter in [('0.1.0', 1027), ('0.3.1', 3076), ('0.4.1', 4100), ('0.5.0', 5123),
                                 ('0.5.2', 5125), ('0.5.12', 5135)]:
            self.assertEqual(v.encode_psx3d(version), counter)
        for version in ['0.5.13', '0.16.0', '4.0.0']:
            with self.assertRaises(ValueError):
                v.encode_psx3d(version)

    def test_breadbox_mapping(self):
        for version, counter in [('0.9.0', 25615), ('0.9.1', 25631), ('0.10.0', 26639), ('1.0.0', 32783),
                                 ('1.0.1', 32799), ('1.1.0', 33807), ('2.15.63', 65535)]:
            self.assertEqual(v.encode(version), counter)
        # The first public release installs over every previous development CIA (all 16xxx,
        # the highest 16641 at the 0.9.0 release) and the header's version is the one packaged.
        self.assertGreater(v.encode('0.9.0'), 16641 + 1000)
        self.assertEqual(v.header_version(ROOT / 'apps/breadbox/version.h'), '0.9.2')

    def test_no_alias_or_field_carry(self):
        for version in ['0.16.0', '3.0.0', '2.16.0', '0.0.64', '1.0.64',
                        '0.05.0', '-1.0.0', '0.5.0-rc4', '0.5']:
            with self.subTest(version=version), self.assertRaises(ValueError):
                v.encode(version)
        # Every permitted version has its own counter, every later version a larger one (the updater installs only
        # a larger number), and a patch's private test counters (micro 0..14) sit above the previous release.
        seen, last = set(), -1
        for major in range(3):
            for minor in range(16):
                for patch in range(64):
                    if v.BASE + 16 * major + minor > 63:
                        continue
                    counter = v.encode(f'{major}.{minor}.{patch}')
                    self.assertNotIn(counter, seen)
                    self.assertGreater(counter, last)
                    self.assertGreater(counter - 15, last)
                    seen.add(counter)
                    last = counter
                    self.assertEqual(counter >> 10, v.BASE + major * 16 + minor)
                    self.assertEqual(counter & 1023, patch * 16 + 15)
        # The development-only counters (16399..16400) and every private test CIA so far (16xxx) are no release.
        self.assertFalse(set(range(16000, 17000)) & {c for c in seen if c >= v.encode('0.9.0')})

    def test_private_counters(self):
        for n in [0, 5136, 5147, 65535]:
            self.assertEqual(v.counter(str(n)), n)
        for n in ['-1', '65536', '5.0', '', '05136']:
            with self.subTest(n=n), self.assertRaises(ValueError):
                v.counter(n)

    def test_portable_contract(self):
        subprocess.run([sys.executable, str(ROOT / 'tools/build/title-version.py'), 'header', '--check'], check=True)
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / 'portable'
            subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror',
                            str(ROOT / 'tests/title_version/portable.c'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
        for value in range(65536):
            result = v.decode(value)
            if value >= 25615 and value & 15 == 15:
                self.assertEqual(v.encode('.'.join(map(str, result))), value)
            else:
                self.assertIsNone(result)
        for value in (16641, 25600, 25614, 25616, 25630):
            self.assertIsNone(v.decode(value))

    def test_real_tmd_offsets(self):
        # Nonaligned section sizes, all supported RSA/ECDSA signature layouts.
        # 5136 is the first current private counter corrupted by stock makerom.
        for signature, size in [(0x10000, 0x240), (0x10001, 0x140), (0x10002, 0x80),
                                (0x10003, 0x240), (0x10004, 0x140), (0x10005, 0x80)]:
            align = lambda n: (n + 63) & ~63
            tmd_at = align(0x2020) + align(23) + align(81)
            data = bytearray(tmd_at + size + 0x9e)
            struct.pack_into('<IHHIII', data, 0, 0x2020, 0, 0, 23, 81, size + 0x9e)
            struct.pack_into('>I', data, tmd_at, signature)
            struct.pack_into('>H', data, tmd_at + size + 0x9c, 5136)
            with tempfile.TemporaryDirectory() as tmp:
                cia = Path(tmp) / 'test.cia'
                cia.write_bytes(data)
                self.assertEqual(v.cia_version(cia), 5136)
                for title in [0x000400000B0B0B00, 0x000400000C3D3D00]:
                    struct.pack_into('>Q', data, tmd_at + size + 0x4c, title)
                    cia.write_bytes(data)
                    self.assertEqual(v.cia_version(cia, title), 5136)
                    with self.assertRaises(ValueError):
                        v.cia_version(cia, title ^ 0x100)

                # Truncation and invalid signature cannot silently pass validation.
                cia.write_bytes(data[:-1])
                with self.assertRaises(ValueError):
                    v.cia_version(cia)
                struct.pack_into('>I', data, tmd_at, 0xffffffff)
                cia.write_bytes(data)
                with self.assertRaises(ValueError):
                    v.cia_version(cia)


if __name__ == '__main__':
    unittest.main()
