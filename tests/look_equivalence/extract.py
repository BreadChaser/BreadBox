"""Compile complete production controller functions with fake platform boundaries."""
from pathlib import Path
import re
ROOT = Path(__file__).resolve().parents[2]
def function(source, name):
    pattern = r'^[ \t]*(?:static[ \t]+)?(?:PSX3D_COLD[ \t]+)?(?:const[ \t]+)?[\w \t*]+\b'+name+r'\([^;{}]*\)\s*\{'
    m = re.search(pattern, source, re.M)
    assert m, name
    start = m.start()
    # A blank/comment before the return type is not part of the declaration.
    declaration = source[start:source.index('{', m.start())]
    while declaration.startswith('\n'):
        start += 1
        declaration = declaration[1:]
    i = source.index('{', m.start()) + 1
    depth = 1
    while depth:
        depth += (source[i] == '{') - (source[i] == '}')
        i += 1
    return source[start:i]
if __name__ == '__main__':
    import sys
    source = (ROOT/sys.argv[1]).read_text()
    print('\n'.join(function(source, name) for name in sys.argv[2:]))
