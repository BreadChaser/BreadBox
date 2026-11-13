#!/usr/bin/env python3
"""Exercise the actual BreadBox raw-disc/playlist probe, independent of UI."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'systems/ps1/library.inc').read_text()
def extract(name):
 start=s.index(name); start=s.rfind('static ',0,start); brace=s.index('{',start); level=1; end=brace+1
 while level:
  if s[end]=='{': level+=1
  if s[end]=='}': level-=1
  end+=1
 return s[start:end]
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)
 for name,system in [('ps1.bin',b'PLAYSTATION'),('pc.iso',b'PC'),('psp.iso',b'PSP GAME')]:
  b=bytearray(17*2048); b[16*2048]=1; b[16*2048+1:16*2048+6]=b'CD001'; b[16*2048+8:16*2048+8+len(system)]=system; (p/name).write_bytes(b)
 for name,text in [('good.cue','FILE "ps1.bin" BINARY\n'),('bad.cue','FILE "pc.iso" BINARY\n'),('good.m3u','# comment\ngood.cue\n'),('bad.m3u','bad.cue\n'),('loop.m3u','loop.m3u\n'),('missing.m3u','missing.bin\n')]: (p/name).write_text(text)
 (p/'ps1.mdf').write_bytes((p/'ps1.bin').read_bytes()); (p/'bad.mdf').write_bytes((p/'pc.iso').read_bytes())
 source='''#define _GNU_SOURCE
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <assert.h>
typedef uint8_t u8; typedef uint32_t u32;
#define PATH_LEN 512
static bool bb_disc_is_ps1_chd(const char *p) { return strstr(p,"good.chd") != NULL; }
'''
 source+='\n'.join(extract(n) for n in ['ext_of(', 'trim(', 'cue_name(', 'ps1_image_at('])
 source+='''
int main(void) { char path[512];
 const char *good[]={"ps1.bin","ps1.mdf","good.cue","good.m3u","good.chd"};
 const char *bad[]={"pc.iso","psp.iso","bad.mdf","bad.cue","bad.m3u","loop.m3u","missing.m3u","bad.chd"};
 for (unsigned i=0;i<sizeof(good)/sizeof(*good);i++) assert(ps1_image_at((snprintf(path,sizeof(path),"./%s",good[i]),path),0));
 for (unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) assert(!ps1_image_at((snprintf(path,sizeof(path),"./%s",bad[i]),path),0));
}
'''
 (p/'check.c').write_text(source)
 subprocess.run(['cc','-std=gnu11','-g','-fsanitize=address,undefined',str(p/'check.c'),'-o',str(p/'check')],check=True)
 subprocess.run([str(p/'check')],cwd=p,check=True)
print('PS1 raw/MDF/CUE/M3U content classification and playlist loops: PASS')
