#!/usr/bin/env python3
"""Remove each new fail-closed/retry guard independently; assertions must catch it.
Never modifies the checkout; builds small mutants sequentially on the home disk.
"""
from pathlib import Path
import os,resource,subprocess,tempfile
R=Path(__file__).resolve().parents[2]
flags=['cc','-std=gnu11','-O1','-g','-Wno-sign-compare','-Wno-misleading-indentation']
flags += ['-I'+str(R/p) for p in ['core/include','systems/ps1','app/ui','include/breadbox','platform/3ds','tests/app/host','core/deps/libretro-common/include']]
wrap=['state_check','memalign','realloc','fwrite','mkdir','lstat','stat','fopen','opendir','open']
source=R/'systems/ps1/breadbox_import.c';ui=R/'app/ui/breadbox_import_ui.c'
mutants=[
 ('not-found',source,'if(errno==ENOENT || errno==ENOTDIR)return 0;','if(false)return 0;',10),
 ('mutation-fence',source,'return ok && (!write || !resource_error);','return ok;',10),
 ('probe-resource',source,'resource_error=true;return -1;','return -1;',10),
 ('exists-unknown',source,'return probe(p,&st)!=0;','return probe(p,&st)==1;',10),
 ('regular-unknown',source,'return probe(p,st)==1 &&','return probe(p,st)!=0 &&',10),
 ('prepare-probe',source,'if(resource_error || budget_cancelled){free(prior);import_failure(r,"probe-resource",d);return;}','',8),
 ('retry-bound',ui,'++j->memory_attempts>=6','++j->memory_attempts>=7',None),
 ('retry-backoff',ui,'(1ULL<<j->memory_attempts)','2ULL',None),
 ('release-writer',ui,'bb_writer_end();j->writer=false;goto measured;','goto measured;',None),
 ('reacquire-writer',ui,'if(!job->writer){if(!bb_writer_begin())return;job->writer=true;}','',None),
 ('log-once',ui,'if(!j->memory_wait)log_printf','log_printf',None),
 ('notice-once',ui,'if(!job->memory_notice){','{',None),
]
with tempfile.TemporaryDirectory(prefix='import-mutations-') as tmp:
 T=Path(tmp)
 for name,file,old,new,scenario in mutants:
  body=file.read_text();assert body.count(old)==1,(name,body.count(old))
  mutant=T/'app/ui/breadbox_import_ui.c' if scenario is None else T/'import.c'
  mutant.parent.mkdir(parents=True,exist_ok=True);mutant.write_text(body.replace(old,new))
  exe=T/'test'
  if scenario is None:
   test=T/'tests/psx3d_import/ui.c';test.parent.mkdir(parents=True,exist_ok=True);test.write_text((R/'tests/psx3d_import/ui.c').read_text())
   cmd=flags+[str(test),str(R/'app/ui/safefile.c'),'-pthread','-o',str(exe)]
  else:
   cmd=[f for f in flags if f not in ['-I'+str(R/'tests/app/host'),'-I'+str(R/'core/deps/libretro-common/include')]]+['-DPSX_IMPORT_TEST','-DPSX_IMPORT_FAULT_TEST']+['-Wl,--wrap='+w for w in wrap]+[str(R/'tests/psx3d_import/resource.c'),str(R/'tests/psx3d_import/strict_fs.c'),str(mutant),str(R/'app/ui/history.c'),str(R/'app/ui/safefile.c'),'-lz','-pthread','-o',str(exe)]
  built=subprocess.run(cmd,cwd=R,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
  assert not built.returncode,built.stderr.decode()
  dest=T/name;dest.mkdir()
  result=subprocess.run([str(exe),str(dest)]+([] if scenario is None else [str(scenario)]),cwd=R,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=30,preexec_fn=lambda:resource.setrlimit(resource.RLIMIT_CORE,(0,0)))
  assert result.returncode and b'Assertion' in result.stderr,(name,result.returncode,result.stderr.decode())
  print('Guard mutant caught:',name,flush=True)
print('All 12 new safety/retry guard mutants caught: PASS')
