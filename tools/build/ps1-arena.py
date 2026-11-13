#!/usr/bin/env python3
"""Bind BreadBox's executable storage and cold PS1 request to the shell pool."""
from pathlib import Path
import os,subprocess
root=Path(__file__).resolve().parents[2]
src=root/'core/pcsx_rearmed_libretro_ctr.a'
out=root/'build/systems/ps1/pcsx_rearmed_libretro_ctr.a';out.parent.mkdir(parents=True,exist_ok=True)
# Fail if a future core introduces another memory-SVC site: the private
# binding is authorized only for new_dynarec_init's single cold PROT call.
objdump=os.environ.get('DEVKITARM','/opt/devkitpro/devkitARM')+'/bin/arm-none-eabi-objdump'
relocs=subprocess.check_output([objdump,'-r',str(src)],text=True)
section=''
sites=[]
for line in relocs.splitlines():
 if line.startswith('RELOCATION RECORDS FOR '):section=line
 if line.split() and line.split()[-1]=='svcControlProcessMemory':sites.append((section,line))
if len(sites)!=1 or sites[0][0]!='RELOCATION RECORDS FOR [.text.new_dynarec_init]:' or 'R_ARM_CALL' not in sites[0][1]:
 raise SystemExit('expected exactly one PS1 process-memory call relocation')
if not out.exists() or max(src.stat().st_mtime_ns,Path(__file__).stat().st_mtime_ns)>out.stat().st_mtime_ns:
 subprocess.run([os.environ.get('DEVKITARM','/opt/devkitpro/devkitARM')+'/bin/arm-none-eabi-objcopy','--globalize-symbol=ndrc_bss','--redefine-sym=svcControlProcessMemory=bb_ps1_exec_request',str(src),str(out)],check=True)

# The reservation follows the actual core type, not a copied struct size.
nm=os.environ.get('DEVKITARM','/opt/devkitpro/devkitARM')+'/bin/arm-none-eabi-nm'
rows=subprocess.check_output([nm,'-S',str(out)],text=True).splitlines()
sizes=[int(p[1],16) for row in rows if len(p:=row.split())==4 and p[-1]=='ndrc_bss']
if len(sizes)!=1: raise SystemExit('expected one sized ndrc_bss')
size=sizes[0]
(root/'build/systems/arena-size.ld').write_text('__bb_exec_size = %d;\n'%size)
(root/'build/systems/arena-size.h').write_text('#define BB_EXEC_BYTES %du\n'%size)
