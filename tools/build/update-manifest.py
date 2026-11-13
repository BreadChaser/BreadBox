#!/usr/bin/env python3
"""Create HTTPS-trusted update metadata; never uploads anything."""
import argparse,hashlib,json,subprocess,re
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('cia',type=Path)
p.add_argument('--title-version',type=int,required=True)
p.add_argument('--version',required=True)
p.add_argument('--url',required=True)
p.add_argument('--out',type=Path,required=True)
p.add_argument('--development',action='store_true',help='allow local HTTP emulator fixtures')
a=p.parse_args()
root=Path(__file__).resolve().parents[2]
if not a.development and 16399<=a.title_version<=16400:p.error("16399..16400 are reserved development title versions")
subprocess.run(['python3',str(root/'tools/build/title-version.py'),'verify-cia',str(a.cia),str(a.title_version),'--title-id','000400000B0B0B00'],check=True)
if not 0x2020<=a.cia.stat().st_size<=64*1024*1024:p.error('invalid CIA size')
if not re.fullmatch(r'[A-Za-z0-9.+-]{1,31}',a.version):p.error('invalid display version')
official=re.fullmatch(r'https://github\.com/BreadChaser/BreadBox/releases/download/(?!\.{1,2}/)[A-Za-z0-9._+-]+/BreadBox\.cia',a.url)
if not official and not (a.development and a.url.startswith('http://')):p.error('expected official release URL')
if not a.url.endswith('/BreadBox.cia') or any(c in a.url for c in ' ?#'):p.error('invalid CIA asset URL')
if not a.development:subprocess.run(['python3',str(root/'tools/build/update-release-check.py'),str(a.cia)],check=True)
a.out.mkdir(parents=True,exist_ok=True)
manifest={'title_id':'000400000B0B0B00','title_version':str(a.title_version),'version':a.version,'size':str(a.cia.stat().st_size),'sha256':hashlib.sha256(a.cia.read_bytes()).hexdigest(),'url':a.url}
path=a.out/'update.json'
path.write_text(json.dumps(manifest,separators=(',',':'))+'\n')
print(path)
print('SHA256:',manifest['sha256'])
