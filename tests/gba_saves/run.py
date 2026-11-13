#!/usr/bin/env python3
"""Execute GBA save routines against a fake core and real SD safety helpers."""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
# Persistence policy now lives in the shell; compile the production service.
subprocess.run(['bash',str(root/'tests/save_service/run.sh')],check=True)
with tempfile.TemporaryDirectory() as d:
 exe=str(Path(d)/'sleep')
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-pthread','-fsanitize=address,undefined',str(root/'tests/gba_saves/sleep.c'),'-o',exe],check=True)
 subprocess.run([exe],check=True)
with tempfile.TemporaryDirectory() as d:
 exe=str(Path(d)/'playlog')
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root/'tests/app/host'),'-I'+str(root/'app/ui'),'-I'+str(root/'include/breadbox'),'-I'+str(root/'platform/3ds'),'-I'+str(root/'systems/gba'),'-I'+str(root/'core/deps/libretro-common/include'),'-include',str(root/'tests/gba_saves/host.h'),str(root/'tests/gba_saves/playlog.c'),str(root/'app/ui/playlog.c'),str(root/'app/ui/safefile.c'),'-o',exe],check=True)
 subprocess.run([exe],check=True)

with tempfile.TemporaryDirectory() as d:
 exe=str(Path(d)/'writer')
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-DSAVE_WRITER_TEST','-pthread','-fsanitize=address,undefined','-I'+str(root/'app/ui'),'-I'+str(root/'include/breadbox'),'-I'+str(root/'platform/3ds'),'-I'+str(root/'systems/gba'),'-I'+str(root/'tests/gba_saves'),str(root/'tests/gba_saves/writer.c'),str(root/'app/ui/save_writer.c'),'-o',exe],check=True)
 subprocess.run([exe],check=True)
with tempfile.TemporaryDirectory() as d:
 exe=str(Path(d)/'adoption')
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root/'app/ui'),'-I'+str(root/'include/breadbox'),'-I'+str(root/'platform/3ds'),'-I'+str(root/'systems/gba'),str(root/'tests/gba_saves/adoption.c'),str(root/'systems/gba/gba_saveid.c'),str(root/'systems/gba/gba_settings.c'),str(root/'app/ui/safefile.c'),'-lz','-o',exe],check=True)
 subprocess.run([exe],cwd=d,check=True)
