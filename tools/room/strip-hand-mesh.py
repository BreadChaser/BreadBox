#!/usr/bin/env python3
"""Keep the authored prop/camera tracks without loading hands, joints or textures.

The output remains BBH1 so both modes use the same case/camera controller.
Regenerate whenever hands.bbh changes; no clips are synthesized.
"""
import struct
from pathlib import Path

def strip(source, target):
    raw = Path(source).read_bytes()
    assert raw[:4] == b'BBH1'
    _, fps, _, _, bones, count = struct.unpack_from('<6H', raw, 4)
    clip_at = struct.unpack_from('<I', raw, 44)[0]
    # Minimal valid mesh/bone tables for the common parser, never uploaded.
    out = bytearray(48 + 16 + 80)
    clips_at = len(out)
    out.extend(bytes(count * 48))
    struct.pack_into('<4s6H8I', out, 0, b'BBH1', 2, fps, 0, 0, 2, count,
                     1, 0, 48, 64, 64, 64, 64, clips_at)
    for i in range(count):
        clip = bytearray(raw[clip_at+i*48:clip_at+(i+1)*48])
        frames, stride, mask, offset = struct.unpack_from('<HHII', clip, 20)
        hand_bytes = sum(48 + 4*(bones-2) for h in range(2) if mask & (1 << h))
        new_stride = stride - hand_bytes
        struct.pack_into('<HHII', clip, 20, frames, new_stride, mask & ~3, len(out))
        out[clips_at+i*48:clips_at+(i+1)*48] = clip
        for frame in range(frames):
            at = offset + frame*stride
            head = bytearray(raw[at:at+20]); head[0] &= ~3
            out.extend(head)
            out.extend(raw[at+20+hand_bytes:at+stride])
    Path(target).write_bytes(out)
    print(f'Prop/camera tracks: {len(raw)} -> {len(out)} bytes, {count} clips')

if __name__ == '__main__':
    import sys
    strip(*sys.argv[1:])
