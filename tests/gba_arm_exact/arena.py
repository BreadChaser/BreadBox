#!/usr/bin/env python3
"""Build real ARM cores, placing candidate BSS/JIT in the CTR-shaped static loan.
Private fixtures are discovered locally, never copied into the repository.
"""
from pathlib import Path
import os,subprocess,shutil,tempfile,concurrent.futures,hashlib,json
r=Path(__file__).resolve().parents[2]
d=Path(os.environ['PSX3D_HOST_TOOLS'])
out=Path(os.environ.get('ARENA_EXACT_OUT',str(d/'breadbox/system-layer/r2-arm-exact')));out.mkdir(parents=True,exist_ok=True)
cc=d/'toolchains/bin/arm-linux-gnueabihf-gcc';ld=d/'toolchains/bin/arm-linux-gnueabihf-ld'
q=d/'dl/qemu-full/build/qemu-arm';sysroot=d/'toolchains/arm-gnu-14.2/arm-none-linux-gnueabihf/libc'
p=d/'breadbox/gba/perf-compat-work'
fixtures=sorted((p/'roms/Games/Classic NES Series').glob('*.gba'))+[x for x in sorted((p/'host-roms').glob('*.gba')) if not x.name.startswith('Classic NES')]
assert len(fixtures)==18,len(fixtures)
ctr=subprocess.check_output(['/opt/devkitpro/devkitARM/bin/arm-none-eabi-nm','-S',r/'build/3ds/BreadBox.elf'],text=True)
loan=[line.split() for line in ctr.splitlines() if line.endswith(' ndrc_bss')][0]
loan_base,loan_bytes=int(loan[0],16),int(loan[1],16)
def run(cmd,log,**kw):
 with (out/log).open('w') as f:subprocess.run(list(map(str,cmd)),stdout=f,stderr=f,check=True,**kw)
with tempfile.TemporaryDirectory(prefix='bb-arena-arm-') as td:
 tmp=Path(td)
 for kind in ['base','arena','reuse']:
  core=tmp/kind;shutil.copytree(r/'gba-core',core,ignore=shutil.ignore_patterns('*.o','*.a','*.so'))
  flags='-DSMALL_TRANSLATION_CACHE -DROM_BUFFER_SIZE=4'+(' -DBREADBOX_ARENA' if kind!='base' else '')
  run(['make','-C',core,'platform=armv6-hardfloat','CC='+str(cc),'CXX='+str(cc).replace('gcc','g++'),'EXTRA_CFLAGS='+flags,'LDFLAGS=-shared','-j4'],kind+'-build.log')
  objs=list(core.rglob('*.o'))
  # Partial-link the SAME objects that make the shared core, then move all BSS
  # as one unit. The static runner keeps every branch within ARM reach.
  run([ld,'-r','-o',tmp/(kind+'.o'),*objs],kind+'-partial.log')
  args=[cc,'-O2','-no-pie','-I'+str(core/'libretro/libretro-common/include'),r/'tests/gba_arm_exact/runner.c',tmp/(kind+'.o'),'-lm','-ldl','-lrt']
  if kind!='base':
   script=tmp/'arena.ld';script.write_text(('''SECTIONS {
 .bb_loan LOAN_BASE (NOLOAD) : {
 __arena_start = .;
 *LOAN_OBJECT.o(.bss .bss.* COMMON)
 . = ALIGN(4096); __arena_statics_end = .;
 . = __arena_start + LOAN_BYTES; __arena_end = .;
 }
} INSERT BEFORE .bss;
''').replace('LOAN_OBJECT',kind).replace('LOAN_BASE',hex(loan_base)).replace('LOAN_BYTES',str(loan_bytes)))
   args+=['-I'+str(r/'app/ui'),'-I'+str(r/'include/breadbox'),'-I'+str(r/'platform/3ds'),'-I'+str(r/'systems/gba'),r/'tests/gba_arm_exact/arena.c','-Wl,-T,'+str(script)]
  if kind=='reuse':args+=['-DARENA_REUSE']
  args+=['-o',tmp/(kind+'-runner')]
  run(args,kind+'-link.log')
 rows=[]
 def fixture(task):
  kind,rom=task;label=rom.stem;frames=1800 if 'Video' in label else 600
  state=tmp/(kind+'-'+label+'.state');log=kind+'-'+label+'.log'
  run([q,'-L',sysroot,tmp/(kind+'-runner'),'1',rom,frames,'0','-',p/'video-keys.txt' if 'Video' in label else '/dev/null','-','-',state],log,env=dict(os.environ,AUDIT='1',REQUIRE_JIT='1'),timeout=180)
  lines=(out/log).read_text().splitlines()
  row=dict(kind=kind,fixture=label,hash=next(x for x in lines if x.startswith('frames=')),jit=next(x for x in lines if x.startswith('jit_used=')),state=hashlib.sha256(state.read_bytes()).hexdigest());print(json.dumps(row),flush=True);return row
 with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:rows=list(pool.map(fixture,[(k,f) for f in fixtures for k in ['base','arena','reuse']]))
 (out/'results.json').write_text(json.dumps(rows,indent=2))
 for rom in fixtures:
  pairs=[x for x in rows if x['fixture']==rom.stem]
  assert len({x['hash'] for x in pairs})==len({x['state'] for x in pairs})==1,pairs
 print('18 ARM JIT fresh/reused arena fixtures: exact video/PCM/state PASS')
