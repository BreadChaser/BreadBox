#!/usr/bin/env python3
"""S11 ratchet over REAL shell sources (app/ui, app/settings, app/services, platform/3ds, include/breadbox; systems/<id>/ is exempt).
The checked-in inventory lists every remaining PS1/GBA reference in a shell
file. References may disappear (rerun --record to lower the baseline); --strict,
which the host suite uses, fails on any new one: system code belongs in its
systems/<id>/ folder or behind the system interface.
"""
import json,re,sys
from pathlib import Path
r=Path(__file__).resolve().parents[2]
# Scan every shell translation unit/header, including UI/input/PS1 legacy
# presentation; adapters live under systems/ and are deliberately excluded.
files=sorted(p for d in (r/'app/ui',r/'app/settings',r/'app/services',r/'platform/3ds',r/'include/breadbox') if d.is_dir() for p in d.iterdir() if p.suffix in ('.c','.h'))
if not files:raise SystemExit('shell-guard: empty scan')
# Include every registered adapter, including optional systems disabled in apps.
ids=sorted(p.name for p in (r/'systems').iterdir() if (p/('sys_'+p.name+'.c')).exists())
lower='(?:'+'|'.join(map(re.escape,ids))+')'
upper='(?:'+'|'.join(re.escape(x.upper()) for x in ids)+')'
pat=re.compile(r'\b(?:bb_system_'+lower+r'|'+lower+r'_[a-zA-Z0-9_]*|'+upper+r'_[A-Z0-9_]*|emu_[a-zA-Z0-9_]*|bb_index_'+lower+r'[a-zA-Z0-9_]*|breadbox_'+lower+r'_[a-zA-Z0-9_]*|BB_SYSTEM_'+upper+r')\b|["\'](?:'+lower+'|'+upper+r')["\']|\.(?:gba|agb|zip|cue|chd|pbp|m3u|iso|img|mdf|bin)\b|covers/'+lower+r'|(?:saves|states|presets)/'+lower)

def inventory():
 out={}
 for p in files:
  file=p.name
  for n,line in enumerate(p.read_text().splitlines(),1):
   for token in pat.findall(line):
    key=f'{file}:{token}'
    out[key]=out.get(key,0)+1
 return out
baseline=r/'tools/architecture/shell-baseline.json'
actual=inventory()
if '--record' in sys.argv:
 baseline.write_text(json.dumps(actual,indent=2,sort_keys=True)+'\n');raise SystemExit(0)
old=json.loads(baseline.read_text());
for k,v in sorted(actual.items()):print('SHELL',k,v)
new={k:v-old.get(k,0) for k,v in actual.items() if v>old.get(k,0)}
for k,v in new.items():print('WARNING NEW shell dependency',k,'+',v)
print(f'S11 real-shell inventory: {sum(actual.values())} references; baseline {sum(old.values())}; {sum(new.values())} additions; {"strict" if "--strict" in sys.argv else "warn"} mode')
if '--strict' in sys.argv and new:raise SystemExit(1)
