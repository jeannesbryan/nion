#!/usr/bin/env python3
"""Counts which paths a browser actually fetched, for the data saver test.

Serves a tiny site containing a document that references an image, a web font
and a video, and appends every requested path to a log file. The test then reads
the log to decide whether the data saver blocked the sub-resources.

Deliberately dumb: it only records, it never decides. The assertion belongs in
the shell script where the start-up race is visible.
"""

import os
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer

ROOT = sys.argv[1]
LOG = sys.argv[2]
PORT = int(sys.argv[3])

CONTENT_TYPES = {
    ".png": "image/png",
    ".ttf": "font/ttf",
    ".webm": "video/webm",
    ".html": "text/html",
}


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):  # noqa: N802 (http.server API)
        path = self.path.split("?")[0]
        with open(LOG, "a", encoding="utf-8") as handle:
            handle.write(path + "\n")

        if path == "/":
            path = "/index.html"

        candidate = os.path.join(ROOT, path.lstrip("/"))
        if not os.path.isfile(candidate):
            self.send_error(404)
            return

        with open(candidate, "rb") as handle:
            body = handle.read()

        extension = os.path.splitext(candidate)[1]
        self.send_response(200)
        self.send_header("Content-Type", CONTENT_TYPES.get(extension, "application/octet-stream"))
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        # The log file is the record; stderr noise would only get in the way.
        pass


if __name__ == "__main__":
    HTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
