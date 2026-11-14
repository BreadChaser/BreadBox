#!/usr/bin/env python3
"""Offline real-core conformance; builds in private, automatically removed trees."""
from pathlib import Path
import os, subprocess, sys, tempfile, resource
from contextlib import nullcontext
r=Path(__file__).resolve().parents[2]
def run(args, **kw): subprocess.run(list(map(str,args)),check=True,**kw)
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
jobs=os.environ.get('BB_BUILD_JOBS','3')
assert jobs.isdecimal() and int(jobs)>0
sid=sys.argv[1]
if sid not in ('gba','nes'): raise SystemExit('No host runtime driver for '+sid)
build=os.environ.get('BB_CONFORMANCE_BUILD')
if build: Path(build).mkdir(parents=True,exist_ok=True)
with (nullcontext(build) if build else tempfile.TemporaryDirectory(prefix='bb-conformance-'+sid+'-')) as td:
 p=Path(td)
 import shutil
 shutil.rmtree(p/'run',ignore_errors=True)
 (p/'run'/('sdmc:/3ds/BreadBox/states/'+sid)).mkdir(parents=True)
 san='-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
 if sid=='gba':
  run(['make','-s','-C',p,'-f',r/'gba-core/Makefile','CORE_DIR='+str(r/'gba-core'),'platform=unix','HAVE_DYNAREC=0','CPU_ARCH=',
       'OPTIMIZE=-O1 -g','EXTRA_CFLAGS='+san,'LDFLAGS='+san,'SHARED=-shared','-j'+jobs])
  lib=p/'gpsp_libretro.so';rom=p/'fixture.gba'
  run([sys.executable,r/'tests/system_conformance/make-gba.py',rom])
 else:
  run([sys.executable,r/'tests/system_conformance/dialect.py',p])
  run(['make','-s','-C',p,'-f',r/'systems/nes/core/Makefile','CORE_DIR='+str(r/'systems/nes/core'),'CXX=c++','AR=ar',
       'FLAGS=-std=gnu++20 -O1 -g -DNDEBUG -fPIC -DNO_UNALIGNED_ACCESS -D__LIBRETRO__ -fno-rtti -fno-exceptions -DGIT_VERSION=\\"26bb785\\" '+san,'-j'+jobs])
  lib=p/'quicknes.so';rom=p/'fixture.nes'
  run(['c++','-shared',*san.split(),'-Wl,--whole-archive',p/'quicknes_libretro_ctr.a','-Wl,--no-whole-archive','-o',lib])
  run([sys.executable,r/'systems/nes/tests/make-rom.py',rom])
 run(['cc','-std=gnu11','-O1','-g',*san.split(),'-DSAVE_WRITER_TEST','-pthread',
      '-Isystems/common','-Iapp/ui','-Iinclude/breadbox','-Iplatform/3ds','-Itests/gba_saves','-Icore/deps/libretro-common/include',
      'tests/system_conformance/runtime.c','systems/common/retro_host.c','app/ui/bb_save.c','app/ui/bb_final.c','app/ui/bb_session.c','app/ui/bb_order.c',
      'app/ui/save_writer.c','app/ui/safefile.c','app/ui/history.c','-lz','-lm','-ldl','-o',p/'check'],cwd=r)
 run([p/'check',lib,rom,sid,p/'run'],env=dict(os.environ,ASAN_OPTIONS='detect_leaks=1:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'),timeout=120)
