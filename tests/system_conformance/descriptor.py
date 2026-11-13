#!/usr/bin/env python3
"""Compile real adapter schema and shared pause labels, then validate their data.
Platform hooks are not called here; real core/service lifecycle is runtime.py.
"""
from pathlib import Path
import subprocess, sys, tempfile, re
r=Path(__file__).resolve().parents[2]
sid=sys.argv[1];src=(r/f'systems/{sid}/sys_{sid}.c').read_text()
# Compile the actual declarations, not a second hand-maintained descriptor.
# Compile each whole real adapter separately; schema executable uses only its
# immutable declarations so unavailable device hooks cannot be called by accident.
parts=['#include <assert.h>\n#include <string.h>\n#include <stdio.h>\n#include <zlib.h>\n#include "bb_system.h"\n']
if sid=='gba':
 a=src.index('static const char *const sizes[]');b=src.index('static const char *solar_value',a)
 parts.append(src[a:b])
if sid=='nes':
 a=src.index('static bb_probe_result probe(');b=src.index('static bb_profile profile;',a)
 parts.append(src[a:b])
pause=(r/'app/ui/bb_pause.c').read_text()
parts.append(re.search(r'static const char \*const kind_name\[\]=\{.*?\};',pause)[0])
budget=re.search(r'\.budget\s*=\s*(\{\{.*?\}\})',src,re.S)
assert budget, sid+': missing memory budget'
parts.append('static const bb_budget budgets[]='+budget[1]+';\n')
parts.append('int main(void){\nfor(unsigned m=0;m<BB_MODELS;m++){assert(budgets[m].heap_kib>0 && budgets[m].exec_kib>0 && budgets[m].keep_kib==0); }\n')
if sid in ('gba','nes'):
 parts.append('assert(sizeof(settings)/sizeof(*settings)==%d);' % (6 if sid=='gba' else 3))
 parts.append('''
 for(unsigned i=0;i<sizeof(settings)/sizeof(*settings);i++){
  const bb_setting_desc *s=&settings[i];assert(s->key && *s->key && s->label && *s->label);
  assert(!(s->flags&~(BB_SET_LIVE|BB_SET_GAME_ONLY)));
  assert(s->kind<=BB_SET_ACTION && s->category<=BB_CAT_ADVANCED && s->models && !(s->models&~BB_MODEL_ALL));
  for(unsigned j=0;j<i;j++)assert(strcmp(s->key,settings[j].key));
  if(s->kind==BB_SET_CHOICE){assert(s->values && s->nvalues);for(unsigned j=0;j<s->nvalues;j++)assert(s->values[j] && *s->values[j]);}
  for(unsigned m=0;m<BB_MODELS;m++){
   if(s->kind==BB_SET_CHOICE)assert(s->def[m]>=0 && s->def[m]<s->nvalues);
   if(s->kind==BB_SET_SLIDER)assert(s->def[m]>=0);
   if(s->kind==BB_SET_TOGGLE)assert(s->def[m]==0 || s->def[m]==1);
  }
 }
''')
if sid=='nes':
 parts.append('''
 bb_content_id id={0};char error[128];
 assert(identify("fixture.nes",NULL,&id,error,sizeof(error)) && id.key[0] && id.crc);
 unsigned char header[16];FILE *f=fopen("fixture.nes","rb");assert(f && fread(header,1,16,f)==16);fclose(f);
 bb_probe_input in={.head=header,.head_len=16,.size=24592,.name="fixture.nes"};bb_entry entry;
 assert(probe(&in,&entry)==BB_PROBE_YES && entry.title[0]);
 header[0]=0;assert(probe(&in,&entry)==BB_PROBE_NO);
 ''')
parts.append('''
 const char *expected[]={"Resume","Undo","Save postcard","Load postcard","Settings","Controls","Cheats","","Reset game","Quit game"};
 assert(sizeof(kind_name)==sizeof(expected));
 for(unsigned i=0;i<10;i++)assert(!strcmp(kind_name[i],expected[i]));
 return 0;}
''')
with tempfile.TemporaryDirectory(prefix='bb-conformance-schema-') as td:
 p=Path(td)
 subprocess.run(['cc','-std=gnu11','-fsyntax-only','-include','stddef.h','-include','string.h','-Itests/app/host','-Iapp/ui','-Iinclude/breadbox','-Iplatform/3ds','-Isystems/gba','-Isystems/common','-Icore/deps/libretro-common/include',str(r/f'systems/{sid}/sys_{sid}.c')],cwd=r,check=True)
 (p/'check.c').write_text(''.join(parts))
 if sid=='nes':subprocess.run([sys.executable,str(r/'systems/nes/tests/make-rom.py'),str(p/'fixture.nes')],check=True)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Wno-unused-function','-Wno-misleading-indentation','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-fsanitize=address,undefined','-fno-sanitize-recover=all','-Itests/app/host','-Iapp/ui','-Iinclude/breadbox','-Iplatform/3ds','-Isystems/gba','-Isystems/common','-Icore/deps/libretro-common/include',str(p/'check.c'),'-lz','-o',str(p/'check')],cwd=r,check=True)
 subprocess.run([str(p/'check')],cwd=p,check=True)
print(sid+': compiled production settings schema + seven shared pause rows PASS')
