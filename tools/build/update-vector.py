#!/usr/bin/env python3
"""Emit the fixed manifest/SHA-256 known-answer header (no keys required)."""
from pathlib import Path
import argparse,hashlib,json
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('manifest',type=Path)
p.add_argument('--out',type=Path,required=True)
a=p.parse_args()
data=a.manifest.read_bytes()
if not 0<len(data)<=2048:p.error('expected a bounded manifest')
json.loads(data)
header='/* Generated from a fixed manifest; SHA-256/parser self-test data. */\n'
header+='static const char update_vector_data[]='+json.dumps(data.decode('ascii'))+';\n'
header+='static const unsigned char update_vector_sha256[32]={' + ','.join(map(str,hashlib.sha256(data).digest())) + '};\n'
a.out.write_text(header)
