#!/usr/bin/env python3
"""Fail closed before packaging updater builds for publication."""
from pathlib import Path
import argparse,sys
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('elf',type=Path)
p.add_argument('--private',action='store_true')
p.add_argument('--development-package',action='store_true')
a=p.parse_args()
if any(marker in a.elf.read_bytes() for marker in (b'BB_UPDATE_DEV_URL=', b'BB_UI_FIXTURES=1')) and not a.development_package:
 sys.exit('Refusing update development URL in CIA/release packaging; emulator fixtures require cia-development')
print('Release updater URL checks passed')
