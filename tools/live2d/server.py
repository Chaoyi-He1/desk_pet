#!/usr/bin/env python3
"""Local helper server for tools/live2d/render.html.

  python3 tools/live2d/server.py [PORT]

Serves the repository read-only (only tools/live2d/ and assets/official/live2d/) and
accepts rendered frames: POST /save?file=<name>/<state>/<n>.png writes the body under
assets/official/l2dframes/. POST /log appends a line to l2dframes/_log.txt.
Listens on 127.0.0.1 only.
"""
import http.server
import os
import sys
import urllib.parse

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "assets", "official", "l2dframes")
ALLOWED = ("tools/live2d/", "assets/official/live2d/")


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **kw):
        super().__init__(*a, directory=ROOT, **kw)

    def log_message(self, *a):
        pass

    def do_GET(self):
        path = urllib.parse.urlparse(self.path).path.lstrip("/")
        if not path.startswith(ALLOWED) or ".." in path:
            self.send_error(403)
            return
        super().do_GET()

    def do_POST(self):
        u = urllib.parse.urlparse(self.path)
        n = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(n)
        if u.path == "/save":
            rel = urllib.parse.parse_qs(u.query).get("file", [""])[0]
            if not rel or ".." in rel or rel.startswith("/"):
                self.send_error(400)
                return
            dst = os.path.join(OUT, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(dst, "wb") as f:
                f.write(body)
        elif u.path == "/log":
            os.makedirs(OUT, exist_ok=True)
            with open(os.path.join(OUT, "_log.txt"), "a", encoding="utf-8") as f:
                f.write(body.decode("utf-8", "replace") + "\n")
        else:
            self.send_error(404)
            return
        self.send_response(204)
        self.end_headers()


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
    http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
