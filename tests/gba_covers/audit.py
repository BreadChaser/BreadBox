#!/usr/bin/env python3
"""Offline NAS audit using the actual C matcher and a host-fetched index.
Usage: audit.py ROM_ROOT INDEX_HTML OUT_DIR
No console networking. Saves candidates and matches for every file.
"""
from pathlib import Path
import csv,html,json,re,subprocess,sys,time,urllib.parse,zipfile
root=Path(__file__).resolve().parents[2]
roms,index,out=map(Path,sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
start=time.monotonic()
index_names={urllib.parse.unquote(html.unescape(x)).removesuffix('.png') for x in re.findall(r'href="([^"]+\.png)"',index.read_text())}
exe=out/'matcher'
subprocess.run(['cc','-std=gnu11','-DGBA_BOXART_TABLE="'+str(root/'assets/romfs/covers/gba-codes.txt')+'"','-I'+str(root/'app/ui'),'-I'+str(root/'include/breadbox'),'-I'+str(root/'platform/3ds'),str(root/'tests/gba_covers/check.c'),str(root/'systems/gba/gba_boxart.c'),str(root/'app/ui/boxart.c'),str(root/'app/ui/safefile.c'),'-lminizip','-lz','-o',str(exe)],check=True)
rows=[]
for p in sorted(roms.rglob('*')):
 if p.suffix.lower() not in ('.gba','.zip') or not p.is_file():continue
 try:
  if p.suffix.lower()=='.zip':
   with zipfile.ZipFile(p) as z:
    files=[n for n in z.namelist() if n.lower().endswith('.gba') and not n.startswith('__MACOSX/')]
    with z.open(files[0]) as f:h=f.read(192)
  else:
   with p.open('rb') as f:h=f.read(192)
  code=h[0xac:0xb0].decode('ascii',errors='replace')
  names=subprocess.check_output([str(exe),str(p),code],text=True).splitlines()
  match=next((n for n in names if n in index_names),'')
  rows.append([str(p.relative_to(roms)),code,match,' | '.join(names)])
 except Exception as e:rows.append([str(p.relative_to(roms)),'','',str(e)])
with (out/'matches.tsv').open('w') as f:
 w=csv.writer(f,delimiter='\t');w.writerow(['ROM','code','existing thumbnail','candidates']);w.writerows(rows)
result=dict(files=len(rows),resolved=sum(bool(r[2]) for r in rows),filename_matches=sum(bool(r[2]) and r[2]==r[3].split(' | ')[0] for r in rows),top_level_files=sum('/' not in r[0] for r in rows),top_level_resolved=sum('/' not in r[0] and bool(r[2]) for r in rows),index_names=len(index_names),seconds=round(time.monotonic()-start,3),missing=[r[0] for r in rows if not r[2]])
(out/'audit.json').write_text(json.dumps(result,indent=2)+'\n');exe.unlink();print(json.dumps(result,indent=2))
