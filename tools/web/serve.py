#!/usr/bin/env python3
"""Serves a web build of Phoenix on this machine, for trying it in a browser.

  python tools/web/serve.py [build folder] [--port 8000] [--open]

The folder defaults to build-wasm32. Phoenix's threads need the page to be
cross-origin isolated, so every response carries the COOP and COEP headers;
a plain `python -m http.server` does not send them and the page stops with
a SharedArrayBuffer error. It listens on localhost only, which browsers
count as a secure context. Files are served uncached, so a rebuild shows on
the next reload.
"""

import argparse
import http.server
import sys
import webbrowser
from functools import partial
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        ".wasm": "application/wasm",
        ".js": "text/javascript",
        ".mjs": "text/javascript",
    }

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def do_GET(self):
        if self.path in ("/", "/index.html") and not (Path(self.directory) / "index.html").exists():
            self.send_response(302)
            self.send_header("Location", "/Phoenix.html")
            self.end_headers()
            return
        if self.send_compressed():
            return
        super().do_GET()

    def send_compressed(self):
        """A packaged build's compressed copy (Phoenix.wasm.br, .gz), where the
        browser takes it, as the site's server sends them."""
        name = self.path.split("?", 1)[0].lstrip("/")
        if not name.endswith((".wasm", ".js")) or "/" in name:
            return False
        accepted = self.headers.get("Accept-Encoding", "")
        for suffix, encoding in ((".br", "br"), (".gz", "gzip")):
            copy = Path(self.directory) / (name + suffix)
            if encoding in accepted and copy.is_file():
                data = copy.read_bytes()
                self.send_response(200)
                self.send_header("Content-Type", self.extensions_map[Path(name).suffix])
                self.send_header("Content-Encoding", encoding)
                self.send_header("Content-Length", str(len(data)))
                self.send_header("Vary", "Accept-Encoding")
                self.end_headers()
                self.wfile.write(data)
                return True
        return False


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("folder", nargs="?", default=str(ROOT / "build-wasm32"))
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument("--open", action="store_true", help="open the page in the default browser")
    args = parser.parse_args()

    folder = Path(args.folder).resolve()
    if not (folder / "Phoenix.wasm").exists():
        sys.exit("%s has no Phoenix.wasm: build it first with tools/web/build_web.py" % folder)

    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), partial(Handler, directory=str(folder)))
    url = "http://localhost:%d/" % args.port
    print("Serving %s at %s (Ctrl+C stops)" % (folder, url))
    if args.open:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
