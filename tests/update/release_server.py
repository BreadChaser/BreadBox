"""Local HTTPS redirect fixture; certificates never leave the temporary directory."""
import http.server
import json
from pathlib import Path
import ssl
import subprocess
import sys
import threading

class Handler(http.server.BaseHTTPRequestHandler):
 def log_message(self,*args):pass
 def redirect(self,url):
  self.send_response(302);self.send_header('Location',url);self.send_header('Content-Length','0');self.end_headers()
 def do_GET(self):
  host=self.headers['Host']
  self.server.requests.append((host,self.path))
  if self.path=='/BreadChaser/BreadBox/releases/latest/download/update.json':
   case=self.server.discovery;self.server.discovery+=1
   return self.redirect([
    'https://objects.githubusercontent.com/manifest-hop',
    'https://foreign.test/update.json',
    'http://github.com/insecure',
    'https://github.com/loop',
    'https://wrong.test/update.json',
    f'https://github.com:{untrusted.server_port}/update.json',
   ][case])
  if self.path=='/manifest-hop':return self.redirect('https://release-assets.githubusercontent.com/update.json')
  if self.path=='/loop':return self.redirect('https://github.com/loop')
  if self.path.endswith('/BreadBox.cia'):
   case=self.server.assets;self.server.assets+=1
   return self.redirect(['https://objects.githubusercontent.com/asset-hop','https://foreign.test/asset','http://github.com/insecure'][case])
  if self.path=='/asset-hop':return self.redirect('https://release-assets.githubusercontent.com/asset')
  if self.path=='/update.json':
   body=json.dumps(dict(title_id='000400000B0B0B00',title_version='16401',version='1.0.1',size='9000',
    sha256='d8d28122647767c99b3d1fcbbc76c20caadec40e56f1cc4ad6eff89305258153',
    url='https://github.com/BreadChaser/BreadBox/releases/download/v1/BreadBox.cia')).encode()
   self.send_response(200)
  else:
   assert self.path=='/asset',self.path
   assert self.headers.get('Range')=='bytes=0-31'
   body=b'Z'*32;self.send_response(206);self.send_header('Content-Range','bytes 0-31/9000')
  self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)

class Server(http.server.ThreadingHTTPServer):
 def handle_error(self,request,address):
  # A rejected TLS peer or the redirect-limit abort can reset the connection.
  if not isinstance(sys.exc_info()[1],(ConnectionResetError,BrokenPipeError)):
   super().handle_error(request,address)

def server(cert,key):
 srv=Server(('127.0.0.1',0),Handler)
 context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);context.load_cert_chain(cert,key)
 srv.socket=context.wrap_socket(srv.socket,server_side=True)
 srv.requests=[];srv.discovery=srv.assets=0
 thread=threading.Thread(target=srv.serve_forever,daemon=True);thread.start()
 return srv,thread

# One trusted fixture certificate covers the accepted hosts and foreign.test,
# so the foreign-host negative cannot pass merely because TLS rejected it.
Path('romfs:').mkdir(exist_ok=True)
for name in ('trusted','untrusted'):
 subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-days','1',
  '-subj','/CN=github.com','-addext','subjectAltName=DNS:github.com,DNS:objects.githubusercontent.com,DNS:release-assets.githubusercontent.com,DNS:foreign.test',
  '-keyout',name+'.key','-out',name+'.pem'],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
Path('romfs:/certs.pem').write_bytes(Path('trusted.pem').read_bytes())
trusted,thread=server('trusted.pem','trusted.key')
untrusted,bad_thread=server('untrusted.pem','untrusted.key')
try:
 subprocess.run([sys.argv[1],str(trusted.server_port),str(untrusted.server_port)],check=True)
 assert trusted.discovery==6 and trusted.assets==3
 assert ('foreign.test','/update.json') in trusted.requests and ('foreign.test','/asset') in trusted.requests
 assert not any(path=='/insecure' for _,path in trusted.requests)
 assert sum(path=='/loop' for _,path in trusted.requests)==5
 assert not any(host=='wrong.test' for host,_ in trusted.requests)
 assert not untrusted.requests
finally:
 for srv,t in ((trusted,thread),(untrusted,bad_thread)):
  srv.shutdown();srv.server_close();t.join()
