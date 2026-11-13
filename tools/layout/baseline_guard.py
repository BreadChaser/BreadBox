"""--record must come from the accepted baseline ELF paired with its own objects, never
from a candidate: a candidate that deleted functions would erase the very requirements
the frozen lists exist to keep."""
import hashlib, json, sys
from pathlib import Path


def require_accepted(base):
    meta = json.loads((Path(__file__).with_name('baseline.json')).read_text())
    have = hashlib.sha256(Path(base).read_bytes()).hexdigest()
    if have != meta['sha256']:
        sys.exit(f'--record refused: {base} is not the accepted baseline ELF (sha256 {have}, baseline.json {meta["sha256"]}). '
                 'Record only from the accepted build with its own objects, and review the name-list diff.')
