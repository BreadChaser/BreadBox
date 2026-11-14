#!/usr/bin/env python3
"""Per-frame frontend objects pinned in core-first.ld: every function of them must keep its
page offset (I-cache set), size and instruction words between two ELFs.
  check-pinned.py BASE.elf NEW.elf BUILD_DIR [--record]
The required names are the union of the NEW objects' pinned functions and pinned-names.json,
the list frozen from the accepted baseline (--record rewrites it from NEW objects found in
BASE). A name absent from either image fails unless pinned-missing.json lists it with a
reason; a function only in NEW has no baseline code and is reported as new."""
import collections, json, os, subprocess, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import accepted, baseline_guard, compare, hotcode
NM = '/opt/devkitpro/devkitARM/bin/arm-none-eabi-nm'
base, new, build = [a for a in sys.argv[1:] if not a.startswith('--')][:3]
record = '--record' in sys.argv
if record: baseline_guard.require_accepted(base)
ld = open(build + '/../core-first.ld').read()
from pin_contract import FRONTEND_OBJECTS, frontend_names
objs = sorted(FRONTEND_OBJECTS)
# Local C names can repeat (e.g. statsview.c and cheats.c both have parse).
# Retain all definitions instead of silently comparing the last unrelated one.
def syms(elf):
    out = collections.defaultdict(set)
    for l in subprocess.run([NM, elf], capture_output=True, text=True, check=True).stdout.splitlines():
        p = l.split()
        if len(p) == 3 and p[1] in 'tT': out[p[2]].add(int(p[0], 16))
    return out
b, n = syms(base), syms(new)
bad = total = 0
allow_missing = json.loads(Path(os.environ.get('PINNED_MISSING') or Path(__file__).with_name('pinned-missing.json')).read_text())['names']
for k, why in allow_missing.items():
    if not str(why).strip(): print(f'FAIL pinned-missing.json: {k} needs a reason'); bad += 1
frozen_file = Path(os.environ.get('PINNED_NAMES') or Path(__file__).with_name('pinned-names.json'))
frozen = {} if record else json.loads(frozen_file.read_text())['objects']
if not record and not frozen:
    print('FAIL pinned-names.json has no objects'); bad += 1
used_missing = set()
accepted_errors = []
accept = accepted.load('frontend', accepted_errors)
for e in accepted_errors: print('FAIL', e); bad += 1
used_accept = set()
frame_names = set()
recorded = {}
for o in sorted(set(objs) | set(frozen)):
    names = frontend_names(build, o, ld)
    if record:
        recorded[o] = sorted(s for s in names if s in b or s in allow_missing)
        continue
    # A baseline function that vanished from the NEW objects (deleted, renamed, inlined)
    # still has to be checked: the frozen list names it.
    names = names | set(frozen.get(o, ()))
    for s in sorted(names):
        in_b, in_n = s in b, s in n
        if in_b and in_n: continue
        if s in allow_missing and not (in_b and in_n): used_missing.add(s); continue
        if in_n and not in_b:
            print(f'new  {o}: {s} is not in the baseline image (no baseline code to compare)'); continue
        print(f'MISSING {o}: {s} is {"missing from both images" if not in_b else "in the baseline but gone from NEW"} and not listed in pinned-missing.json'); bad += 1
    moved = [s for s in names if s in b and s in n and s not in accept and not ({v & 0xfff for v in b[s]} & {v & 0xfff for v in n[s]})]
    same = [s for s in names if s in b and s in n and b[s] & n[s]]
    frame_names.update(names & b.keys() & n.keys())
    total += len(names)
    print(f'{"ok  " if not moved else "MOVED"} {o:16s} {len(names):4d} functions, {len(same)} at the same address, {len(moved)} other page offset')
    for s in moved[:5]: print(f'      {s} {sorted(b[s])} -> {sorted(n[s])}')
    bad += len(moved)
if record:
    frozen_file.write_text(json.dumps({'comment': 'Pinned per-frame function names of the accepted baseline (check-pinned.py --record BASE NEW BUILD). Required by check-pinned.py even when a NEW object no longer defines them.', 'objects': recorded}, indent=1) + '\n')
    print('recorded', sum(map(len, recorded.values())), 'names in', frozen_file)
    sys.exit(0)
for k in sorted(set(allow_missing) - used_missing):
    print(f'FAIL pinned-missing.json lists {k}, which is present in both images: delete the entry'); bad += 1
# Compare executable words, not just entry addresses, for the frame contract.
# Branches and address literals may follow the same moved symbol. All other
# instruction/constant differences and every size change fail closed.
img = hotcode.Images(base, new)
A, B = img.A, img.B
def sizes(path,image):
 # Duplicate local names must use the same definition as the selected address.
 # A dict keyed only by name otherwise takes the last unrelated function size.
 return {name:next(size for addr,size in rows if addr==image['syms'][name][0])
         for name,rows in hotcode.defs(path).items() if name in image['syms']}
asize,bsize=sizes(base,A),sizes(new,B)
hot=sorted(frame_names)
for name in hot:
 if name not in asize or name not in bsize:
  print('FAIL missing frame function',name);bad+=1;continue
 aa,bb=A['syms'][name][0],B['syms'][name][0]
 if asize[name]!=bsize[name]:
  changed=None
 else:
  refs,changed=img.words(aa,bb,asize[name])
 if name in accept:
  # A reviewed change: exactly this before/after, nothing else (accepted.py).
  used_accept.add(name)
  fa,fb=img.fingerprint('base',aa,asize[name]),img.fingerprint('new',bb,bsize[name])
  why=accepted.judge(name,accept[name],(aa,asize[name],fa),(bb,bsize[name],fb),{v&0xfff for v in n[name]})
  print('FAIL' if why else 'ok  ','accepted change',name,why or f'{asize[name]} -> {bsize[name]} bytes, {accept[name]["reason"][:70]}')
  bad+=bool(why)
  continue
 if changed is None:
  print('FAIL size',name,asize[name],bsize[name],'(no accepted-changes.json entry)');bad+=1;continue
 for offset in changed:
  print("    changed",hex(offset),hex(compare.word(A,aa+offset) or 0),hex(compare.word(B,bb+offset) or 0))
 print(('FAIL' if changed else 'ok  '),'code',name,asize[name],'bytes,',refs,'relocated references,',len(changed),'changed words')
 bad+=bool(changed)
for name in sorted(set(accept)-used_accept):
 print('FAIL accepted-changes.json lists',name,'which is not a pinned frame function present in both images: delete the entry');bad+=1
# Newly extracted functions have no build-2650 body. Their explicit frozen
# contract is independent of --record and release-relative acceptance entries.
requirements=json.loads(Path(__file__).with_name('play-controls-required.json').read_text())['functions']
if not requirements:print('FAIL empty frozen play-control requirements');bad+=1
for name,need in requirements.items():
 if name not in B['syms'] or name not in bsize:
  print('FAIL frozen play control missing',name);bad+=1;continue
 address=B['syms'][name][0];size=bsize[name]
 fingerprint=img.frozen_fingerprint('new',address,size,need.get('literal_strings'))
 if (size,fingerprint,address&4095)!=(need['size'],need['fingerprint'],need['page_offset']):
  print('FAIL frozen play control',name,'body/size/cache offset differs from reviewed contract');bad+=1
 else:print('ok  frozen play control',name,size,'bytes')
print('PASS' if not bad else f'FAIL {bad}')
sys.exit(1 if bad else 0)
