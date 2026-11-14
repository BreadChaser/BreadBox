"""Shared object/function discovery for frontend checks and public qualification."""
from pathlib import Path
import os
import re
import subprocess

ARM = str(Path(os.environ.get('DEVKITARM', '/opt/devkitpro/devkitARM')) / 'bin/arm-none-eabi-')
NM = ARM + 'nm'
FRONTEND_OBJECTS = {
    'audio', 'audio_resample', 'audio_tsm', 'display', 'emu', 'gamecfg',
    'gpu_cache', 'gpustereo', 'gpustereo_core', 'input', 'libui', 'picadirect',
    'renderqueue', 'statsview', 'ui', 'ps1_controls', 'ui_play_controls', 'uploadskip', 'video', 'widescreen',
}


def frontend_names(build, obj, linker):
    path = Path(build) / (obj + '.o')
    if not path.is_file():
        return set()
    rows = subprocess.check_output([NM, str(path)], text=True).splitlines()
    names = {p[2] for row in rows if len(p := row.split()) == 3 and p[1] in 'tT'}
    table = subprocess.check_output([ARM + 'objdump', '-t', str(path)], text=True)
    names -= {row.split()[-1] for row in table.splitlines() if '.psx3d_cold.text' in row}
    if obj in ('ui', 'libui', 'gpustereo_core'):
        blocks = re.findall(r'\b' + obj + r'\.o\(([^)]*)\)', linker)
        names &= {name for block in blocks for name in re.findall(r'\.text\.(?:unlikely\.)?([a-z_0-9.]+)', block)}
    return names
