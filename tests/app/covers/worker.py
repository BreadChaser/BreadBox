#!/usr/bin/env python3
import http.server,os,subprocess,sys,tempfile,threading,time
from pathlib import Path
root=Path(__file__).resolve().parents[3]
class Handler(http.server.BaseHTTPRequestHandler):
 def do_GET(self):
  if self.path=='/missing.png' or self.path.startswith('/Nothing') or self.path.startswith('/SLUS'):self.send_error(404);return
  if self.path=='/error.png':self.send_error(503);return
  if self.path=='/logo.png':pass
  self.send_response(200);self.send_header('Content-Length','99999' if self.path=='/slow.png' else str(len(png)));self.end_headers()
  if self.path=='/slow.png':time.sleep(.5);return
  if self.path.endswith('/delayed.png'):time.sleep(2.6)
  self.wfile.write(png)
 def log_message(self,*a):pass
# Small RGBA PNG generated with libpng-independent standard library.
import struct,zlib
def chunk(t,b):return struct.pack('>I',len(b))+t+b+struct.pack('>I',zlib.crc32(t+b))
png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',1,1,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(b'\0\xff\x80\0\xff'))+chunk(b'IEND',b'')
with http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler) as server,tempfile.TemporaryDirectory() as tmp:
 # Exercise the production shelf drain owner with the real cover cache below.
 shelf=(root/'app/ui/shelf.c').read_text()
 start=shelf.index('static bool cover_drain_pending;')
 end=shelf.index('// The shelves',start)
 Path(tmp,'shelf-drain.inc').write_text(shelf[start:end])
 thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
 libs=subprocess.check_output(['pkg-config','--cflags','--libs','libpng','libcurl'],text=True).split()
 Path(tmp,'table').mkdir()
 Path(tmp,'table/names.txt').write_text('Serial Pic (USA)\n')
 Path(tmp,'table/serials.idx').write_text('SLUS00001 0000000\n')
 # the player's own picture: 2x2
 Path(tmp,'user.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',2,2,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(b'\0'+b'\xff\x00\x00\xff'*2+b'\0'+b'\x00\xff\x00\xff'*2))+chunk(b'IEND',b''))
 import shutil
 shutil.copyfile(Path(tmp,'user.png'),Path(tmp,'own.png'))
 # Build the driver; local HTTP is the only network used.
 cmd=['cc','-std=gnu11','-I'+tmp,'-I'+str(root/'tests/app/host/skip'),'-I'+str(root/'app/ui'),'-I'+str(root/'include/breadbox'),'-I'+str(root/'platform/3ds'),'-Wall','-Wextra','-Wno-deprecated-declarations','-g','-fsanitize=address,undefined',f'-DBOXART_DIR="{tmp}/table"',f'-DTHUMB_URL="http://127.0.0.1:{server.server_port}/"',f'-DBB_THUMB_ROOT="http://127.0.0.1:{server.server_port}/"',str(root/'tests/app/covers/worker.c'),str(root/'app/ui/covers_png.c'),str(root/'app/ui/covers_pack.c'),str(root/'app/ui/boxart.c'),str(root/'systems/gba/gba_boxart.c'),str(root/'app/ui/safefile.c'),'-lminizip','-lz','-lm','-Wl,--wrap=curl_multi_poll',*libs,'-o',tmp+'/gba-check']
 subprocess.run(cmd,check=True)
 # Each run starts with a fresh miss/user state.
 shutil.rmtree(Path(tmp,'covers'),ignore_errors=True)
 shutil.copyfile(Path(tmp,'own.png'),Path(tmp,'user.png'))
 subprocess.run([tmp+'/gba-check'],cwd=tmp,check=True)

 server.shutdown();thread.join()
