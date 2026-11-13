"""Emit production Share top/tag functions for the real Settings backdrop test."""
from pathlib import Path
import re,sys
s=(Path(__file__).resolve().parents[2]/'app/ui/components/share.inc').read_text()
def function(name):
 m=re.search(r'static void '+name+r'\(void \*ud\)\s*\{',s);assert m,name
 end=s.index('{',m.start())+1;depth=1
 while depth:
  depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]
print(function('share_tag').replace('share_tag','share_test_tag'))
print(function('draw_top').replace('draw_top(void','share_test_top(void').replace('share_tag','share_test_tag'))
