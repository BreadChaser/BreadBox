"""Reordered pools must not imply that unrelated objects moved together."""
import runpy
from pathlib import Path

m = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'tools/layout/literal-reasons.py'))
values, describe = m['values'], m['describe']
a = values('  100: 0120b080 .word 0x0120b080\n  104: 010a2c00 .word 0x010a2c00\n')
b = values('  200: 010a2c00 .word 0x010a2c00\n  204: 0120b080 .word 0x0120b080\n')
assert '2 words retained by value; removed none; added none' in describe(a, b)
c = values('  200: 010a2c00 .word 0x010a2c00\n  204: 010a2c00 .word 0x010a2c00\n')
assert 'removed 0x0120b080; added 0x010a2c00' in describe(a, c)
print('Literal evidence: reordered slots and duplicate-value multiplicity PASS')
