#!/usr/bin/env python3
"""Optional real-core output gate using private ROM/state fixture manifest.
SAVE_EXACT_FIXTURES is a JSON array of {key,rom,state}. No private inputs are
copied into this repo. Compare no saves vs production snapshot queue writes.
"""
from pathlib import Path
import concurrent.futures,hashlib,json,os,re,shutil,subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
manifest=os.environ.get('SAVE_EXACT_FIXTURES')
if not manifest:
 print('SAVE_EXACT_FIXTURES required');sys.exit(77)
fixtures=json.loads(Path(manifest).read_text())
out=Path(os.environ.get('SAVE_EXACT_OUT') or tempfile.mkdtemp(prefix='save-exact-results-'));out.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(prefix='bb-save-exact-') as td:
 tmp=Path(td);core=tmp/'core';shutil.copytree(root/'gba-core',core,ignore=shutil.ignore_patterns('*.o','*.a','*.so','build-breadbox'))
 with (out/'build.log').open('w') as log:
  subprocess.run(['make','-C',str(core),'platform=unix','-j4'],stdout=log,stderr=log,check=True)
 runner=(root/'tests/gba_arm_exact/runner.c').read_text()
 a=runner.index('if(getenv("REQUIRE_JIT"))');b=runner.index('retro_unload_game();retro_deinit();if(pcm)',a);runner=runner[:a]+runner[b:]
 harness='''
#include "save_writer.h"
#include "safefile.h"
#include <assert.h>
static save_writer *writer;
static unsigned save_count;
static bool disk_snapshot(const void *data) {
 safe_format_t fmt={.size=0x20000};
 bool ok=safe_write("battery.sav",&fmt,data,true);
 if(ok)save_count++;
 return ok;
}
'''
 runner=runner.replace('int main(int argc,char **argv) {',harness+'\nint main(int argc,char **argv) {')
 runner=runner.replace(' for(frame=0;frame<frames;frame++)', ' if(getenv("ASYNC_SAVE")){writer=save_writer_start(0x20000,disk_snapshot,1);assert(writer);}\n for(frame=0;frame<frames;frame++)')
 runner=runner.replace('retro_run();if(now()-frame_start>2.0)', 'retro_run();if(writer && !(frame%180))save_writer_submit(writer,retro_get_memory_data(RETRO_MEMORY_SAVE_RAM));if(now()-frame_start>2.0)')
 runner=runner.replace('retro_unload_game();retro_deinit();if(pcm)', 'if(writer){assert(save_writer_flush(writer));save_writer_stop(writer);printf("snapshot_commits=%u\\n",save_count);}\nretro_unload_game();retro_deinit();if(pcm)')
 src=tmp/'runner.c';src.write_text(runner)
 subprocess.run(['cc','-O2','-DSAVE_WRITER_TEST','-pthread','-I'+str(core/'libretro/libretro-common/include'),'-I'+str(root/'app/ui'),'-I'+str(root/'include/breadbox'),'-I'+str(root/'platform/3ds'),'-I'+str(root/'tests/gba_saves'),str(src),str(root/'app/ui/save_writer.c'),str(root/'app/ui/safefile.c'),'-L'+str(core),'-l:gpsp_libretro.so','-Wl,-rpath,'+str(core),'-o',str(tmp/'runner')],check=True)
 def check(row):
  records=[]
  for strength in (0,256):
   pair=[]
   for mode in ('off','async'):
    run=tmp/(row['key']+'-'+str(strength)+'-'+mode);run.mkdir()
    state=run/'final.state';log=out/(run.name+'.log');env=dict(os.environ,AUDIT='1')
    if mode=='async':env['ASYNC_SAVE']='1'
    n=1800 if row['key']=='video' else 600
    with log.open('w') as f:subprocess.run([str(tmp/'runner'),'0',row['rom'],str(n),str(strength),'-','/dev/null','-',row['state'],str(state)],env=env,cwd=run,stdout=f,stderr=f,check=True,timeout=180)
    text=log.read_text();pair.append(dict(output=re.search(r'frames=\d+ video=\w+ audio=\w+ samples=\d+ nonzero=\d+',text)[0],state=hashlib.sha256(state.read_bytes()).hexdigest()))
    if mode=='async':assert 'snapshot_commits=' in text
   assert pair[0]==pair[1],(row,pair)
   records.append(dict(strength=strength,baseline=pair[0],async_save=pair[1],exact=True))
  print(row['key'],'PASS',flush=True);return dict(key=row['key'],records=records)
 with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:results=list(pool.map(check,fixtures))
 (out/'results.json').write_text(json.dumps(results,indent=2))
 print(f'{len(results)} real-core fixtures x mono/stereo: exact video/PCM/state with concurrent file snapshots PASS')
