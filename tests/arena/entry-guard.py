#!/usr/bin/env python3
"""Exercise the production guard with no loan, a PS1 loan, and a GBA loan."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[2]
s=(r/'systems/gba/gba.c').read_text();a=s.index('static bool core_held(void)');b=s.index('\n}',a)+2
code='''#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include "bb_system.h"
const bb_system_t bb_system_gba={0};
static bb_system_t ps1;
static const bb_system_t *active;
static bool held;
static bool bb_exec_held(void){return held;}
const bb_system_t *bb_session_system(void){return active;}
const bb_system_t *bb_system_by_id(const char *id){return !strcmp(id,"gba")?&bb_system_gba:&ps1;}
'''+s[a:b]+'''
int main(void){
 assert(!core_held());held=true;active=&ps1;assert(!core_held());
 active=&bb_system_gba;assert(core_held());held=false;assert(!core_held());
}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I'+str(r/'app/ui'),'-I'+str(r/'include/breadbox'),'-I'+str(r/'platform/3ds'),'-I'+str(r/'systems/gba'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('production gpSP entry guard refuses PS1-owned and released loans PASS')
