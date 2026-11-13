#!/usr/bin/env python3
"""Private citro3d lighting implementations behind the original pinned entries.
Never edit the installed SDK. The public API and all other members stay intact.
"""
from pathlib import Path
import os, shutil, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
arm=Path(os.environ.get('DEVKITARM','/opt/devkitpro/devkitARM'))/'bin/arm-none-eabi-'
source=Path(os.environ.get('CTRULIB','/opt/devkitpro/libctru'))/'lib/libcitro3d.a'
out=root/'build/systems/gba/sdk/lib/libcitro3d.a';out.parent.mkdir(parents=True,exist_ok=True)
if out.exists() and out.stat().st_mtime_ns>=max(source.stat().st_mtime_ns,Path(__file__).stat().st_mtime_ns):raise SystemExit(0)
with tempfile.TemporaryDirectory(prefix='bb-light-pins-') as d:
 member=Path(d)/'lightenv.o'
 member.write_bytes(subprocess.check_output([str(arm)+'ar','p',str(source),'lightenv.o']))
 subprocess.run([str(arm)+'objcopy','--redefine-sym','C3Di_LightEnvUpdate=bb_gba_lightenv_update',
                 '--redefine-sym','C3Di_LightEnvDirty=bb_gba_lightenv_dirty',str(member)],check=True)
 shutil.copyfile(source,out)
 subprocess.run([str(arm)+'ar','rD',str(out),str(member)],check=True)
 subprocess.run([str(arm)+'ranlib','-D',str(out)],check=True)
