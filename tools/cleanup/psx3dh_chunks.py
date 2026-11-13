"""Parse include/breadbox/psx3d.h into declaration chunks (shared by split_psx3d_h.py)."""
import re

LEX = re.compile(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|//[^\n]*|/\*[\s\S]*?\*/')

def strip(s):
    """Comments out, string and character literals emptied (one pass, so '"' is a char)."""
    def one(m):
        t = m.group(0)
        if t[0] == '"': return '""'
        if t[0] == "'": return "' '"
        return '\n' * t.count('\n') if t.startswith('/*') else ''
    return LEX.sub(one, s)

def chunks(text):
    """[(comment_lines, body_lines)]: body is one declaration, a #define, an #include or an #if block."""
    lines = text.split('\n')
    out, pending, i = [], [], 0
    while i < len(lines):
        l = lines[i]; s = l.strip()
        if not s:
            out.append(([], [l])); i += 1; continue
        if (s.startswith('//') or s.startswith('/*')) and not re.search(r'\*/\s*\S', s):
            body = [l]
            if s.startswith('/*') and '*/' not in s:
                while '*/' not in lines[i]:
                    i += 1; body.append(lines[i])
            pending.extend(body); i += 1; continue
        body = [l]
        if s.startswith('#'):
            if re.match(r'#\s*if', s):
                depth = 1
                while depth:
                    i += 1; body.append(lines[i]); t = lines[i].strip()
                    if re.match(r'#\s*if', t): depth += 1
                    elif re.match(r'#\s*endif', t): depth -= 1
            else:
                while body[-1].rstrip().endswith('\\'):
                    i += 1; body.append(lines[i])
        else:
            depth = 0
            while True:
                t = strip(body[-1]) if len(body) == 1 else strip(lines[i])
                depth += t.count('{') + t.count('(') - t.count('}') - t.count(')')
                ends = t.rstrip().endswith(';') or t.rstrip().endswith('}')
                if depth == 0 and ends: break
                i += 1; body.append(lines[i])
        out.append((pending, body)); pending = []; i += 1
    if pending: out.append((pending, []))
    return out

def names(body):
    """Identifiers a chunk declares (first one is its key)."""
    t = strip('\n'.join(body)).strip()
    m = re.match(r'#\s*define\s+(\w+)', t)
    if m: return [m.group(1)]
    m = re.match(r'#\s*include\s*(\S+)', '\n'.join(body).strip())
    if m: return ['#include ' + m.group(1)]
    if t.startswith('#'):
        inner = '\n'.join(l for l in t.split('\n')[1:-1])
        return names([inner])
    if t.startswith('#') or not t: return []
    m = re.match(r'typedef\s+(?:struct|enum|union)\s*\w*\s*\{.*\}\s*(\w+)\s*;', t, re.S)
    if m: return [m.group(1)]
    m = re.match(r'(?:typedef\s+)?enum\s*\w*\s*\{(.*)\}', t, re.S)
    if m: return [re.match(r'\s*(\w+)', e).group(1) for e in m.group(1).split(',') if e.strip()]
    m = re.match(r'typedef\b.*?\b(\w+)\s*;$', t, re.S)
    if m: return [m.group(1)]
    if re.match(r'extern\b', t) and '(' not in t.split('=')[0].replace('(*', ''):
        decl = re.sub(r'\[[^\]]*\]', '', t.rstrip(';'))
        return [re.findall(r'(\w+)\s*$', p.strip())[0] for p in re.split(r',(?![^()]*\))', decl) if p.strip()]
    m = re.search(r'\(\s*\*\s*(\w+)\s*\)\s*\(', t)
    if m and t.startswith('extern'): return [m.group(1)]
    m = re.search(r'(\w+)\s*\(', t)
    if m: return [m.group(1)]
    raise ValueError('unrecognized declaration: ' + t)

def split_guard(text):
    """(before, inner, after) around the PSX3D_H include guard."""
    m = re.search(r'^#ifndef PSX3D_H\n#define PSX3D_H\n', text, re.M)
    e = text.rstrip().rfind('#endif')
    return text[:m.start()], text[m.end():e], text[e:]
