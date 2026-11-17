"""Release URL guard and keyless manifest generation against CIA metadata."""
from pathlib import Path
import hashlib,json,struct,subprocess,tempfile
script=Path('tools/build/update-release-check.py').resolve()
with tempfile.TemporaryDirectory() as d:
 root=Path(d);p=root/'build.elf';p.write_bytes(b'BB_UPDATE_DEV_URL=http://127.0.0.1/')
 def run(*args):return subprocess.run(['python3',str(script),str(p),*args],capture_output=True,text=True)
 assert run('--private').returncode!=0
 assert run().returncode!=0
 assert run('--private','--development-package').returncode==0
 p.write_bytes(b'BB_UI_FIXTURES=1')
 assert run().returncode!=0
 assert run('--private','--development-package').returncode==0
 p.write_bytes(b'production ELF fixture')
 assert run('--private').returncode==0
 assert run().returncode==0
 print('PASS: release rejects dev URL and needs no signing keys')
 cia=root/'BreadBox.cia';out=root/'out'
 data=bytearray(9000);tmd=0x2040;sig=0x140
 struct.pack_into('<IHHIII',data,0,0x2020,0,0,0,0,sig+0x9e)
 struct.pack_into('>I',data,tmd,0x10004)
 struct.pack_into('>Q',data,tmd+sig+0x4c,0x000400000B0B0B00)
 struct.pack_into('>H',data,tmd+sig+0x9c,16401)
 cia.write_bytes(data)
 official='https://github.com/BreadChaser/BreadBox/releases/download/test/BreadBox.cia'
 def manifest(version=16401,url=official,*extra):
  return subprocess.run(['python3','tools/build/update-manifest.py',str(cia),
   '--title-version',str(version),'--version','1.0.1','--url',url,'--out',str(out),*extra],capture_output=True,text=True)
 result=manifest();assert result.returncode==0,result.stderr
 m=json.loads((out/'update.json').read_text())
 assert set(m)=={'title_id','title_version','version','size','sha256','url'}
 assert m['sha256']==hashlib.sha256(data).hexdigest() and m['sha256'] in result.stdout
 assert m['title_version']=='16401' and m['size']=='9000' and m['url']==official
 assert not (out/'update.json.sig').exists()
 for tag in ['../../../../Evil/Repo/raw/main', 'v1/../../../Evil', '%2e%2e/%2e%2e/x', '.', '..', '%2e', 'v1%2f..', 'v1%2Fother', 'v1%5cother', '', 'v1/extra', 'v1?x', 'v1#x']:
  assert manifest(16401,official.replace('/test/','/'+tag+'/')).returncode!=0,tag
 assert manifest(16401,official.replace('/test/','/v1.2_3+rc-4/')).returncode==0
 for url in [official.replace('https:','http:'),official.replace('BreadChaser/','Other/'),official+'?x',official.replace('BreadBox.cia','Other.cia')]:
  assert manifest(16401,url).returncode!=0
 assert manifest(16402).returncode!=0
 struct.pack_into('>Q',data,tmd+sig+0x4c,0x000400000C3D3D00);cia.write_bytes(data)
 assert manifest().returncode!=0
 struct.pack_into('>Q',data,tmd+sig+0x4c,0x000400000B0B0B00)
 struct.pack_into('>H',data,tmd+sig+0x9c,16400);cia.write_bytes(data)
 result=manifest(16400);assert result.returncode and 'reserved development title versions' in result.stderr
 assert manifest(16400,'http://127.0.0.1:18931/fixture/BreadBox.cia','--development').returncode==0
 print('PASS: keyless manifest SHA/fields, CIA identity/version, URLs, reserved counters and HTTP dev fixtures')

# The target's fixed SHA/parser vector can be regenerated without any key material.
with tempfile.TemporaryDirectory() as d:
 header=Path(d)/'update_vector.h'
 subprocess.run(['python3','tools/build/update-vector.py','tests/update/vector/update.json','--out',str(header)],check=True)
 assert header.read_bytes()==Path('app/services/update_vector.h').read_bytes()
 print('PASS: reproducible keyless SHA-256/parser vector')
