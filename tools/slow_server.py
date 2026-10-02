import http.server, socketserver, time, sys

CHUNK = 128 * 1024
TOTAL = 8 * 1024 * 1024


class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header('Content-Type', 'application/octet-stream')
        self.send_header('Content-Length', str(TOTAL))
        self.end_headers()
        sent = 0
        blob = b'\x5a' * CHUNK
        while sent < TOTAL:
            self.wfile.write(blob)
            sent += CHUNK
            time.sleep(0.12)

    def log_message(self, *a):
        pass


class S(socketserver.ThreadingTCPServer):
    allow_reuse_address = True


port = int(sys.argv[1]) if len(sys.argv) > 1 else 8713
S(('127.0.0.1', port), H).serve_forever()
