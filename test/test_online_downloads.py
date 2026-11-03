# Local HTTP fixture for test_online_downloads.cpp; pass the compiled binary.
import http.server,threading,subprocess,tempfile,sys,gzip
class Handler(http.server.BaseHTTPRequestHandler):
 def do_GET(self):
  if self.path=='/slow.bed':
   import time
   self.send_response(200);self.send_header('Content-Length',str(1024*1024));self.end_headers()
   try:
    for _ in range(256):self.wfile.write(b'x'*4096);self.wfile.flush();time.sleep(.005)
   except (BrokenPipeError,ConnectionResetError):pass
   return
  files={'/table.txt.gz':gzip.compress(b'1\tchr1\t10\t20\tCpG\n'),'/ref.fa':b'>chr1\nACGT\n','/ref.fa.fai':b'chr1\t4\t6\t4\t5\n','/calls.vcf.gz':b'fixture data','/calls.vcf.gz.csi':b'fixture index'}
  b=files.get(self.path)
  if b is None:self.send_error(404);return
  self.send_response(200);self.send_header('Content-Length',str(len(b)));self.end_headers();self.wfile.write(b)
 def log_message(self,*args):pass
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
threading.Thread(target=server.serve_forever,daemon=True).start()
with tempfile.TemporaryDirectory(prefix='gw-download-test-') as folder:
 subprocess.run([sys.argv[1],'http://127.0.0.1:'+str(server.server_port),folder],check=True)
server.shutdown()
