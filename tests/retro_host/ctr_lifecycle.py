#!/usr/bin/env python3
"""Run the production teardown with and without GSP rights after APT exits."""
from pathlib import Path
import subprocess, tempfile
r=Path(__file__).resolve().parents[2]
s=(r/'systems/common/retro_ctr.c').read_text()
body=s[s.index('static void release_texture('):s.index('static void draw(')]
assert 'release_texture();' in s[s.index('while(!back && bb_save_runnable() && aptMainLoop())'):]
assert 'if(!gspHasGpuRight()' in s[s.index('static bool upload('):s.index('static void release_texture(')]
assert 'audio_frames=%llu audio_nonzero=%llu audio_hash=%08lx pictures=%u presented=%u' in s
assert 'emu_ticks*1000.0/(SYSCLOCK_ARM11*interval_frames)' in s
assert 'present_ticks*1000.0/(SYSCLOCK_ARM11*(shown-report_shown))' in s
with tempfile.TemporaryDirectory() as td:
 p=Path(td)/'check.c'
 p.write_text('''#include <assert.h>
#include <stdbool.h>
static bool rights,ready;static unsigned waits,deletes;static int texture;
static bool gspHasGpuRight(void){return rights;}
static void video_gpu_idle(void){assert(rights);waits++;}
static void C3D_TexDelete(void *p){assert(rights && p==&texture);deletes++;}
'''+body+'''
int main(void){
 for(unsigned gpu=0;gpu<2;gpu++)for(unsigned tex=0;tex<2;tex++){
  rights=gpu;ready=tex;unsigned w=waits,d=deletes;
  release_texture();assert(!ready);
  assert(waits==w+gpu && deletes==d+(gpu&&tex));
 }
}
''')
 subprocess.run(['cc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p),'-o',str(Path(td)/'check')],check=True)
 subprocess.run([str(Path(td)/'check')],check=True)
print('shared CTR HOME-close GPU-rights teardown PASS')
