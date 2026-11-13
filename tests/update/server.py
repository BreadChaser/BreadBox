"""Private ephemeral Range server; starts no persistent service."""
import http.server,subprocess,threading,sys,json
class Handler(http.server.BaseHTTPRequestHandler):
 def log_message(self,*args):pass
 def do_GET(self):
  assert not self.path.endswith('.sig'), 'updater requested a signature'
  if self.path=='/update.json':
   m=dict(title_id='000400000B0B0B00',title_version='16401',version='1.0.1',size='9000',
    sha256='d8d28122647767c99b3d1fcbbc76c20caadec40e56f1cc4ad6eff89305258153',
    url='https://github.com/BreadChaser/BreadBox/releases/download/v1.0.1/BreadBox.cia')
   case=self.server.manifest_requests;self.server.manifest_requests+=1
   if case==1:m['title_id']='000400000C3D3D00'
   if case==2:m['url']=m['url'].replace('https:','http:')
   if case==3:m['sha256']='X'*64
   if case==4:m['title_version']='16400'
   if case==5:self.send_response(404);self.send_header('Content-Length','9');self.end_headers();self.wfile.write(b'Not Found');return
   if case==6:self.send_response(503);self.send_header('Content-Length','0');self.end_headers();return
   if case==7:self.connection.close();return   # no answer at all: a transport failure
   body=json.dumps(m).encode();self.send_response(200);self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body);return
  if 'blip' in self.path:self.connection.close();return
  if 'unavailable' in self.path:self.send_response(503);self.end_headers();self.wfile.write(b'temporary');return
  if 'range416' in self.path:self.send_response(416);self.end_headers();return
  body=b'Z'*(600000 if 'large' in self.path else 9000)
  if 'oversize' in self.path:body+=b'Z'*50000
  if 'notfound' in self.path:
   self.send_response(404);self.end_headers();return
  r=self.headers.get('Range')
  if r and 'ignore' not in self.path:
   lo,hi=map(int,r.removeprefix('bytes=').split('-'))
   self.send_response(206);self.send_header('Content-Range',f'bytes {lo}-{hi}/{len(body)}');body=body[lo:hi+1] if 'oversize' not in self.path else body
  else:self.send_response(200)
  self.send_header('Content-Length',str(len(body)));self.end_headers()
  self.wfile.write(body[:len(body)//2] if 'truncated' in self.path else body)
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
server.manifest_requests=0
thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
try:subprocess.run([sys.argv[1],f'http://127.0.0.1:{server.server_port}'],check=True)
finally:server.shutdown();server.server_close();thread.join()
