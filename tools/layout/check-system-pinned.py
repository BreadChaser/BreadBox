#!/usr/bin/env python3
"""Check system frame functions against the accepted baseline: page offset, size AND
instruction words (relocation-aware, tools/layout/hotcode.py). Covers the frame objects
listed in systems/*/frame-pins.txt and each system's core blob (the GBA core).
check-system-pinned.py BASE.elf NEW.elf BUILD_DIR [--record]

The required systems, objects and function names are the UNION of what the candidate
lists (frame-pins.txt, its objects) and system-pinned-names.json, frozen from the accepted
baseline (--record, only from the accepted ELF): deleting a frame-pins.txt row, an object
or a function cannot hide a change. Intended changes need an entry in accepted-changes.json
(gate "system"): reason, evidence and the exact before/after fingerprints."""
from pathlib import Path
import collections,json,os,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parent))
import accepted,baseline_guard,hotcode
NM='/opt/devkitpro/devkitARM/bin/arm-none-eabi-nm'
record='--record' in sys.argv
args=[a for a in sys.argv[1:] if not a.startswith('--')]
base,new,build=map(Path,args[:3]);root=Path(os.environ.get('LAYOUT_ROOT') or Path(__file__).resolve().parents[2])   # LAYOUT_ROOT: test fixtures only
if record:baseline_guard.require_accepted(base)
errors=[];accept=accepted.load('system',errors)
bad=0
for e in errors:print('FAIL',e);bad+=1
frozen_file=Path(os.environ.get('SYSTEM_PINNED_NAMES') or Path(__file__).with_name('system-pinned-names.json'))
frozen={} if record else json.loads(frozen_file.read_text())['systems']
if not record and not frozen:print('FAIL system-pinned-names.json has no systems');bad+=1
def syms(path):
 out=collections.defaultdict(set)
 for line in subprocess.check_output([NM,'-S',str(path)],text=True).splitlines():
  p=line.split()
  if len(p)==4 and p[2] in 'tT':out[p[3]].add((int(p[0],16)&4095,int(p[1],16)))
 return out
b,n=syms(base),syms(new);count=words=0
bdefs,ndefs=hotcode.defs(str(base)),hotcode.defs(str(new))
bimage=subprocess.check_output([NM,str(base)],text=True);nimage=subprocess.check_output([NM,str(new)],text=True)
img=None
used=set();recorded={}
rows={}   # system -> candidate object names (frame-pins.txt rows + its blob)
for pins in sorted((root/'systems').glob('*/frame-pins.txt')):
 system=pins.parent.name
 rows[system]=[x.strip()+'.o' for x in pins.read_text().splitlines() if x.strip() and not x.startswith('#')]+[system+'_core_blob.o']
for system in sorted(set(rows)|set(frozen)):
 marker='__bb_'+system+'_frame_end'
 in_base,in_new=marker in bimage,marker in nimage
 if not in_new and not in_base:continue   # a target that links neither (PS1-only) has no such pins
 if in_base and not in_new:print('FAIL system',system,'is in the baseline image but not in NEW');bad+=1
 if not record and system in frozen and system not in rows:print('FAIL required system',system,'has no systems/'+system+'/frame-pins.txt');bad+=1
 required=set(frozen.get(system,{}))
 for o in sorted(required-set(rows.get(system,()))):
  print('FAIL required object',o,'of system',system,'is no longer listed in frame-pins.txt (or its blob is gone)');bad+=1
 for oname in sorted(set(rows.get(system,()))|required):
  obj=(root/'build/systems'/system/oname) if oname.endswith('_core_blob.o') else build/oname
  names=syms(obj) if obj.exists() else {}
  if record:
   if not names:raise SystemExit('no functions in '+str(obj))
   recorded.setdefault(system,{})[oname]=sorted(x for x in names if x in b);continue
  if oname in rows.get(system,()) and not names:raise SystemExit('no functions in '+str(obj))
  checked=0
  for name in sorted(set(names)|set(frozen.get(system,{}).get(oname,()))):
   # Garbage-collected functions are not part of the image; surviving baseline
   # names must exist with the same size and page offset in the grown image.
   if name not in b:continue
   checked+=1;count+=1
   osizes={s for _,s in names[name]} if name in names else {s for _,s in bdefs.get(name,())}
   if name in accept:
    # A reviewed change: this exact before/after only. Page offset stays pinned.
    used.add(name)
    img=img or hotcode.Images(str(base),str(new))
    entry=accept[name]
    bd=next((d for d in bdefs.get(name,()) if d[1]==entry['base']['size']),None)
    nd=next((d for d in ndefs.get(name,()) if d[1]==entry['new']['size'] and d[0]&4095==entry.get('new_page_offset',(bd or (0,0))[0]&4095)),None) or next(iter(ndefs.get(name,())),None)
    if bd is None or nd is None:print('FAIL accepted change',name,'has no matching definition in',('the baseline' if bd is None else 'NEW'));bad+=1;continue
    why=accepted.judge(name,entry,(bd[0],bd[1],img.fingerprint('base',*bd)),(nd[0],nd[1],img.fingerprint('new',*nd)),{nd[0]&4095})
    print('FAIL' if why else 'ok  ','accepted change',oname,name,why or f'{bd[1]} -> {nd[1]} bytes, {entry["reason"][:70]}');bad+=bool(why)
    continue
   if not b[name]&n.get(name,set()):print('MOVED/MISSING',oname,name,b[name],n.get(name));bad+=1;continue
   # Instruction words: pair each baseline definition of this object's size with a NEW
   # definition of the same page offset and size.
   free=list(ndefs.get(name,()))
   for aa,size in bdefs.get(name,()):
    if size not in osizes:continue
    cand=next((d for d in free if d[1]==size and d[0]&4095==aa&4095),None)
    if cand is None:continue
    free.remove(cand)
    img=img or hotcode.Images(str(base),str(new))
    refs,changed=img.words(aa,cand[0],size);words+=size//4
    if changed:
     print('CHANGED',oname,name,size,'bytes,',len(changed),'changed words, first at +%#x'%changed[0]);bad+=1
  print(oname,checked,'functions')
if record:
 frozen_file.write_text(json.dumps({'comment':'System frame functions of the accepted baseline, per system and object (check-system-pinned.py --record, accepted ELF only). Required even when a candidate drops the frame-pins.txt row, the object or the function.','systems':recorded},indent=1)+'\n')
 print('recorded',sum(len(v) for s in recorded.values() for v in s.values()),'names in',frozen_file);sys.exit(0)
if not count:raise SystemExit('no system pins checked')
for name in sorted(set(accept)-used):
 print('FAIL accepted-changes.json lists',name,'which is not a pinned system function present in the baseline: delete the entry');bad+=1
print('PASS' if not bad else 'FAIL',count,'system frame functions,',words,'instruction words;',bad,'changes')
sys.exit(bool(bad))
