#!/usr/bin/env python3
"""BreadBox host conformance cartridge. SPDX-License-Identifier: CC0-1.0.
Written for this repository; no external/commercial content. Produces mapper-0
16 KiB PRG + 8 KiB zero CHR, battery-backed RAM, a blue screen and a running
frame counter at $6000. Standard NES reset/vector/MMIO addresses only.
Usage: make-rom.py OUT.nes
"""
import pathlib,sys
# 6502 reset code, starting at $8000.
code=bytearray();labels={};fixups=[]
def emit(*b):code.extend(b)
def label(n):labels[n]=0x8000+len(code)
def branch(op,n):emit(op,0);fixups.append((len(code)-1,n,True))
def jump(n):emit(0x4c,0,0);fixups.append((len(code)-2,n,False))
label('reset')
emit(0x78,0xd8,0xa2,0xff,0x9a,0xe8) # SEI CLD LDX #$ff TXS INX
emit(0x8e,0x00,0x20,0x8e,0x01,0x20) # disable NMI/rendering
label('wait1');emit(0x2c,0x02,0x20);branch(0x10,'wait1')
label('wait2');emit(0x2c,0x02,0x20);branch(0x10,'wait2')
emit(0xa9,0x3f,0x8d,0x06,0x20,0xa9,0x00,0x8d,0x06,0x20) # PPUADDR=$3f00
emit(0xa9,0x21,0x8d,0x07,0x20) # universal background blue
emit(0xa9,0x00,0x8d,0x05,0x20,0x8d,0x05,0x20) # scroll=(0,0)
emit(0xa9,0x08,0x8d,0x01,0x20) # enable background
# Audible pulse 1: master enable, constant volume, duty, timer.
for addr,value in ((0x4015,1),(0x4000,0xbf),(0x4002,0x80),(0x4003,0xf8)):
 emit(0xa9,value,0x8d,addr&255,addr>>8)
label('loop');emit(0x2c,0x02,0x20);branch(0x10,'loop')
emit(0xee,0x00,0x60) # INC battery SRAM[$6000], once per vblank
jump('loop')
label('irq');emit(0x40) # RTI
for off,name,relative in fixups:
 address=labels[name]
 if relative:
  delta=address-(0x8000+off+1);assert -128<=delta<=127;code[off]=delta&255
 else:code[off:off+2]=address.to_bytes(2,'little')
prg=code+bytes(16384-len(code))
for off,name in ((0x3ffa,'irq'),(0x3ffc,'reset'),(0x3ffe,'irq')):prg[off:off+2]=labels[name].to_bytes(2,'little')
header=b'NES\x1a'+bytes([1,1,2])+bytes(9)
pathlib.Path(sys.argv[1]).write_bytes(header+prg+bytes(8192))
