#!/usr/bin/env python3
"""Page-only entries accept one explicit movement and no code drift or stale entry."""
import copy, json, os, tempfile
from pathlib import Path
import accepted
base=(0x1000,16,'a'*64); moved=(0x1010,16,'a'*64)
e={'gate':'system','change':'page-only','reason':'bounded settings growth','evidence':'test',
   'base':{'size':16,'fingerprint':'a'*64},'new':{'size':16,'fingerprint':'a'*64},
   'new_page_offset':16,'page_reason':'following the changed function'}
def judge(entry=e,b=base,n=moved,pages={16}):return accepted.judge('fn',entry,b,n,pages)
assert judge() is None
assert judge(n=base,pages={0}) # stale
assert judge(pages={20}) # wrong candidate offset
assert judge(n=(0x1010,16,'b'*64)) # fingerprint drift
assert judge(n=(0x1010,20,'a'*64)) # size drift
assert judge(b=(0x1000,16,'c'*64)) # wrong baseline
for key in ('new_page_offset','page_reason'):
 v=copy.deepcopy(e);del v[key];assert judge(v)
v=copy.deepcopy(e);del v['change'];assert judge(v) # implicit allowance forbidden
v=copy.deepcopy(e);v['page_reason']=' ';assert judge(v)
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'manifest.json';os.environ['ACCEPTED_CHANGES']=str(p)
 for key in ('new_page_offset','page_reason'):
  v=copy.deepcopy(e);del v[key];p.write_text(json.dumps({'functions':{'fn':v}}))
  errors=[];accepted.load('system',errors);assert errors
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'scoped.json';os.environ['ACCEPTED_CHANGES']=str(p)
 a=dict(e,gate='frontend',symbol='environment');b=dict(e,symbol='environment')
 p.write_text(json.dumps({'functions':{'frontend:environment':a,'system:environment':b}}))
 for gate in ('frontend','system'):
  errors=[];entries=accepted.load(gate,errors);assert not errors and list(entries)==['environment']
 p.write_text(json.dumps({'functions':{'one':a,'two':a}}))
 errors=[];accepted.load('frontend',errors);assert any('duplicate' in error for error in errors)
print('page-only exact movement and stale/drift/missing-evidence controls PASS')
