#!/usr/bin/env python3
"""Compile the actual catalog foot-line block and verify complete UTF-8 endings."""
from pathlib import Path
import re,subprocess,tempfile,os
root=Path(__file__).resolve().parents[2]
s=(root/'app/ui/shelf_scene_draw.c').read_text()
buf=re.search(r'char last\[[^\n]+',s).group()
a=s.index('char tail[24 * 4 + 8];');b=s.index('snprintf(last, sizeof(last), "Can\'t read: %s", tail);',a)+len('snprintf(last, sizeof(last), "Can\'t read: %s", tail);')
code='#include <string.h>\n#include <stdio.h>\n#include "ui_format.h"\n#define PATH_LEN 256\nint main(int argc,char **argv){(void)argc;const char *bb_unreadable_dir=argv[1];'+buf+s[a:b]+'puts(last);}\n'
with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
 p=Path(tmp);(p/'check.c').write_text(code)
 subprocess.run(['cc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(root/'include/breadbox'),str(p/'check.c'),'-o',str(p/'check')],check=True)
 for char in ['a','é','麵','🍞']:
  path='sdmc:/'+char*80
  got=subprocess.check_output([str(p/'check'),path]).decode('utf-8').rstrip('\n')
  assert got=="Can't read: ..."+char*24,(char,got)
 print('Catalog foot-line tail PASS: 80 ASCII characters and 2/3/4-byte UTF-8 tails, 24 complete ending characters')
