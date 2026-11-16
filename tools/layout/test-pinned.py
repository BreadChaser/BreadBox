#!/usr/bin/env python3
"""Negative controls for the BreadBox frame gates (check-pinned.py, check-system-pinned.py):
instruction and size mutations, a pinned name missing from the image, a stale
pinned-missing.json entry, a baseline function deleted from NEW (image and object), and
same-size instruction flips in a GBA core function and a GBA frame function.
test-pinned.py BASE.elf NEW.elf BUILD_DIR      (BASE may be NEW itself)
Release-relative (BASE = the release ELF): a mutated function with a reviewed accepted-changes
entry must be rejected by that entry ("candidate differs from the accepted change"); trial
manifests sit on top of the active one."""
from pathlib import Path
import hashlib,json,os,struct,subprocess,sys,tempfile
def hashlib_sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
import compare
base,new,build=sys.argv[1:4];gate=Path(__file__).with_name('check-pinned.py')
subprocess.run([sys.executable,str(Path(__file__).with_name('test-recording-metadata.py')),base,new],check=True)
elf=compare.load(new);address=elf['syms']['video_present_screens'][0]
def run(path,env=None):
 e=dict(os.environ);e.update(env or {})
 return subprocess.run([sys.executable,str(gate),base,str(path),build],capture_output=True,text=True,env=e)
