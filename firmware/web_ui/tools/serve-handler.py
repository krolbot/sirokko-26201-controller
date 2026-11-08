#!/usr/bin/env python3
"""Local browser-test server: exact production-handler gzip/headers, not an ESP32 emulator.
API routes are provided by explicitly labelled Playwright fixtures; no hardware commands.
"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import json

EXPORT = Path(__file__).resolve().parents[2] / 'build-host/web-contract'
PAGE = (EXPORT / 'page.bin').read_bytes()
HEADERS = json.loads((EXPORT / 'headers.json').read_text())


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path not in ('/', '/index.html'):
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header('Content-Type', 'text/html; charset=utf-8')
        self.send_header('Content-Length', str(len(PAGE)))
        for name, value in HEADERS.items():
            self.send_header(name, value)
        self.end_headers()
        self.wfile.write(PAGE)

    def log_message(self, format, *args):
        pass


if __name__ == '__main__':
    print('Production handler fixture: http://127.0.0.1:4173/', flush=True)
    ThreadingHTTPServer(('127.0.0.1', 4173), Handler).serve_forever()
