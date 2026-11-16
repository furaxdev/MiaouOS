#!/usr/bin/env python3
"""Tiny test server for the Miaou Web browser: serves this folder on loopback only (QEMU's 10.0.2.2 reaches it) and echoes GET/POST forms on /echo."""
import http.server, sys, urllib.parse
class H(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        if u.path == '/echo':
            q = urllib.parse.parse_qs(u.query)
            body = '<h1>Echo</h1><p>Requête reçue : <b>%s</b></p>' % ', '.join('%s=%s' % (k, v[0]) for k, v in q.items())
            self.send_response(200); self.send_header('Content-Type', 'text/html; charset=utf-8'); self.end_headers(); self.wfile.write(body.encode()); return
        super().do_GET()
    def log_message(self, *a): pass
http.server.ThreadingHTTPServer(('127.0.0.1', int(sys.argv[1]) if len(sys.argv) > 1 else 8000), H).serve_forever()