with tempfile.TemporaryDirectory() as td:
 # Negative controls normally compare NEW with itself. Release-relative entries
 # must not become stale allowances in this different comparison; require identity.
 if hashlib_sha(base)==hashlib_sha(new):
  candidate_manifest=Path(td)/'candidate-acceptance.json'
  candidate_manifest.write_text(json.dumps({'functions':{}}))
  os.environ['ACCEPTED_CHANGES']=str(candidate_manifest)
 clean=run(new);assert clean.returncode==0,clean.stdout
 # Release-relative (BASE is the release): a reviewed accepted-changes entry for the mutated
 # function rejects any other body first, with its own diagnostic. Require that exact
 # rejection there, and the plain size/code failure when the function has no entry.
 import accepted,re
 def rejected(result,fn,plain,gate='frontend'):
  if fn in accepted.load(gate,[]):
   return result.returncode!=0 and re.search(rf'^FAIL accepted change (\S+\.o )?{re.escape(fn)} {re.escape(fn)}: candidate differs from the accepted change',result.stdout,re.M) is not None
  return result.returncode!=0 and plain in result.stdout
 p=Path(td)/'BreadBox.elf';raw=bytearray(Path(new).read_bytes())
 for va,off,size,_ in elf['secs']:
  if va<=address<va+size:
   pos=off+address-va;raw[pos]^=1;break
 p.write_bytes(raw)
 result=run(p)
 assert rejected(result,'video_present_screens','FAIL code video_present_screens'),result.stdout
 raw=bytearray(Path(new).read_bytes());shoff=struct.unpack_from('<I',raw,32)[0];ents,n=struct.unpack_from('<HH',raw,46)
 sections=[struct.unpack_from('<10I',raw,shoff+i*ents) for i in range(n)]
 for sec in sections:
  if sec[1]!=2:continue
  strings=sections[sec[6]];pool=raw[strings[4]:strings[4]+strings[5]]
  for pos in range(sec[4],sec[4]+sec[5],sec[9]):
   name=struct.unpack_from('<I',raw,pos)[0]
   if pool[name:].split(b'\0',1)[0]==b'video_present_screens':
    struct.pack_into('<I',raw,pos+8,struct.unpack_from('<I',raw,pos+8)[0]+4)
 p.write_bytes(raw)
 result=run(p)
 assert rejected(result,'video_present_screens','FAIL size video_present_screens'),result.stdout
 listed=json.loads((Path(__file__).with_name('pinned-missing.json')).read_text())
 empty=Path(td)/'empty.json';empty.write_text(json.dumps({'names':{}}))
 result=run(new,{'PINNED_MISSING':str(empty)})
 assert result.returncode and 'not listed in pinned-missing.json' in result.stdout,result.stdout
 stale=Path(td)/'stale.json';listed['names']['video_present_screens']='present in both images';stale.write_text(json.dumps(listed))
 result=run(new,{'PINNED_MISSING':str(stale)})
 assert result.returncode and 'delete the entry' in result.stdout,result.stdout

 # A second local parse() must not supply the size of pinned statsview parse().
 raw=bytearray(Path(new).read_bytes());seen=0
 for sec in sections:
  if sec[1]!=2:continue
  strings=sections[sec[6]];pool=raw[strings[4]:strings[4]+strings[5]]
  for pos in range(sec[4],sec[4]+sec[5],sec[9]):
   name=struct.unpack_from('<I',raw,pos)[0]
   if pool[name:].split(b'\0',1)[0]==b'parse':
    seen+=1
    if struct.unpack_from('<I',raw,pos+4)[0]!=elf['syms']['parse'][0]:
     struct.pack_into('<I',raw,pos+8,struct.unpack_from('<I',raw,pos+8)[0]+4)
 assert seen>1,'fixture needs duplicate local names'
 p.write_bytes(raw);result=run(p)
 assert 'FAIL size parse' not in result.stdout,result.stdout
 assert 'code parse 160 bytes' in result.stdout or 'accepted change parse' in result.stdout,result.stdout

 # A baseline function that is gone from NEW (renamed in the image, no longer defined by
 # the object) must fail even though no NEW object names it any more.
 import shutil
 objdump=Path(compare.TOOLS+'objcopy')
 tb=Path(td)/'build';tb.mkdir()
 for o in Path(build).glob('*.o'):shutil.copy(o,tb/o.name)
 shutil.copy(Path(build).parent/'core-first.ld',Path(td)/'core-first.ld')
 subprocess.run([str(objdump),'--redefine-sym','video_present_screens=video_present_screenz',str(tb/'video.o')],check=True)
 raw=bytearray(Path(new).read_bytes())
 assert b'\0video_present_screens\0' in raw
 raw=bytearray(bytes(raw).replace(b'\0video_present_screens\0',b'\0video_present_screenz\0'))
 p.write_bytes(raw)
 e=dict(os.environ)
 result=subprocess.run([sys.executable,str(gate),base,str(p),str(tb)],capture_output=True,text=True,env=e)
 assert result.returncode and 'gone from NEW' in result.stdout and 'video_present_screens' in result.stdout,result.stdout
 # the same without the frozen list would have passed: prove the list is what catches it
 nofreeze=Path(td)/'nofreeze.json';nofreeze.write_text(json.dumps({'objects':{'video':[]}}))
 e['PINNED_NAMES']=str(nofreeze)
 # ... and with no accepted changes: a release-relative entry also names the function
 noaccept=Path(td)/'noaccept.json';noaccept.write_text(json.dumps({'functions':{}}));e['ACCEPTED_CHANGES']=str(noaccept)
 result=subprocess.run([sys.executable,str(gate),base,str(p),str(tb)],capture_output=True,text=True,env=e)
 assert 'video_present_screens' not in result.stdout.replace('video_present_screenz',''),'control is not exercising the frozen list'
 # New contact code must fail even though build 2650 has no matching body.
 address=elf['syms']['ui_play_poll'][0];raw=bytearray(Path(new).read_bytes())
 for va,off,size,_ in elf['secs']:
  if va<=address<va+size:raw[off+address-va+8]^=1;break
 p.write_bytes(raw);result=run(p)
 assert result.returncode and 'FAIL frozen play control ui_play_poll' in result.stdout,result.stdout
 # GBA: same size, same address, one instruction word differs
 sysgate=Path(__file__).with_name('check-system-pinned.py')
 for fn in ('arm_block_ram','draw_gba_settings'):
  address=elf['syms'][fn][0]
  raw=bytearray(Path(new).read_bytes())
  for va,off,size,_ in elf['secs']:
   if va<=address<va+size:
    raw[off+address-va+8]^=4;break
  p.write_bytes(raw)
  result=subprocess.run([sys.executable,str(sysgate),base,str(p),build],capture_output=True,text=True)
  assert rejected(result,fn,'CHANGED','system') and fn in result.stdout,(fn,result.stdout[-600:])
 # a system function deleted from NEW (image symbol renamed) must fail
 raw=bytearray(bytes(Path(new).read_bytes()).replace(b'\0draw_gba_settings\0',b'\0draw_gba_settingz\0'));p.write_bytes(raw)
 result=subprocess.run([sys.executable,str(sysgate),base,str(p),build],capture_output=True,text=True)
 gone='FAIL accepted change draw_gba_settings has no matching definition in NEW' if 'draw_gba_settings' in accepted.load('system',[]) else 'MOVED/MISSING'
 assert result.returncode and gone in result.stdout and 'draw_gba_settings' in result.stdout,result.stdout[-600:]
 clean=subprocess.run([sys.executable,str(sysgate),base,str(new),build],capture_output=True,text=True)
 assert clean.returncode==0,clean.stdout[-600:]
 # ---- reviewed changes (accepted-changes.json): exactly one before/after, nothing else
 import hotcode
 def flip(fn,delta,out):
  address=elf['syms'][fn][0];r=bytearray(Path(new).read_bytes())
  for va,off,size,_ in elf['secs']:
   if va<=address<va+size:r[off+address-va+delta]^=1;break
  out.write_bytes(r)
 def entry(gate,fn,image,reason='trial',evidence='trial'):
  img=hotcode.Images(base,str(image));(ba,bs),(na,ns)=hotcode.defs(base)[fn][0],hotcode.defs(str(image))[fn][0]
  return {'gate':gate,'reason':reason,'evidence':evidence,'base':{'size':bs,'fingerprint':img.fingerprint('base',ba,bs)},'new':{'size':ns,'fingerprint':img.fingerprint('new',na,ns)}}
 def manifest(name,entries):
  # The trial entries on top of the active manifest (empty for BASE=NEW; release-relative,
  # every other reviewed change stays accepted). Any entry for the same symbol is replaced.
  names={e.get('symbol',k) for k,e in entries.items()}|set(entries)
  active=json.loads(accepted.manifest_path().read_text())['functions']
  merged={k:e for k,e in active.items() if e.get('symbol',k.split(':')[-1]) not in names}
  merged.update(entries)
  f=Path(td)/name;f.write_text(json.dumps({'functions':merged}));return {'ACCEPTED_CHANGES':str(f)}
 q1=Path(td)/'q1.elf';q2=Path(td)/'q2.elf'
 flip('video_present_screens',8,q1);flip('video_present_screens',12,q2)   # an accepted flip, and a second one on top
 flip('draw_gba_settings',8,Path(td)/'g1.elf')
 def fe(path,env=None):
  e=dict(os.environ);e.update(env or {})
  return subprocess.run([sys.executable,str(gate),base,str(path),build],capture_output=True,text=True,env=e)
 def sy(path,env=None,args=()):
  e=dict(os.environ);e.update(env or {})
  return subprocess.run([sys.executable,str(sysgate),base,str(path),build,*args],capture_output=True,text=True,env=e)
 assert fe(q1).returncode,'unlisted frontend change must fail'
 okm=manifest('ok.json',{'video_present_screens':entry('frontend','video_present_screens',q1)})
 r=fe(q1,okm);assert r.returncode==0 and 'accepted change video_present_screens' in r.stdout,r.stdout[-500:]
 r=fe(q2,okm);assert r.returncode and 'candidate differs from the accepted change' in r.stdout,r.stdout[-500:]     # drift
 r=fe(new,okm);assert r.returncode and 'FAIL accepted change' in r.stdout,r.stdout[-500:]                          # stale: the function no longer changes
 r=fe(q1,manifest('noreason.json',{'video_present_screens':entry('frontend','video_present_screens',q1,reason=' ')}));assert r.returncode and 'non-empty "reason"' in r.stdout,r.stdout[-500:]
 r=fe(q1,manifest('placeholder.json',{'video_present_screens':entry('frontend','video_present_screens',q1,reason='see {proof_name}')}));assert r.returncode and 'unresolved placeholder' in r.stdout,r.stdout[-500:]
 r=fe(q1,manifest('wrongbase.json',{'video_present_screens':{**entry('frontend','video_present_screens',q1),'base':{'size':1,'fingerprint':'0'*64}}}));assert r.returncode and 'not the recorded "base"' in r.stdout,r.stdout[-500:]
 r=fe(new,manifest('gone.json',{'no_such_function':entry('frontend','video_present_screens',q1)}));assert r.returncode and 'delete the entry' in r.stdout,r.stdout[-500:]
 g1=Path(td)/'g1.elf'
 assert sy(g1).returncode,'unlisted system change must fail'
 r=sy(g1,manifest('sysok.json',{'draw_gba_settings':entry('system','draw_gba_settings',g1)}));assert r.returncode==0 and 'accepted change' in r.stdout,r.stdout[-500:]
 r=sy(new,manifest('sysstale.json',{'draw_gba_settings':entry('system','draw_gba_settings',g1)}));assert r.returncode and 'FAIL accepted change' in r.stdout,r.stdout[-500:]
 r=sy(g1,manifest('wronggate.json',{'draw_gba_settings':entry('frontend','draw_gba_settings',g1)}));assert r.returncode,'a frontend entry must not accept a system change'
 # ---- a changed instruction cannot hide as a relocation next to a data symbol that moved
 img=hotcode.Images(new,new);img.b.elf['data']=bytearray(img.b.elf['data'])
 d=hotcode.defs(new)['draw_gba_settings'][0];at=d[0]+0x24
 for va,off,size,_ in img.b.elf['secs']:
  if va<=at<va+size:img.b.elf['data'][off+at-va]^=4;break
 refs,changed=img.words(d[0],d[0],d[1]);assert changed,'flip must be seen'
 last=max(range(len(img.b.rows)),key=lambda i:img.b.rows[i][0]);s0,z0,n0=img.b.rows[last]
 img.b.rows[last]=(s0+4,z0,n0);img.b.starts[last]=s0+4
 refs,changed=img.words(d[0],d[0],d[1]);assert changed,'a moved data symbol must not turn the flipped instruction into a pointer'
 # ---- frozen contracts distinguish opcode integers from literal-pool pointers.
 # This ADD instruction happens to be an integer inside the large invalid_code
 # BSS extent. Moving that data symbol must not change an unchanged opcode hash;
 # changing the instruction itself must still fail the frozen contract.
 fi=hotcode.Images(new,new)
 fa,fz=hotcode.defs(new)['play_render_touch_rect_draw'][0]
 at=fa+0xb0;word=compare.word(fi.B,at)
 assert fi.b.locate(word) is not None
 before=fi.frozen_fingerprint('new',fa,fz)
 index=max(i for i,row in enumerate(fi.b.rows) if row[0]<=word)
 start,length,name=fi.b.rows[index]
 fi.b.rows[index]=(start+4,length,name);fi.b.starts[index]=start+4
 assert fi.frozen_fingerprint('new',fa,fz)==before,'unchanged opcode must not normalize as a BSS pointer'
 fi.B['data']=bytearray(fi.B['data'])
 for va,off,size,_ in fi.B['secs']:
  if va<=at<va+size:fi.B['data'][off+at-va]^=4;break
 assert fi.frozen_fingerprint('new',fa,fz)!=before,'real opcode flip must change the frozen hash'
 # A declared empty-label operand is stable when string pooling moves, but a
 # different string pointer or an opcode mutation must still fail the contract.
 fi=hotcode.Images(new,new);fa,fz=hotcode.defs(new)['gba_touch_begin'][0]
 strings={'748':''};before=fi.frozen_fingerprint('new',fa,fz,strings)
 need=json.loads(Path(__file__).with_name('play-controls-required.json').read_text())['functions']['gba_touch_begin']
 assert before==need['fingerprint'] and strings==need['literal_strings']
 fi.B['data']=bytearray(fi.B['data'])
 for va,off,size,_ in fi.B['secs']:
  if va<=fa+748<va+size:
   pos=off+fa+748-va
   struct.pack_into('<I',fi.B['data'],pos,compare.word(fi.B,fa+744));break
 assert fi.frozen_fingerprint('new',fa,fz,strings)!=before,'nonempty pointer must fail empty-label contract'
 # ---- dropping a frame-pins.txt row cannot hide the change (the required objects are frozen)
 lr=Path(td)/'layout-root';(lr/'build').mkdir(parents=True)
 os.symlink(Path(__file__).resolve().parents[2]/'build/systems',lr/'build/systems')
 for pins in (Path(__file__).resolve().parents[2]/'systems').glob('*/frame-pins.txt'):
  dst=lr/'systems'/pins.parent.name;dst.mkdir(parents=True)
  (dst/'frame-pins.txt').write_text(''.join(l for l in pins.read_text().splitlines(True) if l.strip()!='gba'))
 r=sy(g1,{'LAYOUT_ROOT':str(lr)});assert rejected(r,'draw_gba_settings','CHANGED','system') and 'gba.o' in r.stdout and 'no longer listed' in r.stdout,r.stdout[-700:]
 # ---- the frozen lists are recorded only from the accepted baseline ELF
 r=subprocess.run([sys.executable,str(gate),str(new),str(new),build,'--record'],capture_output=True,text=True)
 if hashlib_sha(new)!=json.loads(Path(__file__).with_name('baseline.json').read_text())['sha256']:
  assert r.returncode and 'not the accepted baseline' in (r.stdout+r.stderr),r.stdout+r.stderr
print('frame gates reject instruction, size, missing-name, deleted-baseline-function, dropped-pin-row, pointer-lookalike, literal-aware frozen hashes, stale/drifted accepted changes and candidate re-records PASS')
