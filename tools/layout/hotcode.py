"""Relocation-aware instruction comparison and fingerprints of functions in ELFs.

Shared by check-pinned.py (frontend frame functions) and check-system-pinned.py (system
frame functions and the system core blobs). A word may differ between two images only as

  * a branch (B/BL) or a pointer to the SAME named symbol at the SAME offset, where the
    target must land inside that symbol's own extent [start, start+size) in each image
    (a symbol-relative match is never accepted outside the symbol, so a changed
    instruction cannot hide as a pointer next to a data symbol that moved), or
  * a pointer to the same zero-size/linker symbol address, a pointer to the same unnamed
    read-only block (same gap between the same neighbouring symbols, or identical
    string content), or a renumbered function-local static.

Everything else is a change. fingerprint() hashes a function with the same normalization,
so an accepted change (accepted.py) is pinned to its exact before/after code."""
import bisect, collections, hashlib, re, struct, subprocess
import compare

NM = compare.TOOLS + 'nm'
SIZED_KINDS = 'tTdDbBrRvVwW'


def defs(path):
    """{name: [(address, size), ...]} of every sized function (local names repeat)."""
    out = collections.defaultdict(list)
    for line in subprocess.check_output([NM, '-S', path], text=True).splitlines():
        p = line.split()
        if len(p) == 4 and p[2] in 'tT':
            out[p[3]].append((int(p[0], 16), int(p[1], 16)))
    return out


def _local_objects(path):
    # GCC numbers function-local statics across a translation unit. Extraction can
    # renumber cache.8 to cache.10 without changing its identity or representation.
    # Accept this only for a unique (stem, type, size) in each ELF, at its exact base.
    objects = collections.defaultdict(list)
    for line in subprocess.check_output([NM, '-S', path], text=True).splitlines():
        p = line.split()
        if len(p) == 4 and p[2] in 'bdr' and re.search(r'\.\d+$', p[3]):
            objects[(re.sub(r'\.\d+$', '', p[3]), p[2], int(p[1], 16))].append(int(p[0], 16))
    return objects


def _extents(path):
    """Sorted [(start, size, name)] of every sized symbol; one name per start (smallest)."""
    best = {}
    for line in subprocess.check_output([NM, '-n', '-S', path], text=True).splitlines():
        p = line.split()
        if len(p) == 4 and p[2] in SIZED_KINDS and int(p[1], 16) > 0 and not p[3].startswith('$'):
            start, size = int(p[0], 16), int(p[1], 16)
            if start not in best or (-size, p[3]) < (-best[start][0], best[start][1]):
                best[start] = (size, p[3])
    rows = sorted((s, z, n) for s, (z, n) in best.items())
    return rows, [r[0] for r in rows]


class Image:
    """One ELF with its loaded sections, symbols and symbol extents."""

    def __init__(self, path):
        self.path = path
        self.elf = compare.load(path)
        self.rows, self.starts = _extents(path)
        self.local = _local_objects(path)

    def gap(self, addr):
        """(previous symbol, next symbol, gap size, offset in gap) for an address in
        read-only data outside every named symbol (an unnamed constant, a table, a literal
        pool), else None. Equal keys in two images mean the same unnamed block in the same
        place between the same neighbours, wherever the link put it."""
        if not compare.in_rodata(self.elf, addr) or self.locate(addr) is not None:
            return None
        i = bisect.bisect_right(self.starts, addr) - 1
        if i < 0 or i + 1 >= len(self.starts):
            return None
        start, size, name = self.rows[i]
        nstart, _, nname = self.rows[i + 1]
        return (name, nname, nstart - (start + size), addr - (start + size))

    def locate(self, addr):
        """(name, offset) when addr is inside a sized symbol, else None."""
        i = bisect.bisect_right(self.starts, addr) - 1
        if i >= 0:
            start, size, name = self.rows[i]
            if addr < start + size:
                return (name, addr - start)
        return None


