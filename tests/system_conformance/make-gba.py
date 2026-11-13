#!/usr/bin/env python3
"""CC0-1.0: original ARM7 test cartridge; no Nintendo logo or commercial bytes.
Mode 3 red framebuffer, square-wave channel 1, SRAM counter per vblank.
Uses the core's bundled open BIOS. The ARM instruction encoding below needs
only Python; no downloaded assets or cross assembler.
"""
import struct, sys
from pathlib import Path
rom=bytearray(32768)
rom[:4]=struct.pack('<I',0xea00002e) # branch to 0xc0
rom[0xa0:0xac]=b'BB CONFORM  '
rom[0xac:0xb0]=b'BBC0';rom[0xb2]=0x96
rom[0xbd]=(-sum(rom[0xa0:0xbd])-0x19)&255
# Small ARM assembler for MOV immediate, literal LDR and halfword stores.
code=[]; literals=[]
def emit(x): code.append(x)
def mov(r,v): emit(0xe3a00000|(r<<12)|v)
def lit(r,v): literals.append((len(code),r,v));emit(0)
def strh(r,b,off=0): emit(0xe1c000b0|(b<<16)|(r<<12)|((off&0xf0)<<4)|(off&15))
lit(0,0x04000000);lit(1,0x0403);strh(1,0) # mode3 + BG2
lit(0,0x06000000);lit(1,0x001f);lit(2,38400)
strh(1,0);emit(0xe2800002);emit(0xe2522001);emit(0x1afffffb) # fill framebuffer
lit(0,0x04000000)
mov(1,0x80);strh(1,0,0x84) # sound master
lit(1,0x1177);strh(1,0,0x80) # route square1 to L/R
lit(1,0xf080);strh(1,0,0x62);lit(1,0x8700);strh(1,0,0x64)
lit(3,0x0e000000);mov(4,0);emit(0xe5c34000)
wait=len(code);emit(0xe1d010b6);emit(0xe35100a0);emit(0x1afffffc) # VCOUNT==160
emit(0xe2844001);emit(0xe5c34000)
emit(0xe1d010b6);emit(0xe35100a0);emit(0x0afffffc)
# B wait
emit(0xea000000|((wait-len(code)-2)&0xffffff))
for i,r,v in literals:
 off=(len(code)-i-2)*4;code[i]=0xe59f0000|(r<<12)|off;code.append(v)
blob=struct.pack('<'+'I'*len(code),*code);rom[0xc0:0xc0+len(blob)]=blob
rom[0x300:0x309]=b'SRAM_V113'
Path(sys.argv[1]).write_bytes(rom)
