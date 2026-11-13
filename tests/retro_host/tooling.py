#!/usr/bin/env python3
"""Regression cases for architecture review should-fixes 3/4/5."""
from pathlib import Path
import subprocess,tempfile,shutil,json,os
r=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as td:
 t=Path(td)
 # Scaffold strings compile with quotes/backslashes, metacharacters fail before
 # creating any directory. No shell command is used for those inputs.
 args=['python3',str(r/'tools/new-system.py')]
 for core in ('../outside','/absolute','core;echo','core$(id)','a b','core#x'):
  result=subprocess.run(args+['gb','Game Boy','GB','--core',core,'--root',str(t)],capture_output=True)
  assert result.returncode and not (t/'systems/gb').exists(),core
 for ext in ('nes,"bad','nes;','../gb'):
  assert subprocess.run(args+['gb','Game Boy','GB','--ext',ext,'--root',str(t)],capture_output=True).returncode
 subprocess.run(args+['gb','Game "Boy" \\ Portable','G"B','--hint','a\\b"c','--root',str(t)],check=True,capture_output=True)
 subprocess.run(['cc','-std=c11','-fsyntax-only','-Wall','-Werror','-I'+str(r/'app/ui'),'-I'+str(r/'include/breadbox'),'-I'+str(r/'platform/3ds'),str(t/'systems/gb/sys_gb.c')],check=True)
 subprocess.run(args+['nes','NES','NES','--core','systems/nes/core','--ext','nes','--root',str(t)],check=True,capture_output=True)
 assert 'bb_retro_ctr_run' in (t/'systems/nes/sys_nes.c').read_text()
 # Execute the real ratchet in an isolated tree with a newly registered id.
 (t/'tools/architecture').mkdir(parents=True);(t/'app/ui').mkdir(parents=True)
 for name in ('shell-guard.py','shell-baseline.json'):shutil.copy(r/'tools/architecture'/name,t/'tools/architecture'/name)
 shell=t/'app/ui/example.c';shell.write_text('int gb_leak;\n')
 result=subprocess.run(['python3',str(t/'tools/architecture/shell-guard.py'),'--strict'],capture_output=True,text=True)
 assert result.returncode and 'gb_leak' in result.stdout
 shell.write_text('const char *x="gb";\n');assert subprocess.run(['python3',str(t/'tools/architecture/shell-guard.py'),'--strict'],capture_output=True).returncode
# Exercise the actual make parser before it can select a duplicate via VPATH.
with tempfile.TemporaryDirectory() as td:
 t=Path(td);(t/'a').mkdir();(t/'b').mkdir()
 (t/'a/duplicate.c').write_text('int a;')
 (t/'b/duplicate.cpp').write_text('int b;')
 result=subprocess.run(['make','-n','-C',str(r/'build/3ds'),'APP=breadbox',
     'SOURCES='+str(t/'a')+' '+str(t/'b')],env=dict(os.environ,DEVKITARM=os.environ.get('DEVKITARM','/opt/devkitpro/devkitARM')),capture_output=True,text=True)
 assert result.returncode and 'Duplicate source basenames: duplicate' in result.stderr,result.stderr
make=(r/'build/3ds/Makefile').read_text()
assert '$(sort $(filter-out $(LINK_ORDER),$(CFILES)))' in make
subprocess.run(['python3',str(r/'tools/build/retro-legacy-guards.py'),'--check'],check=True)
print('scaffold escaping/validation, registered-id shell ratchet and generated guards PASS')