class Images:
    def __init__(self, base, new):
        self.a, self.b = Image(base), Image(new)
        self.A, self.B = self.a.elf, self.b.elf
        self.local_moves = {(v[0], self.b.local[k][0]) for k, v in self.a.local.items()
                            if len(v) == 1 and len(self.b.local.get(k, ())) == 1}

    def same(self, x, y):
        """Do two words in BASE and NEW denote the same symbol-relative pointer?"""
        if (x, y) in self.local_moves or (self.A['exact'][x] & self.B['exact'][y]):
            return True
        lx, ly = self.a.locate(x), self.b.locate(y)
        return lx is not None and lx == ly

    def _anonymous(self, image, addr):
        """The bytes of the unnamed read-only constant at addr (a string, a table, a literal
        pool entry): up to the first NUL, the next symbol, the section end, or 256 bytes.
        None when addr is not in read-only data or lies inside a named symbol."""
        elf = image.elf
        if not compare.in_rodata(elf, addr) or image.locate(addr) is not None:
            return None
        for va, off, size, _ in elf['secs']:
            if va <= addr < va + size:
                end = min(size - (addr - va), 256)
                k = bisect.bisect_right(image.starts, addr)
                if k < len(image.starts):
                    end = min(end, image.starts[k] - addr)
                return bytes(elf['data'][off + addr - va:off + addr - va + end]).split(b'\0', 1)[0]

    @staticmethod
    def branch_target(word, at):
        return at + 8 + ((((word & 0xffffff) ^ 0x800000) - 0x800000) << 2)

    def words(self, aa, bb, size):
        """(relocated reference count, [byte offsets of changed words]) of the function at
        aa in BASE against the one at bb in NEW (equal sizes)."""
        A, B = self.A, self.B
        refs, changed = 0, []
        for offset in range(0, size, 4):
            wa, wb = compare.word(A, aa + offset), compare.word(B, bb + offset)
            if wa == wb:
                continue
            if wa is not None and wb is not None:
                if (wa >> 25) & 7 == 5 and (wa ^ wb) >> 24 == 0:
                    if self.same(self.branch_target(wa, aa + offset), self.branch_target(wb, bb + offset)):
                        refs += 1
                        continue
                if self.same(wa, wb):
                    refs += 1
                    continue
                ga = self.a.gap(wa)
                if ga is not None and ga == self.b.gap(wb):
                    refs += 1
                    continue
                left, right = self._anonymous(self.a, wa), self._anonymous(self.b, wb)
                if left is not None and left == right:
                    refs += 1
                    continue
            changed.append(offset)
        return refs, changed

    def fingerprint(self, side, addr, size):
        """sha256 of the function's words with relocatable references normalized to
        symbol+offset (same normalization as words(); side is 'base' or 'new')."""
        image = self.a if side == 'base' else self.b
        elf = image.elf
        h = hashlib.sha256()
        h.update(struct.pack('<I', size))
        for offset in range(0, size, 4):
            w = compare.word(elf, addr + offset)
            token = None
            if w is not None:
                if (w >> 25) & 7 == 5:
                    t = image.locate(self.branch_target(w, addr + offset))
                    if t is not None:
                        token = ('b', w >> 24, t)
                if token is None:
                    t = image.locate(w)
                    if t is not None:
                        token = ('p', t)
            h.update(repr(token).encode() if token else struct.pack('<I', w or 0))
        return h.hexdigest()

    def frozen_fingerprint(self, side, addr, size, literal_strings=None):
        """Literal-aware contract for newly extracted functions.

        Instructions remain raw except real branch targets. Only disassembler-marked
        .word pools may contain data pointers; opcode integers must never be mistaken
        for pointers into a moved BSS extent. Anonymous strings/blocks use the same
        content/gap identity accepted by words(), rather than their link addresses.
        Release-relative fingerprint() deliberately remains unchanged.
        """
        image = self.a if side == 'base' else self.b
        if not hasattr(image, 'literal_words'):
            dump = subprocess.check_output([compare.TOOLS + 'objdump', '-d', image.path], text=True)
            image.literal_words = {int(m[0], 16) for m in re.findall(
                r'^\s*([0-9a-f]+):\s+[0-9a-f]+\s+(\.word)\b', dump, re.M)}
        h = hashlib.sha256(struct.pack('<I', size))
        for offset in range(0, size, 4):
            at = addr + offset
            w = compare.word(image.elf, at)
            token = None
            if at in image.literal_words:
                # Explicitly reviewed string operands may include the empty
                # label. Do not infer an empty string from arbitrary zero-filled
                # tables: only declared offsets have this semantic contract.
                expected = (literal_strings or {}).get(str(offset))
                if expected is not None:
                    content = self._anonymous(image, w)
                    if content == expected.encode():
                        token = ('string', content)
                    else:
                        token = ('string-mismatch', expected, content, w)
                    h.update(repr(token).encode())
                    continue
                target = image.locate(w)
                if target is not None:
                    token = ('p', target)
                else:
                    content = self._anonymous(image, w)
                    if content and all(32 <= c < 127 for c in content):
                        token = ('string', content)
                    else:
                        gap = image.gap(w)
                        if gap is not None:
                            token = ('gap', gap)
            elif (w >> 25) & 7 == 5:
                target = image.locate(self.branch_target(w, at))
                if target is not None:
                    token = ('b', w >> 24, target)
            h.update(repr(token).encode() if token else struct.pack('<I', w or 0))
        return h.hexdigest()
